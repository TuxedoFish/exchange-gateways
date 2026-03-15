#include "../../include/marketdata/HyperliquidMDApplicationBase.h"
#include <spdlog/spdlog.h>
#include <set>

HyperliquidMDApplicationBase::~HyperliquidMDApplicationBase() = default;

void HyperliquidMDApplicationBase::start()
{
    std::stringstream desiredCoinsCsv(m_config.getString("coins", "BTC"));
    std::string coin;
    while(std::getline(desiredCoinsCsv, coin, ','))
    {
        m_desiredCoins.insert(coin);
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
    m_infoApi->metaAsync();
    m_infoApi->metaAsync("xyz");
}

void HyperliquidMDApplicationBase::onDisconnected(bool hasError, const std::string& errMsg) {
}

// hyperliquid::RestApiListener
void HyperliquidMDApplicationBase::onMessage(const std::string& message, hyperliquid::RestEndpointType type) {
    m_restParser.parse(message, type);
}

// hyperliquid::RestEndpointListener
void HyperliquidMDApplicationBase::onMeta(const hyperliquid::MetaResponse& response) {
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
