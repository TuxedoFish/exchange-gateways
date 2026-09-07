// ApplicationPersister.h - Logging extension
#pragma once

#include "../marketdata/DeribitMDApplicationBase.h"
#include "MarketDataLogger.h"
#include "../util/SimpleConfig.h"
#include <memory>
#include <string>

class DeribitPersister : public DeribitApplicationBase {
private:
    std::unique_ptr<MarketDataLogger> m_logger;

public:
    // Constructor with optional log directory
    explicit DeribitPersister(SimpleConfig& config);
    virtual ~DeribitPersister() = default;

    // Override FIX::Application interface to add logging
    void toApp(FIX::Message& message, const FIX::SessionID& sessionID) noexcept override;
    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept override;
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept override;
};