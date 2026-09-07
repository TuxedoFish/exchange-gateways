#include "../include/main.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

static void crashHandler(int signal)
{
    // Write directly to stderr in case spdlog is broken
    const char* name = "UNKNOWN";
    switch (signal) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGILL:  name = "SIGILL";  break;
    }

    // Backtrace (async-signal-safe enough for crash diagnostics)
    void* frames[64];
    int nframes = backtrace(frames, 64);

    // Try spdlog first (may work if heap isn't corrupted)
    spdlog::critical("Fatal signal {} ({}) received", name, signal);
    spdlog::critical("Backtrace ({} frames):", nframes);

    char** symbols = backtrace_symbols(frames, nframes);
    if (symbols) {
        for (int i = 0; i < nframes; i++) {
            spdlog::critical("  [{}] {}", i, symbols[i]);
        }
        free(symbols);
    }
    spdlog::default_logger()->flush();

    // Also dump raw backtrace to stderr as fallback
    fprintf(stderr, "\nFatal signal %s (%d) - backtrace:\n", name, signal);
    backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

    // Re-raise to get default behaviour (core dump if enabled)
    std::signal(signal, SIG_DFL);
    raise(signal);
}

static void installCrashHandlers()
{
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);
    std::signal(SIGBUS,  crashHandler);
    std::signal(SIGILL,  crashHandler);
}

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
    installCrashHandlers();

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
