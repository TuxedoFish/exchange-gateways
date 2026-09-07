# pragma once

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Values.h"
#include "quickfix/Mutex.h"
#include "quickfix/Session.h"
#include "quickfix/fix44/MarketDataRequest.h"
#include "quickfix/fix44/MarketDataSnapshotFullRefresh.h"
#include "quickfix/fix44/SecurityList.h"
#include "quickfix/fix44/OrderCancelReject.h"
#include "quickfix/fix44/ExecutionReport.h"
#include "../../generated/com_liversedge_messages/ExecutionReport.h"
#include "../../generated/com_liversedge_messages/OrderCancelReject.h"
#include <iostream>
#include <chrono>
#include <string>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <algorithm>
#include <vector>
#include "../util/AuthHandler.h"
#include "../historical/MarketDataLogger.h"
#include "../util/SimpleConfig.h"
#include "../fix/FIXUtils.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../sbe/SBEUtils.h"
#include "RefDataHolder.h"
#include "./DeribitMessageConverter.h"

using encoding_t = unsigned char const*;

class DeribitGWApplication : public FIX::Application, public FIX::MessageCracker
{
public:
    DeribitGWApplication(SimpleConfig& config, RefDataHolder& refDataHolder, SBEBinaryWriter& sbeWriter);
    ~DeribitGWApplication() = default;

    // Application interface
    void onCreate(const FIX::SessionID&) override;
    void onLogon(const FIX::SessionID&) override;
    void onLogout(const FIX::SessionID&) override;
    void toAdmin(FIX::Message&, const FIX::SessionID&) override;
    void toApp(FIX::Message&, const FIX::SessionID&) noexcept;
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) noexcept;
    void fromApp(const FIX::Message&, const FIX::SessionID&) noexcept;

    // Message sending interface for OrdersHandler
    bool sendMessage(FIX::Message& message);
    bool isLoggedOn() const { return m_loggedOn; }

private:
    FIX::SessionID m_sessionID;
    bool m_loggedOn = false;
    SimpleConfig& m_config;
    RefDataHolder& m_refDataHolder;
    SBEBinaryWriter& m_sbeWriter;

    // Overloaded onMessage
    void onMessage(const FIX44::OrderCancelReject&, const FIX::SessionID&);
    void onMessage(const FIX44::ExecutionReport&, const FIX::SessionID&);

};