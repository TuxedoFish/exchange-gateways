#include "../../include/marketdata/HyperliquidMDApplication.h"

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

void HyperliquidMDApplication::onOutcomeMeta(const hyperliquid::OutcomeMetaResponse& response) {
    m_processor.onOutcomeMeta(response, m_desiredOutcomes);
    HyperliquidMDApplicationBase::onOutcomeMeta(response);
}

void HyperliquidMDApplication::onL2Book(const hyperliquid::L2BookSnapshot& snapshot) {
    if (m_processor.hasExpiredOutcomes())
    {
        spdlog::info("Detected expired outcomes, removing SecurityDefinitions");
        m_processor.removeExpiredOutcomes();
    }

    if (m_processor.shouldRefetchOutcomeMeta())
    {
        spdlog::info("Refetching outcomeMeta after expiry delay");
        HyperliquidMDApplicationBase::refetchOutcomeMeta();
    }

    m_processor.onL2Book(snapshot);
}

void HyperliquidMDApplication::onBbo(const hyperliquid::BboUpdate& update) {
    m_processor.onBbo(update);
}

void HyperliquidMDApplication::onTrade(const hyperliquid::Trade& trade) {
    m_processor.onTrade(trade);
}
