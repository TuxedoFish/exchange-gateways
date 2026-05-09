#include "../include/main.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

static void setupLogging(const SimpleConfig& config)
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    std::string logFilePath = config.getString("log_file_path", "");
    if (!logFilePath.empty())
    {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true));
    }

    sinks[0]->set_level(spdlog::level::info); // stdout: info+
    // file sink (if present) defaults to trace, so debug passes through

    auto logger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);
    spdlog::set_default_logger(logger);

    if (!logFilePath.empty())
    {
        spdlog::info("Logging to file: {}", logFilePath);
    }
}

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::info);

    CmdLineOptions options(argc, argv);
    std::string applicationName = "UNSET";
    if (options.cmdOptionExists("--app"))
    {
        applicationName = options.getCmdOption("--app");
    }
    // Defaults to e.g. settings.md-process.txt
    std::string configName = applicationName;
    if (options.cmdOptionExists("--config-override"))
    {
        configName = options.getCmdOption("--config-override");
    }

    SimpleConfig config("config/settings." + configName + ".txt");
    setupLogging(config);
    spdlog::info("Running as {}", applicationName);

    AppRunner app(config);

    // Marketdata
    if (applicationName.rfind("md-hist") != std::string::npos) {
        return app.runMarketdataHistoricalStorage();
    }
    if (applicationName.rfind("md-process") != std::string::npos) {
        return app.runProcessRawMarketdata();
    }
    if (applicationName.rfind("md-") != std::string::npos) {
        return app.runMarketdata();
    }

    // Gateway
    if (applicationName.rfind("gw-testnet", 0) != std::string::npos) {
        return app.runGateway();
    }
    if (applicationName.rfind("gw-prod", 0) != std::string::npos) {
        return app.runGateway();
    }
    spdlog::info("Unknown application: {}", applicationName);
    return 0;
}
