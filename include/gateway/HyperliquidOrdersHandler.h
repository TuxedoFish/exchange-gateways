#pragma once

#include <memory>
#include <iostream>
#include <unordered_map>
#include "../sbe/SBEMessageListener.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../sbe/SBEUtils.h"
#include "RefDataHolder.h"
#include "HyperliquidGWApplication.h"
#include "DeribitMessageConverter.h"

class HyperliquidGWApplication;

class HyperliquidOrdersHandler: public SBEMessageListener
{
public:
    explicit HyperliquidOrdersHandler(RefDataHolder& refDataHolder, HyperliquidGWApplication& gwApplication, SBEBinaryWriter& sbeWriter);
    ~HyperliquidOrdersHandler() = default;

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

    // Cloid mapping methods (called by HyperliquidGWApplication)
    std::string lookupClientOrderId(const std::string& cloid) const;
    std::string lookupClientOrderIdByOid(uint64_t oid) const;
    void registerOid(uint64_t oid, const std::string& cloid);
    void setActiveOid(uint64_t oid, const std::string& cloid);
    bool isActiveOid(uint64_t oid, const std::string& cloid) const;
    void removeOrder(const std::string& cloid);

    // Order state tracking for cumQty
    struct OrderState
    {
        double origSz = 0.0;
        double cumQty = 0.0;
        std::int32_t securityId = 0;
        com::liversedge::messages::OrderType::Value orderType = com::liversedge::messages::OrderType::LIMIT;
        com::liversedge::messages::OrderType::Value pendingOrderType = com::liversedge::messages::OrderType::LIMIT;
    };

    void initOrderState(const std::string& cloid, double origSz, std::int32_t securityId);
    void setPendingOrderType(const std::string& cloid, com::liversedge::messages::OrderType::Value orderType);
    void commitPendingOrderType(const std::string& cloid);
    com::liversedge::messages::OrderType::Value getOrderType(const std::string& cloid) const;
    OrderState applyFill(const std::string& cloid, double fillSz);
    std::string lookupCloidByOid(uint64_t oid) const;

private:
    bool m_isReplay = false;
    RefDataHolder& m_refDataHolder;
    HyperliquidGWApplication& m_gwApplication;
    SBEBinaryWriter& m_sbeWriter;

    void sendCancelReject(com::liversedge::messages::CancelOrder& cancelOrder);
    void sendNewOrderReject(com::liversedge::messages::NewOrder& newOrder);

    static hyperliquid::Tif mapTif(com::liversedge::messages::TimeInForce::Value timeInForce,
                                   com::liversedge::messages::OrderType::Value orderType,
                                   std::uint8_t isPostOnly);

    // Bidirectional mapping: internal clientOrderId <-> Hyperliquid cloid
    std::unordered_map<std::string, std::string> m_clientToCloid;
    std::unordered_map<std::string, std::string> m_cloidToClient;
    // Exchange oid -> Hyperliquid cloid (for fill correlation)
    std::unordered_map<uint64_t, std::string> m_oidToCloid;
    // Cloid -> active oid (tracks current oid after amends)
    std::unordered_map<std::string, uint64_t> m_cloidToOid;

    // HL asset identity stored at place time (survives security removal)
    struct OrderAssetInfo
    {
        std::string asset;              // coin string (e.g. "BTC")
        std::optional<int> assetId;     // prediction market asset id
    };
    std::unordered_map<std::string, OrderAssetInfo> m_cloidToAsset;

    // Live order state for cumQty tracking (keyed by cloid)
    std::unordered_map<std::string, OrderState> m_cloidToState;
};
