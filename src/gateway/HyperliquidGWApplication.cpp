#include "../../include/gateway/HyperliquidGWApplication.h"
#include "../../include/gateway/HyperliquidOrdersHandler.h"
#include <chrono>
#include <spdlog/spdlog.h>

HyperliquidGWApplication::HyperliquidGWApplication(SimpleConfig& config, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter)
    : m_config(config), m_refDataHolder(refDataHolder), m_sbeWriter(sbeWriter), m_restParser(*this)
{
}

void HyperliquidGWApplication::start()
{
    hyperliquid::setLogLevel(hyperliquid::LogLevel::Debug);
    m_apiConfig.env = getEnvironment(m_config.getString("environment"));
    m_apiConfig.wallet = hyperliquid::Wallet{
        m_config.getString("hl_account_address"),
        m_config.getString("hl_private_key")
    };

    m_websocket = std::make_unique<hyperliquid::WebsocketApi>(m_apiConfig, *this);
    m_websocket->start();
}

void HyperliquidGWApplication::stop()
{
    if (m_websocket)
    {
        m_websocket->stop();
    }
}

// WebsocketApiListener

void HyperliquidGWApplication::onMessage(const std::string& message)
{
    m_wsParser.crack(message, *this);
}

void HyperliquidGWApplication::onPostResponse(const std::string& message, hyperliquid::RestEndpointType type)
{
    m_restParser.parse(message, type);
}

void HyperliquidGWApplication::onConnected()
{
    spdlog::info("HyperliquidGW: Connected");
    m_connected = true;
    m_websocket->subscribe(hyperliquid::SubscriptionType::OrderUpdates);
    m_websocket->subscribe(hyperliquid::SubscriptionType::UserFills);
}

void HyperliquidGWApplication::onDisconnected(bool hasError, const std::string& errMsg)
{
    spdlog::info("HyperliquidGW: Disconnected{}", hasError ? " (error: " + errMsg + ")" : "");
    m_connected = false;

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPlaces = {};
    m_pendingModifies = {};
    m_pendingCancels = {};
}

// WebsocketMessageHandler

