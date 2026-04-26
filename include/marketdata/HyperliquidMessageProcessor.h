#pragma once

#include "MessageProcessor.h"
#include "hyperliquid/types/ResponseTypes.h"

class HyperliquidMessageProcessor : public MessageProcessor
{
public:
    explicit HyperliquidMessageProcessor(SBEBinaryWriter& writer);
    ~HyperliquidMessageProcessor() = default;

    void onConnected();
    void onDisconnected(bool hasError, const std::string& errMsg);
    void onMeta(const hyperliquid::MetaResponse& response);
    void onL2BookLevel(const hyperliquid::L2BookUpdate& book, const hyperliquid::PriceLevel& level);
    void onTrade(const hyperliquid::Trade& trade);

    void setDesiredCoins(const std::set<std::string>& desiredCoins);
private:
    struct PendingAsset {
        std::string name;
        int szDecimals;
        int securityId;
    };

    void emitSecurityDefinition(const PendingAsset& asset, double price);
    void emitSecurityDefinitionWithPricePrecision(const PendingAsset& asset, int instrumentPricePrecision);
    void drainTimedOutSecDefs(uint64_t bookTimeMs);

    std::set<std::string> m_desiredCoins;
    std::set<std::string> m_observedCoins;
    std::unordered_map<int, PendingAsset> m_pendingSecDefs;
    uint64_t m_connectedTimeMs{0};
    uint64_t m_metaReceivedTimeMs{0};
};
