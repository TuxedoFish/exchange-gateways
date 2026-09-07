#pragma once

#include "../util/SimpleConfig.h"
#include "MarketDataLogger.h"
#include "../marketdata/HyperliquidMDApplicationBase.h"
#include <memory>
#include <string>

class HyperliquidPersister : public HyperliquidMDApplicationBase {
private:
    std::unique_ptr<MarketDataLogger> m_logger;

public:
    explicit HyperliquidPersister(SimpleConfig& config);
    virtual ~HyperliquidPersister() = default;

    void onMessage(const std::string& message) override;
    void onConnected() override;
    void onDisconnected(bool hasError, const std::string& errMsg) override;
    virtual void onMessage(const std::string& message, hyperliquid::RestEndpointType type) override;
};
