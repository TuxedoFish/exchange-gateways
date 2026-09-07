#include "../../include/historical/MarketdataHistoricalRunnerBase.h"
#include <spdlog/spdlog.h>

MarketdataHistoricalRunnerBase::MarketdataHistoricalRunnerBase(SimpleConfig& config) : config_{ config } {
}

std::string MarketdataHistoricalRunnerBase::getMonthDayString(int dayOrMonth) {
    if (dayOrMonth > 9) {
        return std::to_string(dayOrMonth);
    }
    else {
        return "0" + std::to_string(dayOrMonth);
    }
}

std::string MarketdataHistoricalRunnerBase::findValidFilePath(const std::string& rawCapturesLoc, tm& currentDate) {
    std::string datePath = std::to_string(1900 + currentDate.tm_year)
        + kPathSeparator + getMonthDayString(currentDate.tm_mon + 1)
        + kPathSeparator + getMonthDayString(currentDate.tm_mday);
    std::string filePath = rawCapturesLoc + kPathSeparator + datePath + ".txt";

    if (!boost::filesystem::exists(filePath)) {
        if (currentDate.tm_mday < 28) {
            return ""; // Signal file not found
        }

        // Try the next month / year
        currentDate.tm_mday = 1;
        currentDate.tm_mon += 1;
        if (currentDate.tm_mon > 11) {
            currentDate.tm_mon = 0; // Reset to January
            currentDate.tm_year += 1;
        }
        datePath = std::to_string(1900 + currentDate.tm_year)
            + kPathSeparator + getMonthDayString(currentDate.tm_mon + 1)
            + kPathSeparator + getMonthDayString(currentDate.tm_mday);
        filePath = rawCapturesLoc + kPathSeparator + datePath + ".txt";
    }

    return filePath;
}

std::string MarketdataHistoricalRunnerBase::getStringSafe(const char* data, size_t size) {
    // Fast path: use SIMD-optimized memchr to check for null bytes
    if (std::memchr(data, '\0', size) == nullptr) {
        return std::string(data, size);
    }

    // Slow path: null bytes found — corrupted message, discard
    spdlog::error("WARNING: Null bytes detected in message of length {} - DISCARDING MESSAGE due to corruption", size);
    return "";
}

void MarketdataHistoricalRunnerBase::logProgress(const char* current, const char* start, const char* end,
                                             const std::chrono::steady_clock::time_point& startTime,
                                             std::chrono::steady_clock::time_point& lastProgressTime) {
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastProgress = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastProgressTime).count();

    if (timeSinceLastProgress >= 5) {
        size_t totalBytes = end - start;
        size_t processedBytes = current - start;
        double progressPercent = (static_cast<double>(processedBytes) / totalBytes) * 100.0;
        auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();

        if (progressPercent > 0) {
            double estimatedTotalSeconds = (elapsedTime * 100.0) / progressPercent;
            double remainingSeconds = estimatedTotalSeconds - elapsedTime;

            int remainingHours = static_cast<int>(remainingSeconds) / 3600;
            int remainingMinutes = (static_cast<int>(remainingSeconds) % 3600) / 60;
            int remainingSecs = static_cast<int>(remainingSeconds) % 60;

            spdlog::info("Progress: {:.2f}% - Estimated time remaining: {}h {}m {}s",
                         progressPercent, remainingHours, remainingMinutes, remainingSecs);
        }

        lastProgressTime = currentTime;
    }
}

void MarketdataHistoricalRunnerBase::readFrom(const char* lineStart, const char* end, LineProcessor processor)
{
    const char* fileStart = lineStart;
    auto startTime = std::chrono::steady_clock::now();
    auto lastProgressTime = startTime;

    while (lineStart < end) {
        const char* lineEnd = std::find(lineStart, end, '\n');

        if (lineEnd == lineStart) {
            lineStart++;
            continue;
        }

        const char* pipePos = std::find(lineStart, lineEnd, '|');
        if (pipePos != lineEnd) {
            // Find the actual end of the message data (before \r\n)
            const char* msgEnd = lineEnd;

            // Back up past any \r characters (Windows)
            while (msgEnd > pipePos + 1 && *(msgEnd - 1) == '\r') {
                msgEnd--;
            }

            size_t msgLen = msgEnd - pipePos - 1;

            // Check for null bytes (corrupted data)
            if (std::memchr(pipePos + 1, '\0', msgLen) != nullptr) {
                spdlog::error("Null bytes in message of length {} - discarding", msgLen);
                lineStart = lineEnd + 1;
                continue;
            }

            std::string_view msg(pipePos + 1, msgLen);

            try {
                processor(msg);
            }
            catch (const std::exception& e) {
                spdlog::error("Message processing error: {}", e.what());
            }
        }

        logProgress(lineStart, fileStart, end, startTime, lastProgressTime);

        lineStart = lineEnd + 1;
    }
}
