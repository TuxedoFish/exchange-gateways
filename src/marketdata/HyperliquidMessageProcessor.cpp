#include "../../include/marketdata/HyperliquidMessageProcessor.h"
#include "../../include/sbe/SBEUtils.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace
{
    std::string formatUtcMs(uint64_t ms)
    {
        time_t secs = ms / 1000;
        int millis = ms % 1000;
        struct tm tm;
        gmtime_r(&secs, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        char result[40];
        snprintf(result, sizeof(result), "%s.%03d", buf, millis);
        return result;
    }

    constexpr uint64_t STALE_THRESHOLD_MS = 0;
}

HyperliquidMessageProcessor::HyperliquidMessageProcessor(SBEBinaryWriter& writer)
    : MessageProcessor(writer)
{
}

void HyperliquidMessageProcessor::setDesiredCoins(const std::set<std::string>& desiredCoins)
{
    m_desiredCoins = desiredCoins;
}

void HyperliquidMessageProcessor::onConnected()
{
    if (getConnectionStatus() == com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE)
    {
        spdlog::error("Received connect while still online, invalidating the state");
        invalidateState(0);
    }

    // Track connection time in UTC millis (same epoch as Hyperliquid timestamps)
    auto now = std::chrono::system_clock::now();
    m_connectedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::STARTING, 0);
}

void HyperliquidMessageProcessor::onDisconnected(bool hasError, const std::string& errMsg)
{
    m_observedCoins.clear();
    invalidateState(0);
}

void HyperliquidMessageProcessor::onMeta(const hyperliquid::MetaResponse& response)
{
    for (const auto& asset : response.universe)
    {
        if (m_desiredCoins.find(asset.name) == m_desiredCoins.end())
        {
            // Ignore undesired coins
            continue;
        }
        m_observedCoins.insert(asset.name);

        int id = createSecurity(asset.name);

        if (m_shouldOutput)
        {
            if (!m_writer.prepareMessage(m_securityDefinition))
            {
                spdlog::error("Error preparing security definition for {}", asset.name);
                removeSecurity(id);
                continue;
            }

            // Metadata
            m_securityDefinition.id(id);
            m_securityDefinition.timestamp(0);
            m_securityDefinition.action(com::liversedge::messages::ActionEnum::ADD);

            // TODO: Currency -> QtyCurrency | CommCurrency -> AmtCurrency
            m_securityDefinition.currency(com::liversedge::messages::Currency::CONTRACT);
            m_securityDefinition.commCurrency(com::liversedge::messages::Currency::USDC);
            m_securityDefinition.settlCurrency(com::liversedge::messages::Currency::USDC);

            // Every contract treated as a perpetual futures contract
            m_securityDefinition.securityType(com::liversedge::messages::SecurityType::FUT);
            m_securityDefinition.contractMultiplier().mantissa(SBEUtils::stringToMantissa("1", -8));
            m_securityDefinition.settlType(com::liversedge::messages::SettlType::REGULAR);
            m_securityDefinition.maturityDate().year(3000) // Consistent with Deribit perpetual
                                               .month(1)
                                               .day(1);

            // Price precision: max decimal places = MAX_DECIMALS(6) - szDecimals
            int instrumentPricePrecision = 6 - asset.szDecimals;
            m_securityDefinition.instrumentPricePrecision(instrumentPricePrecision);
            m_securityDefinition.minPriceIncrement().mantissa(SBEUtils::powerOfTenMantissa(instrumentPricePrecision, -8));
            m_securityDefinition.minSizeIncrement().mantissa(SBEUtils::powerOfTenMantissa(asset.szDecimals, -8));

            // Variable length fields must be last
            SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.symbol(), asset.name);

            if (!m_writer.writeMessage(m_securityDefinition))
            {
                spdlog::error("Error writing security definition for {}", asset.name);
                removeSecurity(id);
                continue;
            }
        }

        if (!updateSecurityStatus(id, 0, com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT))
        {
            spdlog::error("Error updating security status to PENDING_SNAPSHOT for {}", asset.name);
        }
    }

    std::set<std::string> missing;
    std::set_difference(
        m_desiredCoins.begin(), m_desiredCoins.end(),
        m_observedCoins.begin(), m_observedCoins.end(),
        std::inserter(missing, missing.begin())
    );

    if (!missing.empty()) {
        std::string missingCoins;
        for (const auto& coin : missing) {
            missingCoins += coin + " ";
        }
        spdlog::info("Waiting for coins: {}", missingCoins);
    } else {
        spdlog::info("All desired coins available.");
        updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE, 0);
    }
}

void HyperliquidMessageProcessor::onL2BookLevel(const hyperliquid::L2BookUpdate& book, const hyperliquid::PriceLevel& level)
{
    if (m_connectedTimeMs > 0 && book.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(book.coin);
    if (securityId == -1)
    {
        return;
    }

    // Transition from PENDING_SNAPSHOT to ONLINE on first book level
    if (getSecurityStatus(securityId) == com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT)
    {
        updateSecurityStatus(securityId, book.time * 1000 * 1000, com::liversedge::messages::SecurityStatusEnum::Value::ONLINE);
    }

    if (!m_shouldOutput)
    {
        return;
    }

    if (!m_writer.prepareMessage(m_mdUpdate))
    {
        spdlog::error("Error preparing MDUpdate message");
        return;
    }

    m_mdUpdate.securityId(securityId);
    m_mdUpdate.timestamp(book.time * 1000 * 1000); // Nanos
    m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::BOOK_UPDATE);
    m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::CHANGE);
    m_mdUpdate.side(level.side == hyperliquid::Side::Bid ? com::liversedge::messages::MDSide::BID : com::liversedge::messages::MDSide::ASK);
    SBEUtils::setPrice(m_mdUpdate.price(), level.px);
    SBEUtils::setQty(m_mdUpdate.qty(), level.sz);

    if (!m_writer.writeMessage(m_mdUpdate))
    {
        spdlog::error("Error writing MDUpdate message");
    }
}

void HyperliquidMessageProcessor::onTrade(const hyperliquid::Trade& trade)
{
    if (m_connectedTimeMs > 0 && trade.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(trade.coin);
    if (securityId == -1)
    {
        return;
    }

    if (!m_shouldOutput)
    {
        return;
    }

    if (!m_writer.prepareMessage(m_mdUpdate))
    {
        spdlog::error("Error preparing MDUpdate message");
        return;
    }

    m_mdUpdate.securityId(securityId);
    m_mdUpdate.timestamp(trade.time * 1000 * 1000); // nanos
    m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::TRADE);
    m_mdUpdate.side(trade.side == 'B' ? com::liversedge::messages::MDSide::BID : com::liversedge::messages::MDSide::ASK);
    m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::NULL_VALUE);
    SBEUtils::setPrice(m_mdUpdate.price(), trade.px);
    SBEUtils::setQty(m_mdUpdate.qty(), trade.sz);
    m_mdUpdate.tradeId(trade.tid);

    if (!m_writer.writeMessage(m_mdUpdate))
    {
        spdlog::error("Error writing MDUpdate message");
    }
}
