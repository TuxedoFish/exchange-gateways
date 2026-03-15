#include "../../include/historical/MarketDataLogger.h"
#include <spdlog/spdlog.h>

MarketDataLogger::MarketDataLogger(const std::string& logDirectory)
    : m_logDirectory(logDirectory) {

    // Create the base log directory if it doesn't exist
    try {
        boost::filesystem::create_directories(m_logDirectory);
    }
    catch (const boost::filesystem::filesystem_error& e) {
        spdlog::error("Warning: Failed to create log directory '{}': {}", m_logDirectory, e.what());
    }
}

MarketDataLogger::~MarketDataLogger() {
    closeCurrentLogFile();
}

void MarketDataLogger::writeToLog(const std::string& direction, const std::string& message) {
    // ATOMIC OPERATION: Hold lock for entire write process
    std::lock_guard<std::mutex> lock(m_logMutex);

    try {
        // Ensure file is open (without additional locking)
        ensureLogFileOpenUnsafe();

        if (m_logFile.is_open()) {
            // Write: DIRECTION|RAW_FIX_MESSAGE
            m_logFile << direction << "|" << message << std::endl;
            m_logFile.flush();  // Ensure immediate write
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error writing to log: {}", e.what());
    }
}

void MarketDataLogger::setLogDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(m_logMutex);

    // Close current file if open
    closeCurrentLogFile();

    // Update directory and create if needed
    m_logDirectory = directory;
    try {
        boost::filesystem::create_directories(m_logDirectory);
    }
    catch (const boost::filesystem::filesystem_error& e) {
        spdlog::error("Warning: Failed to create log directory '{}': {}", m_logDirectory, e.what());
    }

    // Reset current date to force new file creation
    m_currentLogDate.clear();
}

std::string MarketDataLogger::getLogDirectory() const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    return m_logDirectory;
}

bool MarketDataLogger::isLogFileOpen() const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    return m_logFile.is_open();
}

std::string MarketDataLogger::getCurrentLogFilePath() const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    if (!m_currentLogDate.empty()) {
        return buildLogFilePath(m_currentLogDate);
    }
    return "";
}

std::string MarketDataLogger::getCurrentDateString() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y/%m/%d");
    return ss.str();
}

void MarketDataLogger::ensureLogFileOpenUnsafe() {
    // NOTE: This function assumes the caller already holds m_logMutex
    std::string currentDate = getCurrentDateString();

    // Check if we need to rotate to a new file
    if (currentDate != m_currentLogDate || !m_logFile.is_open()) {
        // Close current file if open
        if (m_logFile.is_open()) {
            m_logFile.close();
        }

        try {
            // Create directory structure for new date
            std::string year = currentDate.substr(0, 4);
            std::string month = currentDate.substr(5, 2);
            std::string directoryPath = m_logDirectory + "/" + year + "/" + month;
            boost::filesystem::create_directories(directoryPath);

            // Open new file
            std::string filename = buildLogFilePath(currentDate);
            m_logFile.open(filename, std::ios::app);

            if (m_logFile.is_open()) {
                m_currentLogDate = currentDate;
                spdlog::info("Opened new log file: {}", filename);
            }
            else {
                spdlog::error("Failed to open log file: {}", filename);
            }

        }
        catch (const boost::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error creating log directories: {}", e.what());
        }
        catch (const std::exception& e) {
            spdlog::error("Error opening log file: {}", e.what());
        }
    }
}

void MarketDataLogger::closeCurrentLogFile() {
    if (m_logFile.is_open()) {
        m_logFile.close();
        m_currentLogDate.clear();
    }
}

std::string MarketDataLogger::buildLogFilePath(const std::string& dateString) const {
    std::string year = dateString.substr(0, 4);
    std::string month = dateString.substr(5, 2);
    std::string day = dateString.substr(8, 2);

    return m_logDirectory + "/" + year + "/" + month + "/" + day + ".txt";
}