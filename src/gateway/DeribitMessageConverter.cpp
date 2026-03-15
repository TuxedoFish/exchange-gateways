#include "../../include/gateway/DeribitMessageConverter.h"
#include <spdlog/spdlog.h>

void DeribitMessageConverter::convertOrderCancelReject(
    const FIX44::OrderCancelReject& message,
    com::liversedge::messages::OrderCancelReject& sbeReject,
    RefDataHolder& refDataHolder
)
{
    sbeReject.timestamp(extractSendingTimeFromFix(message));
    if (message.isSetField(FIX::FIELD::OrdStatus))
    {
        FIX::OrdStatus ordStatus{};
        message.get(ordStatus);
        sbeReject.ordStatus(SBEUtils::ordStatusFromFix(ordStatus));
    }
    if (message.isSetField(FIX::FIELD::ClOrdID))
    {
        std::string clientOrderId = message.getField(FIX::FIELD::ClOrdID);
        SBEUtils::setVarString(sbeReject, sbeReject.clientOrderId(), clientOrderId);
    }
    else
    {
        SBEUtils::setVarString(sbeReject, sbeReject.clientOrderId(), "");
    }
    if (message.isSetField(FIX::FIELD::OrigClOrdID))
    {
        std::string origClientOrderId = message.getField(FIX::FIELD::OrigClOrdID);
        SBEUtils::setVarString(sbeReject, sbeReject.origClientOrderId(), origClientOrderId);
    }
    else
    {
        SBEUtils::setVarString(sbeReject, sbeReject.origClientOrderId(), "");
    }
    if (message.isSetField(FIX::FIELD::Text))
    {
        SBEUtils::setVarString(sbeReject, sbeReject.text(), message.getField(FIX::FIELD::Text));
    }
    else
    {
        SBEUtils::setVarString(sbeReject, sbeReject.text(), "");
    }
}

void DeribitMessageConverter::convertExecutionReport(
    const FIX44::ExecutionReport& message,
    com::liversedge::messages::ExecutionReport& sbeExecReport,
    RefDataHolder& refDataHolder
)
{
    std::string origClientOrderId = message.getField(FIX::FIELD::OrigClOrdID);

    if (message.isSetField(FIX::FIELD::Symbol))
    {
        std::string symbol = message.getField(FIX::FIELD::Symbol);
        std::int32_t securityId = refDataHolder.getSecurityIdBySymbol(symbol);
        sbeExecReport.securityId(securityId);
    }

    sbeExecReport.timestamp(extractSendingTimeFromFix(message));

    if (message.isSetField(FIX::FIELD::TransactTime))
    {
        FIX::TransactTime transactTime;
        message.getField(transactTime);
        sbeExecReport.timestamp(transactTime.getValue().getTimeT() * 1000000000ULL);
    }
    FIX::OrdStatus ordStatus{};
    message.get(ordStatus);
    sbeExecReport.ordStatus(SBEUtils::ordStatusFromFix(ordStatus));
    if (message.isSetField(FIX::FIELD::Side))
    {
        FIX::Side side{};
        message.get(side);
        sbeExecReport.side(SBEUtils::sideFromFix(side));
    }
    if (message.isSetField(FIX::FIELD::OrdType))
    {
        FIX::OrdType ordType{};
        message.get(ordType);
        sbeExecReport.orderType(SBEUtils::ordTypeFromFix(ordType));
    } else
    {
        sbeExecReport.orderType(com::liversedge::messages::OrderType::LIMIT);
    }
    if (message.isSetField(FIX::FIELD::OrdRejReason))
    {
        FIX::OrdRejReason ordRejReason{};
        message.get(ordRejReason);
        sbeExecReport.ordRejReason(SBEUtils::ordRejReasonFromFix(ordRejReason));
    }
    if (message.isSetField(FIX::FIELD::LeavesQty))
    {
        SBEUtils::setQty(sbeExecReport.leavesQty(), message.getField(FIX::FIELD::LeavesQty));
    }
    if (message.isSetField(FIX::FIELD::CumQty))
    {
        SBEUtils::setQty(sbeExecReport.cumQty(), message.getField(FIX::FIELD::CumQty));
    }
    if (message.isSetField(FIX::FIELD::OrderQty))
    {
        SBEUtils::setQty(sbeExecReport.orderQty(), message.getField(FIX::FIELD::OrderQty));
    }
    if (message.isSetField(FIX::FIELD::Price))
    {
        SBEUtils::setPrice(sbeExecReport.price(), message.getField(FIX::FIELD::Price));
    }
    if (message.isSetField(FIX::FIELD::LastPx))
    {
        SBEUtils::setPrice(sbeExecReport.lastPx(), message.getField(FIX::FIELD::Price));
    }
    if (message.isSetField(FIX::FIELD::LastQty))
    {
        SBEUtils::setQty(sbeExecReport.lastQty(), message.getField(FIX::FIELD::LastQty));
    }
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), origClientOrderId);
    if (message.isSetField(FIX::FIELD::ClOrdID))
    {
        std::string clientOrderId = message.getField(FIX::FIELD::ClOrdID);
        SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), clientOrderId);
    }
    else
    {
        SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), "");
    }
    if (message.isSetField(FIX::FIELD::Text))
    {
        SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), message.getField(FIX::FIELD::Text));
    }
    else
    {
        SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");
    }
}

