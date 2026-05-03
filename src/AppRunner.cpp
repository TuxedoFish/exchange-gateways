#include "../include/AppRunner.h"
#include "../include/gateway/RefDataHolder.h"
#include <spdlog/spdlog.h>
#include "../include/marketdata/HyperliquidMDApplication.h"
#include "../include/sbe/SBEBinaryWriter.h"
#include "../include/util/ConsoleUtils.h"
#include "../include/util/SimpleConfig.h"

AppRunner::AppRunner(SimpleConfig& config) : config_{config}
{
}

int AppRunner::runMarketdata()
{
    std::string exchangeName = (config_.getString("exchange_name", "UNSET"));

    if (exchangeName == "deribit") {
        // Create application
        DeribitApplication application(config_);

        // Create FIX runner and start session
        FIXRunner fixRunner(config_);
        std::string startupMessage = "Publishing messages to: " + config_.getString("md_file_path");

        return fixRunner.run(application, startupMessage);
    }
    if (exchangeName == "hyperliquid")
    {
        HyperliquidMDApplication application(config_);
        application.start();

        spdlog::info("Publishing messages to: {}", config_.getString("md_file_path"));
        ConsoleUtils::waitForUserInput();
        return 1;
    }

    spdlog::error("Unrecognized exchange type: {}", exchangeName);
    return 1;
}

int AppRunner::runGateway()
{
    // Create RefDataHolder
    RefDataHolder refDataHolder;

    // Create SBE writer
    SBEBinaryWriter sbeWriter;
    sbeWriter.openNewFile(config_.getString("gw_outbound_file_path") + kPathSeparator + "messages.sbe");

    std::string exchangeName = config_.getString("exchange_name", "UNSET");

    if (exchangeName == "deribit")
    {
        // Create application
        DeribitGWApplication application(config_, refDataHolder, sbeWriter);

        // Create FIX runner and gateway runner (passing application reference)
        FIXRunner fixRunner(config_);
        DeribitOrdersHandler ordersHandler(refDataHolder, application, sbeWriter);
        GWRunner gatewayRunner(config_, ordersHandler, refDataHolder, sbeWriter);
        std::string startupMessage = "Publishing inbound executions to: " + config_.getString("gw_inbound_file_path");

        return fixRunner.run(application, startupMessage, [&gatewayRunner]() { gatewayRunner.run(); }, true);
    }
    if (exchangeName == "hyperliquid")
    {
        HyperliquidGWApplication application(config_, refDataHolder, sbeWriter);
        application.start();

        HyperliquidOrdersHandler ordersHandler(refDataHolder, application, sbeWriter);
        application.setOrdersHandler(&ordersHandler);
        GWRunner gatewayRunner(config_, ordersHandler, refDataHolder, sbeWriter);
        gatewayRunner.run();

        application.stop();
        return 0;
    }

    spdlog::error("Unrecognized exchange type: {}", exchangeName);
    return 1;
}

int AppRunner::runProcessRawMarketdata()
{
    MarketdataHistoricalRunner runner(config_);
    return runner.run();
}

int AppRunner::runMarketdataHistoricalStorage()
{
    std::string exchangeName = config_.getString("exchange_name", "UNSET");

    if (exchangeName == "deribit") {
        // Create application
        DeribitPersister application(config_);

        // Create FIX runner and start session
        FIXRunner fixRunner(config_);
        std::string startupMessage = "Publishing raw FIX messages to: " + config_.getString("md_raw_file_path");

        return fixRunner.run(application, startupMessage);
    }
    if (exchangeName == "hyperliquid")
    {
        HyperliquidPersister application(config_);
        application.start();

        spdlog::info("Publishing raw WS messages to: {}", config_.getString("md_raw_file_path"));
        ConsoleUtils::waitForUserInput();
        application.stop();
        return 1;
    }

    spdlog::error("Unrecognized exchange type: {}", exchangeName);
    return 1;
}
