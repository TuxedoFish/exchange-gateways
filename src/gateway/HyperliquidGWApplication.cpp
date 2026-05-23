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

    m_iocTimeoutSecs = m_config.getInt("hl_ioc_timeout_secs", 5);

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

void HyperliquidGWApplication::onPostResponse(const std::string& message, hyperliquid::RestEndpointType type,
                                               std::optional<uint64_t> correlationId)
{
    m_lastPostResponse = message;
    m_restParser.parse(message, type, correlationId);
}

void HyperliquidGWApplication::onConnected()
{
    spdlog::info("Connected");
    m_connected = true;
    m_websocket->subscribe(hyperliquid::SubscriptionType::OrderUpdates);
    m_websocket->subscribe(hyperliquid::SubscriptionType::UserFills);
}

void HyperliquidGWApplication::onDisconnected(bool hasError, const std::string& errMsg)
{
    spdlog::info("Disconnected{}", hasError ? " (error: " + errMsg + ")" : "");
    m_connected = false;

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingPlaces.clear();
    m_pendingModifies.clear();
    m_pendingCancels.clear();

    if (!m_bufferedFills.empty())
    {
        spdlog::warn("Clearing {} buffered fills on disconnect", m_bufferedFills.size());
        m_bufferedFills.clear();
    }
}

// WebsocketMessageHandler

