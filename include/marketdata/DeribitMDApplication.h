# pragma once

#include "DeribitMDApplicationBase.h"
#include "DeribitMessageProcessor.h"

class DeribitApplication : public DeribitApplicationBase
{
public:
    explicit DeribitApplication(const SimpleConfig& config);
    virtual ~DeribitApplication() = default;

    // Override FIX::Application interface to pass to processor
    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept override;
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept override;

    void onSecurityListRequestSent(const std::string& reqId) override;

private:
    SBEBinaryWriter m_writer;
    DeribitMessageProcessor m_processor;
};
