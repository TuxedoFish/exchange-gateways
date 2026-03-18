#include "../../include/gateway/HyperliquidOrdersHandler.h"
#include <spdlog/spdlog.h>
#include <hyperliquid/types/RequestTypes.h>

HyperliquidOrdersHandler::HyperliquidOrdersHandler(RefDataHolder& refDataHolder, HyperliquidGWApplication& gwApplication, SBEBinaryWriter& sbeWriter)
    : m_refDataHolder(refDataHolder), m_gwApplication(gwApplication), m_sbeWriter(sbeWriter)
{
}

void HyperliquidOrdersHandler::onSecurityDefinition(com::liversedge::messages::SecurityDefinition& decoder, std::uint64_t timestamp)
{
    m_refDataHolder.onSecurityDefinition(decoder, timestamp);
}

void HyperliquidOrdersHandler::onNewOrder(com::liversedge::messages::NewOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        auto side = decoder.side();
        auto timeInForce = decoder.timeInForce();
        auto price = SBEUtils::convertPrice(decoder.price());
        auto quantity = SBEUtils::convertQty(decoder.quantity());
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());

        spdlog::info("Received SBE NewOrder clientOrderId={} securityId={} side={} tif={} price={} qty={}",
                     clientOrderId, securityId, (int)side, (int)timeInForce, price.str(8, std::ios_base::fixed), quantity.str(8, std::ios_base::fixed));

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("Security not found for ID: {}", securityId);
            sendNewOrderReject(decoder);
            return;
        }

        if (!m_gwApplication.isConnected()) {
            spdlog::error("Not connected, rejecting order {}", clientOrderId);
            sendNewOrderReject(decoder);
            return;
        }

        hyperliquid::OrderRequest order;
        order.asset = secInfo->getSymbol();
        order.isBuy = (side == com::liversedge::messages::Side::BUY);
        order.price = std::stod(price.str(8, std::ios_base::fixed));
        order.size = std::stod(quantity.str(8, std::ios_base::fixed));
        order.reduceOnly = false;
        order.limit = hyperliquid::LimitOrderType{mapTimeInForce(timeInForce)};
        std::string cloid = hyperliquid::generateCloid();
        m_clientToCloid[clientOrderId] = cloid;
        m_cloidToClient[cloid] = clientOrderId;
        order.cloid = cloid;

        spdlog::info("Sending placeOrder {} cloid={} ({}) price={} size={}",
                     clientOrderId, cloid, secInfo->getSymbol(), order.price, order.size);
        m_gwApplication.trackPendingPlace(cloid, securityId);
        m_gwApplication.getWebsocket().placeOrder({order}, hyperliquid::Grouping::Na);

    } catch (const std::exception& e) {
        spdlog::error("Error processing NewOrder: {}", e.what());
        sendNewOrderReject(decoder);
    }
}

void HyperliquidOrdersHandler::onAmendOrder(com::liversedge::messages::AmendOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        auto side = decoder.side();
        auto timeInForce = decoder.timeInForce();
        auto price = SBEUtils::convertPrice(decoder.price());
        auto quantity = SBEUtils::convertQty(decoder.quantity());
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());

        spdlog::info("Received SBE AmendOrder clientOrderId={} securityId={} side={} tif={} price={} qty={}",
                     clientOrderId, securityId, (int)side, (int)timeInForce, price.str(0, std::ios_base::fixed), quantity.str(0, std::ios_base::fixed));

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("Security not found for ID: {}", securityId);
            return;
        }

        if (!m_gwApplication.isConnected()) {
            spdlog::error("Not connected, cannot amend {}", clientOrderId);
            return;
        }

        auto it = m_clientToCloid.find(clientOrderId);
        if (it == m_clientToCloid.end()) {
            spdlog::error("No cloid mapping found for amend clientOrderId={}", clientOrderId);
            return;
        }
        const std::string& cloid = it->second;

        hyperliquid::OrderRequest order;
        order.asset = secInfo->getSymbol();
        order.isBuy = (side == com::liversedge::messages::Side::BUY);
        order.price = price.convert_to<double>();
        order.size = quantity.convert_to<double>();
        order.reduceOnly = false;
        order.limit = hyperliquid::LimitOrderType{mapTimeInForce(timeInForce)};
        order.cloid = cloid;

        hyperliquid::ModifyRequest modify;
        modify.cloid = cloid;
        modify.order = order;

        m_gwApplication.trackPendingModify(cloid, securityId);
        m_gwApplication.getWebsocket().modifyOrder(modify);
        spdlog::info("Sent modifyOrder for {} cloid={} ({})", clientOrderId, cloid, secInfo->getSymbol());

    } catch (const std::exception& e) {
        spdlog::error("Error processing AmendOrder: {}", e.what());
    }
}

