#include "../../include/historical/FileMessageProcessor.h"
#include <spdlog/spdlog.h>

FileMessageProcessor::FileMessageProcessor(const std::string& dataDictionaryFilePath, DeribitMessageProcessor& messageProcessor, SBEBinaryWriter& writer) :
    m_dataDictionary(dataDictionaryFilePath), m_writer(writer), m_processor(messageProcessor) {
}

void FileMessageProcessor::process(std::string msgStr) {
    try
    {
        auto msg = FIX::Message(msgStr, m_dataDictionary, true);

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
        spdlog::error("Message string: {}", msgStr);
    } catch (const FIX::FieldNotFound& e)
    {
        spdlog::error("Field not found in FIX message: {}", e.what());
        spdlog::error("Message string: {}", msgStr);
    } catch (const std::exception& e)
    {
        spdlog::error("Error processing FIX message: {}", e.what());
        spdlog::error("Message string: {}", msgStr);
    }
}

void FileMessageProcessor::nextFile(std::string filePath) {
    m_writer.openNewFile(filePath);
}

bool FileMessageProcessor::isLogon(const std::string& msgStr) {
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