#pragma once

#include <memory>
#include "../sbe/SBEMessageListener.h"
#include "../sbe/SBEBinaryWriter.h"
#include "RefDataHolder.h"
#include "quickfix/fix44/NewOrderSingle.h"
#include "quickfix/fix44/OrderCancelRequest.h"
#include "quickfix/fix44/OrderCancelReplaceRequest.h"
#include "quickfix/Fields.h"
#include "../../include/gateway/DeribitGWApplication.h"
#include "../../include/gateway/DeribitMessageConverter.h"
#include "../../include/sbe/SBEUtils.h"
#include "../../include/fix/FIXCustomTags.h"
#include <iostream>

// Forward declaration to avoid circular dependency
class DeribitGWApplication;

class DeribitOrdersHandler : public SBEMessageListener
{
public:
    explicit DeribitOrdersHandler(RefDataHolder& refDataHolder, DeribitGWApplication& gwApplication, SBEBinaryWriter& sbeWriter);
    ~DeribitOrdersHandler() = default;

    // SBEMessageListener implementation
    void onConnectionStatus(com::liversedge::messages::ConnectionStatus& decoder, std::uint64_t timestamp) override {}
    void onSecurityDefinition(com::liversedge::messages::SecurityDefinition& decoder, std::uint64_t timestamp) override;
    void onSecurityStatus(com::liversedge::messages::SecurityStatus& decoder, std::uint64_t timestamp) override {}
    void onMDUpdate(com::liversedge::messages::MDUpdate& decoder, std::uint64_t timestamp) override {}
    void onMDFullBook(com::liversedge::messages::MDFullBook& decoder, std::uint64_t timestamp) override {}
    void onNewOrder(com::liversedge::messages::NewOrder& decoder, std::uint64_t timestamp) override;
    void onAmendOrder(com::liversedge::messages::AmendOrder& decoder, std::uint64_t timestamp) override;
    void onCancelOrder(com::liversedge::messages::CancelOrder& decoder, std::uint64_t timestamp) override;
    void onOrderCancelReject(com::liversedge::messages::OrderCancelReject& decoder, std::uint64_t timestamp) override {}
    void onExecutionReport(com::liversedge::messages::ExecutionReport& decoder, std::uint64_t timestamp) override {}

    // Access to reference data
    RefDataHolder& getRefDataHolder() { return m_refDataHolder; }
    void setIsReplay (const bool isReplay) { m_isReplay = isReplay; }

private:
    bool m_isReplay = false;
    RefDataHolder& m_refDataHolder;
    DeribitGWApplication& m_gwApplication;
    SBEBinaryWriter& m_sbeWriter;

    void sendCancelReject(com::liversedge::messages::CancelOrder& cancelOrder);
    void sendNewOrderReject(com::liversedge::messages::NewOrder& newOrder);
};