#pragma once

#include <string>
#include <string_view>
#include "../sbe/SBEBinaryWriter.h"
#include "../marketdata/HyperliquidMessageProcessor.h"
#include "../marketdata/HyperliquidMDApplicationBase.h"
#include "hyperliquid/websocket/WebsocketMessageParser.h"
#include "hyperliquid/websocket/WebsocketMessageHandler.h"
#include "hyperliquid/rest/RestApiMessageParser.h"

class HyperliquidFileMessageProcessor : public hyperliquid::WebsocketMessageHandler
{
public:
    HyperliquidFileMessageProcessor(HyperliquidMessageProcessor& processor,
                                     SBEBinaryWriter& writer,
                                     const std::vector<DesiredOutcome>& desiredOutcomes);

    void process(std::string_view line);
    void nextFile(const std::string& filePath);

    static bool isConnect(std::string_view msgStr);

    // WebsocketMessageHandler overrides — delegate to processor
    void onL2Book(const hyperliquid::L2BookSnapshot& snapshot) override;
    void onBbo(const hyperliquid::BboUpdate& update) override;
    void onTrade(const hyperliquid::Trade& trade) override;

private:
    void processRestMessage(std::string_view line);

    HyperliquidMessageProcessor& m_processor;
    SBEBinaryWriter& m_writer;
    hyperliquid::WebsocketMessageParser m_wsParser;
    hyperliquid::RestApiMessageParser m_restParser;
    const std::vector<DesiredOutcome>& m_desiredOutcomes;
};
