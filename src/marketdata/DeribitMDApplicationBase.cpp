#include "../../include/marketdata/DeribitMDApplicationBase.h"
#include <spdlog/spdlog.h>

#include "../../include/fix/FIXCustomTags.h"

void DeribitApplicationBase::onCreate(const FIX::SessionID& sessionID)
{
    spdlog::info("Session created: {}", sessionID.toString());
    m_sessionID = sessionID;
}

void DeribitApplicationBase::onLogon(const FIX::SessionID& sessionID)
{
    spdlog::info("Logged on to Deribit: {}", sessionID.toString());
    m_loggedOn = true;
    getSymbols();
}

void DeribitApplicationBase::onLogout(const FIX::SessionID& sessionID)
{
    spdlog::info("Logged out from Deribit: {}", sessionID.toString());
    m_loggedOn = false;
}

void DeribitApplicationBase::toAdmin(FIX::Message& message, const FIX::SessionID& sessionID)
{
    // Add authentication for logon messages
    FIX::MsgType msgType;
    message.getHeader().getField(msgType);

    if (msgType.getValue() == FIX::MsgType_Logon)
    {
        FixUtils::addDeribitAuth(message, m_config);
    }
}

void DeribitApplicationBase::toApp(FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{
    FixUtils::logFixMessage("Sending: ", message);
}

void DeribitApplicationBase::fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{
    FixUtils::logFixMessage("Received admin: ", message);
}

void DeribitApplicationBase::fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) noexcept
{    
    // This automatically calls down to the corresponding onMessage implementation
    crack(message, sessionID);
}

void DeribitApplicationBase::getSymbols()
{
    // Create SecurityListRequest message
    FIX::Message secListRequest;
    secListRequest.getHeader().setField(FIX::MsgType("x")); // SecurityListRequest

    // Required fields
    secListRequest.setField(FIX::SecurityReqID("SYMBOLS_001"));
    secListRequest.setField(FIX::SecurityListRequestType(4)); // All Securities
    secListRequest.setField(FIX::Currency("BTC"));
    secListRequest.setField(FIX::SecurityType("FUT"));  // Futures

    try {
        FIX::Session::sendToTarget(secListRequest, m_sessionID);
    }
    catch (const std::exception& e) {
        spdlog::error("Error sending SecurityListRequest: {}", e.what());
    }

    secListRequest.setField(FIX::SecurityReqID("SYMBOLS_002"));
    secListRequest.setField(FIX::SecurityType("FUTCO"));  // Futures

    try {
        FIX::Session::sendToTarget(secListRequest, m_sessionID);
    }
    catch (const std::exception& e) {
        spdlog::error("Error sending SecurityListRequest: {}", e.what());
    }
}

void DeribitApplicationBase::subscribe(std::string symbols[], int nSymbols)
{
    // Create market data request with default constructor
    FIX44::MarketDataRequest mdRequest;

    // Set required fields individually
    mdRequest.set(FIX::MDReqID("1"));
    mdRequest.set(FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_SNAPSHOT_PLUS_UPDATES));
    mdRequest.set(FIX::MarketDepth(0));
    mdRequest.set(FIX::MDUpdateType(FIX::MDUpdateType_INCREMENTAL_REFRESH));
    
    // Set group counts BEFORE adding groups
    mdRequest.set(FIX::NoMDEntryTypes(3));  // Bid, Ask & Trades
    mdRequest.set(FIX::NoRelatedSym(1));    // One symbol

    // Add block trade field so we can identify
    mdRequest.setField(FIX::DeribitShowBlockTradeId(true));
    mdRequest.setField(FIX::DeribitSkipBlockTrades(false));

    // Add entry types
    FIX44::MarketDataRequest::NoMDEntryTypes bidEntryGroup;
    bidEntryGroup.set(FIX::MDEntryType(FIX::MDEntryType_BID));
    mdRequest.addGroup(bidEntryGroup);

    FIX44::MarketDataRequest::NoMDEntryTypes askEntryGroup;
    askEntryGroup.set(FIX::MDEntryType(FIX::MDEntryType_OFFER));
    mdRequest.addGroup(askEntryGroup);

    FIX44::MarketDataRequest::NoMDEntryTypes tradeEntryGroup;
    tradeEntryGroup.set(FIX::MDEntryType(FIX::MDEntryType_TRADE));
    mdRequest.addGroup(tradeEntryGroup);

    // Add symbol
    FIX44::MarketDataRequest::NoRelatedSym symbolGroup;
    for (int i = 0; i < nSymbols; i++) {
        symbolGroup.set(FIX::Symbol(symbols[i]));
        mdRequest.addGroup(symbolGroup);
    }

    // Send the request
    FIX::Session::sendToTarget(mdRequest, m_sessionID);
}

void DeribitApplicationBase::onMessage(const FIX44::MarketDataRequest& message, const FIX::SessionID& sessionID) {
}

void DeribitApplicationBase::onMessage(const FIX44::MarketDataRequestReject& message, const FIX::SessionID& sessionID) {
}

void DeribitApplicationBase::onMessage(const FIX44::MarketDataSnapshotFullRefresh& message, const FIX::SessionID& sessionID) {
    FIX::Symbol symbol;
    message.get(symbol);
    spdlog::info("Received MarketDataSnapshotFullRefresh for: {}", symbol.getValue());
}

void DeribitApplicationBase::onMessage(const FIX44::MarketDataIncrementalRefresh& message, const FIX::SessionID& sessionID) {
}

void DeribitApplicationBase::onMessage(const FIX44::SecurityList& message, const FIX::SessionID& sessionID) {
    FixUtils::logFixMessage("Received SecurityList: ", message);

    FIX::NoRelatedSym noSecuritiesField;
    message.get(noSecuritiesField);
    int noSecurities = noSecuritiesField.getValue();
    std::string* symbols = new std::string[noSecurities];

    // FIX repeating groups are 1-indexed
    for (int i = 1; i < noSecurities + 1; i++) {
        FIX44::SecurityList::NoRelatedSym security;
        message.getGroup(i, security);
        FIX::Symbol symbol;
        security.get(symbol);
        symbols[i-1] = symbol.getString();
    }

    std::string symbolsList;
    for (int i = 0; i < noSecurities; i++) {
        symbolsList += symbols[i] + ", ";
    }
    spdlog::info("Symbols: {}", symbolsList);
    subscribe(symbols, noSecurities);
}

