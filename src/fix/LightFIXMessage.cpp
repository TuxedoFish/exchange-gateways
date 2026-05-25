#include "../../include/fix/LightFIXMessage.h"
#include <charconv>
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
    struct tm tm = {};
    tm.tm_year = (ts[0] - '0') * 1000 + (ts[1] - '0') * 100 +
                 (ts[2] - '0') * 10   + (ts[3] - '0') - 1900;
    tm.tm_mon  = (ts[4] - '0') * 10 + (ts[5] - '0') - 1;
    tm.tm_mday = (ts[6] - '0') * 10 + (ts[7] - '0');
    tm.tm_hour = (ts[9] - '0') * 10 + (ts[10] - '0');
    tm.tm_min  = (ts[12] - '0') * 10 + (ts[13] - '0');
    tm.tm_sec  = (ts[15] - '0') * 10 + (ts[16] - '0');

    time_t secs = timegm(&tm);
    uint64_t nanos = static_cast<uint64_t>(secs) * 1000000000ULL;

    // Parse fractional seconds after '.' at position 17
    if (ts.size() > 18) {
        size_t fracDigits = ts.size() - 18;
        uint64_t frac = 0;
        for (size_t i = 18; i < ts.size(); ++i) {
            frac = frac * 10 + (ts[i] - '0');
        }
        // Scale to nanoseconds based on number of fractional digits
        static constexpr uint64_t scale[] = {
            1000000000ULL, // 0 digits (unused)
            100000000ULL,  // 1 digit
            10000000ULL,   // 2 digits
            1000000ULL,    // 3 digits (millis)
            100000ULL,     // 4 digits
            10000ULL,      // 5 digits
            1000ULL,       // 6 digits (micros)
            100ULL,        // 7 digits
            10ULL,         // 8 digits
            1ULL,          // 9 digits (nanos)
        };
        if (fracDigits <= 9) {
            nanos += frac * scale[fracDigits];
        }
    }

    return nanos;
}
