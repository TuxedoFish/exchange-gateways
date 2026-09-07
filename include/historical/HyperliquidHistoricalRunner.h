#pragma once

#include "MarketdataHistoricalRunnerBase.h"
#include "HyperliquidFileMessageProcessor.h"
#include "../marketdata/HyperliquidMessageProcessor.h"
#include "../marketdata/HyperliquidMDApplicationBase.h"

class HyperliquidHistoricalRunner : public MarketdataHistoricalRunnerBase
{
public:
    explicit HyperliquidHistoricalRunner(SimpleConfig& config);
    int run() override;

private:
    std::set<std::string> parseCoins(const std::string& coinsCsv);
    std::vector<DesiredOutcome> parseOutcomes(const std::string& outcomesCsv);
};
