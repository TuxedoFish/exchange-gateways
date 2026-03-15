#include "../../include/marketdata/MessageProcessor.h"
#include <spdlog/spdlog.h>

MessageProcessor::MessageProcessor(SBEBinaryWriter& writer)
    : m_writer(writer)
{
}

int MessageProcessor::createSecurity(const std::string& symbol)
{
    int id = m_securityIdCounter++;
    m_securities[id] = ProcessorSecurityInfo{symbol, com::liversedge::messages::SecurityStatusEnum::Value::NULL_VALUE};
    m_symbolToSecurityId[symbol] = id;
    return id;
}

int MessageProcessor::getSecurityId(const std::string& symbol) const
{
    auto it = m_symbolToSecurityId.find(symbol);
    return it != m_symbolToSecurityId.end() ? it->second : -1;
}

com::liversedge::messages::SecurityStatusEnum::Value MessageProcessor::getSecurityStatus(int securityId) const
{
    auto it = m_securities.find(securityId);
    if (it == m_securities.end())
    {
        return com::liversedge::messages::SecurityStatusEnum::Value::NULL_VALUE;
    }
    return it->second.status;
}

bool MessageProcessor::invalidateState(std::uint64_t timestamp)
{
    // Send out SecurityStatus - Offline and removal messages for all securities
    for (const auto& pair : m_securities)
    {
        updateSecurityStatus(pair.first, timestamp, com::liversedge::messages::SecurityStatusEnum::Value::OFFLINE);

        // Send removal message without cleaning up maps (we clear everything below)
        if (m_shouldOutput)
        {
            if (m_writer.prepareMessage(m_securityDefinition))
            {
                m_securityDefinition.id(pair.first);
                m_securityDefinition.action(com::liversedge::messages::ActionEnum::REMOVE);
                m_writer.writeMessage(m_securityDefinition);
            }
        }
    }

    // Send out ConnectionStatus - Offline
    updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::OFFLINE, timestamp);

    // Reset state
    m_securityIdCounter = 0;
    m_securities.clear();
    m_symbolToSecurityId.clear();
    return true;
}

com::liversedge::messages::ConnectionStatusEnum::Value MessageProcessor::getConnectionStatus() const
{
    return m_lastConnectionStatus;
}

bool MessageProcessor::updateConnectionStatus(com::liversedge::messages::ConnectionStatusEnum::Value value, const std::uint64_t timestamp)
{
    m_lastConnectionStatus = value;

    if (!m_shouldOutput)
    {
        return true;
    }
    if (!m_writer.prepareMessage(m_connectionStatus))
    {
        spdlog::error("Error preparing connection status update");
        return false;
    }

    m_connectionStatus.status(value);
    m_connectionStatus.timestamp(timestamp);

    if (!m_writer.writeMessage(m_connectionStatus))
    {
        spdlog::error("Error writing connection status update");
        return false;
    }

    return true;
}

bool MessageProcessor::removeSecurity(int securityId)
{
    if (m_shouldOutput)
    {
        if (!m_writer.prepareMessage(m_securityDefinition))
        {
            spdlog::error("Error preparing security removal message");
            return false;
        }

        m_securityDefinition.id(securityId);
        m_securityDefinition.action(com::liversedge::messages::ActionEnum::REMOVE);

        if (!m_writer.writeMessage(m_securityDefinition))
        {
            spdlog::error("Error writing security removal message");
            return false;
        }
    }

    // Clean up internal state
    auto it = m_securities.find(securityId);
    if (it != m_securities.end())
    {
        m_symbolToSecurityId.erase(it->second.symbol);
        m_securities.erase(it);
    }

    return true;
}

bool MessageProcessor::updateSecurityStatus(int securityId, const std::uint64_t timestamp, com::liversedge::messages::SecurityStatusEnum::Value newStatus)
{
    auto it = m_securities.find(securityId);
    if (it == m_securities.end())
    {
        spdlog::error("Error security {} not found", securityId);
        return false;
    }
    if (it->second.status == newStatus)
    {
        // No change don't send
        return false;
    }
    it->second.status = newStatus;

    if (!m_shouldOutput)
    {
        return true;
    }
    if (!m_writer.prepareMessage(m_securityStatus))
    {
        spdlog::error("Error preparing security status update");
        return false;
    }

    m_securityStatus.securityId(securityId);
    m_securityStatus.timestamp(timestamp);
    m_securityStatus.status(newStatus);

    if (!m_writer.writeMessage(m_securityStatus))
    {
        spdlog::error("Error writing security status update");
        return false;
    }

    return true;
}

void MessageProcessor::setShouldOutput(bool shouldOutput)
{
    spdlog::info("MessageProcessor: Setting shouldOutput to {}", shouldOutput);
    m_shouldOutput = shouldOutput;
}