void HyperliquidOrdersHandler::onCancelOrder(com::liversedge::messages::CancelOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());
        std::string origClientOrderId = SBEUtils::extractVarString(decoder.origClientOrderId(), decoder.sbeBlockLength(), clientOrderId.length());

        spdlog::info("Received SBE CancelOrder clientOrderId={} origClientOrderId={} securityId={}",
                     clientOrderId, origClientOrderId, securityId);

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("Security not found for ID: {}", securityId);
            sendCancelReject(decoder);
            return;
        }

        if (!m_gwApplication.isConnected()) {
            spdlog::error("Not connected, cannot cancel {}", origClientOrderId);
            sendCancelReject(decoder);
            return;
        }

        auto it = m_clientToCloid.find(origClientOrderId);
        if (it == m_clientToCloid.end()) {
            spdlog::error("No cloid mapping found for cancel origClientOrderId={}", origClientOrderId);
            sendCancelReject(decoder);
            return;
        }
        const std::string& cloid = it->second;

        hyperliquid::CancelByCloidRequest cancel;
        cancel.asset = secInfo->getSymbol();
        cancel.cloid = cloid;

        m_gwApplication.trackPendingCancel(cloid, securityId);
        m_gwApplication.getWebsocket().cancelOrderByCloid({cancel});
        spdlog::info("Sent cancelOrderByCloid for {} cloid={} ({})", origClientOrderId, cloid, secInfo->getSymbol());

    } catch (const std::exception& e) {
        spdlog::error("Error processing CancelOrder: {}", e.what());
        sendCancelReject(decoder);
    }
}

void HyperliquidOrdersHandler::sendCancelReject(com::liversedge::messages::CancelOrder& cancelOrder)
{
    com::liversedge::messages::OrderCancelReject sbeReject;
    if (m_sbeWriter.prepareMessage(sbeReject)) {
        DeribitMessageConverter::createInternalOrderCancelReject(cancelOrder, sbeReject);
        spdlog::info("Sending SBE OrderCancelReject securityId={}", cancelOrder.securityId());
        m_sbeWriter.writeMessage(sbeReject);
    }
}

void HyperliquidOrdersHandler::sendNewOrderReject(com::liversedge::messages::NewOrder& newOrder)
{
    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (m_sbeWriter.prepareMessage(sbeExecReport)) {
        DeribitMessageConverter::createNewOrderReject(newOrder, sbeExecReport);
        spdlog::info("Sending SBE NewOrderReject securityId={}", newOrder.securityId());
        m_sbeWriter.writeMessage(sbeExecReport);
    }
}

std::string HyperliquidOrdersHandler::lookupClientOrderId(const std::string& cloid) const
{
    auto it = m_cloidToClient.find(cloid);
    if (it != m_cloidToClient.end()) {
        return it->second;
    }
    spdlog::warn("No client order ID mapping found for cloid={}", cloid);
    return "";
}

std::string HyperliquidOrdersHandler::lookupClientOrderIdByOid(uint64_t oid) const
{
    auto oidIt = m_oidToCloid.find(oid);
    if (oidIt == m_oidToCloid.end()) {
        spdlog::warn("No cloid mapping found for oid={}", oid);
        return "";
    }
    return lookupClientOrderId(oidIt->second);
}

void HyperliquidOrdersHandler::registerOid(uint64_t oid, const std::string& cloid)
{
    m_oidToCloid[oid] = cloid;
}

void HyperliquidOrdersHandler::removeOrder(const std::string& cloid)
{
    auto clientIt = m_cloidToClient.find(cloid);
    if (clientIt != m_cloidToClient.end()) {
        m_clientToCloid.erase(clientIt->second);
        m_cloidToClient.erase(clientIt);
    }

    // Remove oid entries pointing to this cloid
    for (auto it = m_oidToCloid.begin(); it != m_oidToCloid.end(); ) {
        if (it->second == cloid) {
            it = m_oidToCloid.erase(it);
        } else {
            ++it;
        }
    }
}

hyperliquid::Tif HyperliquidOrdersHandler::mapTimeInForce(com::liversedge::messages::TimeInForce::Value tif)
{
    switch (tif)
    {
    case com::liversedge::messages::TimeInForce::GTC:
        return hyperliquid::Tif::Gtc;
    case com::liversedge::messages::TimeInForce::IOC:
        return hyperliquid::Tif::Ioc;
    case com::liversedge::messages::TimeInForce::FOK:
    default:
        return hyperliquid::Tif::Alo;
    }
}
