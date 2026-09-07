#include "../../include/sbe/SBEUtils.h"

#include <stdexcept>
#include <cstring>
#include <quickfix/FixValues.h>

using namespace com::liversedge::messages;

static constexpr int64_t kPow10[] = {
    1LL,
    10LL,
    100LL,
    1000LL,
    10000LL,
    100000LL,
    1000000LL,
    10000000LL,
    100000000LL,
    1000000000LL,
    10000000000LL,
    100000000000LL,
    1000000000000LL,
    10000000000000LL,
    100000000000000LL,
    1000000000000000LL,
    10000000000000000LL,
    100000000000000000LL,
    1000000000000000000LL,
};

int64_t SBEUtils::stringToMantissa(std::string_view str, int8_t exponent)
{
    const int scale = -exponent;
    const char* p = str.data();
    const char* end = p + str.size();

    bool negative = false;
    if (p < end && *p == '-') {
        negative = true;
        p++;
    }

    // Parse integer part
    int64_t intPart = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        intPart = intPart * 10 + (*p - '0');
        p++;
    }

    // Parse fractional part (up to 'scale' digits)
    int64_t fracPart = 0;
    int fracDigits = 0;
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9' && fracDigits < scale) {
            fracPart = fracPart * 10 + (*p - '0');
            fracDigits++;
            p++;
        }
    }

    // Pad fractional part to fill remaining decimal places
    if (fracDigits < scale) {
        fracPart *= kPow10[scale - fracDigits];
    }

    int64_t mantissa = intPart * kPow10[scale] + fracPart;
    return negative ? -mantissa : mantissa;
}

int64_t SBEUtils::powerOfTenMantissa(int decimals, int8_t exponent)
{
    int power = -decimals + (-exponent);
    if (power >= 0 && power < 19) {
        return kPow10[power];
    }
    return 0;
}

// setVarString is now a template in the header file

void SBEUtils::setQty(Qty& field, std::string_view value)
{
    field.mantissa(stringToMantissa(value, -8));
}

void SBEUtils::setPrice(Price& field, std::string_view value)
{
    field.mantissa(stringToMantissa(value, -8));
}

void SBEUtils::setDate(Date& field, const std::string& date)
{
    int dateInt = std::stoi(date);
    field.year(dateInt / 10000)
         .month((dateInt / 100) % 100)
         .day(dateInt % 100);
}

Currency::Value SBEUtils::currencyFromString(const std::string& currency)
{
    if (currency == "USD")
    {
        return Currency::USD;
    }
    if (currency == "BTC")
    {
        return Currency::BTC;
    }
    if (currency == "ETH")
    {
        return Currency::ETH;
    }
    if (currency == "USDC")
    {
        return Currency::USDC;
    }
    if (currency == "TRUMP")
    {
        return Currency::TRUMP;
    }
    if (currency == "UNI")
    {
        return Currency::UNI;
    }
    if (currency == "ADA")
    {
        return Currency::ADA;
    }
    if (currency == "BCH")
    {
        return Currency::BCH;
    }
    if (currency == "DOGE")
    {
        return Currency::DOGE;
    }
    return Currency::NULL_VALUE;
}

SettlType::Value SBEUtils::settlTypeFromString(const std::string& settlType)
{
    if (settlType == "D1")
    {
        return SettlType::D1;
    }
    if (settlType == "W1")
    {
        return SettlType::W1;
    }
    if (settlType == "M1")
    {
        return SettlType::M1;
    }
    if (settlType == "M3")
    {
        return SettlType::M3;
    }
    if (settlType == "0")
    {
        return SettlType::REGULAR; // Perp
    }
    return SettlType::NULL_VALUE;
}

SecurityType::Value SBEUtils::securityTypeFromString(const std::string& securityType)
{
    if (securityType == "FUT")
    {
        return SecurityType::FUT;
    }
    if (securityType == "FUTCO")
    {
        return SecurityType::FUTCO;
    }
    if (securityType == "FXSPOT")
    {
        return SecurityType::FXSPOT;
    }
    return SecurityType::NULL_VALUE;
}

OrdRejReason::Value SBEUtils::ordRejReasonFromFix(const FIX::OrdRejReason& ordRejReason)
{
    // TODO: Implement
    return OrdRejReason::Value::OTHER;
}

