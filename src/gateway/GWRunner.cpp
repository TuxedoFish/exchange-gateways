#include "../../include/gateway/GWRunner.h"
#include "../../include/gateway/DeribitOrdersHandler.h"
#include "../../include/gateway/DeribitGWApplication.h"
#include "../../include/util/SimpleConfig.h"
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

GWRunner::GWRunner(const SimpleConfig& config, SBEMessageListener& ordersHandler, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter)
    : m_config(config), m_ordersHandler(ordersHandler), m_refDataHolder(refDataHolder), m_sbeWriter(sbeWriter) {
    setupPollers();
}

void GWRunner::run() {
    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Replay streams
    spdlog::info("Replaying streams...");
    m_ordersHandler.setIsReplay(true);
    while (m_mdPoller->next()) {
    }
    while (m_gwInPoller->next()) {
    }
    m_ordersHandler.setIsReplay(false);
    spdlog::info("Finished replay.");

    while (true) {
        // Poll the marketdata queue
        while (m_mdPoller->next()) {
        }
        // Poll the inbound orders stream
        while (m_gwInPoller->next()) {
        }

        // Non-blocking check for input
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == 'q') {
                break;
            }
        }

        // Small sleep to prevent 100% CPU usage
        usleep(1000); // 1ms sleep
    }

    // Restore stdin to blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);
}

void GWRunner::setupPollers() {
    m_mdPoller = createPoller(m_config.getString("md_file_path"), m_ordersHandler);
    m_gwInPoller = createPoller(m_config.getString("gw_inbound_file_path"), m_ordersHandler);
}

std::unique_ptr<SBEQueuePoller> GWRunner::createPoller(std::string dataDirectory, SBEMessageListener& listener)
{
    auto poller = std::make_unique<SBEQueuePoller>(dataDirectory, listener);

    boost::filesystem::path path = boost::filesystem::path(dataDirectory) / "messages.sbe";
    spdlog::info("Reading from: {}", path.string());

    poller->readFrom(path, true); // true = live mode
    return poller;
}