void HyperliquidGWApplication::onOrderUpdate(const hyperliquid::OrderUpdate& update)
{
    spdlog::info("OrderUpdate coin={} side={} status={} oid={} sz={} origSz={} limitPx={} cloid={}",
                 update.coin, update.side, hyperliquid::toString(update.status),
                 update.oid, update.sz, update.origSz, update.limitPx, update.cloid);

    // Check for timed-out buffered fills on every order update
    checkTimeouts();

    // Register oid->cloid mapping for fill correlation
    if (m_ordersHandler) {
        m_ordersHandler->registerOid(update.oid, update.cloid);
    }

    if (update.status == hyperliquid::OrderStatus::Open) {
        if (m_ordersHandler) {
            m_ordersHandler->setActiveOid(update.oid, update.cloid);
            m_ordersHandler->initOrderState(update.cloid, update.origSz,
                                            m_refDataHolder.getSecurityIdBySymbol(update.coin));
            m_ordersHandler->commitPendingOrderType(update.cloid);
        }
        // Fall through to emit NEW ER as ack, then replay buffered fills
    } else if (m_ordersHandler && !m_ordersHandler->isActiveOid(update.oid, update.cloid)) {
        spdlog::info("Skipping stale order update oid={} cloid={} status={} (not active oid)",
                     update.oid, update.cloid, hyperliquid::toString(update.status));
        return;
    } else if (update.status == hyperliquid::OrderStatus::Filled) {
        if (m_ordersHandler) {
            const auto* state = m_ordersHandler->getOrderState(update.cloid);
            if (state && state->origSz > 0.0 && state->cumQty < state->origSz) {
                // Order lifecycle complete but not fully filled (e.g. IOC partial fill).
                // Emit a terminal CANCELLED ER for the unfilled portion.
                std::string clientOrderId = m_ordersHandler->lookupClientOrderId(update.cloid);
                if (!clientOrderId.empty()) {
                    com::liversedge::messages::ExecutionReport sbeExecReport;
                    if (m_sbeWriter.prepareMessage(sbeExecReport)) {
                        auto now = std::chrono::system_clock::now();
                        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

                        sbeExecReport.timestamp(timestamp);
                        sbeExecReport.transactTime(update.statusTimestamp * 1000000ULL);
                        sbeExecReport.securityId(m_refDataHolder.getSecurityIdBySymbol(update.coin));
                        sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::CANCELLED);
                        sbeExecReport.side(mapSide(update.side));
                        sbeExecReport.orderType(m_ordersHandler->getOrderType(update.cloid));
                        sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::NO_REJECT);

                        SBEUtils::setPrice(sbeExecReport.price(), std::to_string(update.limitPx));
                        SBEUtils::setQty(sbeExecReport.orderQty(), std::to_string(state->origSz));
                        SBEUtils::setQty(sbeExecReport.cumQty(), std::to_string(state->cumQty));
                        SBEUtils::setQty(sbeExecReport.leavesQty(), std::to_string(0.0));

                        SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
                        SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
                        SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");

                        spdlog::info("Sending terminal CANCELLED ER for partial fill: clientOrderId={} securityId={} orderQty={} cumQty={}",
                                     clientOrderId, m_refDataHolder.getSecurityIdBySymbol(update.coin),
                                     state->origSz, state->cumQty);
                        m_sbeWriter.writeMessage(sbeExecReport);
                    }
                }
            }

            std::string cloid_check = m_ordersHandler->lookupCloidByOid(update.oid);
            if (!cloid_check.empty()) {
                spdlog::info("OrderUpdate Filled oid={} cloid={} - cleaning up", update.oid, update.cloid);
                m_ordersHandler->removeOrder(update.cloid);
            } else {
                spdlog::info("OrderUpdate Filled oid={} cloid={} - already cleaned up", update.oid, update.cloid);
            }
        }
        return;
    }

    // Open/Canceled/Rejected/MarginCanceled/OracleRejected — emit ER
    std::string clientOrderId;
    if (m_ordersHandler) {
        clientOrderId = m_ordersHandler->lookupClientOrderId(update.cloid);
    }

    if (clientOrderId.empty()) {
        spdlog::info("Skipping OrderUpdate ER for cloid={} - order already handled/removed", update.cloid);
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
    sbeExecReport.orderType(m_ordersHandler ? m_ordersHandler->getOrderType(update.cloid) : com::liversedge::messages::OrderType::LIMIT);
    sbeExecReport.ordRejReason(
        (update.status == hyperliquid::OrderStatus::Rejected || update.status == hyperliquid::OrderStatus::OracleRejected
         || update.status == hyperliquid::OrderStatus::IocCancelRejected)
            ? com::liversedge::messages::OrdRejReason::OTHER
            : com::liversedge::messages::OrdRejReason::NO_REJECT);

    SBEUtils::setPrice(sbeExecReport.price(), std::to_string(update.limitPx));

    // Use tracked state quantities when available.
    // Hyperliquid may report a sub-order's origSz for IOC splits or modifies, not the requested qty.
    double orderQty = update.origSz;
    double cumQty = update.origSz - update.sz;
    double leavesQty = update.sz;
    if (m_ordersHandler) {
        const auto* state = m_ordersHandler->getOrderState(update.cloid);
        if (state && state->origSz > 0.0) {
            orderQty = state->origSz;
            if (update.status == hyperliquid::OrderStatus::Open) {
                cumQty = 0.0;
                leavesQty = orderQty;
            } else {
                cumQty = state->cumQty;
                leavesQty = 0.0;  // terminal state
            }
        }
    }

    SBEUtils::setQty(sbeExecReport.orderQty(), std::to_string(orderQty));
    SBEUtils::setQty(sbeExecReport.leavesQty(), std::to_string(leavesQty));
    SBEUtils::setQty(sbeExecReport.cumQty(), std::to_string(cumQty));

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");

    spdlog::info("Sending SBE ExecutionReport clientOrderId={} securityId={} ordStatus={} side={} price={} orderQty={} leavesQty={} cumQty={}",
                 clientOrderId, m_refDataHolder.getSecurityIdBySymbol(update.coin),
                 (int)mapOrderStatus(update.status), update.side,
                 update.limitPx, orderQty, leavesQty, cumQty);
    m_sbeWriter.writeMessage(sbeExecReport);

    if (update.status == hyperliquid::OrderStatus::Open) {
        // Replay any fills that arrived before this oid was registered
        replayBufferedFills(update.oid);
    } else {
        // Clean up mapping on terminal cancel/reject states
        if (m_ordersHandler) {
            m_ordersHandler->removeOrder(update.cloid);
        }
    }
}

