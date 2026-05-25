#pragma once

#include "MessageProcessor.h"
#include "hyperliquid/types/ResponseTypes.h"
#include "hyperliquid/types/RequestTypes.h"

struct DesiredOutcome;

class HyperliquidMessageProcessor : public MessageProcessor
{
public:
    explicit HyperliquidMessageProcessor(SBEBinaryWriter& writer);
    ~HyperliquidMessageProcessor() = default;

    void onConnected();
    void onDisconnected(bool hasError, const std::string& errMsg);
    void onMeta(const hyperliquid::MetaResponse& response);
    void onOutcomeMeta(const hyperliquid::OutcomeMetaResponse& response,
                       const std::vector<DesiredOutcome>& desiredOutcomes);
    void onL2Book(const hyperliquid::L2BookSnapshot& snapshot);
    void onBbo(const hyperliquid::BboUpdate& update);
    void onTrade(const hyperliquid::Trade& trade);

    void setDesiredCoins(const std::set<std::string>& desiredCoins);
    void setHistoricalMode(bool enabled);

    bool hasExpiredOutcomes() const;
    void removeExpiredOutcomes();
    bool shouldRefetchOutcomeMeta() const;

private:
    struct PendingAsset {
        std::string name;
        int szDecimals;
        int securityId;
    };

    struct OutcomeInstrument {
        std::string symbol;       // "BTC-1D-YES"
        std::string coin;         // "#1230"
        std::string underlying;   // "BTC"
        std::string targetPrice;  // Strike/threshold (e.g. "70000")
        std::string sideName;     // "Yes" or "No"
        int outcomeIndex;
        int side;
        std::chrono::system_clock::time_point expiry;
        int securityId;
    };

    void emitSecurityDefinition(const PendingAsset& asset, double price);
    void emitSecurityDefinitionWithPricePrecision(const PendingAsset& asset, int instrumentPricePrecision);
    void emitOutcomeSecurityDefinition(const OutcomeInstrument& outcome);
    void drainTimedOutSecDefs(uint64_t bookTimeMs);

    static std::string buildOutcomeSymbol(const std::string& underlying,
                                          const std::string& period,
                                          const std::string& sideName);

    std::set<std::string> m_desiredCoins;
    std::set<std::string> m_observedCoins;
    std::unordered_map<int, PendingAsset> m_pendingSecDefs;
    std::unordered_map<int, OutcomeInstrument> m_pendingOutcomeSecDefs;
    std::vector<OutcomeInstrument> m_activeOutcomes;
    uint64_t m_connectedTimeMs{0};
    uint64_t m_metaReceivedTimeMs{0};
    std::chrono::system_clock::time_point m_lastOutcomeExpiry{};
    bool m_pendingRefetch{false};
    bool m_historicalMode{false};
};
