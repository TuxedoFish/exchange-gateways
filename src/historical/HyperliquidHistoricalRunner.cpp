#include "../../include/historical/HyperliquidHistoricalRunner.h"
#include <spdlog/spdlog.h>
#include <boost/iostreams/device/mapped_file.hpp>

HyperliquidHistoricalRunner::HyperliquidHistoricalRunner(SimpleConfig& config)
    : MarketdataHistoricalRunnerBase(config)
{
}

std::set<std::string> HyperliquidHistoricalRunner::parseCoins(const std::string& coinsCsv)
{
    std::set<std::string> coins;
    std::stringstream ss(coinsCsv);
    std::string coin;
    while (std::getline(ss, coin, ','))
    {
        coins.insert(coin);
    }
    return coins;
}

std::vector<DesiredOutcome> HyperliquidHistoricalRunner::parseOutcomes(const std::string& outcomesCsv)
{
    std::vector<DesiredOutcome> outcomes;
    if (outcomesCsv.empty()) return outcomes;

    std::stringstream ss(outcomesCsv);
    std::string entry;
    while (std::getline(ss, entry, ','))
    {
        auto colonPos = entry.find(':');
        if (colonPos != std::string::npos)
        {
            outcomes.push_back({
                entry.substr(0, colonPos),
                entry.substr(colonPos + 1)
            });
        }
    }
    return outcomes;
}

// Prime state by finding a connect event and the meta responses that follow it.
// Only processes the connect + meta lines (not the entire file), so it's fast.
// Returns true if state was primed successfully.
static bool primeFromConnectSequence(
    const char* data, const char* end, bool searchBackwards,
    HyperliquidFileMessageProcessor& historicalProcessor,
    HyperliquidMessageProcessor& processor)
{
    const char* connectLine = nullptr;

    if (searchBackwards)
    {
        // Scan backwards to find the LAST connect event
        const char* lineEnd = end;
        for (const char* pos = end - 1; pos >= data; --pos)
        {
            if (*pos == '\n' || pos == data)
            {
                const char* lineStart = (pos == data) ? pos : pos + 1;
                if (lineEnd > lineStart)
                {
                    const char* pipePos = std::find(lineStart, lineEnd, '|');
                    if (pipePos != lineEnd)
                    {
                        std::string_view msg(pipePos + 1, lineEnd - pipePos - 1);
                        if (HyperliquidFileMessageProcessor::isConnect(msg))
                        {
                            connectLine = lineStart;
                            break;
                        }
                    }
                }
                lineEnd = pos;
            }
        }
    }
    else
    {
        // Scan forwards to find the FIRST connect event
        const char* pos = data;
        while (pos < end)
        {
            const char* lineEnd = std::find(pos, end, '\n');
            const char* pipePos = std::find(pos, lineEnd, '|');
            if (pipePos != lineEnd)
            {
                std::string_view msg(pipePos + 1, lineEnd - pipePos - 1);
                if (HyperliquidFileMessageProcessor::isConnect(msg))
                {
                    connectLine = pos;
                    break;
                }
            }
            pos = lineEnd + 1;
        }
    }

    if (!connectLine) return false;

    // Process just the connect line and the meta responses that follow.
    // Stop after we've seen a WebSocket data message (l2Book, bbo, trades)
    // which means meta loading is complete.
    processor.setShouldOutput(false);

    const char* pos = connectLine;
    int metaCount = 0;
    while (pos < end)
    {
        const char* lineEnd = std::find(pos, end, '\n');
        if (lineEnd == pos) { pos++; continue; }

        const char* pipePos = std::find(pos, lineEnd, '|');
        if (pipePos != lineEnd)
        {
            // Back up past \r
            const char* msgEnd = lineEnd;
            while (msgEnd > pipePos + 1 && *(msgEnd - 1) == '\r') msgEnd--;

            std::string_view msgStr(pipePos + 1, msgEnd - pipePos - 1);
            if (!msgStr.empty())
            {
                historicalProcessor.process(msgStr);

                // After connect + meta(s), stop once we see an actual market data message
                // (l2Book/bbo/trades indicate meta loading is done)
                if (metaCount > 0 &&
                    (msgStr.find("\"l2Book\"") != std::string_view::npos ||
                     msgStr.find("\"bbo\"") != std::string_view::npos ||
                     msgStr.find("\"trades\"") != std::string_view::npos))
                {
                    break;
                }

                // Track meta responses
                if (msgStr.find("\"universe\"") != std::string_view::npos ||
                    msgStr.find("\"outcomes\"") != std::string_view::npos)
                {
                    metaCount++;
                }
            }
        }
        pos = lineEnd + 1;
    }

    processor.setShouldOutput(true);
    return true;
}

