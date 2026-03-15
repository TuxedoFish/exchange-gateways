#pragma once

#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>

class DateUtils
{
public:
    static std::tm getDateFromString(const std::string& dateString);
};