void HyperliquidGWApplication::onOrderUpdate(const hyperliquid::OrderUpdate& update, bool isSnapshot)
{
    spdlog::info("HyperliquidGW: OrderUpdate coin={} side={} status={} oid={} sz={} limitPx={} cloid={} snapshot={}",
                 update.coin, update.side, hyperliquid::toString(update.status),
                 update.oid, update.sz, update.limitPx, update.cloid, isSnapshot);

    if (isSnapshot)
    {
        spdlog::info("HyperliquidGW: Skipping snapshot order update");
        return;
    }

    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (!m_sbeWriter.prepareMessage(sbeExecReport))
    {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.transactTime(update.statusTimestamp * 1000000ULL); // ms -> ns
    sbeExecReport.securityId(m_refDataHolder.getSecurityIdBySymbol(update.coin));
    sbeExecReport.ordStatus(mapOrderStatus(update.status));
    sbeExecReport.side(mapSide(update.side));
    sbeExecReport.orderType(com::liversedge::messages::OrderType::LIMIT);
    sbeExecReport.ordRejReason(
        (update.status == hyperliquid::OrderStatus::Rejected || update.status == hyperliquid::OrderStatus::OracleRejected)
            ? com::liversedge::messages::OrdRejReason::OTHER
            : com::liversedge::messages::OrdRejReason::NO_REJECT);

    SBEUtils::setPrice(sbeExecReport.price(), std::to_string(update.limitPx));
    SBEUtils::setQty(sbeExecReport.orderQty(), std::to_string(update.origSz));
    SBEUtils::setQty(sbeExecReport.leavesQty(), std::to_string(update.sz));

    double cumQty = update.origSz - update.sz;
    SBEUtils::setQty(sbeExecReport.cumQty(), std::to_string(cumQty));

    // Register oid->cloid mapping and resolve internal clientOrderId
    std::string clientOrderId;
    if (m_ordersHandler) {
        m_ordersHandler->registerOid(update.oid, update.cloid);
        clientOrderId = m_ordersHandler->lookupClientOrderId(update.cloid);
    }

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");

    spdlog::info("HyperliquidGW: Sending SBE ExecutionReport clientOrderId={} securityId={} ordStatus={} side={} price={} orderQty={} leavesQty={} cumQty={}",
                 clientOrderId, m_refDataHolder.getSecurityIdBySymbol(update.coin),
                 (int)mapOrderStatus(update.status), update.side,
                 update.limitPx, update.origSz, update.sz, cumQty);
    m_sbeWriter.writeMessage(sbeExecReport);

    // Clean up mapping on terminal states
    if (m_ordersHandler &&
        (update.status == hyperliquid::OrderStatus::Filled ||
         update.status == hyperliquid::OrderStatus::Canceled ||
         update.status == hyperliquid::OrderStatus::Rejected ||
         update.status == hyperliquid::OrderStatus::MarginCanceled ||
         update.status == hyperliquid::OrderStatus::OracleRejected)) {
        m_ordersHandler->removeOrder(update.cloid);
    }
}

void HyperliquidGWApplication::onUserFill(const hyperliquid::Fill& fill, bool isSnapshot)
{
    spdlog::info("HyperliquidGW: Fill coin={} side={} px={} sz={} oid={} snapshot={}",
                 fill.coin, fill.side, fill.px, fill.sz, fill.oid, isSnapshot);

    if (isSnapshot)
    {
        spdlog::info("HyperliquidGW: Skipping snapshot fill");
        return;
    }

    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (!m_sbeWriter.prepareMessage(sbeExecReport))
    {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.transactTime(fill.time * 1000000ULL); // ms -> ns
    sbeExecReport.securityId(m_refDataHolder.getSecurityIdBySymbol(fill.coin));
    sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::PARTIALLY_FILLED);
    sbeExecReport.side(mapSide(fill.side));
    sbeExecReport.orderType(com::liversedge::messages::OrderType::LIMIT);
    sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::NO_REJECT);

    SBEUtils::setPrice(sbeExecReport.lastPx(), std::to_string(fill.px));
    SBEUtils::setQty(sbeExecReport.lastQty(), std::to_string(fill.sz));

    std::string clientOrderId;
    if (m_ordersHandler) {
        clientOrderId = m_ordersHandler->lookupClientOrderIdByOid(fill.oid);
    }

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");

    spdlog::info("HyperliquidGW: Sending SBE Fill ExecutionReport clientOrderId={} securityId={} side={} lastPx={} lastQty={}",
                 clientOrderId, m_refDataHolder.getSecurityIdBySymbol(fill.coin),
                 fill.side, fill.px, fill.sz);
    m_sbeWriter.writeMessage(sbeExecReport);
}

// RestEndpointListener

void HyperliquidGWApplication::onPlaceOrder(const hyperliquid::PlaceOrderResponse& response)
{
    spdlog::info("HyperliquidGW: PlaceOrder response status={}", response.status);

    for (const auto& s : response.statuses)
    {
        auto pending = popPending(m_pendingPlaces, "PlaceOrder");

        if (s.error)
        {
            spdlog::error("HyperliquidGW: PlaceOrder error: {} cloid={}", *s.error, pending.cloid);
            sendNewOrderReject(pending.cloid, pending.securityId, *s.error);
        }
        else if (s.resting)
        {
            spdlog::info("HyperliquidGW: PlaceOrder resting oid={}", s.resting->oid);
        }
        else if (s.filled)
        {
            spdlog::info("HyperliquidGW: PlaceOrder filled oid={} avgPx={} totalSz={}",
                         s.filled->oid, s.filled->avgPx, s.filled->totalSz);
        }
    }
}

void HyperliquidGWApplication::onModifyOrder(const hyperliquid::ModifyOrderResponse& response)
{
    spdlog::info("HyperliquidGW: ModifyOrder response status={}", response.status);

    auto pending = popPending(m_pendingModifies, "ModifyOrder");

    if (response.status != "ok")
    {
        spdlog::error("HyperliquidGW: ModifyOrder error status={} cloid={}", response.status, pending.cloid);
        sendAmendReject(pending.cloid, pending.securityId, response.status);
    }
}

void HyperliquidGWApplication::onCancelOrder(const hyperliquid::CancelOrderResponse& response)
{
    spdlog::info("HyperliquidGW: CancelOrder response status={}", response.status);

    for (const auto& s : response.statuses)
    {
        auto pending = popPending(m_pendingCancels, "CancelOrder");

        if (s.error)
        {
            spdlog::error("HyperliquidGW: CancelOrder error: {} cloid={}", *s.error, pending.cloid);
            sendCancelReject(pending.cloid, pending.securityId, *s.error);
        }
        else if (s.success)
        {
            spdlog::info("HyperliquidGW: CancelOrder success: {}", *s.success);
        }
    }
}

