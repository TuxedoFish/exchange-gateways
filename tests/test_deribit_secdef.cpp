#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <set>
#include <vector>

#include "historical/FileMessageProcessor.h"
#include "marketdata/DeribitMessageProcessor.h"
#include "sbe/SBEBinaryWriter.h"
#include "sbe/SBEQueuePoller.h"
#include "sbe/SBEMessageListener.h"
#include "sbe/SBEUtils.h"

static const char* FIXTURE_PATH = "tests/data/deribit_seclist_fixture.txt";
static const char* DATA_DICT_PATH = "config/Deribit_FIX44.xml";
static const char* TEMP_OUTPUT = "/tmp/test_deribit_secdef_output";

namespace msgs = com::liversedge::messages;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        failures++; \
    } else { \
        printf("  PASS: %s\n", msg); \
    } \
} while(0)

struct SecDefInfo {
    std::string symbol;
    msgs::Currency::Value baseCurrency;
    msgs::Currency::Value quoteCurrency;
    msgs::Currency::Value settlCurrency;
    msgs::Currency::Value positionCurrency;
    msgs::SecurityType::Value securityType;
    msgs::MarginingType::Value marginingType;
};

// Test listener that captures security definitions
struct TestListener : public SBEMessageListener
{
    std::vector<SecDefInfo> secDefs;

    void onSecurityDefinition(msgs::SecurityDefinition& decoder, std::uint64_t) override
    {
        SecDefInfo info;
        info.baseCurrency = decoder.baseCurrency();
        info.quoteCurrency = decoder.quoteCurrency();
        info.settlCurrency = decoder.settlCurrency();
        info.positionCurrency = decoder.positionCurrency();
        info.securityType = decoder.securityType();
        info.marginingType = decoder.marginingType();

        auto& sym = decoder.symbol();
        info.symbol = SBEUtils::extractVarString(sym, decoder.encodedLength());

        secDefs.push_back(info);
    }

    // Stubs for other callbacks
    void onConnectionStatus(msgs::ConnectionStatus&, std::uint64_t) override {}
    void onSecurityStatus(msgs::SecurityStatus&, std::uint64_t) override {}
    void onMDFullBook(msgs::MDFullBook&, std::uint64_t) override {}
    void onMDUpdate(msgs::MDUpdate&, std::uint64_t) override {}
    void onNewOrder(msgs::NewOrder&, std::uint64_t) override {}
    void onAmendOrder(msgs::AmendOrder&, std::uint64_t) override {}
    void onCancelOrder(msgs::CancelOrder&, std::uint64_t) override {}
    void onOrderCancelReject(msgs::OrderCancelReject&, std::uint64_t) override {}
    void onExecutionReport(msgs::ExecutionReport&, std::uint64_t) override {}
};

int main()
{
    int failures = 0;

    // Read fixture lines
    std::ifstream fixture(FIXTURE_PATH);
    if (!fixture.is_open())
    {
        printf("FAIL: Could not open fixture file %s\n", FIXTURE_PATH);
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(fixture, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    printf("Loaded %zu fixture lines\n", lines.size());

    // Set up processor and write SBE output
    SBEBinaryWriter writer;
    writer.openNewFile(TEMP_OUTPUT);
    DeribitMessageProcessor processor(writer);
    processor.setShouldOutput(true);
    FileMessageProcessor fileProcessor(DATA_DICT_PATH, processor, writer);

    // Set perp currencies — triggers SYMBOLS_004 to be treated as perp-only
    std::set<std::string> perpCurrencies = {"TRUMP", "ONDO", "XRP", "HYPE"};
    fileProcessor.setPerpCurrencies(perpCurrencies);

    // Process each line (strip the IN_ADMIN|/IN_APP|/OUT| prefix)
    for (const auto& l : lines)
    {
        auto pipePos = l.find('|');
        if (pipePos != std::string::npos)
        {
            std::string msg = l.substr(pipePos + 1);
            fileProcessor.process(msg);
        }
    }

    writer.close();

    // Read back using SBEQueuePoller
    TestListener listener;
    SBEQueuePoller poller(TEMP_OUTPUT, listener);
    poller.readFrom(TEMP_OUTPUT, false);
    while (poller.next()) {}

    printf("Found %zu security definitions\n", listener.secDefs.size());

    bool foundTrump = false;
    bool foundBtcPerp = false;

    for (const auto& sd : listener.secDefs)
    {
        if (sd.symbol.find("TRUMP_USDC") != std::string::npos)
        {
            foundTrump = true;
            printf("\n=== Checking TRUMP_USDC-PERPETUAL ===\n");
            CHECK(sd.baseCurrency == msgs::Currency::CONTRACT,
                  "baseCurrency=CONTRACT");
            CHECK(sd.quoteCurrency == msgs::Currency::USDC,
                  "quoteCurrency=USDC");
            CHECK(sd.settlCurrency == msgs::Currency::USDC,
                  "settlCurrency=USDC");
            CHECK(sd.positionCurrency == msgs::Currency::CONTRACT,
                  "positionCurrency=CONTRACT");
            CHECK(sd.securityType == msgs::SecurityType::FUT,
                  "securityType=FUT");
            CHECK(sd.marginingType == msgs::MarginingType::LINEAR,
                  "marginingType=LINEAR");
        }

        if (sd.symbol == "BTC-PERPETUAL")
        {
            foundBtcPerp = true;
            printf("\n=== Checking BTC-PERPETUAL (inverse) ===\n");
            CHECK(sd.positionCurrency == msgs::Currency::CONTRACT,
                  "positionCurrency=CONTRACT");
            CHECK(sd.marginingType == msgs::MarginingType::INVERSE,
                  "marginingType=INVERSE");
        }
    }

    CHECK(foundTrump, "TRUMP_USDC-PERPETUAL found in output");
    CHECK(foundBtcPerp, "BTC-PERPETUAL found in output");

    // Cleanup
    std::remove(TEMP_OUTPUT);
    std::remove((std::string(TEMP_OUTPUT) + ".idx").c_str());

    if (failures > 0)
    {
        printf("\n%d TEST(S) FAILED\n", failures);
        return 1;
    }

    printf("\nALL TESTS PASSED\n");
    return 0;
}
