#include "../../include/marketdata/HyperliquidMDApplication.h"
#include <spdlog/spdlog.h>

HyperliquidMDApplication::HyperliquidMDApplication(const SimpleConfig& config)
    : HyperliquidMDApplicationBase(config), m_processor(m_writer)
{
    m_writer.openNewFile(config.getString("md_file_path") + kPathSeparator + "messages.sbe");
}

void HyperliquidMDApplication::onConnected() {
    m_processor.onConnected();
    HyperliquidMDApplicationBase::onConnected();
}

void HyperliquidMDApplication::onDisconnected(bool hasError, const std::string& errMsg) {
    if (hasError)
    {
        spdlog::info("Disconnected: {}", errMsg);
    } else
    {
        spdlog::info("Disconnected.");
    }
    m_processor.onDisconnected(hasError, errMsg);
    HyperliquidMDApplicationBase::onDisconnected(hasError, errMsg);
}

void HyperliquidMDApplication::onMeta(const hyperliquid::MetaResponse& response) {
    m_processor.setDesiredCoins(m_desiredCoins);
    m_processor.onMeta(response);
    HyperliquidMDApplicationBase::onMeta(response);
}

void HyperliquidMDApplication::onL2BookLevel(const hyperliquid::L2BookUpdate& book, const hyperliquid::PriceLevel& level) {
    m_processor.onL2BookLevel(book, level);
}

void HyperliquidMDApplication::onTrade(const hyperliquid::Trade& trade) {
    m_processor.onTrade(trade);
}
