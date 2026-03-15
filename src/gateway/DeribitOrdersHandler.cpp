#include "../../include/gateway/DeribitOrdersHandler.h"
#include <spdlog/spdlog.h>

DeribitOrdersHandler::DeribitOrdersHandler(RefDataHolder& refDataHolder, DeribitGWApplication& gwApplication, SBEBinaryWriter& sbeWriter)
    : m_refDataHolder(refDataHolder), m_gwApplication(gwApplication), m_sbeWriter(sbeWriter)
{
}

void DeribitOrdersHandler::onSecurityDefinition(com::liversedge::messages::SecurityDefinition& decoder, std::uint64_t timestamp)
{
    m_refDataHolder.onSecurityDefinition(decoder, timestamp);
}

void DeribitOrdersHandler::onNewOrder(com::liversedge::messages::NewOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        auto side = decoder.side();
        auto orderType = decoder.orderType();
        auto timeInForce = decoder.timeInForce();
        auto price = SBEUtils::convertPrice(decoder.price());
        auto quantity = SBEUtils::convertQty(decoder.quantity());
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("OrdersHandler: Security not found for ID: {}", securityId);
            return;
        }

        FIX44::NewOrderSingle newOrderSingle;

        newOrderSingle.setField(FIX::ClOrdID(clientOrderId));
        newOrderSingle.setField(FIX::Symbol(secInfo->getSymbol()));
        newOrderSingle.setField(SBEUtils::convertSide(side));
        newOrderSingle.setField(FIX::OrderQty(quantity.convert_to<double>()));
        newOrderSingle.setField(SBEUtils::convertOrderType(orderType));

        if (orderType == com::liversedge::messages::OrderType::LIMIT) {
            // Always set post only flags
            newOrderSingle.setField(FIX::ExecInst(FIX::ExecInst_PARTICIPATE_DONT_INITIATE_NO_CROSS));
        }
        if (orderType != com::liversedge::messages::OrderType::MARKET) {
            newOrderSingle.setField(FIX::Price(price.convert_to<double>()));
        }

        newOrderSingle.setField(SBEUtils::convertTimeInForce(timeInForce));

        if (m_gwApplication.sendMessage(newOrderSingle)) {
            spdlog::info("OrdersHandler: Sent NewOrderSingle for {} ({})", clientOrderId, secInfo->getSymbol());
        } else {
            spdlog::error("OrdersHandler: Failed to send NewOrderSingle for {}", clientOrderId);
            sendNewOrderReject(decoder);
        }

    } catch (const std::exception& e) {
        spdlog::error("OrdersHandler: Error processing NewOrder: {}", e.what());
        sendNewOrderReject(decoder);
    }
}

void DeribitOrdersHandler::onAmendOrder(com::liversedge::messages::AmendOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        auto side = decoder.side();
        auto orderType = decoder.orderType();
        auto timeInForce = decoder.timeInForce();
        auto price = SBEUtils::convertPrice(decoder.price());
        auto quantity = SBEUtils::convertQty(decoder.quantity());
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("OrdersHandler: Security not found for ID: {}", securityId);
            return;
        }

        FIX44::OrderCancelReplaceRequest orderCancelReplaceRequest;

        orderCancelReplaceRequest.setField(FIX::ClOrdID(clientOrderId));
        orderCancelReplaceRequest.setField(FIX::Symbol(secInfo->getSymbol()));
        orderCancelReplaceRequest.setField(SBEUtils::convertSide(side));
        orderCancelReplaceRequest.setField(FIX::OrderQty(quantity.convert_to<double>()));
        orderCancelReplaceRequest.setField(SBEUtils::convertOrderType(orderType));

        if (orderType == com::liversedge::messages::OrderType::LIMIT) {
            // Always set post only flags
            orderCancelReplaceRequest.setField(FIX::ExecInst(FIX::ExecInst_PARTICIPATE_DONT_INITIATE_NO_CROSS));
        }
        if (orderType != com::liversedge::messages::OrderType::MARKET) {
            orderCancelReplaceRequest.setField(FIX::Price(price.convert_to<double>()));
        }

        orderCancelReplaceRequest.setField(SBEUtils::convertTimeInForce(timeInForce));

        if (m_gwApplication.sendMessage(orderCancelReplaceRequest)) {
            spdlog::info("OrdersHandler: Sent OrderCancelReplaceRequest for {} ({})", clientOrderId, secInfo->getSymbol());
        } else {
            spdlog::error("OrdersHandler: Failed to send NewOrderSingle for {}", clientOrderId);
            // TODO Handle internal reject
            // sendCancelReject(decoder);
        }

    } catch (const std::exception& e) {
        spdlog::error("OrdersHandler: Error processing AmendOrder: {}", e.what());
        // TODO Handle internal reject
        // sendCancelReject(decoder);
    }
}

void DeribitOrdersHandler::onCancelOrder(com::liversedge::messages::CancelOrder& decoder, std::uint64_t timestamp)
{
    if (m_isReplay)
    {
        return;
    }

    try {
        std::int32_t securityId = decoder.securityId();
        std::string clientOrderId = SBEUtils::extractVarString(decoder.clientOrderId(), decoder.sbeBlockLength());
        std::string origClientOrderId = SBEUtils::extractVarString(decoder.origClientOrderId(), decoder.sbeBlockLength(), clientOrderId.length());

        const SecurityInfo* secInfo = m_refDataHolder.getSecurityInfo(securityId);
        if (!secInfo) {
            spdlog::error("OrdersHandler: Security not found for ID: {}", securityId);
            return;
        }

        FIX44::OrderCancelRequest cancelRequest;

        cancelRequest.setField(FIX::ClOrdID(origClientOrderId));
        cancelRequest.setField(FIX::Symbol(secInfo->getSymbol()));

        if (m_gwApplication.sendMessage(cancelRequest)) {
            spdlog::info("OrdersHandler: Sent OrderCancelRequest for {} ({})", origClientOrderId, secInfo->getSymbol());
        } else {
            spdlog::error("OrdersHandler: Failed to send OrderCancelRequest for {}", origClientOrderId);
            sendCancelReject(decoder);
        }

    } catch (const std::exception& e) {
        spdlog::error("OrdersHandler: Error processing CancelOrder: {}", e.what());
        sendCancelReject(decoder);
    }
}

void DeribitOrdersHandler::sendCancelReject(com::liversedge::messages::CancelOrder& cancelOrder)
{
    com::liversedge::messages::OrderCancelReject sbeReject;
    if (m_sbeWriter.prepareMessage(sbeReject)) {
        DeribitMessageConverter::createInternalOrderCancelReject(cancelOrder, sbeReject);
        m_sbeWriter.writeMessage(sbeReject);
    }
}

void DeribitOrdersHandler::sendNewOrderReject(com::liversedge::messages::NewOrder& newOrder)
{
    com::liversedge::messages::ExecutionReport sbeExecReport;
    if (m_sbeWriter.prepareMessage(sbeExecReport)) {
        DeribitMessageConverter::createNewOrderReject(newOrder, sbeExecReport);
        m_sbeWriter.writeMessage(sbeExecReport);
    }
}

