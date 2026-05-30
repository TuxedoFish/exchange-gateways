#include <cassert>
#include <cstdio>
#include <string>
#include "hyperliquid/websocket/WebsocketMessageParser.h"
#include "hyperliquid/websocket/WebsocketMessageHandler.h"

struct TestHandler : public hyperliquid::WebsocketMessageHandler
{
    hyperliquid::L2BookSnapshot lastSnapshot;
    int callCount = 0;

    void onL2Book(const hyperliquid::L2BookSnapshot& snapshot) override
    {
        // Copy snapshot (string_views point into parser-owned memory,
        // so convert px/sz to owned strings for later inspection)
        lastSnapshot.coin = snapshot.coin;
        lastSnapshot.time = snapshot.time;
        lastSnapshot.numBids = snapshot.numBids;
        lastSnapshot.numAsks = snapshot.numAsks;
        for (uint8_t i = 0; i < snapshot.numBids; i++)
            lastSnapshot.bids[i] = snapshot.bids[i];
        for (uint8_t i = 0; i < snapshot.numAsks; i++)
            lastSnapshot.asks[i] = snapshot.asks[i];
        callCount++;
    }
};

int main()
{
    // 20 bids, 20 asks — exactly L2_BOOK_MAX_LEVELS on each side
    const std::string msg = R"({"channel":"l2Book","data":{"coin":"xyz:AAPL","time":1778803273189,"levels":[[{"px":"297.73","sz":"1.465","n":1},{"px":"297.72","sz":"15.0","n":1},{"px":"297.63","sz":"4.202","n":1},{"px":"297.55","sz":"271.822","n":2},{"px":"297.53","sz":"17.103","n":1},{"px":"297.51","sz":"2.973","n":1},{"px":"297.47","sz":"0.15","n":1},{"px":"297.46","sz":"83.938","n":1},{"px":"297.45","sz":"2.453","n":1},{"px":"297.43","sz":"4.203","n":1},{"px":"297.42","sz":"42.752","n":3},{"px":"297.4","sz":"9.114","n":1},{"px":"297.39","sz":"3.535","n":1},{"px":"297.38","sz":"9.887","n":2},{"px":"297.37","sz":"9.136","n":2},{"px":"297.36","sz":"5.523","n":1},{"px":"297.34","sz":"6.349","n":1},{"px":"297.3","sz":"0.17","n":1},{"px":"297.29","sz":"5.947","n":1},{"px":"297.24","sz":"5.322","n":2}],[{"px":"297.84","sz":"11.988","n":1},{"px":"297.85","sz":"6.345","n":1},{"px":"297.86","sz":"6.318","n":1},{"px":"297.89","sz":"16.785","n":1},{"px":"297.9","sz":"2.441","n":1},{"px":"297.91","sz":"16.784","n":1},{"px":"297.92","sz":"12.687","n":1},{"px":"297.97","sz":"16.78","n":1},{"px":"297.98","sz":"19.028","n":1},{"px":"297.99","sz":"25.369","n":1},{"px":"298.0","sz":"16.779","n":1},{"px":"298.01","sz":"16.137","n":1},{"px":"298.02","sz":"1.616","n":1},{"px":"298.03","sz":"31.708","n":1},{"px":"298.07","sz":"22.185","n":1},{"px":"298.08","sz":"83.938","n":1},{"px":"298.09","sz":"9.887","n":1},{"px":"298.1","sz":"0.17","n":1},{"px":"298.14","sz":"2.747","n":1},{"px":"298.15","sz":"6.332","n":1}]]}})";

    hyperliquid::WebsocketMessageParser parser;
    TestHandler handler;

    parser.crack(msg, handler);

    printf("callCount: %d\n", handler.callCount);
    printf("coin: %s\n", handler.lastSnapshot.coin.c_str());
    printf("time: %lu\n", handler.lastSnapshot.time);
    printf("numBids: %d\n", handler.lastSnapshot.numBids);
    printf("numAsks: %d\n", handler.lastSnapshot.numAsks);

    assert(handler.callCount == 1 && "onL2Book should be called exactly once");
    assert(handler.lastSnapshot.coin == "xyz:AAPL");
    assert(handler.lastSnapshot.time == 1778803273189ULL);
    assert(handler.lastSnapshot.numBids == 20 && "Expected 20 bid levels");
    assert(handler.lastSnapshot.numAsks == 20 && "Expected 20 ask levels");

    // Spot-check first and last bid
    assert(handler.lastSnapshot.bids[0].px == "297.73");
    assert(handler.lastSnapshot.bids[19].px == "297.24");

    // Spot-check first and last ask
    assert(handler.lastSnapshot.asks[0].px == "297.84");
    assert(handler.lastSnapshot.asks[19].px == "298.15");

    printf("ALL TESTS PASSED\n");
    return 0;
}
