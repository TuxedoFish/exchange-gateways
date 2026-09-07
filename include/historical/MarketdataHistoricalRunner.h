#pragma once

#include "MarketdataHistoricalRunnerBase.h"
#include "FileMessageProcessor.h"
#include "../marketdata/DeribitMessageProcessor.h"

class MarketdataHistoricalRunner : public MarketdataHistoricalRunnerBase
{
public:
    explicit MarketdataHistoricalRunner(SimpleConfig& config);
    int run() override;
};
