#include "../../include/historical/FileMessageProcessor.h"
#include <spdlog/spdlog.h>
#include <charconv>

FileMessageProcessor::FileMessageProcessor(const std::string& dataDictionaryFilePath, DeribitMessageProcessor& messageProcessor, SBEBinaryWriter& writer) :
    m_dataDictionary(dataDictionaryFilePath), m_writer(writer), m_processor(messageProcessor) {
}

void FileMessageProcessor::process(std::string_view msgStr) {
    // Fast path: skip full FIX parsing until we see a Logon
    if (!m_hasSeenLogon)
    {
        if (!isLogon(msgStr))
            return;
        m_hasSeenLogon = true;
    }

    // Fast-path dispatch: once session is initialized, bypass QuickFIX for
    // the two hottest message types (35=X incremental, 35=W snapshot)
    if (m_sessionInitialized)
    {
        char msgType = extractMsgType(msgStr);
        if (msgType == 'X')
        {
            processIncrementalFast(msgStr);
            return;
        }
        if (msgType == 'W')
        {
            processSnapshotFast(msgStr);
            return;
        }
        // All other types (Logon, Logout, SecurityList) fall through to QuickFIX
    }

    // Reuse buffer to avoid per-message allocation
    m_msgBuffer.assign(msgStr.data(), msgStr.size());

    try
    {
        auto msg = FIX::Message(m_msgBuffer, m_dataDictionary, false);

        if (!m_sessionInitialized)
        {
            // Extract the three required fields
            std::string beginString = msg.getHeader().getField(FIX::FIELD::BeginString);
            std::string senderCompID = msg.getHeader().getField(FIX::FIELD::SenderCompID);
            std::string targetCompID = msg.getHeader().getField(FIX::FIELD::TargetCompID);

            // Create session ID
            m_sessionID = FIX::SessionID(beginString, senderCompID, targetCompID);
            m_sessionInitialized = true;
        }

        std::string msgType = msg.getHeader().getField(FIX::FIELD::MsgType);
        if (msgType == FIX::MsgType_SecurityList)
        {
            m_processor.onMessage(FIX44::SecurityList(msg), m_sessionID);
        } else if (msgType == FIX::MsgType_Logon)
        {
            m_processor.onMessage(FIX44::Logon(msg), m_sessionID);
        } else if (msgType == FIX::MsgType_Logout)
        {
            m_processor.onMessage(FIX44::Logout(msg), m_sessionID);
        } else if (msgType == FIX::MsgType_MarketDataIncrementalRefresh)
        {
            m_processor.onMessage(FIX44::MarketDataIncrementalRefresh(msg), m_sessionID);
        } else if (msgType == FIX::MsgType_MarketDataSnapshotFullRefresh)
        {
            m_processor.onMessage(FIX44::MarketDataSnapshotFullRefresh(msg), m_sessionID);
        }
    } catch (const FIX::InvalidMessage& e)
    {
        spdlog::error("Invalid FIX message: {}", e.what());
        spdlog::error("Message string: {}", m_msgBuffer);
    } catch (const FIX::FieldNotFound& e)
    {
        spdlog::error("Field not found in FIX message: {}", e.what());
        spdlog::error("Message string: {}", m_msgBuffer);
    } catch (const std::exception& e)
    {
        spdlog::error("Error processing FIX message: {}", e.what());
        spdlog::error("Message string: {}", m_msgBuffer);
    }
}

void FileMessageProcessor::nextFile(std::string filePath) {
    m_writer.openNewFile(filePath);
}

bool FileMessageProcessor::isLogon(std::string_view msgStr) {
    // Quick check for logon message (MsgType=A) without full parsing
    // Look for the pattern "35=A" in the FIX message
    size_t pos = msgStr.find("35=A");
    if (pos == std::string::npos) {
        return false;
    }

    // Ensure it's followed by SOH (field separator) or end of string
    if (pos + 4 < msgStr.length()) {
        return msgStr[pos + 4] == '\x01'; // SOH character
    }

    return pos + 4 == msgStr.length(); // End of string
}

char FileMessageProcessor::extractMsgType(std::string_view msg) {
    // 35=X is near the start of FIX messages; search first 64 bytes
    size_t searchLen = std::min(msg.size(), size_t(64));
    for (size_t i = 0; i + 4 < searchLen; ++i) {
        if (msg[i] == '3' && msg[i + 1] == '5' && msg[i + 2] == '=' &&
            (i == 0 || msg[i - 1] == '\x01') && msg[i + 4] == '\x01') {
            return msg[i + 3];
        }
    }
    return '\0';
}

