#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "../util/SimpleConfig.h"
#include "hyperliquid/rest/RestApi.h"
#include "hyperliquid/websocket/WebsocketMessageHandler.h"
#include "hyperliquid/websocket/WebsocketApiListener.h"
#include "hyperliquid/rest/RestEndpointListener.h"
#include "hyperliquid/rest/RestApiListener.h"
#include "hyperliquid/rest/RestApiMessageParser.h"
#include "hyperliquid/websocket/WebsocketMessageParser.h"
#include "hyperliquid/websocket/WebsocketApi.h"
#include "hyperliquid/types/RequestTypes.h"

struct DesiredOutcome {
    std::string underlying;
    std::string period;
};

class HyperliquidMDApplicationBase : public hyperliquid::WebsocketApiListener,
                                     public hyperliquid::RestApiListener,
                                     public hyperliquid::RestEndpointListener,
                                     public hyperliquid::WebsocketMessageHandler
{
public:
    HyperliquidMDApplicationBase(const SimpleConfig& config) : m_config{ config }, m_restParser(*this) {}
    virtual ~HyperliquidMDApplicationBase();

    // Lifecycle
    void start();
    void stop();

    // hyperliquid::WebsocketApiListener
    virtual void onMessage(const std::string& message) override;
    virtual void onConnected() override;
    virtual void onDisconnected(bool hasError, const std::string& errMsg) override;

    // hyperliquid::RestApiListener
    virtual void onMessage(const std::string& message, hyperliquid::RestEndpointType type) override;

    // hyperliquid::RestEndpointListener
    virtual void onMeta(const hyperliquid::MetaResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;
    virtual void onOutcomeMeta(const hyperliquid::OutcomeMetaResponse& response, std::optional<uint64_t> correlationId = std::nullopt) override;

    void refetchOutcomeMeta();

protected:
    virtual void subscribeToMarket(const std::string& coin);
    const SimpleConfig& m_config;
    std::set<std::string> m_desiredCoins;
    std::vector<DesiredOutcome> m_desiredOutcomes;
    std::vector<hyperliquid::AssetMeta> m_universe;

private:
    static hyperliquid::Environment getEnvironment(std::string envName);

    hyperliquid::ApiConfig m_apiConfig;
    std::unique_ptr<hyperliquid::WebsocketApi> m_marketData;
    std::unique_ptr<hyperliquid::RestApi> m_infoApi;
    hyperliquid::RestApiMessageParser m_restParser;
    hyperliquid::WebsocketMessageParser m_wsParser;
};