void HyperliquidGWApplication::onUserFill(const hyperliquid::Fill& fill)
{
    spdlog::info("Fill coin={} side={} px={} sz={} oid={} snapshot={}",
                 fill.coin, fill.side, fill.px, fill.sz, fill.oid, fill.isSnapshot);

    if (fill.isSnapshot)
    {
        spdlog::info("Skipping snapshot fill");
        return;
    }

    // Check for timed-out buffered fills
    checkTimeouts();

    std::string clientOrderId;
    std::string cloid;
    if (m_ordersHandler) {
        clientOrderId = m_ordersHandler->lookupClientOrderIdByOid(fill.oid);
        cloid = m_ordersHandler->lookupCloidByOid(fill.oid);
    }

    if (clientOrderId.empty())
    {
        spdlog::warn("Fill for unknown oid={}, buffering until OrderUpdate arrives", fill.oid);
        m_bufferedFills.push_back({fill, std::chrono::steady_clock::now()});
        return;
    }

    emitFillExecutionReport(fill, clientOrderId, cloid);
}

void HyperliquidGWApplication::emitFillExecutionReport(const hyperliquid::Fill& fill, const std::string& clientOrderId, const std::string& cloid)
{
    // Apply fill to tracked state
    HyperliquidOrdersHandler::OrderState state{};
    if (m_ordersHandler) {
        state = m_ordersHandler->applyFill(cloid, fill.sz);
    }

    double leavesQty = std::max(0.0, state.origSz - state.cumQty);
    bool isFilled = leavesQty <= 0.0;

    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (!m_sbeWriter.prepareMessage(sbeExecReport))
    {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.transactTime(fill.time * 1000000ULL); // ms -> ns
    sbeExecReport.securityId(state.securityId ? state.securityId : m_refDataHolder.getSecurityIdBySymbol(fill.coin));
    sbeExecReport.ordStatus(isFilled
        ? com::liversedge::messages::OrdStatus::FILLED
        : com::liversedge::messages::OrdStatus::PARTIALLY_FILLED);
    sbeExecReport.side(mapSide(fill.side));
    sbeExecReport.orderType(state.orderType);
    sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::NO_REJECT);

    SBEUtils::setPrice(sbeExecReport.lastPx(), std::to_string(fill.px));
    SBEUtils::setQty(sbeExecReport.lastQty(), std::to_string(fill.sz));
    SBEUtils::setQty(sbeExecReport.orderQty(), std::to_string(state.origSz));
    SBEUtils::setQty(sbeExecReport.cumQty(), std::to_string(state.cumQty));
    SBEUtils::setQty(sbeExecReport.leavesQty(), std::to_string(leavesQty));

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");

    spdlog::info("Sending SBE Fill ExecutionReport clientOrderId={} securityId={} ordStatus={} side={} lastPx={} lastQty={} cumQty={} leavesQty={} orderQty={}",
                 clientOrderId, sbeExecReport.securityId(),
                 isFilled ? "FILLED" : "PARTIALLY_FILLED",
                 fill.side, fill.px, fill.sz, state.cumQty, leavesQty, state.origSz);
    m_sbeWriter.writeMessage(sbeExecReport);

    // Clean up on full fill (only for non-IOC; IOC cleanup via orderUpdate terminal state)
    if (isFilled && m_ordersHandler && !state.isIoc) {
        m_ordersHandler->removeOrder(cloid);
    }
}

// RestEndpointListener

void HyperliquidGWApplication::onPlaceOrder(const hyperliquid::PlaceOrderResponse& response,
                                             std::optional<uint64_t> correlationId)
{
    auto pending = takePending(m_pendingPlaces, correlationId, "PlaceOrder");

    if (response.status != "ok" && response.statuses.empty())
    {
        spdlog::error("PlaceOrder error: {}", m_lastPostResponse);
        if (!pending.cloid.empty())
        {
            sendNewOrderReject(pending.cloid, pending.securityId, response.status);
        }
        return;
    }

    if (response.status == "ok")
    {
        spdlog::info("PlaceOrder response status={}", response.status);
    }

    for (const auto& s : response.statuses)
    {
        if (s.error)
        {
            spdlog::error("PlaceOrder error: {} cloid={}", *s.error, pending.cloid);
            std::string clientOrderId;
            if (m_ordersHandler) clientOrderId = m_ordersHandler->lookupClientOrderId(pending.cloid);
            if (clientOrderId.empty()) {
                spdlog::info("PlaceOrder error for cloid={} but order already removed, skipping reject", pending.cloid);
            } else {
                sendNewOrderReject(pending.cloid, pending.securityId, *s.error);
            }
        }
        else if (s.resting)
        {
            spdlog::info("PlaceOrder resting oid={} cloid={}", s.resting->oid, pending.cloid);
        }
        else if (s.filled)
        {
            spdlog::info("PlaceOrder filled oid={} cloid={} avgPx={} totalSz={}",
                         s.filled->oid, pending.cloid, s.filled->avgPx, s.filled->totalSz);
        }
    }
}

