#include "../include/main.h"
#include <spdlog/spdlog.h>

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

    // Marketdata
    if (applicationName.rfind("md-hist") != std::string::npos) {
        spdlog::info("Running as md-hist: {}", applicationName);
        SimpleConfig config("config/settings." + configName + ".txt");
        AppRunner app(config);
        return app.runMarketdataHistoricalStorage();
    }
    if (applicationName.rfind("md-process") != std::string::npos) {
        spdlog::info("Running as md-process: {}", applicationName);
        SimpleConfig config("config/settings." + configName + ".txt");
        AppRunner app(config);
        return app.runProcessRawMarketdata();
    }
    if (applicationName.rfind("md-") != std::string::npos) {
        spdlog::info("Running as md: {}", applicationName);
        SimpleConfig config("config/settings." + configName + ".txt");
        AppRunner app(config);
        return app.runMarketdata();
    }

    // Gateway
    if (applicationName.rfind("gw-testnet", 0)  != std::string::npos) {
        spdlog::info("Running as gw: {}", applicationName);
        SimpleConfig config("config/settings." + configName + ".txt");
        AppRunner app(config);
        return app.runGateway();
    }
    if (applicationName.rfind("gw-prod", 0) != std::string::npos) {
        spdlog::info("Running as gw: {}", applicationName);
        SimpleConfig config("config/settings." + configName + ".txt");
        AppRunner app(config);
        return app.runGateway();
    }
    spdlog::info("Unknown application: {}", applicationName);
    return 0;
}
