#pragma once

#include <unordered_map>
#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/fix44/MarketDataRequest.h"
#include "quickfix/fix44/MarketDataRequestReject.h"
#include "quickfix/fix44/MarketDataSnapshotFullRefresh.h"
#include "quickfix/fix44/MarketDataIncrementalRefresh.h"
#include "quickfix/fix44/SecurityList.h"
#include "quickfix/fix44/Logon.h"
#include "quickfix/fix44/Logout.h"
#include "../sbe/SBEBinaryWriter.h"
#include "../fix/FIXCustomTags.h"
#include "../sbe/SBEUtils.h"
#include "../fix/FIXUtils.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"
#include "../../generated/com_liversedge_messages/SecurityStatus.h"
#include "../../generated/com_liversedge_messages/SecurityType.h"
#include "../../generated/com_liversedge_messages/ConnectionStatus.h"
#include "../../generated/com_liversedge_messages/MDFullBook.h"
#include "../../generated/com_liversedge_messages/MDUpdate.h"

struct ProcessorSecurityInfo
{
    std::string symbol;
    com::liversedge::messages::SecurityStatusEnum::Value status;
};

// Caps levels to 50 to ensure they fit in 128kb buffer
constexpr int MAX_LEVELS = 50;

class MessageProcessor
{
public:
    explicit MessageProcessor(SBEBinaryWriter& writer);
    ~MessageProcessor() = default;

    void setShouldOutput(bool shouldOutput);
protected:
    SBEBinaryWriter& m_writer;
    com::liversedge::messages::ConnectionStatus m_connectionStatus;
    com::liversedge::messages::SecurityDefinition m_securityDefinition;
    com::liversedge::messages::SecurityStatus m_securityStatus;
    com::liversedge::messages::MDFullBook m_mdFullBook;
    com::liversedge::messages::MDUpdate m_mdUpdate;
    bool m_shouldOutput = true;

    int createSecurity(const std::string& symbol);
    int getSecurityId(const std::string& symbol) const;
    com::liversedge::messages::SecurityStatusEnum::Value getSecurityStatus(int securityId) const;

    bool updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::Value value, std::uint64_t timestamp);
    bool updateSecurityStatus(int securityId, std::uint64_t timestamp, com::liversedge::messages::SecurityStatusEnum::Value newStatus);
    bool invalidateState(std::uint64_t timestamp);
    bool removeSecurity(int securityId);

    com::liversedge::messages::ConnectionStatusEnum::Value getConnectionStatus() const;
    const std::unordered_map<std::string, int>& getSymbolMap() const { return m_symbolToSecurityId; }

private:
    std::int32_t m_securityIdCounter{0};
    std::unordered_map<int, ProcessorSecurityInfo> m_securities;
    std::unordered_map<std::string, int> m_symbolToSecurityId;
    com::liversedge::messages::ConnectionStatusEnum::Value m_lastConnectionStatus{com::liversedge::messages::ConnectionStatusEnum::Value::NULL_VALUE};
};