void HyperliquidGWApplication::onModifyOrder(const hyperliquid::ModifyOrderResponse& response,
                                              std::optional<uint64_t> correlationId)
{
    auto pending = takePending(m_pendingModifies, correlationId, "ModifyOrder");

    if (response.status != "ok")
    {
        spdlog::error("ModifyOrder error: {}", m_lastPostResponse);
        if (!pending.cloid.empty())
        {
            std::string clientOrderId;
            if (m_ordersHandler) clientOrderId = m_ordersHandler->lookupClientOrderId(pending.cloid);
            if (clientOrderId.empty()) {
                spdlog::info("ModifyOrder error for cloid={} but order already removed, skipping reject", pending.cloid);
            } else {
                sendAmendReject(pending.cloid, pending.securityId, response.status);
            }
        }
    }
    else
    {
        spdlog::info("ModifyOrder response status={} cloid={}", response.status, pending.cloid);
    }

    for (const auto& s : response.statuses)
    {
        if (s.error)
        {
            spdlog::error("ModifyOrder error: {} cloid={}", *s.error, pending.cloid);
        }
        else if (s.resting)
        {
            spdlog::info("ModifyOrder resting oid={} cloid={}", s.resting->oid, pending.cloid);
        }
        else if (s.filled)
        {
            spdlog::info("ModifyOrder filled oid={} cloid={} avgPx={} totalSz={}",
                         s.filled->oid, pending.cloid, s.filled->avgPx, s.filled->totalSz);
        }
    }
}

void HyperliquidGWApplication::onCancelOrder(const hyperliquid::CancelOrderResponse& response,
                                              std::optional<uint64_t> correlationId)
{
    auto pending = takePending(m_pendingCancels, correlationId, "CancelOrder");

    if (response.status != "ok" && response.statuses.empty())
    {
        spdlog::error("CancelOrder error: {}", m_lastPostResponse);
        if (!pending.cloid.empty())
        {
            sendCancelReject(pending.cloid, pending.securityId, response.status);
        }
        return;
    }

    if (response.status == "ok")
    {
        spdlog::info("CancelOrder response status={}", response.status);
    }

    for (const auto& s : response.statuses)
    {
        if (s.error)
        {
            spdlog::error("CancelOrder error: {} cloid={}", *s.error, pending.cloid);
            sendCancelReject(pending.cloid, pending.securityId, *s.error);
        }
        else if (s.success)
        {
            spdlog::info("CancelOrder success: {}", *s.success);
        }
    }
}

HyperliquidGWApplication::PendingRequest HyperliquidGWApplication::takePending(
    std::unordered_map<uint64_t, PendingRequest>& map,
    std::optional<uint64_t> correlationId, const std::string& label)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (!correlationId)
    {
        spdlog::warn("{} response with no correlationId", label);
        return {};
    }
    auto it = map.find(*correlationId);
    if (it == map.end())
    {
        spdlog::warn("{} response for unknown correlationId={}", label, *correlationId);
        return {};
    }
    auto pending = it->second;
    map.erase(it);
    return pending;
}

uint64_t HyperliquidGWApplication::trackPendingPlace(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    uint64_t id = m_nextCorrelationId++;
    m_pendingPlaces[id] = {cloid, securityId};
    return id;
}

uint64_t HyperliquidGWApplication::trackPendingModify(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    uint64_t id = m_nextCorrelationId++;
    m_pendingModifies[id] = {cloid, securityId};
    return id;
}

