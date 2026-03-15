#include "../../include/historical/HyperliquidPersister.h"
#include <spdlog/spdlog.h>

HyperliquidPersister::HyperliquidPersister(SimpleConfig& config) : HyperliquidMDApplicationBase(config),
    m_logger(std::make_unique<MarketDataLogger>(config.getString("md_raw_file_path")))
{
}

void HyperliquidPersister::onMessage(const std::string& message)
{
    if (m_logger)
    {
        m_logger->writeToLog("IN_APP", message);
    }
}

void HyperliquidPersister::onConnected()
{
    spdlog::info("Connected");
    if (m_logger)
    {
        m_logger->writeToLog("IN_APP", R"({"channel": "connect"})");
    }
    HyperliquidMDApplicationBase::onConnected();
}

void HyperliquidPersister::onDisconnected(bool hasError, const std::string& errMsg)
{
    spdlog::info("Disconnected");
    if (m_logger)
    {
        m_logger->writeToLog("IN_APP", R"({"channel": "disconnect"})");
    }
    HyperliquidMDApplicationBase::onDisconnected(hasError, errMsg);
}

void HyperliquidPersister::onMessage(const std::string& message, hyperliquid::RestEndpointType type)
{
    if (m_logger)
    {
        m_logger->writeToLog("IN_APP", message);
    }
    HyperliquidMDApplicationBase::onMessage(message, type);
}
