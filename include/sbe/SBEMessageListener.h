#pragma once

#include <cstdint>
#include "../../generated/com_liversedge_messages/ConnectionStatus.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"
#include "../../generated/com_liversedge_messages/SecurityStatus.h"
#include "../../generated/com_liversedge_messages/MDFullBook.h"
#include "../../generated/com_liversedge_messages/MDUpdate.h"
#include "../../generated/com_liversedge_messages/NewOrder.h"
#include "../../generated/com_liversedge_messages/CancelOrder.h"
#include "../../generated/com_liversedge_messages/OrderCancelReject.h"
#include "../../generated/com_liversedge_messages/ExecutionReport.h"
#include "com_liversedge_messages/AmendOrder.h"

class SBEMessageListener
{
public:
    virtual ~SBEMessageListener() = default;

    // Marketdata
    virtual void onConnectionStatus(com::liversedge::messages::ConnectionStatus& decoder, std::uint64_t timestamp) = 0;
    virtual void onSecurityDefinition(com::liversedge::messages::SecurityDefinition& decoder, std::uint64_t timestamp) = 0;
    virtual void onSecurityStatus(com::liversedge::messages::SecurityStatus& decoder, std::uint64_t timestamp) = 0;
    virtual void onMDFullBook(com::liversedge::messages::MDFullBook& decoder, std::uint64_t timestamp) = 0;
    virtual void onMDUpdate(com::liversedge::messages::MDUpdate& decoder, std::uint64_t timestamp) = 0;

    // Orders
    virtual void onNewOrder(com::liversedge::messages::NewOrder& decoder, std::uint64_t timestamp) = 0;
    virtual void onAmendOrder(com::liversedge::messages::AmendOrder& decoder, std::uint64_t timestamp) = 0;
    virtual void onCancelOrder(com::liversedge::messages::CancelOrder& decoder, std::uint64_t timestamp) = 0;
    virtual void onOrderCancelReject(com::liversedge::messages::OrderCancelReject& decoder, std::uint64_t timestamp) = 0;
    virtual void onExecutionReport(com::liversedge::messages::ExecutionReport& decoder, std::uint64_t timestamp) = 0;

    // Utility functions
    virtual void setIsReplay(bool isReplay) {}

};
