#include "../../include/gateway/RefDataHolder.h"
#include "../../generated/com_liversedge_messages/ActionEnum.h"
#include <spdlog/spdlog.h>

void RefDataHolder::onSecurityDefinition(com::liversedge::messages::SecurityDefinition& decoder, std::uint64_t timestamp)
{
    auto action = decoder.action();
    std::int32_t securityId = decoder.id();

    if (action == com::liversedge::messages::ActionEnum::ADD)
    {
        auto securityInfo = std::make_unique<SecurityInfo>(decoder);
        spdlog::info("Added security: {}", securityInfo->toString());

        // Build reverse lookup map
        std::string symbol = securityInfo->getSymbol();
        m_symbolToSecurityId[symbol] = securityId;
        m_securities[securityId] = std::move(securityInfo);
    }
    else if (action == com::liversedge::messages::ActionEnum::REMOVE)
    {
        // Remove from both maps
        auto it = m_securities.find(securityId);
        if (it != m_securities.end() && it->second) {
            std::string symbol = it->second->getSymbol();
            m_symbolToSecurityId.erase(symbol);
        }
        m_securities.erase(securityId);
    }
}

const SecurityInfo* RefDataHolder::getSecurityInfo(std::int32_t securityId) const
{
    auto it = m_securities.find(securityId);
    return (it != m_securities.end()) ? it->second.get() : nullptr;
}

std::int32_t RefDataHolder::getSecurityIdBySymbol(const std::string& symbol) const
{
    auto it = m_symbolToSecurityId.find(symbol);
    return (it != m_symbolToSecurityId.end()) ? it->second : 0;
}