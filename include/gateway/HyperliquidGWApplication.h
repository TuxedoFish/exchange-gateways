#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../util/SimpleConfig.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../sbe/SBEUtils.h"
#include "RefDataHolder.h"
#include "hyperliquid/websocket/WebsocketApi.h"
#include "hyperliquid/websocket/WebsocketApiListener.h"
#include "hyperliquid/websocket/WebsocketMessageHandler.h"
#include "hyperliquid/websocket/WebsocketMessageParser.h"
#include "hyperliquid/rest/RestEndpointListener.h"
#include "hyperliquid/rest/RestApiMessageParser.h"
#include "../../generated/com_liversedge_messages/ExecutionReport.h"
#include "../../generated/com_liversedge_messages/OrderCancelReject.h"

class HyperliquidOrdersHandler;

class HyperliquidGWApplication : public hyperliquid::WebsocketApiListener,
                                  public hyperliquid::WebsocketMessageHandler,
                                  public hyperliquid::RestEndpointListener
{
public:
    HyperliquidGWApplication(SimpleConfig& config, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter);
    ~HyperliquidGWApplication() = default;

    void start();
    void stop();
    bool isConnected() const { return m_connected; }
    void setOrdersHandler(HyperliquidOrdersHandler* handler) { m_ordersHandler = handler; }
    hyperliquid::WebsocketApi& getWebsocket() { return *m_websocket; }

    // Track pending requests for post response correlation (returns correlationId)
    uint64_t trackPendingPlace(const std::string& cloid, std::int32_t securityId);
    uint64_t trackPendingModify(const std::string& cloid, std::int32_t securityId);
    uint64_t trackPendingCancel(const std::string& cloid, std::int32_t securityId);

    // WebsocketApiListener
    void onMessage(const std::string& message) override;
    void onPostResponse(const std::string& message, hyperliquid::RestEndpointType type,
                        std::optional<uint64_t> correlationId = std::nullopt) override;
    void onConnected() override;
    void onDisconnected(bool hasError, const std::string& errMsg) override;

    // WebsocketMessageHandler
    void onOrderUpdate(const hyperliquid::OrderUpdate& update) override;
    void onUserFill(const hyperliquid::Fill& fill) override;

    // RestEndpointListener
    void onPlaceOrder(const hyperliquid::PlaceOrderResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;
    void onModifyOrder(const hyperliquid::ModifyOrderResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;
    void onCancelOrder(const hyperliquid::CancelOrderResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;

private:
    SimpleConfig& m_config;
    RefDataHolder& m_refDataHolder;
    SBEBinaryWriter& m_sbeWriter;
    std::unique_ptr<hyperliquid::WebsocketApi> m_websocket;
    hyperliquid::ApiConfig m_apiConfig;
    hyperliquid::WebsocketMessageParser m_wsParser;
    hyperliquid::RestApiMessageParser m_restParser;
    bool m_connected = false;
    HyperliquidOrdersHandler* m_ordersHandler = nullptr;
    std::string m_lastPostResponse;

    struct PendingRequest
    {
        std::string cloid;
        std::int32_t securityId;
    };
    mutable std::mutex m_pendingMutex;
    std::atomic<uint64_t> m_nextCorrelationId{1};
    std::unordered_map<uint64_t, PendingRequest> m_pendingPlaces;
    std::unordered_map<uint64_t, PendingRequest> m_pendingModifies;
    std::unordered_map<uint64_t, PendingRequest> m_pendingCancels;

    PendingRequest takePending(std::unordered_map<uint64_t, PendingRequest>& map,
                               std::optional<uint64_t> correlationId, const std::string& label);

    void sendNewOrderReject(const std::string& cloid, std::int32_t securityId, const std::string& reason);
    void sendAmendReject(const std::string& cloid, std::int32_t securityId, const std::string& reason);
    void sendCancelReject(const std::string& cloid, std::int32_t securityId, const std::string& reason);

    static hyperliquid::Environment getEnvironment(const std::string& envName);
    static com::liversedge::messages::OrdStatus::Value mapOrderStatus(hyperliquid::OrderStatus status);
    static com::liversedge::messages::Side::Value mapSide(char side);

    // Fill buffering for race condition: fills arriving before oid registration
    struct BufferedFill
    {
        hyperliquid::Fill fill;
        std::chrono::steady_clock::time_point bufferedAt;
    };
    std::vector<BufferedFill> m_bufferedFills;

    void replayBufferedFills(uint64_t oid);
    void checkBufferedFillTimeouts();
    void emitFillExecutionReport(const hyperliquid::Fill& fill, const std::string& clientOrderId, const std::string& cloid);
};
