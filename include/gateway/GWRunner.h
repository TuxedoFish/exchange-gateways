#pragma once

#include <iostream>
#include <string>
#include <memory>
#include "../util/SimpleConfig.h"
#include "../sbe/SBEQueuePoller.h"
#include "../sbe/SBEBinaryWriter.h"
#include "DeribitOrdersHandler.h"

// Forward declaration
class DeribitGWApplication;
class RefDataHolder;

class GWRunner
{
public:
    explicit GWRunner(const SimpleConfig& config, SBEMessageListener& ordersHandler, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter);
    ~GWRunner() = default;

    // Instance run method with access to config
    void run();

private:
    const SimpleConfig& m_config;
    SBEMessageListener& m_ordersHandler;
    RefDataHolder& m_refDataHolder;
    SBEBinaryWriter& m_sbeWriter;
    std::unique_ptr<SBEQueuePoller> m_mdPoller;
    std::unique_ptr<SBEQueuePoller> m_gwInPoller;

    void setupPollers();
    std::unique_ptr<SBEQueuePoller> createPoller(std::string directory, SBEMessageListener& listener);
};