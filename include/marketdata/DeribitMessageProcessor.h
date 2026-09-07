#pragma once

#include <unordered_map>
#include <unordered_set>
#include <set>

#include "MessageProcessor.h"
#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/fix44/MarketDataRequest.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../fix/FIXUtils.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"
#include "../../generated/com_liversedge_messages/SecurityStatus.h"
#include "../../generated/com_liversedge_messages/ConnectionStatus.h"
#include "../../generated/com_liversedge_messages/MDFullBook.h"
#include "../../generated/com_liversedge_messages/MDUpdate.h"

class DeribitMessageProcessor : public FIX::MessageCracker, public MessageProcessor
{
public:
    explicit DeribitMessageProcessor(SBEBinaryWriter& writer);
    ~DeribitMessageProcessor() override = default;

    void onMessage(const FIX44::MarketDataRequest&, const FIX::SessionID&) override;
    void onMessage(const FIX44::MarketDataRequestReject&, const FIX::SessionID&) override;
    void onMessage(const FIX44::MarketDataSnapshotFullRefresh&, const FIX::SessionID&) override;
    void onMessage(const FIX44::MarketDataIncrementalRefresh&, const FIX::SessionID&) override;
    void onMessage(const FIX44::SecurityList&, const FIX::SessionID&) override;
    void onMessage(const FIX44::Logout&, const FIX::SessionID&) override;
    void onMessage(const FIX44::Logon&, const FIX::SessionID&) override;

private:
    static uint64_t GetSendingTime(FIX44::Message message);
    template<typename T>
    bool ProcessMDEntry(const T& entry, int securityId, uint64_t timestamp);

    std::unordered_set<std::string> m_offlineWarned;
    std::unordered_set<std::string> m_pendingSecurityLists;
    std::unordered_set<std::string> m_perpOnlyReqIds;
    std::set<std::string> m_perpCurrencies;

public:
    void addPendingSecurityList(const std::string& reqId);
    void addPerpOnlySecurityList(const std::string& reqId);
    void setPerpCurrencies(const std::set<std::string>& currencies);
};
