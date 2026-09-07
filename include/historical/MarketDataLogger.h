#pragma once

#include <fstream>
#include <boost/filesystem.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <string>
#include <iostream>
#include "quickfix/Message.h"

class MarketDataLogger {
public:
    // Constructor with default log directory
    explicit MarketDataLogger(const std::string& logDirectory = "market_data_logs");

    // Destructor to ensure file cleanup
    ~MarketDataLogger();

    // Main logging method
    void writeToLog(const std::string& direction, const std::string& message);

    // Configuration methods
    void setLogDirectory(const std::string& directory);
    std::string getLogDirectory() const;

    // Utility methods
    bool isLogFileOpen() const;
    std::string getCurrentLogFilePath() const;

private:
    // File management members
    std::ofstream m_logFile;
    std::string m_currentLogDate;
    std::string m_logDirectory;
    mutable std::mutex m_logMutex;  // Thread safety for file operations

    // Helper methods
    std::string getCurrentDateString() const;
    void ensureLogFileOpenUnsafe();  // Assumes caller holds mutex
    void closeCurrentLogFile();
    std::string formatTimestamp() const;
    std::string buildLogFilePath(const std::string& dateString) const;

    // Non-copyable
    MarketDataLogger(const MarketDataLogger&) = delete;
    MarketDataLogger& operator=(const MarketDataLogger&) = delete;
};