HyperliquidGWApplication::PendingRequest HyperliquidGWApplication::popPending(
    std::queue<PendingRequest>& queue, const std::string& label)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (queue.empty())
    {
        spdlog::warn("HyperliquidGW: {} response with no pending request", label);
        return {};
    }
    auto pending = queue.front();
    queue.pop();
    return pending;
}

void HyperliquidGWApplication::trackPendingPlace(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPlaces.push({cloid, securityId});
}

void HyperliquidGWApplication::trackPendingModify(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingModifies.push({cloid, securityId});
}

void HyperliquidGWApplication::trackPendingCancel(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingCancels.push({cloid, securityId});
}

// Reject helpers

void HyperliquidGWApplication::sendNewOrderReject(const std::string& cloid, std::int32_t securityId, const std::string& reason)
{
    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (!m_sbeWriter.prepareMessage(sbeExecReport)) return;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.transactTime(timestamp);
    sbeExecReport.securityId(securityId);
    sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::REJECTED);
    sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::OTHER);

    std::string clientOrderId;
    if (m_ordersHandler) clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), reason);

    spdlog::info("HyperliquidGW: Sending NewOrderReject clientOrderId={} reason={}", clientOrderId, reason);
    m_sbeWriter.writeMessage(sbeExecReport);

    if (m_ordersHandler) m_ordersHandler->removeOrder(cloid);
}

void HyperliquidGWApplication::sendAmendReject(const std::string& cloid, std::int32_t securityId, const std::string& reason)
{
    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (!m_sbeWriter.prepareMessage(sbeExecReport)) return;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.transactTime(timestamp);
    sbeExecReport.securityId(securityId);
    sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::REJECTED);
    sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::OTHER);

    std::string clientOrderId;
    if (m_ordersHandler) clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), reason);

    spdlog::info("HyperliquidGW: Sending AmendReject clientOrderId={} reason={}", clientOrderId, reason);
    m_sbeWriter.writeMessage(sbeExecReport);
}

void HyperliquidGWApplication::sendCancelReject(const std::string& cloid, std::int32_t securityId, const std::string& reason)
{
    com::liversedge::messages::OrderCancelReject sbeReject;
    if (!m_sbeWriter.prepareMessage(sbeReject)) return;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeReject.timestamp(timestamp);
    sbeReject.securityId(securityId);
    sbeReject.ordStatus(com::liversedge::messages::OrdStatus::REJECTED);

    std::string clientOrderId;
    if (m_ordersHandler) clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);

    SBEUtils::setVarString(sbeReject, sbeReject.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeReject, sbeReject.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeReject, sbeReject.text(), reason);

    spdlog::info("HyperliquidGW: Sending CancelReject clientOrderId={} reason={}", clientOrderId, reason);
    m_sbeWriter.writeMessage(sbeReject);
}

// Private helpers

hyperliquid::Environment HyperliquidGWApplication::getEnvironment(const std::string& envName)
{
    if (envName == "prod")
    {
        return hyperliquid::Environment::Mainnet;
    }
    else if (envName == "testnet")
    {
        return hyperliquid::Environment::Testnet;
    }
    else
    {
        throw std::runtime_error("Unrecognized environment: " + envName);
    }
}

com::liversedge::messages::OrdStatus::Value HyperliquidGWApplication::mapOrderStatus(hyperliquid::OrderStatus status)
{
    switch (status)
    {
    case hyperliquid::OrderStatus::Open:
    case hyperliquid::OrderStatus::Triggered:
        return com::liversedge::messages::OrdStatus::NEW;
    case hyperliquid::OrderStatus::Filled:
        return com::liversedge::messages::OrdStatus::FILLED;
    case hyperliquid::OrderStatus::Canceled:
    case hyperliquid::OrderStatus::MarginCanceled:
        return com::liversedge::messages::OrdStatus::CANCELLED;
    case hyperliquid::OrderStatus::Rejected:
    case hyperliquid::OrderStatus::OracleRejected:
        return com::liversedge::messages::OrdStatus::REJECTED;
    default:
        return com::liversedge::messages::OrdStatus::NEW;
    }
}

com::liversedge::messages::Side::Value HyperliquidGWApplication::mapSide(char side)
{
    return (side == 'B') ? com::liversedge::messages::Side::BUY : com::liversedge::messages::Side::SELL;
}