void DeribitMessageConverter::createInternalOrderCancelReject(
    com::liversedge::messages::CancelOrder& cancelOrder,
    com::liversedge::messages::OrderCancelReject& sbeReject
)
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    std::uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    sbeReject.timestamp(timestamp);
    sbeReject.securityId(cancelOrder.securityId());
    sbeReject.ordStatus(com::liversedge::messages::OrdStatus::REJECTED);

    std::string clientOrderId = SBEUtils::extractVarString(cancelOrder.clientOrderId(), cancelOrder.sbeBlockLength());
    std::string origClientOrderId = SBEUtils::extractVarString(cancelOrder.origClientOrderId(),
                                                               cancelOrder.sbeBlockLength(), clientOrderId.length());

    SBEUtils::setVarString(sbeReject, sbeReject.clientOrderId(), "");
    SBEUtils::setVarString(sbeReject, sbeReject.origClientOrderId(), origClientOrderId);
    SBEUtils::setVarString(sbeReject, sbeReject.text(), "orders_offline");
}

void DeribitMessageConverter::createNewOrderReject(
    com::liversedge::messages::NewOrder& newOrder,
    com::liversedge::messages::ExecutionReport& sbeExecReport
)
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    std::uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    sbeExecReport.timestamp(timestamp);
    sbeExecReport.securityId(newOrder.securityId());
    sbeExecReport.ordStatus(com::liversedge::messages::OrdStatus::REJECTED);
    sbeExecReport.side(newOrder.side());
    sbeExecReport.orderQty().mantissa(SBEUtils::getInt64(newOrder.buffer(),
                                                         newOrder.quantityEncodingOffset() + HEADER_LENGTH));
    sbeExecReport.price().mantissa(
        SBEUtils::getInt64(newOrder.buffer(), newOrder.priceEncodingOffset() + HEADER_LENGTH));
    sbeExecReport.ordRejReason(com::liversedge::messages::OrdRejReason::CONNECTION_OFFLINE);

    std::string clientOrderId = SBEUtils::extractVarString(newOrder.clientOrderId(), newOrder.sbeBlockLength());
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.clientOrderId(), "");
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.origClientOrderId(), clientOrderId);
    SBEUtils::setVarString(sbeExecReport, sbeExecReport.text(), "");
}

std::uint64_t DeribitMessageConverter::extractSendingTimeFromFix(const FIX::Message& message)
{
    try
    {
        FIX::SendingTime sendingTime;
        message.getHeader().getField(sendingTime);
        return sendingTime.getValue().getTimeT() * 1000000000ULL;
    }
    catch (const std::exception& e)
    {
        spdlog::warn("SendingTime not found in FIX message, using current time");
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
}
