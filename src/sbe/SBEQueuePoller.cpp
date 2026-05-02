#include "../../include/sbe/SBEQueuePoller.h"

SBEQueuePoller::SBEQueuePoller(const std::string& dataDirectory, SBEMessageListener& listener)
    : m_dataDirectory(dataDirectory), m_listener(listener), m_buffer(INITIAL_BUFFER_SIZE)
{
}

SBEQueuePoller::~SBEQueuePoller()
{
    close();
}

void SBEQueuePoller::readFrom(const boost::filesystem::path& filePath, bool liveMode)
{
    if (m_isValid) {
        throw std::runtime_error("Poller is already in valid state. Call close() first.");
    }

    m_currentFilePath = filePath;
    m_isLiveMode = liveMode;


    // If file does not exist or size is 0 - invalid can't poll
    bool fileExists = boost::filesystem::exists(filePath);
    if (!fileExists || boost::filesystem::file_size(filePath) == 0) {
        if (!liveMode) {
            throw std::runtime_error("Data file not found: " + filePath.string());
        }
        // In live mode, file doesn't exist yet - just set up for polling
        spdlog::info("No messages.sbe file found at: {}", filePath.string());
        spdlog::info("Will poll for new file creation...");
        return;
    }

    if (initializeFileMapping()) {
        m_isValid = true;
    } else {
        throw std::runtime_error("Failed to initialize file mapping for: " + filePath.string());
    }
}

