#include "../../include/fix/FIXRunner.h"
#include "../../include/util/ConsoleUtils.h"

FIXRunner::FIXRunner(SimpleConfig& config) : config_(config)
{
}

int FIXRunner::run(FIX::Application& application, const std::string& startupMessage)
{
    return run(application, startupMessage, ConsoleUtils::waitForUserInput, false);
}

int FIXRunner::run(FIX::Application& application, const std::string& startupMessage, std::function<void()> mainLoop,
                   bool logFixMessages)
{
    try
    {
        // Load configuration
        FIX::SessionSettings settings(config_.getString("fix_settings_file_path"));

        spdlog::info("{}", startupMessage);

        // Create stores and logs
        FIX::FileStoreFactory storeFactory(settings);
        if (logFixMessages)
        {
            FIX::FileLogFactory logFactory(settings);
            FIX::ThreadedSSLSocketInitiator initiator(application, storeFactory, settings, logFactory);
            runWithInitiator(mainLoop, initiator);
        }
        else
        {
            NullLogFactory logFactory;
            FIX::ThreadedSSLSocketInitiator initiator(application, storeFactory, settings, logFactory);
            runWithInitiator(mainLoop, initiator);
        }
    }
    catch (std::exception& e)
    {
        spdlog::error("Error: {}", e.what());
        return 1;
    }

    return 0;
}

void FIXRunner::runWithInitiator(std::function<void()> mainLoop, FIX::ThreadedSSLSocketInitiator& initiator)
{
    // Start the connection
    initiator.start();
    spdlog::info("FIX client started. Type q to quit...");

    // Run the main loop
    mainLoop();

    // Stop the connection
    initiator.stop();
}

