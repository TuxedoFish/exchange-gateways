#include "../../include/marketdata/HyperliquidMessageProcessor.h"
#include "../../include/marketdata/HyperliquidMDApplicationBase.h"
#include "../../include/sbe/SBEUtils.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <algorithm>

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
    constexpr auto EXPIRY_BUFFER = std::chrono::minutes(5);
    constexpr auto REFETCH_DELAY = std::chrono::minutes(5);
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
    m_pendingSecDefs.clear();
    m_pendingOutcomeSecDefs.clear();
    m_activeOutcomes.clear();
    invalidateState(0);
}

void HyperliquidMessageProcessor::onMeta(const hyperliquid::MetaResponse& response)
{
    auto now = std::chrono::system_clock::now();
    m_metaReceivedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    for (const auto& asset : response.universe)
    {
        if (m_desiredCoins.find(asset.name) == m_desiredCoins.end())
        {
            // Ignore undesired coins
            continue;
        }
        m_observedCoins.insert(asset.name);

        int id = createSecurity(asset.name);
        m_pendingSecDefs[id] = {asset.name, asset.szDecimals, id};

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
        spdlog::info("All desired coins available, waiting for first price updates before going ONLINE.");
    }
}

void HyperliquidMessageProcessor::onOutcomeMeta(
    const hyperliquid::OutcomeMetaResponse& response,
    const std::vector<DesiredOutcome>& desiredOutcomes)
{
    m_pendingRefetch = false;

    for (const auto& outcome : response.outcomes)
    {
        for (const auto& desired : desiredOutcomes)
        {
            if (outcome.description.underlying != desired.underlying ||
                outcome.description.period != desired.period)
            {
                continue;
            }

            for (int side = 0; side < static_cast<int>(outcome.sideSpecs.size()); side++)
            {
                std::string coin = hyperliquid::outcomeCoin(outcome.outcome, side);
                std::string symbol = buildOutcomeSymbol(
                    outcome.description.underlying,
                    outcome.description.period,
                    outcome.sideSpecs[side].name);

                int id = createSecurity(coin);

                OutcomeInstrument inst;
                inst.symbol = symbol;
                inst.coin = coin;
                inst.underlying = outcome.description.underlying;
                inst.outcomeIndex = outcome.outcome;
                inst.side = side;
                inst.expiry = outcome.description.expiry;
                inst.securityId = id;

                m_pendingOutcomeSecDefs[id] = inst;

                if (!updateSecurityStatus(id, 0,
                    com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT))
                {
                    spdlog::error("Error updating security status to PENDING_SNAPSHOT for {}", symbol);
                }

                spdlog::info("Registered outcome instrument {} coin={} expiryEpochMs={}",
                             symbol, coin,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 inst.expiry.time_since_epoch()).count());
            }
            break;
        }
    }
}

std::string HyperliquidMessageProcessor::buildOutcomeSymbol(
    const std::string& underlying,
    const std::string& period,
    const std::string& sideName)
{
    std::string p = period;
    std::transform(p.begin(), p.end(), p.begin(), ::toupper);
    std::string s = sideName;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return underlying + "-" + p + "-" + s;
}

