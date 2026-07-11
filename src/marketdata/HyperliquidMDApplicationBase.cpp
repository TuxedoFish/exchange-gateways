#include "../../include/marketdata/HyperliquidMDApplicationBase.h"
#include <spdlog/spdlog.h>
#include <set>
#include <algorithm>
#include <thread>

HyperliquidMDApplicationBase::~HyperliquidMDApplicationBase() = default;

void HyperliquidMDApplicationBase::start()
{
    std::stringstream desiredCoinsCsv(m_config.getString("coins", "BTC"));
    std::string coin;
    while(std::getline(desiredCoinsCsv, coin, ','))
    {
        m_desiredCoins.insert(coin);
    }

    // Parse outcomes config: "BTC:1d,ETH:1d" -> vector of {underlying, period}
    std::string outcomesCfg = m_config.getString("outcomes", "");
    if (!outcomesCfg.empty())
    {
        std::stringstream outcomesCsv(outcomesCfg);
        std::string entry;
        while (std::getline(outcomesCsv, entry, ','))
        {
            auto colonPos = entry.find(':');
            if (colonPos != std::string::npos)
            {
                m_desiredOutcomes.push_back({
                    entry.substr(0, colonPos),
                    entry.substr(colonPos + 1)
                });
            }
        }
        spdlog::info("Configured {} desired outcomes", m_desiredOutcomes.size());
    }

    m_apiConfig.env = getEnvironment(m_config.getString("environment"));

    m_infoApi = std::make_unique<hyperliquid::RestApi>(m_apiConfig, *this);

    m_marketData = std::make_unique<hyperliquid::WebsocketApi>(m_apiConfig, *this);
    m_marketData->start();
}

void HyperliquidMDApplicationBase::stop()
{
    if (m_marketData)
    {
        m_marketData->stop();
    }
    m_infoApi.reset();
}

void HyperliquidMDApplicationBase::subscribeToMarket(const std::string& coin)
{
    m_marketData->subscribe(hyperliquid::SubscriptionType::L2Book, {{"coin", coin}});
    m_marketData->subscribe(hyperliquid::SubscriptionType::Bbo, {{"coin", coin}});
    m_marketData->subscribe(hyperliquid::SubscriptionType::Trades, {{"coin", coin}});
}

// hyperliquid::WebsocketApiListener
void HyperliquidMDApplicationBase::onMessage(const std::string& message) {
    m_wsParser.crack(message, *this);
}

void HyperliquidMDApplicationBase::onConnected() {
    m_metaReceived = false;
    m_outcomeMetaReceived = false;
    m_infoApi->metaAsync();
    m_infoApi->metaAsync("xyz");
    if (!m_desiredOutcomes.empty())
    {
        m_infoApi->outcomeMetaAsync();
    }
}

void HyperliquidMDApplicationBase::onDisconnected(bool hasError, const std::string& errMsg) {
}

// hyperliquid::RestApiListener
void HyperliquidMDApplicationBase::onMessage(const std::string& message, hyperliquid::RestEndpointType type) {
    m_restParser.parse(message, type);
}

// hyperliquid::RestApiListener
void HyperliquidMDApplicationBase::onError(hyperliquid::RestEndpointType type, const std::string& errorMessage) {
    spdlog::warn("REST API error for {}: {}", hyperliquid::toString(type), errorMessage);

    if (type == hyperliquid::RestEndpointType::Meta && !m_metaReceived) {
        scheduleRetry(type);
    } else if (type == hyperliquid::RestEndpointType::OutcomeMeta && !m_outcomeMetaReceived) {
        scheduleRetry(type);
    }
}

void HyperliquidMDApplicationBase::scheduleRetry(hyperliquid::RestEndpointType type) {
    spdlog::info("Scheduling retry for {} in 5s", hyperliquid::toString(type));
    std::thread([this, type]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        switch (type) {
        case hyperliquid::RestEndpointType::Meta:
            if (!m_metaReceived && m_infoApi) {
                spdlog::info("Retrying meta request");
                m_infoApi->metaAsync();
            }
            break;
        case hyperliquid::RestEndpointType::OutcomeMeta:
            if (!m_outcomeMetaReceived && m_infoApi) {
                spdlog::info("Retrying outcomeMeta request");
                m_infoApi->outcomeMetaAsync();
            }
            break;
        default:
            break;
        }
    }).detach();
}

// hyperliquid::RestEndpointListener
void HyperliquidMDApplicationBase::onMeta(const hyperliquid::MetaResponse& response,
                                           std::optional<uint64_t> correlationId) {
    m_metaReceived = true;
    m_universe = response.universe;
    spdlog::info("Loaded {} assets", m_universe.size());

    for (const auto& coin : m_universe)
    {
        if (m_desiredCoins.find(coin.name) != m_desiredCoins.end())
        {
            subscribeToMarket(coin.name);
        }
    }
}

void HyperliquidMDApplicationBase::refetchOutcomeMeta()
{
    if (m_infoApi && !m_desiredOutcomes.empty())
    {
        m_infoApi->outcomeMetaAsync();
    }
}

void HyperliquidMDApplicationBase::onOutcomeMeta(const hyperliquid::OutcomeMetaResponse& response,
                                                   std::optional<uint64_t> correlationId)
{
    m_outcomeMetaReceived = true;
    spdlog::info("Loaded {} outcomes", response.outcomes.size());

    for (const auto& outcome : response.outcomes)
    {
        for (const auto& desired : m_desiredOutcomes)
        {
            if (outcome.description.underlying == desired.underlying &&
                outcome.description.period == desired.period)
            {
                spdlog::info("Matched outcome: {} index={} class={} expiryEpochMs={} targetPrice={}",
                             outcome.name, outcome.outcome,
                             outcome.description.outcomeClass,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 outcome.description.expiry.time_since_epoch()).count(),
                             outcome.description.targetPrice);

                for (int side = 0; side < static_cast<int>(outcome.sideSpecs.size()); side++)
                {
                    std::string coin = hyperliquid::outcomeCoin(outcome.outcome, side);
                    spdlog::info("  Subscribing to side {} ({}) coin={}",
                                 side, outcome.sideSpecs[side].name, coin);
                    subscribeToMarket(coin);
                }
                break;
            }
        }
    }
}

hyperliquid::Environment HyperliquidMDApplicationBase::getEnvironment(std::string envName)
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
