#include "../../include/historical/MarketdataHistoricalRunner.h"
#include <spdlog/spdlog.h>

MarketdataHistoricalRunner::MarketdataHistoricalRunner(SimpleConfig& config) : MarketdataHistoricalRunnerBase(config) {
}

int MarketdataHistoricalRunner::run() {
    // Fetch configuration
    const std::string rawFixCapturesLoc = config_.getString("md_raw_file_path");
    const std::string processedCapturesLoc = config_.getString("md_processed_file_path");
    const std::string dataDictionaryLoc = config_.getString("data_dictionary_file_path");
    const std::string startDateStr = config_.getString("start_date", "");
    const std::string endDateStr = config_.getString("end_date", "");

    if (startDateStr == "" || endDateStr == "") {
        spdlog::info("Required start_date and end_date missing.");
    }

    spdlog::info("Processing from {} until {}", startDateStr, endDateStr);
    tm startDate = DateUtils::getDateFromString(startDateStr);
    tm endDate = DateUtils::getDateFromString(endDateStr);
    tm currentDate = startDate;
    SBEBinaryWriter writer{};
    DeribitMessageProcessor processor{ writer };
    FileMessageProcessor historicalProcessor{ dataDictionaryLoc, processor, writer };

    auto processLine = [&](std::string_view msgStr) {
        historicalProcessor.process(std::string(msgStr));
    };

    if (!config_.getBool("from_start"))
    {
        // Check for previous day file and prime state if needed
        tm previousDate = startDate;
        previousDate.tm_mday -= 1;
        std::string previousFilePath = findValidFilePath(rawFixCapturesLoc, previousDate);
        if (!previousFilePath.empty() && boost::filesystem::exists(previousFilePath)) {
            spdlog::info("Found previous day file: {}, priming state...", previousFilePath);

            // Disable output while priming state
            processor.setShouldOutput(false);

            // Read file backwards to find last logon
            boost::iostreams::mapped_file_source previousFile(previousFilePath);
            const char* data = previousFile.data();
            const char* end = data + previousFile.size();

            // Find lines in reverse order
            const char* lineEnd = end;
            bool foundLogon = false;
            size_t linesToReplay = 0;
            const char* replayFrom;

            // Go backwards through the file to collect all lines
            for (const char* pos = end - 1; pos >= data; --pos) {
                if (*pos == '\n' || pos == data) {
                    const char* lineStart = (pos == data) ? pos : pos + 1;
                    if (lineEnd > lineStart) {
                        const char* pipePos = std::find(lineStart, lineEnd, '|');
                        if (pipePos != lineEnd) {
                            // Extract message part after pipe
                            std::string msgStr = getStringSafe(pipePos + 1, lineEnd - pipePos - 1);
                            linesToReplay++;
                            if (!msgStr.empty() & FileMessageProcessor::isLogon(msgStr)) {
                                replayFrom = lineStart;
                                foundLogon = true;
                                break;
                            }
                        }
                    }
                    lineEnd = pos;
                }
            }

            if (foundLogon) {
                readFrom(replayFrom, end, processLine);
            } else {
                spdlog::error("No logon message found in previous day file");
                return 0;
            }

            previousFile.close();
        } else {
            spdlog::error("No previous day file found ({}), exiting early", previousFilePath);
            return 0;
        }
    }

    // Main app loop
    while (true) {
        // Re-enable output for normal processing
        processor.setShouldOutput(true);

        std::string filePath = findValidFilePath(rawFixCapturesLoc, currentDate);

        if (filePath.empty() || !boost::filesystem::exists(filePath)) {
            spdlog::info("Could not find: {} exiting.", filePath);
            break;
        }

        // Construct datePath for output file
        std::string datePath = std::to_string(1900 + currentDate.tm_year)
            + kPathSeparator + getMonthDayString(currentDate.tm_mon + 1)
            + kPathSeparator + getMonthDayString(currentDate.tm_mday);

        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::string timeStr = std::ctime(&nowTime);
        if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();
        spdlog::info("Processing {} started at {}", filePath, timeStr);
        boost::iostreams::mapped_file_source file(filePath);
        historicalProcessor.nextFile(processedCapturesLoc + kPathSeparator + datePath);

        const char* data = file.data();
        const char* end = data + file.size();

        const char* lineStart = data;

        auto startTime = std::chrono::steady_clock::now();
        readFrom(lineStart, end, processLine);
        auto endTime = std::chrono::steady_clock::now();

        // Log completion for this file
        auto totalTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
        int hours = totalTime / 3600;
        int minutes = (totalTime % 3600) / 60;
        int seconds = totalTime % 60;

        spdlog::info("Completed processing {} in {}h {}m {}s", filePath, hours, minutes, seconds);

        currentDate.tm_mday += 1;
        if (currentDate.tm_year == endDate.tm_year && currentDate.tm_mon == endDate.tm_mon && currentDate.tm_mday == endDate.tm_mday) {
            spdlog::info("Finished processing.");
            break;
        }
    }

    // Cleanup
    return 0;
}
