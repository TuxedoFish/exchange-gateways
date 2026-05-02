#include "../../include/marketdata/HyperliquidMessageProcessor.h"
#include "../../include/sbe/SBEUtils.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>

namespace
{
    std::string formatUtcMs(uint64_t ms)
    {
        time_t secs = ms / 1000;
        int millis = ms % 1000;
        struct tm tm;
        gmtime_r(&secs, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        char result[40];
        snprintf(result, sizeof(result), "%s.%03d", buf, millis);
        return result;
    }

    constexpr uint64_t STALE_THRESHOLD_MS = 0;
}

HyperliquidMessageProcessor::HyperliquidMessageProcessor(SBEBinaryWriter& writer)
    : MessageProcessor(writer)
{
}

void HyperliquidMessageProcessor::setDesiredCoins(const std::set<std::string>& desiredCoins)
{
    m_desiredCoins = desiredCoins;
}

void HyperliquidMessageProcessor::onConnected()
{
    if (getConnectionStatus() == com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE)
    {
        spdlog::error("Received connect while still online, invalidating the state");
        invalidateState(0);
    }

    // Track connection time in UTC millis (same epoch as Hyperliquid timestamps)
    auto now = std::chrono::system_clock::now();
    m_connectedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::STARTING, 0);
}

void HyperliquidMessageProcessor::onDisconnected(bool hasError, const std::string& errMsg)
{
    m_observedCoins.clear();
    m_pendingSecDefs.clear();
    invalidateState(0);
}

void HyperliquidMessageProcessor::onMeta(const hyperliquid::MetaResponse& response)
{
    auto now = std::chrono::system_clock::now();
    m_metaReceivedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    for (const auto& asset : response.universe)
    {
        if (m_desiredCoins.find(asset.name) == m_desiredCoins.end())
        {
            // Ignore undesired coins
            continue;
        }
        m_observedCoins.insert(asset.name);

        int id = createSecurity(asset.name);
        m_pendingSecDefs[id] = {asset.name, asset.szDecimals, id};

        if (!updateSecurityStatus(id, 0, com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT))
        {
            spdlog::error("Error updating security status to PENDING_SNAPSHOT for {}", asset.name);
        }
    }

    std::set<std::string> missing;
    std::set_difference(
        m_desiredCoins.begin(), m_desiredCoins.end(),
        m_observedCoins.begin(), m_observedCoins.end(),
        std::inserter(missing, missing.begin())
    );

    if (!missing.empty()) {
        std::string missingCoins;
        for (const auto& coin : missing) {
            missingCoins += coin + " ";
        }
        spdlog::info("Waiting for coins: {}", missingCoins);
    } else {
        spdlog::info("All desired coins available, waiting for first price updates before going ONLINE.");
    }
}

