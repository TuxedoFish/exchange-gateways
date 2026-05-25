#pragma once

#include <string>
#include <string_view>
#include <exception>
#include "quickfix/Application.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../marketdata/DeribitMessageProcessor.h"
#include "../fix/LightFIXMessage.h"

class FileMessageProcessor
{
public:
    explicit FileMessageProcessor(const std::string&, DeribitMessageProcessor&, SBEBinaryWriter&);
    virtual ~FileMessageProcessor() = default;

    void process(std::string_view msgStr);
    void nextFile(std::string filePath);
    static bool isLogon(std::string_view msgStr);

private:
    // Fast-path handlers that bypass QuickFIX parsing
    void processIncrementalFast(std::string_view msgStr);
    void processSnapshotFast(std::string_view msgStr);
    void processMDEntryFast(const LightFIXMessage::GroupView& entry, int securityId, uint64_t timestamp);
    static char extractMsgType(std::string_view msg);

    FIX::SessionID m_sessionID;
    bool m_sessionInitialized = false;
    bool m_hasSeenLogon = false;
    FIX::DataDictionary m_dataDictionary;
    SBEBinaryWriter& m_writer;
    DeribitMessageProcessor& m_processor;
    std::string m_msgBuffer;
    LightFIXMessage m_lightMsg;
};
