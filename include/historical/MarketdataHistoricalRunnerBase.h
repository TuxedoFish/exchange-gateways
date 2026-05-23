#pragma once

#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <sstream>
#include <boost/iostreams/device/mapped_file.hpp>
#include "../util/SimpleConfig.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../util/DateUtils.h"

class MarketdataHistoricalRunnerBase
{
public:
    explicit MarketdataHistoricalRunnerBase(SimpleConfig& config);
    virtual ~MarketdataHistoricalRunnerBase() = default;

    virtual int run() = 0;

protected:
    SimpleConfig& config_;

    std::string getMonthDayString(int dayOrMonth);
    std::string findValidFilePath(const std::string& rawCapturesLoc, tm& currentDate);
    std::string getStringSafe(const char* data, size_t size);
    void logProgress(const char* current, const char* start, const char* end,
                     const std::chrono::steady_clock::time_point& startTime,
                     std::chrono::steady_clock::time_point& lastProgressTime);

    // Subclasses must implement: given a line (after the pipe), process it
    using LineProcessor = std::function<void(std::string_view)>;

    void readFrom(const char* lineStart, const char* end, LineProcessor processor);
};
