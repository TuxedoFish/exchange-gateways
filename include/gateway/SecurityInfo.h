#pragma once

#include <string>
#include <cstdint>
#include <ctime>
#include "../util/DecimalTypes.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"
#include "../../generated/com_liversedge_messages/Currency.h"
#include "../../generated/com_liversedge_messages/SettlType.h"
#include "../../generated/com_liversedge_messages/SecurityType.h"
#include "../../generated/com_liversedge_messages/ActionEnum.h"
#include "../../generated/com_liversedge_messages/Price.h"
#include "../../generated/com_liversedge_messages/Qty.h"
#include "../../generated/com_liversedge_messages/Date.h"

class SecurityInfo
{
public:
    SecurityInfo() = default;

    explicit SecurityInfo(const com::liversedge::messages::SecurityDefinition& secDef);

    std::int32_t getId() const { return m_id; }
    std::string getSymbol() const { return m_symbol; }
    std::string getMarketSymbol() const { return m_marketSymbol; }
    com::liversedge::messages::SecurityType::Value getSecurityType() const { return m_securityType; }
    com::liversedge::messages::Currency::Value getCurrency() const { return m_baseCurrency; }
    com::liversedge::messages::Currency::Value getCommCurrency() const { return m_quoteCurrency; }
    com::liversedge::messages::Currency::Value getSettlCurrency() const { return m_settlCurrency; }
    com::liversedge::messages::SettlType::Value getSettlType() const { return m_settlType; }
    std::tm getMaturityDate() const { return m_maturityDate; }
    Dec getMinPriceIncrement() const { return m_minPriceIncrement; }
    std::int8_t getInstrumentPricePrecision() const { return m_instrumentPricePrecision; }
    Dec getMinSizeIncrement() const { return m_minSizeIncrement; }
    Dec getContractMultiplier() const { return m_contractMultiplier; }

    std::string toString() const;

private:
    std::int32_t m_id = 0;
    std::string m_symbol;
    std::string m_marketSymbol;
    com::liversedge::messages::Currency::Value m_baseCurrency = com::liversedge::messages::Currency::NULL_VALUE;
    com::liversedge::messages::Currency::Value m_quoteCurrency = com::liversedge::messages::Currency::NULL_VALUE;
    com::liversedge::messages::Currency::Value m_settlCurrency = com::liversedge::messages::Currency::NULL_VALUE;
    com::liversedge::messages::Currency::Value m_positionCurrency = com::liversedge::messages::Currency::NULL_VALUE;
    com::liversedge::messages::SettlType::Value m_settlType = com::liversedge::messages::SettlType::NULL_VALUE;
    com::liversedge::messages::MarginingType::Value m_marginingType = com::liversedge::messages::MarginingType::NULL_VALUE;
    std::tm m_maturityDate = {};
    Dec m_minPriceIncrement = 0;
    std::int8_t m_instrumentPricePrecision = 0;
    Dec m_minSizeIncrement = 0;
    Dec m_minSize = 0;
    Dec m_minAmount = 0;
    Dec m_contractMultiplier = 0;
    com::liversedge::messages::SecurityType::Value m_securityType = com::liversedge::messages::SecurityType::NULL_VALUE;
};