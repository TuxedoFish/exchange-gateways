#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <cstdint>
#include <boost/filesystem.hpp>
#include <stdexcept>
#include <chrono>
#include <spdlog/spdlog.h>
#include <boost/iostreams/device/mapped_file.hpp>
#include "SBEMessageListener.h"
#include "../../generated/com_liversedge_messages/MessageHeader.h"
#include "../../generated/com_liversedge_messages/ConnectionStatus.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"
#include "../../generated/com_liversedge_messages/SecurityStatus.h"
#include "../../generated/com_liversedge_messages/MDFullBook.h"
#include "../../generated/com_liversedge_messages/MDUpdate.h"
#include "../../generated/com_liversedge_messages/NewOrder.h"
#include "../../generated/com_liversedge_messages/AmendOrder.h"

class SBEQueuePoller
{
public:
    static constexpr std::size_t INITIAL_BUFFER_SIZE = 4092;
    static constexpr std::size_t MESSAGE_HEADER_SIZE = 8;

    explicit SBEQueuePoller(const std::string& dataDirectory, SBEMessageListener& listener);
    ~SBEQueuePoller();

    // Non-copyable, movable
    SBEQueuePoller(const SBEQueuePoller&) = delete;
    SBEQueuePoller& operator=(const SBEQueuePoller&) = delete;
    SBEQueuePoller(SBEQueuePoller&&) = default;
    SBEQueuePoller& operator=(SBEQueuePoller&&) = default;

    void readFrom(const boost::filesystem::path& filePath, bool liveMode = false);

    bool next();

    bool isValid() const noexcept { return m_isValid; }
    std::size_t getFilePosition() const noexcept { return m_filePosition; }
    std::size_t getFileSize() const noexcept { return m_fileSize; }
    bool isAtEndOfFile() const noexcept { return m_filePosition >= m_fileSize; }

    void close();

private:
    std::string m_dataDirectory;
    SBEMessageListener& m_listener;

    // State management
    bool m_isValid = false;
    bool m_isLiveMode = false;

    // File I/O
    std::unique_ptr<boost::iostreams::mapped_file_source> m_mappedFile;
    const char* m_fileData = nullptr;
    std::size_t m_filePosition = 0;
    std::size_t m_fileSize = 0;
    boost::filesystem::path m_currentFilePath;

    // Buffer for reading
    std::vector<char> m_buffer;
    std::size_t m_bufferLimit = 0;

    // SBE decoders
    com::liversedge::messages::MessageHeader m_messageHeader;
    com::liversedge::messages::ConnectionStatus m_connectionStatusFlyweight;
    com::liversedge::messages::SecurityDefinition m_securityDefinitionFlyweight;
    com::liversedge::messages::SecurityStatus m_securityStatusFlyweight;
    com::liversedge::messages::MDFullBook m_mdFullBookFlyweight;
    com::liversedge::messages::MDUpdate m_mdUpdateFlyweight;
    com::liversedge::messages::NewOrder m_newOrderFlyweight;
    com::liversedge::messages::AmendOrder m_amendOrderFlyweight;
    com::liversedge::messages::CancelOrder m_cancelOrderFlyweight;

    // Helper methods
    bool fillBuffer();
    bool initializeFileMapping();
    void closeResources();
    std::uint64_t getCurrentTimestamp() const;
};