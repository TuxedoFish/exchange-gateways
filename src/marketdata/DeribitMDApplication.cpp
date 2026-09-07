//
// Created by markl on 25/09/2025.
//

#include "../../include/marketdata/DeribitMDApplication.h"

DeribitApplication::DeribitApplication(const SimpleConfig& config)
    : DeribitApplicationBase(config), m_writer{}, m_processor(m_writer)
{
    m_writer.openNewFile(config.getString("md_file_path") + kPathSeparator + "messages.sbe");
}

void DeribitApplication::fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{
    DeribitApplicationBase::fromApp(message, sessionID);
    m_processor.crack(message, sessionID);
}

void DeribitApplication::fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept {
    DeribitApplicationBase::fromApp(message, sessionID);
    m_processor.crack(message, sessionID);
}

void DeribitApplication::onSecurityListRequestSent(const std::string& reqId) {
    m_processor.addPendingSecurityList(reqId);
}

void DeribitApplication::onPerpSecurityListRequestSent(const std::string& reqId) {
    DeribitApplicationBase::onPerpSecurityListRequestSent(reqId);
    m_processor.addPendingSecurityList(reqId);
    m_processor.addPerpOnlySecurityList(reqId);
    m_processor.setPerpCurrencies(m_perpCurrencies);
}
