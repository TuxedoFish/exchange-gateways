#pragma once

#include <cstdint>
#include <string_view>

struct LightFIXField {
    int tag;
    std::string_view value;
};

class LightFIXMessage {
public:
    void parse(std::string_view msg);
    std::string_view getField(int tag) const;
    int getIntField(int tag) const;
    int fieldCount() const { return m_count; }

    struct GroupView {
        const LightFIXField* begin;
        const LightFIXField* end;
        std::string_view getField(int tag) const;
    };
    GroupView getGroup(int delimiterTag, int index) const;

private:
    static constexpr int MAX_FIELDS = 512;
    LightFIXField m_fields[MAX_FIELDS];
    int m_count = 0;
};

// Fast FIX timestamp parser: "YYYYMMDD-HH:MM:SS.mmm" -> nanoseconds since epoch
uint64_t parseFIXTimestampNanos(std::string_view ts);
