#pragma once

#include <chrono>
#include <iostream>
#include "quickfix/fix44/OrderCancelReject.h"
#include "quickfix/fix44/ExecutionReport.h"
#include "../../generated/com_liversedge_messages/OrderCancelReject.h"
#include "../../generated/com_liversedge_messages/ExecutionReport.h"
#include "../../generated/com_liversedge_messages/CancelOrder.h"
#include "../../generated/com_liversedge_messages/NewOrder.h"
#include "RefDataHolder.h"
#include "../sbe/SBEUtils.h"
#include <spdlog/spdlog.h>
#include "../../include/fix/FIXUtils.h"

class DeribitMessageConverter
{
public:
    static void convertOrderCancelReject(
        const FIX44::OrderCancelReject& fixMessage,
        com::liversedge::messages::OrderCancelReject& sbeMessage,
        RefDataHolder& refDataHolder
    );

    static void convertExecutionReport(
        const FIX44::ExecutionReport& fixMessage,
        com::liversedge::messages::ExecutionReport& sbeMessage,
        RefDataHolder& refDataHolder
    );

    static void createInternalOrderCancelReject(
        com::liversedge::messages::CancelOrder& cancelOrder,
        com::liversedge::messages::OrderCancelReject& sbeReject
    );

    static void createNewOrderReject(
        com::liversedge::messages::NewOrder& newOrder,
        com::liversedge::messages::ExecutionReport& sbeExecReport
    );

private:
    DeribitMessageConverter() = default;
    static std::uint64_t extractSendingTimeFromFix(const FIX::Message& message);
};