// ---------------------------------------------------------------------------
// Fast-path handler for 35=X (MarketDataIncrementalRefresh)
// ---------------------------------------------------------------------------
void FileMessageProcessor::processIncrementalFast(std::string_view msgStr) {
    if (!m_processor.shouldOutput())
        return;

    m_lightMsg.parse(msgStr);

    const uint64_t timestamp = parseFIXTimestampNanos(m_lightMsg.getField(52));

    const auto symbol = m_lightMsg.getField(55);
    if (!m_processor.isSecurityRegistered(symbol))
    {
        return;
    }
    const int securityId = m_processor.getSecurityId(symbol);

    if (m_processor.getSecurityStatus(securityId) != com::liversedge::messages::SecurityStatusEnum::Value::ONLINE)
    {
        return;
    }

    const int noMDEntries = m_lightMsg.getIntField(268);
    if (noMDEntries == 0)
        return;

    // Delimiter tag 279 (MDUpdateAction) marks the start of each entry in 35=X
    for (int i = 0; i < noMDEntries; ++i)
    {
        auto group = m_lightMsg.getGroup(279, i);
        if (!group.begin) break;
        processMDEntryFast(group, securityId, timestamp);
    }
}

// ---------------------------------------------------------------------------
// Fast-path handler for 35=W (MarketDataSnapshotFullRefresh)
// ---------------------------------------------------------------------------
void FileMessageProcessor::processSnapshotFast(std::string_view msgStr) {
    m_lightMsg.parse(msgStr);

    const uint64_t timestamp = parseFIXTimestampNanos(m_lightMsg.getField(52));

    const auto symbol = m_lightMsg.getField(55);
    if (!m_processor.isSecurityRegistered(symbol))
    {
        return;
    }
    const int securityId = m_processor.getSecurityId(symbol);

    if (!m_processor.shouldOutput())
    {
        m_processor.updateSecurityStatus(securityId, timestamp,
            com::liversedge::messages::SecurityStatusEnum::Value::ONLINE);
        return;
    }

    const int noMDEntries = m_lightMsg.getIntField(268);
    if (noMDEntries == 0)
        return;

    // Delimiter tag 269 (MDEntryType) marks the start of each entry in 35=W
    constexpr int DELIM_TAG = 269;

    // Check first entry — if it's a trade snapshot, ignore entirely
    auto firstGroup = m_lightMsg.getGroup(DELIM_TAG, 0);
    if (firstGroup.begin)
    {
        auto entryType = firstGroup.getField(269);
        if (!entryType.empty() && entryType[0] == '2') // MDEntryType_TRADE
            return;
    }

    // Count bids and asks
    int bidCount = 0;
    int askCount = 0;
    for (int i = 0; i < noMDEntries; ++i)
    {
        auto group = m_lightMsg.getGroup(DELIM_TAG, i);
        if (!group.begin) break;
        auto entryType = group.getField(269);
        if (entryType.empty()) continue;
        if (entryType[0] == '0') ++bidCount;
        else if (entryType[0] == '1') ++askCount;
    }

    // Prepare MDFullBook message
    if (!m_writer.prepareMessage(m_processor.mdFullBook()))
    {
        spdlog::error("Error preparing MDFullBook message");
        return;
    }

    m_processor.mdFullBook().securityId(securityId);
    m_processor.mdFullBook().timestamp(timestamp);

    // Write bid levels (up to MAX_LEVELS)
    auto nBidLevels = std::min(MAX_LEVELS, bidCount);
    auto& bidLevels = m_processor.mdFullBook().bidLevelsCount(nBidLevels);
    int bidIdx = 0;
    for (int i = 0; i < noMDEntries && bidIdx < nBidLevels; ++i)
    {
        auto group = m_lightMsg.getGroup(DELIM_TAG, i);
        if (!group.begin) break;
        auto entryType = group.getField(269);
        if (!entryType.empty() && entryType[0] == '0')
        {
            auto& bidLevel = bidLevels.next();
            SBEUtils::setPrice(bidLevel.price(), group.getField(270));
            SBEUtils::setQty(bidLevel.qty(), group.getField(271));
            ++bidIdx;
        }
    }

    // Write ask levels (up to MAX_LEVELS)
    auto nAskLevels = std::min(MAX_LEVELS, askCount);
    auto& askLevels = m_processor.mdFullBook().askLevelsCount(nAskLevels);
    int askIdx = 0;
    for (int i = 0; i < noMDEntries && askIdx < nAskLevels; ++i)
    {
        auto group = m_lightMsg.getGroup(DELIM_TAG, i);
        if (!group.begin) break;
        auto entryType = group.getField(269);
        if (!entryType.empty() && entryType[0] == '1')
        {
            auto& askLevel = askLevels.next();
            SBEUtils::setPrice(askLevel.price(), group.getField(270));
            SBEUtils::setQty(askLevel.qty(), group.getField(271));
            ++askIdx;
        }
    }

    // Write the MDFullBook message
    if (!m_writer.writeMessage(m_processor.mdFullBook()))
    {
        spdlog::error("Error writing MDFullBook message");
        return;
    }

    // Process overflow entries (beyond MAX_LEVELS) as incremental updates
    if (bidCount > MAX_LEVELS || askCount > MAX_LEVELS)
    {
        int overflowBidIdx = 0;
        int overflowAskIdx = 0;
        for (int i = 0; i < noMDEntries; ++i)
        {
            auto group = m_lightMsg.getGroup(DELIM_TAG, i);
            if (!group.begin) break;
            auto entryType = group.getField(269);
            if (entryType.empty()) continue;

            if (entryType[0] == '0')
            {
                ++overflowBidIdx;
                if (overflowBidIdx > MAX_LEVELS)
                    processMDEntryFast(group, securityId, timestamp);
            }
            else if (entryType[0] == '1')
            {
                ++overflowAskIdx;
                if (overflowAskIdx > MAX_LEVELS)
                    processMDEntryFast(group, securityId, timestamp);
            }
            else if (entryType[0] == '2')
            {
                processMDEntryFast(group, securityId, timestamp);
            }
        }
    }

    // Update security status to ONLINE
    m_processor.updateSecurityStatus(securityId, timestamp,
        com::liversedge::messages::SecurityStatusEnum::Value::ONLINE);
}