uint64_t HyperliquidGWApplication::trackPendingCancel(const std::string& cloid, std::int32_t securityId)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    uint64_t id = m_nextCorrelationId++;
    m_pendingCancels[id] = {cloid, securityId};
    return id;
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
    if (m_ordersHandler) {
        clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);
        const auto* state = m_ordersHandler->getOrderState(cloid);
        if (state) sbeExecReport.side(state->side);
    }

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), reason);

    spdlog::info("Sending NewOrderReject clientOrderId={} reason={}", clientOrderId, reason);
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
    if (m_ordersHandler) {
        clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);
        const auto* state = m_ordersHandler->getOrderState(cloid);
        if (state) sbeExecReport.side(state->side);
    }

    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), reason);

    spdlog::info("Sending AmendReject clientOrderId={} reason={}", clientOrderId, reason);
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

    spdlog::info("Sending CancelReject clientOrderId={} reason={}", clientOrderId, reason);
    m_sbeWriter.writeMessage(sbeReject);
}

// Fill buffering

void HyperliquidGWApplication::replayBufferedFills(uint64_t oid)
{
    std::vector<hyperliquid::Fill> fills;
    for (auto it = m_bufferedFills.begin(); it != m_bufferedFills.end(); )
    {
        if (it->fill.oid == oid) {
            fills.push_back(it->fill);
            it = m_bufferedFills.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& fill : fills)
    {
        std::string clientOrderId;
        std::string cloid;
        if (m_ordersHandler) {
            clientOrderId = m_ordersHandler->lookupClientOrderIdByOid(oid);
            cloid = m_ordersHandler->lookupCloidByOid(oid);
        }
        spdlog::info("Replaying buffered fill oid={} clientOrderId={} px={} sz={}",
                     oid, clientOrderId, fill.px, fill.sz);
        emitFillExecutionReport(fill, clientOrderId, cloid);
    }
}

void HyperliquidGWApplication::checkTimeouts()
{
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_bufferedFills.begin(); it != m_bufferedFills.end(); )
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->bufferedAt).count();
        if (elapsed >= 5)
        {
            spdlog::critical("Buffered fill timed out after {}s: oid={} coin={} px={} sz={} - OrderUpdate never arrived",
                             elapsed, it->fill.oid, it->fill.coin, it->fill.px, it->fill.sz);
            it = m_bufferedFills.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // IOC order timeout: if an IOC order hasn't received a terminal state, auto-cancel
    if (m_ordersHandler) {
        m_ordersHandler->sweepIocTimeouts(now, m_iocTimeoutSecs, [this](const std::string& cloid,
                                          const HyperliquidOrdersHandler::OrderState& state) {
            spdlog::warn("IOC order timed out cloid={} origSz={} cumQty={} - sending cancel ER",
                         cloid, state.origSz, state.cumQty);

            std::string clientOrderId = m_ordersHandler->lookupClientOrderId(cloid);
            if (clientOrderId.empty()) return;

            com::liversedge::messages::ExecutionReport sbeExecReport;
            if (!m_sbeWriter.prepareMessage(sbeExecReport)) return;

            auto ts = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();

            sbeExecReport.timestamp(timestamp);
            sbeExecReport.transactTime(timestamp);
            sbeExecReport.securityId(state.securityId);
            sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::CANCELLED);
            sbeExecReport.side(com::liversedge::messages::Side::NULL_VALUE);
            sbeExecReport.orderType(state.orderType);
            sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::NO_REJECT);

            SBEUtils::setQty(sbeExecReport.orderQty(), std::to_string(state.origSz));
            SBEUtils::setQty(sbeExecReport.cumQty(), std::to_string(state.cumQty));
            SBEUtils::setQty(sbeExecReport.leavesQty(), std::to_string(0.0));

            SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
            SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
            SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "IOC timeout");

            spdlog::info("Sending IOC timeout cancel ER clientOrderId={} securityId={} orderQty={} cumQty={}",
                         clientOrderId, state.securityId, state.origSz, state.cumQty);
            m_sbeWriter.writeMessage(sbeExecReport);

            m_ordersHandler->removeOrder(cloid);
        });
    }
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
    case hyperliquid::OrderStatus::IocCancelRejected:
        return com::liversedge::messages::OrdStatus::REJECTED;
    default:
        return com::liversedge::messages::OrdStatus::REJECTED;
    }
}

com::liversedge::messages::Side::Value HyperliquidGWApplication::mapSide(char side)
{
    return (side == 'B') ? com::liversedge::messages::Side::BUY : com::liversedge::messages::Side::SELL;
}