void HyperliquidMessageProcessor::emitOutcomeSecurityDefinition(const OutcomeInstrument& outcome)
{
    if (!m_shouldOutput) return;

    if (!m_writer.prepareMessage(m_securityDefinition))
    {
        spdlog::error("Error preparing security definition for {}", outcome.symbol);
        removeSecurity(outcome.securityId);
        return;
    }

    m_securityDefinition.id(outcome.securityId);
    m_securityDefinition.timestamp(0);
    m_securityDefinition.action(com::liversedge::messages::ActionEnum::ADD);
    m_securityDefinition.baseCurrency(SBEUtils::currencyFromString(outcome.underlying));
    m_securityDefinition.quoteCurrency(com::liversedge::messages::Currency::USDH);
    m_securityDefinition.settlCurrency(com::liversedge::messages::Currency::USDH);
    m_securityDefinition.positionCurrency(com::liversedge::messages::Currency::CONTRACT);
    m_securityDefinition.securityType(com::liversedge::messages::SecurityType::PREDICTION_MARKET);
    m_securityDefinition.marginingType(com::liversedge::messages::MarginingType::LINEAR);
    m_securityDefinition.contractMultiplier().mantissa(SBEUtils::stringToMantissa("1", -8));

    // Derive settlType from period
    m_securityDefinition.settlType(com::liversedge::messages::SettlType::D1);

    // Convert time_point expiry to maturityDate + maturityTime
    {
        time_t secs = std::chrono::system_clock::to_time_t(outcome.expiry);
        struct tm tm;
        gmtime_r(&secs, &tm);
        m_securityDefinition.maturityDate()
            .year(tm.tm_year + 1900)
            .month(tm.tm_mon + 1)
            .day(tm.tm_mday);
        m_securityDefinition.maturityTime()
            .hour(tm.tm_hour)
            .minute(tm.tm_min);
    }

    // Fixed 5 decimal precision for outcome prices (0-1 range)
    m_securityDefinition.instrumentPricePrecision(5);
    m_securityDefinition.minPriceIncrement().mantissa(SBEUtils::powerOfTenMantissa(5, -8));
    m_securityDefinition.minSizeIncrement().mantissa(SBEUtils::powerOfTenMantissa(0, -8));
    SBEUtils::setQty(m_securityDefinition.minSize(), "0");
    SBEUtils::setQty(m_securityDefinition.minAmount(), "10");
    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.symbol(), outcome.symbol);
    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.marketSymbol(),
        hyperliquid::outcomeCoin(outcome.outcomeIndex, outcome.side));

    if (!m_writer.writeMessage(m_securityDefinition))
    {
        spdlog::error("Error writing security definition for {}", outcome.symbol);
        removeSecurity(outcome.securityId);
        return;
    }

    spdlog::info("SecurityDefinition {} type=PREDICTION_MARKET maturity={:04d}-{:02d}-{:02d}",
                 outcome.symbol,
                 static_cast<int>(m_securityDefinition.maturityDate().year()),
                 static_cast<int>(m_securityDefinition.maturityDate().month()),
                 static_cast<int>(m_securityDefinition.maturityDate().day()));

    m_activeOutcomes.push_back(outcome);
}

bool HyperliquidMessageProcessor::hasExpiredOutcomes() const
{
    if (m_activeOutcomes.empty()) return false;

    auto now = std::chrono::system_clock::now();
    for (const auto& outcome : m_activeOutcomes)
    {
        if (now >= outcome.expiry - EXPIRY_BUFFER)
        {
            return true;
        }
    }
    return false;
}