void HyperliquidMessageProcessor::onL2Book(const hyperliquid::L2BookSnapshot& snapshot)
{
    if (m_connectedTimeMs > 0 && snapshot.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(snapshot.coin);
    if (securityId == -1)
    {
        return;
    }

    uint64_t timestampNanos = snapshot.time * 1000 * 1000;

    // Transition from PENDING_SNAPSHOT to ONLINE on first snapshot
    if (getSecurityStatus(securityId) == com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT)
    {
        updateSecurityStatus(securityId, timestampNanos, com::liversedge::messages::SecurityStatusEnum::Value::ONLINE);
    }

    // Emit deferred SecurityDefinition on first price update
    auto pendingIt = m_pendingSecDefs.find(securityId);
    if (pendingIt != m_pendingSecDefs.end())
    {
        if (snapshot.numBids > 0)
        {
            emitSecurityDefinition(pendingIt->second, std::stod(snapshot.bids[0].px));
        }
        else if (snapshot.numAsks > 0)
        {
            emitSecurityDefinition(pendingIt->second, std::stod(snapshot.asks[0].px));
        }
        m_pendingSecDefs.erase(pendingIt);
    }

    if (!m_pendingSecDefs.empty())
    {
        drainTimedOutSecDefs(snapshot.time);
    }

    if (m_pendingSecDefs.empty() && getConnectionStatus() != com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE)
    {
        spdlog::info("All SecurityDefinitions emitted, going ONLINE.");
        updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE, timestampNanos);
    }

    if (!m_shouldOutput)
    {
        return;
    }

    // Emit MDFullBook snapshot
    if (!m_writer.prepareMessage(m_mdFullBook))
    {
        spdlog::error("Error preparing MDFullBook message");
        return;
    }

    m_mdFullBook.securityId(securityId);
    m_mdFullBook.timestamp(timestampNanos);

    auto bidLevels = m_mdFullBook.bidLevelsCount(snapshot.numBids);
    for (uint8_t i = 0; i < snapshot.numBids; i++)
    {
        auto bidLevel = bidLevels.next();
        SBEUtils::setPrice(bidLevel.price(), snapshot.bids[i].px);
        SBEUtils::setQty(bidLevel.qty(), snapshot.bids[i].sz);
    }

    auto askLevels = m_mdFullBook.askLevelsCount(snapshot.numAsks);
    for (uint8_t i = 0; i < snapshot.numAsks; i++)
    {
        auto askLevel = askLevels.next();
        SBEUtils::setPrice(askLevel.price(), snapshot.asks[i].px);
        SBEUtils::setQty(askLevel.qty(), snapshot.asks[i].sz);
    }

    if (!m_writer.writeMessage(m_mdFullBook))
    {
        spdlog::error("Error writing MDFullBook message");
    }
}

void HyperliquidMessageProcessor::onBbo(const hyperliquid::BboUpdate& update)
{
    if (m_connectedTimeMs > 0 && update.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(update.coin);
    if (securityId == -1)
    {
        return;
    }

    if (getSecurityStatus(securityId) != com::liversedge::messages::SecurityStatusEnum::Value::ONLINE)
    {
        return;
    }

    if (!m_shouldOutput)
    {
        return;
    }

    uint64_t timestampNanos = update.time * 1000 * 1000;

    if (update.hasBid)
    {
        if (!m_writer.prepareMessage(m_mdUpdate))
        {
            spdlog::error("Error preparing MDUpdate message");
            return;
        }

        m_mdUpdate.securityId(securityId);
        m_mdUpdate.timestamp(timestampNanos);
        m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::BOOK_UPDATE);
        m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::CHANGE);
        m_mdUpdate.side(com::liversedge::messages::MDSide::BID);
        SBEUtils::setPrice(m_mdUpdate.price(), update.bid.px);
        SBEUtils::setQty(m_mdUpdate.qty(), update.bid.sz);

        if (!m_writer.writeMessage(m_mdUpdate))
        {
            spdlog::error("Error writing MDUpdate message");
        }
    }

    if (update.hasAsk)
    {
        if (!m_writer.prepareMessage(m_mdUpdate))
        {
            spdlog::error("Error preparing MDUpdate message");
            return;
        }

        m_mdUpdate.securityId(securityId);
        m_mdUpdate.timestamp(timestampNanos);
        m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::BOOK_UPDATE);
        m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::CHANGE);
        m_mdUpdate.side(com::liversedge::messages::MDSide::ASK);
        SBEUtils::setPrice(m_mdUpdate.price(), update.ask.px);
        SBEUtils::setQty(m_mdUpdate.qty(), update.ask.sz);

        if (!m_writer.writeMessage(m_mdUpdate))
        {
            spdlog::error("Error writing MDUpdate message");
        }
    }
}

void HyperliquidMessageProcessor::onTrade(const hyperliquid::Trade& trade)
{
    if (m_connectedTimeMs > 0 && trade.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(trade.coin);
    if (securityId == -1)
    {
        return;
    }

    if (!m_shouldOutput)
    {
        return;
    }

    if (!m_writer.prepareMessage(m_mdUpdate))
    {
        spdlog::error("Error preparing MDUpdate message");
        return;
    }

    m_mdUpdate.securityId(securityId);
    m_mdUpdate.timestamp(trade.time * 1000 * 1000); // nanos
    m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::TRADE);
    m_mdUpdate.side(trade.side == 'B' ? com::liversedge::messages::MDSide::BID : com::liversedge::messages::MDSide::ASK);
    m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::NULL_VALUE);
    SBEUtils::setPrice(m_mdUpdate.price(), trade.px);
    SBEUtils::setQty(m_mdUpdate.qty(), trade.sz);
    m_mdUpdate.tradeId(trade.tid);

    if (!m_writer.writeMessage(m_mdUpdate))
    {
        spdlog::error("Error writing MDUpdate message");
    }
}

