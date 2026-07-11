#ifndef APPLICATION_H
#define APPLICATION_H

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Values.h"
#include "quickfix/Mutex.h"
#include "quickfix/Session.h"
#include "quickfix/fix44/MarketDataRequest.h"
#include "quickfix/fix44/MarketDataSnapshotFullRefresh.h"
#include "quickfix/fix44/SecurityList.h"
#include <iostream>
#include <chrono>
#include <string>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <algorithm>
#include <vector>
#include <set>
#include <unordered_set>
#include <sstream>
#include "../util/AuthHandler.h"
#include "../historical/MarketDataLogger.h"
#include "../util/SimpleConfig.h"
#include "../fix/FIXUtils.h"

using encoding_t = unsigned char const*;

class DeribitApplicationBase : public FIX::Application, public FIX::MessageCracker
{
public:
    DeribitApplicationBase(const SimpleConfig& config) : m_config{ config }
    {
        if (config.hasKey("perp_currencies"))
        {
            std::string currencies = config.getString("perp_currencies");
            std::istringstream ss(currencies);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                // Trim whitespace
                token.erase(0, token.find_first_not_of(' '));
                token.erase(token.find_last_not_of(' ') + 1);
                if (!token.empty())
                {
                    m_perpCurrencies.insert(token);
                }
            }
        }
    }

    // Application interface
    void onCreate(const FIX::SessionID&) override;
    void onLogon(const FIX::SessionID&) override;
    void onLogout(const FIX::SessionID&) override;
    void toAdmin(FIX::Message&, const FIX::SessionID&) override;
    void toApp(FIX::Message&, const FIX::SessionID&) noexcept;
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) noexcept;
    void fromApp(const FIX::Message&, const FIX::SessionID&) noexcept;

    // Deribit marketdata functionality
    void subscribe(std::vector<std::string>);
    void getSymbols();

private:
    FIX::SessionID m_sessionID;
    bool m_loggedOn = false;
    const SimpleConfig& m_config;

    // Overloaded onMessage
    void onMessage(const FIX44::MarketDataRequest&, const FIX::SessionID&);
    void onMessage(const FIX44::MarketDataRequestReject&, const FIX::SessionID&);
    void onMessage(const FIX44::MarketDataSnapshotFullRefresh&, const FIX::SessionID&);
    void onMessage(const FIX44::MarketDataIncrementalRefresh&, const FIX::SessionID&);
    void onMessage(const FIX44::SecurityList&, const FIX::SessionID&);

protected:
    std::set<std::string> m_perpCurrencies;
    std::unordered_set<std::string> m_perpOnlyReqIds;

    virtual void onSecurityListRequestSent(const std::string& reqId) {}
    virtual void onPerpSecurityListRequestSent(const std::string& reqId) { m_perpOnlyReqIds.insert(reqId); }

};

#endif