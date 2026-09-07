# pragma once

#include <cstdint>
#include <string>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <quickfix/FieldTypes.h>
#include <quickfix/Message.h>
#include "../util/SimpleConfig.h"
#include "../util/AuthHandler.h"

class FixUtils
{
    public:
    static std::uint64_t convertFIXTimeToNanos(const FIX::UtcTimeStamp& fixTime);
    static void logFixMessage(const std::string& prefix, const FIX::Message& message);
    static void addDeribitAuth(FIX::Message& message, const SimpleConfig& config);
};