bool SBEQueuePoller::next()
{
    if (!m_isValid) {
        // In live mode, try to initialize if file becomes available
        if (m_isLiveMode && !m_currentFilePath.empty()
            && boost::filesystem::exists(m_currentFilePath) \
            && boost::filesystem::file_size(m_currentFilePath) != 0) {
            if (initializeFileMapping()) {
                spdlog::info("File found, starting to poll: {}", m_currentFilePath.string());
                m_isValid = true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    if (!fillBuffer()) {
        return false; // End of file reached
    }

    // Check if we have enough data for a message header
    if (m_bufferLimit < MESSAGE_HEADER_SIZE) {
        return false; // Not enough data remaining
    }

    // Wrap the message header
    m_messageHeader.wrap(m_buffer.data(), 0, m_bufferLimit, m_bufferLimit);

    std::size_t messageHeaderLength = com::liversedge::messages::MessageHeader::encodedLength();
    std::size_t messageDataOffset = messageHeaderLength;
    std::uint16_t blockLength = m_messageHeader.blockLength();

    // Check if we have enough data for the fixed part of the message
    if (m_bufferLimit < messageHeaderLength + blockLength) {
        return false; // Not enough data for fixed part
    }

    std::uint64_t timestamp = getCurrentTimestamp();
    std::size_t actualMessageLength;

    // Dispatch based on template ID: compute actual message length, validate
    // buffer has the complete message, then deliver to listener.
    switch (m_messageHeader.templateId()) {
        case com::liversedge::messages::ConnectionStatus::sbeTemplateId():
        {
            m_connectionStatusFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                         blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_connectionStatusFlyweight.encodedLength();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("ConnectionStatus", actualMessageLength);
            m_listener.onConnectionStatus(m_connectionStatusFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::SecurityDefinition::sbeTemplateId():
        {
            m_securityDefinitionFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                           blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_securityDefinitionFlyweight.encodedLength() + m_securityDefinitionFlyweight.symbol().length();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("SecurityDefinition", actualMessageLength);
            m_listener.onSecurityDefinition(m_securityDefinitionFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::SecurityStatus::sbeTemplateId():
        {
            m_securityStatusFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                       blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_securityStatusFlyweight.encodedLength();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("SecurityStatus", actualMessageLength);
            m_listener.onSecurityStatus(m_securityStatusFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::MDUpdate::sbeTemplateId():
        {
            m_mdUpdateFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                 blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_mdUpdateFlyweight.encodedLength();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("MDUpdate", actualMessageLength);
            m_listener.onMDUpdate(m_mdUpdateFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::MDFullBook::sbeTemplateId():
        {
            m_mdFullBookFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                   blockLength, m_messageHeader.version(), m_bufferLimit);
            try {
                actualMessageLength = m_mdFullBookFlyweight.decodeLength();
            } catch (const std::runtime_error&) {
                return handleBufferTooSmall("MDFullBook", m_bufferLimit);
            }
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("MDFullBook", actualMessageLength);
            m_listener.onMDFullBook(m_mdFullBookFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::NewOrder::sbeTemplateId():
        {
            m_newOrderFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                   blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_newOrderFlyweight.encodedLength()
                + m_newOrderFlyweight.clientOrderId().length();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("NewOrder", actualMessageLength);
            m_listener.onNewOrder(m_newOrderFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::CancelOrder::sbeTemplateId():
        {
            m_cancelOrderFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                   blockLength, m_messageHeader.version(), m_bufferLimit);
            actualMessageLength = m_cancelOrderFlyweight.encodedLength()
                + m_cancelOrderFlyweight.origClientOrderId().length()
                + m_cancelOrderFlyweight.clientOrderId().length();
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("CancelOrder", actualMessageLength);
            m_listener.onCancelOrder(m_cancelOrderFlyweight, timestamp);
            break;
        }

        case com::liversedge::messages::AmendOrder::sbeTemplateId():
            {
                m_amendOrderFlyweight.wrapForDecode(m_buffer.data(), messageDataOffset,
                                       blockLength, m_messageHeader.version(), m_bufferLimit);
            try {
                actualMessageLength = m_amendOrderFlyweight.encodedLength()
                    + m_amendOrderFlyweight.clientOrderId().length();
            } catch (const std::runtime_error&)
            {
                return handleBufferTooSmall("AmendOrder", m_bufferLimit);
            }
            if (actualMessageLength > INITIAL_BUFFER_SIZE || m_bufferLimit < messageHeaderLength + actualMessageLength)
                return handleBufferTooSmall("AmendOrder", actualMessageLength);
            m_listener.onAmendOrder(m_amendOrderFlyweight, timestamp);
            break;
        }

        default:
            // Unknown message type - skip using block length
            actualMessageLength = blockLength;
            break;
    }

    // Advance file position by the actual message length (header + data)
    std::size_t totalMessageLength = messageHeaderLength + actualMessageLength;
    m_filePosition += totalMessageLength;

    return true;
}

bool SBEQueuePoller::fillBuffer()
{
    // In live mode, refresh file size to detect new data
    if (m_isLiveMode) {
        // Check if file has grown by getting current file size
        boost::filesystem::path filePath(m_dataDirectory + "/messages.sbe");
        if (boost::filesystem::exists(filePath)) {
            std::size_t currentFileSize = boost::filesystem::file_size(filePath);
            if (currentFileSize > m_fileSize) {
                // File has grown, remap it
                m_mappedFile.reset();
                m_mappedFile = std::make_unique<boost::iostreams::mapped_file_source>(filePath.string());
                m_fileData = m_mappedFile->data();
                m_fileSize = m_mappedFile->size();
            }
        }
    }

    if (m_filePosition >= m_fileSize) {
        if (m_isLiveMode) {
            // In live mode, no data available yet but don't give up
            return false;
        } else {
            return false; // End of file in non-live mode
        }
    }

    // Calculate how much data we can read
    std::size_t remainingBytes = m_fileSize - m_filePosition;
    std::size_t bytesToRead = std::min(remainingBytes, m_buffer.size());

    // Copy data from memory-mapped file to buffer
    std::memcpy(m_buffer.data(), m_fileData + m_filePosition, bytesToRead);
    m_bufferLimit = bytesToRead;

    return true;
}

void SBEQueuePoller::close()
{
    closeResources();
    m_isValid = false;
}

void SBEQueuePoller::closeResources()
{
    if (m_mappedFile) {
        m_mappedFile.reset();
        m_fileData = nullptr;
    }
}

bool SBEQueuePoller::initializeFileMapping()
{
    try {
        m_mappedFile = std::make_unique<boost::iostreams::mapped_file_source>(m_currentFilePath.string());
        if (!m_mappedFile->is_open()) {
            spdlog::error("Failed to open file: {}", m_currentFilePath.string());
            return false;
        }

        m_fileData = m_mappedFile->data();
        m_fileSize = m_mappedFile->size();
        m_filePosition = 0;
        m_bufferLimit = 0;
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Error initializing file mapping: {}", e.what());
        closeResources();
        return false;
    }
}

std::uint64_t SBEQueuePoller::getCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

bool SBEQueuePoller::handleBufferTooSmall(const char* messageType, std::size_t actualMessageLength)
{
    if (m_filePosition != m_lastProcessedPosition) {
        m_lastProcessedPosition = m_filePosition;
        m_consecutiveFailedPolls = 1;
    } else {
        m_consecutiveFailedPolls++;
    }
    if (m_consecutiveFailedPolls >= FAILED_POLL_LOG_THRESHOLD) {
        spdlog::error("[{}] Message length {} exceeds buffer limit {}: not parsing message (stuck for {} polls)",
                      messageType, actualMessageLength, m_bufferLimit, m_consecutiveFailedPolls);
    }
    return false;
}