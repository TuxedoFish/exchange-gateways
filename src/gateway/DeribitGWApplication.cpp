#include "../../include/gateway/DeribitGWApplication.h"
#include <spdlog/spdlog.h>

DeribitGWApplication::DeribitGWApplication(SimpleConfig& config, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter)
    : m_config(config), m_refDataHolder(refDataHolder), m_sbeWriter(sbeWriter)
{
}

void DeribitGWApplication::onCreate(const FIX::SessionID& sessionID)
{
    spdlog::info("Session created: {}", sessionID.toString());
    m_sessionID = sessionID;
}

void DeribitGWApplication::onLogon(const FIX::SessionID& sessionID)
{
    spdlog::info("Logged on to Deribit: {}", sessionID.toString());
    m_loggedOn = true;
}

void DeribitGWApplication::onLogout(const FIX::SessionID& sessionID)
{
    spdlog::info("Logged out from Deribit: {}", sessionID.toString());
    m_loggedOn = false;
}

void DeribitGWApplication::toAdmin(FIX::Message& message, const FIX::SessionID& sessionID)
{
    // Add authentication for logon messages
    FIX::MsgType msgType;
    message.getHeader().getField(msgType);

    if (msgType.getValue() == FIX::MsgType_Logon)
    {
        FixUtils::addDeribitAuth(message, m_config);
    }
}

void DeribitGWApplication::toApp(FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{
    FixUtils::logFixMessage("Sending: ", message);
}

void DeribitGWApplication::fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{
    FixUtils::logFixMessage("Received admin: ", message);
}

void DeribitGWApplication::fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{    
    // This automatically calls down to the corresponding onMessage implementation
    crack(message, sessionID);
}


void DeribitGWApplication::onMessage(const FIX44::OrderCancelReject& message, const FIX::SessionID& sessionID) {
    FixUtils::logFixMessage("Received OrderCancelReject: ", message);

    com::liversedge::messages::OrderCancelReject sbeReject;
    if (m_sbeWriter.prepareMessage(sbeReject)) {
        DeribitMessageConverter::convertOrderCancelReject(message, sbeReject, m_refDataHolder);
        m_sbeWriter.writeMessage(sbeReject);
        spdlog::info("Sent SBE OrderCancelReject");
    }
}

void DeribitGWApplication::onMessage(const FIX44::ExecutionReport& message, const FIX::SessionID& sessionID)
{
    FixUtils::logFixMessage("Received ExecutionReport: ", message);

    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (m_sbeWriter.prepareMessage(sbeExecReport))
    {
        DeribitMessageConverter::convertExecutionReport(message, sbeExecReport, m_refDataHolder);
        m_sbeWriter.writeMessage(sbeExecReport);
        spdlog::info("Sent SBE ExecutionReport");
    }
}

bool DeribitGWApplication::sendMessage(FIX::Message& message)
{
    if (!m_loggedOn) {
        spdlog::error("Cannot send message: not logged on");
        return false;
    }

    try {
        FIX::Session::sendToTarget(message, m_sessionID);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to send message: {}", e.what());
        return false;
    }
}


