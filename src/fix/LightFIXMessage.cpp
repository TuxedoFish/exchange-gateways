#include "../../include/fix/LightFIXMessage.h"
#include <charconv>
#include <cstring>
#include <ctime>

void LightFIXMessage::parse(std::string_view msg) {
    m_count = 0;
    const char* p = msg.data();
    const char* end = p + msg.size();

    while (p < end && m_count < MAX_FIELDS) {
        // Parse tag: digits before '='
        int tag = 0;
        while (p < end && *p != '=') {
            tag = tag * 10 + (*p - '0');
            ++p;
        }
        if (p >= end) break;
        ++p; // skip '='

        // Value runs until SOH or end of string
        const char* valStart = p;
        while (p < end && *p != '\x01') {
            ++p;
        }

        m_fields[m_count].tag = tag;
        m_fields[m_count].value = std::string_view(valStart, p - valStart);
        ++m_count;

        if (p < end) ++p; // skip SOH
    }
}

std::string_view LightFIXMessage::getField(int tag) const {
    for (int i = 0; i < m_count; ++i) {
        if (m_fields[i].tag == tag) return m_fields[i].value;
    }
    return {};
}

int LightFIXMessage::getIntField(int tag) const {
    auto sv = getField(tag);
    if (sv.empty()) return 0;
    int result = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), result);
    return result;
}

std::string_view LightFIXMessage::GroupView::getField(int tag) const {
    for (auto* p = begin; p != end; ++p) {
        if (p->tag == tag) return p->value;
    }
    return {};
}

LightFIXMessage::GroupView LightFIXMessage::getGroup(int delimiterTag, int index) const {
    int found = -1;
    int startIdx = -1;
    for (int i = 0; i < m_count; ++i) {
        if (m_fields[i].tag == delimiterTag) {
            ++found;
            if (found == index) {
                startIdx = i;
            } else if (found == index + 1) {
                return {&m_fields[startIdx], &m_fields[i]};
            }
        }
    }
    if (startIdx >= 0) {
        return {&m_fields[startIdx], &m_fields[m_count]};
    }
    return {nullptr, nullptr};
}

uint64_t parseFIXTimestampNanos(std::string_view ts) {
    // Format: YYYYMMDD-HH:MM:SS.fff...
    // Positions: 0123456789012345678...
    //
    // Cache the date-to-epoch conversion: timegm() is expensive and the date
    // portion (YYYYMMDD) changes at most once per day in chronological data.
    static uint64_t cachedDateBytes = 0;   // first 8 bytes as uint64_t
    static uint64_t cachedMidnightNanos = 0;

    uint64_t dateBytes;
    std::memcpy(&dateBytes, ts.data(), 8);

    if (dateBytes != cachedDateBytes) {
        struct tm tm = {};
        tm.tm_year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 +
                     (ts[2] - '0') * 10   + (ts[3] - '0') - 1900;
        tm.tm_mon  = (ts[4] - '0') * 10 + (ts[5] - '0') - 1;
        tm.tm_mday = (ts[6] - '0') * 10 + (ts[7] - '0');
        cachedMidnightNanos = static_cast<uint64_t>(timegm(&tm)) * 1000000000ULL;
        cachedDateBytes = dateBytes;
    }

    // Time-of-day: pure arithmetic, no syscall
    uint64_t nanos = cachedMidnightNanos;
    nanos += static_cast<uint64_t>((ts[9]  - '0') * 10 + (ts[10] - '0')) * 3600000000000ULL;
    nanos += static_cast<uint64_t>((ts[12] - '0') * 10 + (ts[13] - '0')) * 60000000000ULL;
    nanos += static_cast<uint64_t>((ts[15] - '0') * 10 + (ts[16] - '0')) * 1000000000ULL;

    // Parse fractional seconds after '.' at position 17
    if (ts.size() > 18) {
        size_t fracDigits = ts.size() - 18;
        uint64_t frac = 0;
        for (size_t i = 18; i < ts.size(); ++i) {
            frac = frac * 10 + (ts[i] - '0');
        }
        static constexpr uint64_t scale[] = {
            1000000000ULL, 100000000ULL, 10000000ULL, 1000000ULL,
            100000ULL,     10000ULL,     1000ULL,     100ULL,
            10ULL,         1ULL,
        };
        if (fracDigits <= 9) {
            nanos += frac * scale[fracDigits];
        }
    }

    return nanos;
}