int HyperliquidHistoricalRunner::run()
{
    // Fetch configuration
    const std::string rawCapturesLoc = config_.getString("md_raw_file_path");
    const std::string processedCapturesLoc = config_.getString("md_processed_file_path");
    const std::string startDateStr = config_.getString("start_date", "");
    const std::string endDateStr = config_.getString("end_date", "");

    if (startDateStr.empty() || endDateStr.empty()) {
        spdlog::error("Required start_date and end_date missing.");
        return 1;
    }

    auto desiredCoins = parseCoins(config_.getString("coins", "BTC"));
    auto desiredOutcomes = parseOutcomes(config_.getString("outcomes", ""));

    spdlog::info("Processing Hyperliquid data from {} until {} ({} coins, {} outcomes)",
                 startDateStr, endDateStr, desiredCoins.size(), desiredOutcomes.size());

    tm startDate = DateUtils::getDateFromString(startDateStr);
    tm endDate = DateUtils::getDateFromString(endDateStr);
    tm currentDate = startDate;

    SBEBinaryWriter writer{};
    writer.setBatchMode(true);

    HyperliquidMessageProcessor processor{ writer };
    processor.setDesiredCoins(desiredCoins);
    processor.setHistoricalMode(true);

    HyperliquidFileMessageProcessor historicalProcessor{ processor, writer, desiredOutcomes };

    auto processLine = [&](std::string_view msgStr) {
        historicalProcessor.process(msgStr);
    };

    bool statePrimed = false;

    if (!config_.getBool("from_start"))
    {
        // Prime state from the previous day's file
        tm previousDate = startDate;
        previousDate.tm_mday -= 1;
        std::string previousFilePath = findValidFilePath(rawCapturesLoc, previousDate);
        if (!previousFilePath.empty() && boost::filesystem::exists(previousFilePath)) {
            spdlog::info("Found previous day file: {}, priming state...", previousFilePath);

            boost::iostreams::mapped_file_source previousFile(previousFilePath);
            const char* data = previousFile.data();
            const char* end = data + previousFile.size();

            // Search backwards for the last connect+meta sequence
            statePrimed = primeFromConnectSequence(data, end, true,
                                                    historicalProcessor, processor);
            previousFile.close();

            if (!statePrimed) {
                spdlog::error("No connect message found in previous day file");
                return 0;
            }
            spdlog::info("State primed from previous day file");
        } else {
            spdlog::error("No previous day file found ({}), exiting early", previousFilePath);
            return 0;
        }
    }

    // Main processing loop
    while (true) {
        processor.setShouldOutput(true);

        std::string filePath = findValidFilePath(rawCapturesLoc, currentDate);

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

        spdlog::info("File size: {} MB", file.size() / (1024 * 1024));

        // If state hasn't been primed yet (from_start=true), find the first
        // connect+meta sequence in this file and prime from it
        if (!statePrimed)
        {
            spdlog::info("Priming state from first connect event in current file...");
            statePrimed = primeFromConnectSequence(data, end, false,
                                                    historicalProcessor, processor);
            if (!statePrimed)
            {
                spdlog::warn("No connect event found in {}, skipping file", filePath);
                currentDate.tm_mday += 1;
                continue;
            }
            spdlog::info("State primed successfully");
        }

        auto startTime = std::chrono::steady_clock::now();
        readFrom(data, end, processLine);

        // Flush remaining buffered writes
        writer.flushNow();

        auto endTime = std::chrono::steady_clock::now();

        auto totalTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
        int hours = totalTime / 3600;
        int minutes = (totalTime % 3600) / 60;
        int seconds = totalTime % 60;

        spdlog::info("Completed processing {} ({} messages) in {}h {}m {}s",
                     filePath, writer.getMessageCount(), hours, minutes, seconds);

        currentDate.tm_mday += 1;
        if (currentDate.tm_year == endDate.tm_year && currentDate.tm_mon == endDate.tm_mon && currentDate.tm_mday == endDate.tm_mday) {
            spdlog::info("Finished processing.");
            break;
        }
    }

    return 0;
}