void HyperliquidMessageProcessor::removeExpiredOutcomes()
{
    auto now = std::chrono::system_clock::now();
    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    uint64_t timestampNanos = nowMs * 1000 * 1000;

    auto it = m_activeOutcomes.begin();
    while (it != m_activeOutcomes.end())
    {
        if (now >= it->expiry - EXPIRY_BUFFER)
        {
            spdlog::info("Outcome {} expired, removing SecurityDefinition", it->symbol);
            if (it->expiry > m_lastOutcomeExpiry)
            {
                m_lastOutcomeExpiry = it->expiry;
            }
            m_pendingRefetch = true;

            if (m_shouldOutput)
            {
                if (m_writer.prepareMessage(m_securityDefinition))
                {
                    m_securityDefinition.id(it->securityId);
                    m_securityDefinition.timestamp(timestampNanos);
                    m_securityDefinition.action(com::liversedge::messages::ActionEnum::REMOVE);
                    // Must still set var-length fields for valid SBE
                    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.symbol(), it->symbol);
                    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.marketSymbol(),
                        hyperliquid::outcomeCoin(it->outcomeIndex, it->side));
                    m_writer.writeMessage(m_securityDefinition);
                }
            }

            updateSecurityStatus(it->securityId, timestampNanos,
                com::liversedge::messages::SecurityStatusEnum::Value::OFFLINE);
            removeSecurity(it->securityId);

            it = m_activeOutcomes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool HyperliquidMessageProcessor::shouldRefetchOutcomeMeta() const
{
    if (!m_pendingRefetch) return false;
    auto now = std::chrono::system_clock::now();
    return now >= m_lastOutcomeExpiry + REFETCH_DELAY;
}

void HyperliquidMessageProcessor::onL2Book(const hyperliquid::L2BookSnapshot& snapshot)
{
    if (m_connectedTimeMs > 0 && snapshot.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(snapshot.coin);
    if (securityId == -1)
    {
        return;
    }

    uint64_t timestampNanos = snapshot.time * 1000 * 1000;

    // Transition from PENDING_SNAPSHOT to ONLINE on first snapshot
    if (getSecurityStatus(securityId) == com::liversedge::messages::SecurityStatusEnum::Value::PENDING_SNAPSHOT)
    {
        updateSecurityStatus(securityId, timestampNanos, com::liversedge::messages::SecurityStatusEnum::Value::ONLINE);
    }

    // Emit deferred SecurityDefinition on first price update
    auto pendingIt = m_pendingSecDefs.find(securityId);
    if (pendingIt != m_pendingSecDefs.end())
    {
        if (snapshot.numBids > 0)
        {
            emitSecurityDefinition(pendingIt->second, std::stod(snapshot.bids[0].px));
        }
        else if (snapshot.numAsks > 0)
        {
            emitSecurityDefinition(pendingIt->second, std::stod(snapshot.asks[0].px));
        }
        m_pendingSecDefs.erase(pendingIt);
    }

    // Emit deferred outcome SecurityDefinition on first price update
    auto pendingOutcomeIt = m_pendingOutcomeSecDefs.find(securityId);
    if (pendingOutcomeIt != m_pendingOutcomeSecDefs.end())
    {
        emitOutcomeSecurityDefinition(pendingOutcomeIt->second);
        m_pendingOutcomeSecDefs.erase(pendingOutcomeIt);
    }

    if (!m_pendingSecDefs.empty())
    {
        drainTimedOutSecDefs(snapshot.time);
    }

    if (m_pendingSecDefs.empty() && m_pendingOutcomeSecDefs.empty() &&
        getConnectionStatus() != com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE)
    {
        spdlog::info("All SecurityDefinitions emitted, going ONLINE.");
        updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::Value::ONLINE, timestampNanos);
    }

    if (!m_shouldOutput)
    {
        return;
    }

    // Emit MDFullBook snapshot
    if (!m_writer.prepareMessage(m_mdFullBook))
    {
        spdlog::error("Error preparing MDFullBook message");
        return;
    }

    m_mdFullBook.securityId(securityId);
    m_mdFullBook.timestamp(timestampNanos);

    auto bidLevels = m_mdFullBook.bidLevelsCount(snapshot.numBids);
    for (uint8_t i = 0; i < snapshot.numBids; i++)
    {
        auto bidLevel = bidLevels.next();
        SBEUtils::setPrice(bidLevel.price(), snapshot.bids[i].px);
        SBEUtils::setQty(bidLevel.qty(), snapshot.bids[i].sz);
    }

    auto askLevels = m_mdFullBook.askLevelsCount(snapshot.numAsks);
    for (uint8_t i = 0; i < snapshot.numAsks; i++)
    {
        auto askLevel = askLevels.next();
        SBEUtils::setPrice(askLevel.price(), snapshot.asks[i].px);
        SBEUtils::setQty(askLevel.qty(), snapshot.asks[i].sz);
    }

    if (!m_writer.writeMessage(m_mdFullBook))
    {
        spdlog::error("Error writing MDFullBook message");
    }
}

void HyperliquidMessageProcessor::onBbo(const hyperliquid::BboUpdate& update)
{
    if (m_connectedTimeMs > 0 && update.time + STALE_THRESHOLD_MS < m_connectedTimeMs)
    {
        return;
    }

    int securityId = getSecurityId(update.coin);
    if (securityId == -1)
    {
        return;
    }

    if (getSecurityStatus(securityId) != com::liversedge::messages::SecurityStatusEnum::Value::ONLINE)
    {
        return;
    }

    if (!m_shouldOutput)
    {
        return;
    }

    uint64_t timestampNanos = update.time * 1000 * 1000;

    if (update.hasBid)
    {
        if (!m_writer.prepareMessage(m_mdUpdate))
        {
            spdlog::error("Error preparing MDUpdate message");
            return;
        }

        m_mdUpdate.securityId(securityId);
        m_mdUpdate.timestamp(timestampNanos);
        m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::BOOK_UPDATE);
        m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::CHANGE);
        m_mdUpdate.side(com::liversedge::messages::MDSide::BID);
        SBEUtils::setPrice(m_mdUpdate.price(), update.bid.px);
        SBEUtils::setQty(m_mdUpdate.qty(), update.bid.sz);

        if (!m_writer.writeMessage(m_mdUpdate))
        {
            spdlog::error("Error writing MDUpdate message");
        }
    }

    if (update.hasAsk)
    {
        if (!m_writer.prepareMessage(m_mdUpdate))
        {
            spdlog::error("Error preparing MDUpdate message");
            return;
        }

        m_mdUpdate.securityId(securityId);
        m_mdUpdate.timestamp(timestampNanos);
        m_mdUpdate.updateType(com::liversedge::messages::MDUpdateType::BOOK_UPDATE);
        m_mdUpdate.action(com::liversedge::messages::MDUpdateAction::Value::CHANGE);
        m_mdUpdate.side(com::liversedge::messages::MDSide::ASK);
        SBEUtils::setPrice(m_mdUpdate.price(), update.ask.px);
        SBEUtils::setQty(m_mdUpdate.qty(), update.ask.sz);

        if (!m_writer.writeMessage(m_mdUpdate))
        {
            spdlog::error("Error writing MDUpdate message");
        }
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

void HyperliquidMessageProcessor::emitSecurityDefinition(const PendingAsset& asset, double price)
{
    int maxDecimals = 6 - asset.szDecimals;

    int sigFigDecimals = maxDecimals;
    if (price > 0)
    {
        int intDigits = static_cast<int>(std::floor(std::log10(price))) + 1;
        sigFigDecimals = std::max(0, 5 - intDigits);
    }

    int instrumentPricePrecision = std::min(maxDecimals, sigFigDecimals);

    spdlog::info("SecurityDefinition {} price={} szDecimals={} maxDecimals={} sigFigDecimals={} precision={}",
                 asset.name, price, asset.szDecimals, maxDecimals, sigFigDecimals, instrumentPricePrecision);

    emitSecurityDefinitionWithPricePrecision(asset, instrumentPricePrecision);
}

void HyperliquidMessageProcessor::emitSecurityDefinitionWithPricePrecision(const PendingAsset& asset, int instrumentPricePrecision)
{
    if (!m_shouldOutput) return;

    if (!m_writer.prepareMessage(m_securityDefinition))
    {
        spdlog::error("Error preparing security definition for {}", asset.name);
        removeSecurity(asset.securityId);
        return;
    }

    m_securityDefinition.id(asset.securityId);
    m_securityDefinition.timestamp(0);
    m_securityDefinition.action(com::liversedge::messages::ActionEnum::ADD);
    m_securityDefinition.baseCurrency(com::liversedge::messages::Currency::CONTRACT);
    m_securityDefinition.quoteCurrency(com::liversedge::messages::Currency::USDC);
    m_securityDefinition.settlCurrency(com::liversedge::messages::Currency::USDC);
    m_securityDefinition.positionCurrency(com::liversedge::messages::Currency::CONTRACT);
    m_securityDefinition.securityType(com::liversedge::messages::SecurityType::FUT);
    m_securityDefinition.marginingType(com::liversedge::messages::MarginingType::LINEAR);
    m_securityDefinition.contractMultiplier().mantissa(SBEUtils::stringToMantissa("1", -8));
    m_securityDefinition.settlType(com::liversedge::messages::SettlType::REGULAR);
    m_securityDefinition.maturityDate().year(3000).month(1).day(1);
    m_securityDefinition.maturityTime().hour(0).minute(0);
    m_securityDefinition.instrumentPricePrecision(instrumentPricePrecision);
    m_securityDefinition.minPriceIncrement().mantissa(SBEUtils::powerOfTenMantissa(instrumentPricePrecision, -8));
    m_securityDefinition.minSizeIncrement().mantissa(SBEUtils::powerOfTenMantissa(asset.szDecimals, -8));
    SBEUtils::setQty(m_securityDefinition.minSize(), "0");
    SBEUtils::setQty(m_securityDefinition.minAmount(), "10");
    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.symbol(), asset.name);
    SBEUtils::setVarString(m_securityDefinition, m_securityDefinition.marketSymbol(), asset.name);

    if (!m_writer.writeMessage(m_securityDefinition))
    {
        spdlog::error("Error writing security definition for {}", asset.name);
        removeSecurity(asset.securityId);
    }
}

void HyperliquidMessageProcessor::drainTimedOutSecDefs(uint64_t bookTimeMs)
{
    if (m_pendingSecDefs.empty() || m_metaReceivedTimeMs == 0) return;

    auto now = std::chrono::system_clock::now();
    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (nowMs - m_metaReceivedTimeMs < 10000) return;

    for (const auto& [id, asset] : m_pendingSecDefs)
    {
        int instrumentPricePrecision = 6 - asset.szDecimals;
        spdlog::warn("No price received for [{}], sending SecurityDefinition from raw metadata precision={}",
                     asset.name, instrumentPricePrecision);
        emitSecurityDefinitionWithPricePrecision(asset, instrumentPricePrecision);
    }
    m_pendingSecDefs.clear();
}