void HyperliquidMessageProcessor::emitSecurityDefinition(const PendingAsset& asset, double price)
{
    int maxDecimals = 6 - asset.szDecimals;

    int sigFigDecimals = maxDecimals;
    if (price > 0)
    {
        int intDigits = static_cast<int>(std::floor(std::log10(price))) + 1;
        sigFigDecimals = std::max(0, 5 - intDigits);
    }

    int instrumentPricePrecision = std::min(maxDecimals, sigFigDecimals);

    spdlog::info("SecurityDefinition {} price={} szDecimals={} maxDecimals={} sigFigDecimals={} precision={}",
                 asset.name, price, asset.szDecimals, maxDecimals, sigFigDecimals, instrumentPricePrecision);

    emitSecurityDefinitionWithPricePrecision(asset, instrumentPricePrecision);
}

void HyperliquidMessageProcessor::emitSecurityDefinitionWithPricePrecision(const PendingAsset& asset, int instrumentPricePrecision)
{
    if (!m_shouldOutput) return;

    if (!m_writer.prepareMessage(m_securityDefinition))
    {
        spdlog::error("Error preparing security definition for {}", asset.name);
        removeSecurity(asset.securityId);
        return;
    }

    m_securityDefinition.id(asset.securityId);
    m_securityDefinition.timestamp(0);
    m_securityDefinition.action(com::liversedge::messages::ActionEnum::ADD);
    m_securityDefinition.baseCurrency(com::liversedge::messages::Currency::CONTRACT);
    m_securityDefinition.quoteCurrency(com::liversedge::messages::Currency::USDC);
    m_securityDefinition.settlCurrency(com::liversedge::messages::Currency::USDC);
    m_securityDefinition.positionCurrency(com::liversedge::messages::Currency::CONTRACT);
    m_securityDefinition.securityType(com::liversedge::messages::SecurityType::FUT);
    m_securityDefinition.marginingType(com::liversedge::messages::MarginingType::LINEAR);
    m_securityDefinition.contractMultiplier().mantissa(SBEUtils::stringToMantissa("1", -8));
    m_securityDefinition.settlType(com::liversedge::messages::SettlType::REGULAR);
    m_securityDefinition.maturityDate().year(3000).month(1).day(1);
    m_securityDefinition.instrumentPricePrecision(instrumentPricePrecision);
    m_securityDefinition.minPriceIncrement().mantissa(SBEUtils::powerOfTenMantissa(instrumentPricePrecision, -8));
    m_securityDefinition.minSizeIncrement().mantissa(SBEUtils::powerOfTenMantissa(asset.szDecimals, -8));
    SBEUtils::setQty(m_securityDefinition.minSize(), "0");
    SBEUtils::setQty(m_securityDefinition.minAmount(), "10");
    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.symbol(), asset.name);

    if (!m_writer.writeMessage(m_securityDefinition))
    {
        spdlog::error("Error writing security definition for {}", asset.name);
        removeSecurity(asset.securityId);
    }
}

void HyperliquidMessageProcessor::drainTimedOutSecDefs(uint64_t bookTimeMs)
{
    if (m_pendingSecDefs.empty() || m_metaReceivedTimeMs == 0) return;

    auto now = std::chrono::system_clock::now();
    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (nowMs - m_metaReceivedTimeMs < 10000) return;

    for (const auto& [id, asset] : m_pendingSecDefs)
    {
        int instrumentPricePrecision = 6 - asset.szDecimals;
        spdlog::warn("No price received for [{}], sending SecurityDefinition from raw metadata precision={}",
                     asset.name, instrumentPricePrecision);
        emitSecurityDefinitionWithPricePrecision(asset, instrumentPricePrecision);
    }
    m_pendingSecDefs.clear();
}
