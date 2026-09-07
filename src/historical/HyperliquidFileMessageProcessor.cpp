#include "../../include/historical/HyperliquidFileMessageProcessor.h"
#include <spdlog/spdlog.h>

HyperliquidFileMessageProcessor::HyperliquidFileMessageProcessor(
    HyperliquidMessageProcessor& processor,
    SBEBinaryWriter& writer,
    const std::vector<DesiredOutcome>& desiredOutcomes)
    : m_processor(processor), m_writer(writer), m_desiredOutcomes(desiredOutcomes)
{
}

void HyperliquidFileMessageProcessor::process(std::string_view line)
{
    if (line.empty()) return;

    // All WS messages (including connect/disconnect) have "channel".
    // REST responses (meta, outcomeMeta) do not.
    if (line.find("\"channel\"") == std::string_view::npos)
    {
        processRestMessage(line);
        return;
    }

    // Hot path: regular WS messages always have a "data" field.
    // Connect/disconnect markers do not — check "data" to skip rare-path logic.
    if (line.find("\"data\"") != std::string_view::npos)
    {
        try
        {
            m_wsParser.crack(line, *this);
        }
        catch (const std::exception& e)
        {
            spdlog::error("WS parse error: {}", e.what());
        }
        return;
    }

    // Rare path: no "data" field — connect or disconnect marker
    if (isConnect(line))
    {
        m_processor.onConnected();
        return;
    }

    if (line.find("\"disconnect\"") != std::string_view::npos)
    {
        m_processor.onDisconnected(false, "");
        return;
    }

    // Unknown message with channel but no data — try parsing anyway
    try
    {
        m_wsParser.crack(line, *this);
    }
    catch (const std::exception& e)
    {
        spdlog::error("WS parse error: {}", e.what());
    }
}

void HyperliquidFileMessageProcessor::processRestMessage(std::string_view line)
{
    try
    {
        if (line.empty()) return;

        if (line.find("\"universe\"") != std::string_view::npos)
        {
            // Meta response — rare, string construction is fine
            std::string msg(line);
            auto response = m_restParser.parseMeta(msg);
            m_processor.onMeta(response);
        }
        else if (line.find("\"outcomes\"") != std::string_view::npos)
        {
            std::string msg(line);
            auto response = m_restParser.parseOutcomeMeta(msg);
            m_processor.onOutcomeMeta(response, m_desiredOutcomes);
        }
        else
        {
            spdlog::debug("Skipping unrecognized REST message: {}",
                          line.substr(0, std::min(line.size(), static_cast<size_t>(100))));
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error("REST parse error: {}", e.what());
    }
}

void HyperliquidFileMessageProcessor::nextFile(const std::string& filePath)
{
    m_writer.openNewFile(filePath);
}

bool HyperliquidFileMessageProcessor::isConnect(std::string_view msgStr)
{
    // Check for the synthetic connect marker: {"channel": "connect"}
    // Must have "channel" and "connect" but not "data" (which would be a regular WS message)
    return msgStr.find("\"channel\"") != std::string_view::npos &&
           msgStr.find("\"connect\"") != std::string_view::npos &&
           msgStr.find("\"data\"") == std::string_view::npos;
}

// WebsocketMessageHandler overrides
void HyperliquidFileMessageProcessor::onL2Book(const hyperliquid::L2BookSnapshot& snapshot)
{
    m_processor.onL2Book(snapshot);
}

void HyperliquidFileMessageProcessor::onBbo(const hyperliquid::BboUpdate& update)
{
    m_processor.onBbo(update);
}

void HyperliquidFileMessageProcessor::onTrade(const hyperliquid::Trade& trade)
{
    m_processor.onTrade(trade);
}
