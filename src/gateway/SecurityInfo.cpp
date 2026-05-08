#include "../../include/gateway/SecurityInfo.h"
#include "../../include/sbe/SBEUtils.h"
#include <sstream>

SecurityInfo::SecurityInfo(const com::liversedge::messages::SecurityDefinition& secDef)
{
    m_id = secDef.id();

    // Extract symbol string from variable-length field
    auto symbolField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).symbol();
    m_symbol = SBEUtils::extractVarString(symbolField, secDef.sbeBlockLength());

    auto marketSymbolField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).marketSymbol();
    m_marketSymbol = SBEUtils::extractVarString(marketSymbolField, secDef.sbeBlockLength(), m_symbol.length());

    m_baseCurrency = secDef.baseCurrency();
    m_quoteCurrency = secDef.quoteCurrency();
    m_settlCurrency = secDef.settlCurrency();
    m_positionCurrency = secDef.positionCurrency();
    m_settlType = secDef.settlType();
    m_marginingType = secDef.marginingType();

    // Convert SBE Date to std::tm
    auto maturityDate = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).maturityDate();
    m_maturityDate.tm_year = maturityDate.year() - 1900;  // tm_year is years since 1900
    m_maturityDate.tm_mon = maturityDate.month() - 1;     // tm_mon is 0-11
    m_maturityDate.tm_mday = maturityDate.day();

    auto maturityTime = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).maturityTime();
    m_maturityDate.tm_hour = maturityTime.hour();
    m_maturityDate.tm_min = maturityTime.minute();

    // Convert Price and Qty fields using SBEUtils
    auto minPriceIncrementField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).minPriceIncrement();
    m_minPriceIncrement = SBEUtils::convertPrice(minPriceIncrementField);

    m_instrumentPricePrecision = secDef.instrumentPricePrecision();

    auto minSizeIncrementField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).minSizeIncrement();
    m_minSizeIncrement = SBEUtils::convertQty(minSizeIncrementField);
    auto minSizeField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).minSize();
    m_minSize = SBEUtils::convertQty(minSizeField);
    auto minAmountField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).minAmount();
    m_minAmount = SBEUtils::convertQty(minAmountField);
    auto contractMultiplierField = const_cast<com::liversedge::messages::SecurityDefinition&>(secDef).contractMultiplier();
    m_contractMultiplier = SBEUtils::convertPrice(contractMultiplierField);

    m_securityType = secDef.securityType();
}

std::string SecurityInfo::toString() const
{
    std::ostringstream oss;
    oss << "SecurityInfo{";
    oss << "id=" << m_id;
    oss << ", symbol='" << m_symbol << "'";
    oss << ", baseCurrency=" << static_cast<int>(m_baseCurrency);
    oss << ", quoteCurrency=" << static_cast<int>(m_quoteCurrency);
    oss << ", settlCurrency=" << static_cast<int>(m_settlCurrency);
    oss << ", positionCurrency=" << static_cast<int>(m_positionCurrency);
    oss << ", settlType=" << static_cast<int>(m_settlType);
    oss << ", marginingType=" << static_cast<int>(m_marginingType);
    oss << ", maturityDate=" << (m_maturityDate.tm_year + 1900) << "-"
        << (m_maturityDate.tm_mon + 1) << "-" << m_maturityDate.tm_mday
        << "T" << m_maturityDate.tm_hour << ":" << m_maturityDate.tm_min;
    oss << ", minPriceIncrement=" << m_minPriceIncrement;
    oss << ", instrumentPricePrecision=" << static_cast<int>(m_instrumentPricePrecision);
    oss << ", minSizeIncrement=" << m_minSizeIncrement;
    oss << ", minSize=" << m_minSize;
    oss << ", minAmount=" << m_minAmount;
    oss << ", contractMultiplier=" << m_contractMultiplier;
    oss << ", securityType=" << static_cast<int>(m_securityType);
    oss << "}";
    return oss.str();
}