// ---------------------------------------------------------------------------
// Shared fast-path helper: write a single MDUpdate from a group slice
// ---------------------------------------------------------------------------
void FileMessageProcessor::processMDEntryFast(
    const LightFIXMessage::GroupView& entry, int securityId, uint64_t timestamp)
{
    auto entryType = entry.getField(269);
    if (entryType.empty()) return;
    char type = entryType[0];
    if (type != '0' && type != '1' && type != '2') return;

    if (!m_writer.prepareMessage(m_processor.mdUpdate()))
    {
        spdlog::error("Error preparing MDUpdate message");
        return;
    }

    auto& mdUpdate = m_processor.mdUpdate();
    mdUpdate.securityId(securityId);
    mdUpdate.timestamp(timestamp);

    // Parse MDUpdateAction (tag 279) — may be absent in snapshot overflow
    auto actionSv = entry.getField(279);
    auto action = com::liversedge::messages::MDUpdateAction::Value::NULL_VALUE;
    if (!actionSv.empty())
    {
        switch (actionSv[0])
        {
            case '0': action = com::liversedge::messages::MDUpdateAction::Value::NEW; break;
            case '1': action = com::liversedge::messages::MDUpdateAction::Value::CHANGE; break;
            case '2': action = com::liversedge::messages::MDUpdateAction::Value::DELETE; break;
            default: break;
        }
    }

    if (type == '0') // BID
    {
        mdUpdate.updateType(com::liversedge::messages::MDUpdateType::Value::BOOK_UPDATE);
        mdUpdate.side(com::liversedge::messages::MDSide::Value::BID);
        mdUpdate.action(action);
        SBEUtils::setPrice(mdUpdate.price(), entry.getField(270));
        SBEUtils::setQty(mdUpdate.qty(), entry.getField(271));
    }
    else if (type == '1') // OFFER/ASK
    {
        mdUpdate.updateType(com::liversedge::messages::MDUpdateType::Value::BOOK_UPDATE);
        mdUpdate.side(com::liversedge::messages::MDSide::Value::ASK);
        mdUpdate.action(action);
        SBEUtils::setPrice(mdUpdate.price(), entry.getField(270));
        SBEUtils::setQty(mdUpdate.qty(), entry.getField(271));
    }
    else // type == '2': TRADE
    {
        mdUpdate.updateType(com::liversedge::messages::MDUpdateType::Value::TRADE);
        SBEUtils::setPrice(mdUpdate.price(), entry.getField(270));
        SBEUtils::setQty(mdUpdate.qty(), entry.getField(271));

        // Side (tag 54): '1' = BUY (taker) -> ASK, '2' = SELL (taker) -> BID
        auto sideSv = entry.getField(54);
        if (!sideSv.empty())
        {
            mdUpdate.side(sideSv[0] == '1'
                ? com::liversedge::messages::MDSide::Value::ASK
                : com::liversedge::messages::MDSide::Value::BID);
        }

        // Trade ID (tag 278)
        auto tradeIdSv = entry.getField(278);
        if (!tradeIdSv.empty())
        {
            uint64_t tradeId = 0;
            std::from_chars(tradeIdSv.data(), tradeIdSv.data() + tradeIdSv.size(), tradeId);
            mdUpdate.tradeId(tradeId);
        }
    }

    if (!m_writer.writeMessage(m_processor.mdUpdate()))
    {
        spdlog::error("Error writing MDUpdate message");
    }
}
