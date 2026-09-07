#include "../../include/historical/DeribitPersister.h"

DeribitPersister::DeribitPersister(SimpleConfig& config)
    : DeribitApplicationBase(config), m_logger(std::make_unique<MarketDataLogger>(config.getString("md_raw_file_path"))) {
}

void DeribitPersister::toApp(FIX::Message& message, const FIX::SessionID& sessionID) noexcept {
    if (m_logger) {
        m_logger->writeToLog("OUT", message.toString());
    }
    DeribitApplicationBase::toApp(message, sessionID);
}

void DeribitPersister::fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept {
    if (m_logger) {
        m_logger->writeToLog("IN_ADMIN", message.toString());
    }
    DeribitApplicationBase::fromAdmin(message, sessionID);
}

void DeribitPersister::fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept {
    if (m_logger) {
        m_logger->writeToLog("IN_APP", message.toString());
    }
    DeribitApplicationBase::fromApp(message, sessionID);
}
