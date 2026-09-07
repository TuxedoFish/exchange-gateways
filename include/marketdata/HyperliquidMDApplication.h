#pragma once

#include "HyperliquidMDApplicationBase.h"
#include "HyperliquidMessageProcessor.h"
#include <spdlog/spdlog.h>

class HyperliquidMDApplication : public HyperliquidMDApplicationBase
{
public:
    explicit HyperliquidMDApplication(const SimpleConfig& config);
    virtual ~HyperliquidMDApplication() = default;

    void onConnected() override;
    void onDisconnected(bool hasError, const std::string& errMsg) override;
    void onL2Book(const hyperliquid::L2BookSnapshot& snapshot) override;
    void onBbo(const hyperliquid::BboUpdate& update) override;
    void onMeta(const hyperliquid::MetaResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;
    void onOutcomeMeta(const hyperliquid::OutcomeMetaResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;
    void onTrade(const hyperliquid::Trade& trade) override;

private:
    SBEBinaryWriter m_writer;
    HyperliquidMessageProcessor m_processor;
};