OrderType::Value SBEUtils::ordTypeFromFix(FIX::OrdType ordType)
{
    if (ordType == FIX::OrdType_MARKET)
    {
        return OrderType::MARKET;
    }
    return OrderType::LIMIT;
}

OrdStatus::Value SBEUtils::ordStatusFromFix(const FIX::OrdStatus& ordStatus)
{
    if (ordStatus == FIX::OrdStatus_NEW)
    {
        return OrdStatus::Value::NEW;
    }
    if (ordStatus == FIX::OrdStatus_PARTIALLY_FILLED)
    {
        return OrdStatus::Value::PARTIALLY_FILLED;
    } else if (ordStatus == FIX::OrdStatus_FILLED)
    {
        return OrdStatus::Value::FILLED;
    } else if (ordStatus == FIX::OrdStatus_CANCELED)
    {
        return OrdStatus::Value::CANCELLED;
    } else if (ordStatus == FIX::OrdStatus_REJECTED)
    {
        return OrdStatus::Value::REJECTED;
    } else if (ordStatus == FIX::OrdStatus_REPLACED)
    {
        return OrdStatus::Value::REPLACED;
    }
return OrdStatus::Value::NULL_VALUE;
}

Side::Value SBEUtils::sideFromFix(const FIX::Side side)
{
    if (side == FIX::Side_BUY)
    {
        return Side::Value::BUY;
    }
    if (side == FIX::Side_SELL)
    {
        return Side::Value::SELL;
    }
    return Side::Value::NULL_VALUE;
}

Dec SBEUtils::convertPrice(const Price& price)
{
    int64_t mantissa = price.mantissa();
    int8_t exponent = price.exponent();

    Dec value(mantissa);
    Dec scale = pow(Dec(10), exponent);
    return value * scale;
}

Dec SBEUtils::convertQty(const Qty& qty)
{
    int64_t mantissa = qty.mantissa();
    int8_t exponent = qty.exponent();

    Dec value(mantissa);
    Dec scale = pow(Dec(10), exponent);
    return value * scale;
}

std::string SBEUtils::extractVarString(const VarStringEncoding& varString, const int encodedLength, const int variableOffset)
{
    // VarStringEncoding has the length stored first, then the actual string data
    std::uint32_t stringLength = varString.length();

    if (stringLength > 0) {
        // The string data starts after the length field (4 bytes)
        const char* stringStart = varString.buffer() + encodedLength + variableOffset + HEADER_LENGTH;
        return std::string(stringStart, stringLength);
    }
    return "";
}

FIX::Side SBEUtils::convertSide(const Side::Value& sbeType)
{
    switch (sbeType) {
    case Side::BUY:
            return FIX::Side(FIX::Side_BUY);
        case Side::SELL:
            return FIX::Side(FIX::Side_SELL);
        default:
            throw std::invalid_argument("Unknown SBE Side value");
    }
}

FIX::OrdType SBEUtils::convertOrderType(const OrderType::Value& sbeType)
{
    switch (sbeType) {
        case OrderType::MARKET:
            return FIX::OrdType(FIX::OrdType_MARKET);
        case OrderType::LIMIT:
            return FIX::OrdType(FIX::OrdType_LIMIT);
        case OrderType::STOP:
            return FIX::OrdType(FIX::OrdType_STOP);
        case OrderType::STOP_LIMIT:
            return FIX::OrdType(FIX::OrdType_STOP_LIMIT);
        default:
            throw std::invalid_argument("Unknown SBE OrderType value");
    }
}

FIX::TimeInForce SBEUtils::convertTimeInForce(const TimeInForce::Value& sbeType)
{
    switch (sbeType) {
        case TimeInForce::IOC:
            return FIX::TimeInForce(FIX::TimeInForce_IMMEDIATE_OR_CANCEL);
        case TimeInForce::FOK:
            return FIX::TimeInForce(FIX::TimeInForce_FILL_OR_KILL);
        case TimeInForce::GTC:
            return FIX::TimeInForce(FIX::TimeInForce_GOOD_TILL_CANCEL);
        case TimeInForce::DAY:
            return FIX::TimeInForce(FIX::TimeInForce_DAY);
        default:
            throw std::invalid_argument("Unknown SBE TimeInForce value");
    }
}

std::int64_t SBEUtils::getInt64(const char* buffer, std::size_t offset)
{
    std::int64_t value;
    std::memcpy(&value, buffer + offset, sizeof(std::int64_t));
    return value;
}
