#pragma once

#include <string>
#include <string_view>
#include <exception>
#include "quickfix/Application.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../marketdata/DeribitMessageProcessor.h"

class FileMessageProcessor
{
public:
    explicit FileMessageProcessor(const std::string&, DeribitMessageProcessor&, SBEBinaryWriter&);
    virtual ~FileMessageProcessor() = default;

    void process(std::string_view msgStr);
    void nextFile(std::string filePath);
    static bool isLogon(std::string_view msgStr);

private:
    FIX::SessionID m_sessionID;
    bool m_sessionInitialized = false;
    bool m_hasSeenLogon = false;
    FIX::DataDictionary m_dataDictionary;
    SBEBinaryWriter& m_writer;
    DeribitMessageProcessor& m_processor;
    std::string m_msgBuffer;
};
