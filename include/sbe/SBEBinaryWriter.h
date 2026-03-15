#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <mutex>
#include <boost/filesystem.hpp>
#include "../../generated/com_liversedge_messages/MessageHeader.h"
#include "../../generated/com_liversedge_messages/SecurityDefinition.h"

class SBEBinaryWriter {
private:
    std::ofstream file_;
    std::string filename_;
    size_t messageCount_;
    std::vector<char> buffer_;
    mutable std::mutex writeMutex_;

    static constexpr size_t BUFFER_SIZE = 4092;

public:
    SBEBinaryWriter();
    ~SBEBinaryWriter();

    template<typename T>
    bool writeMessage(T& message);
    template<typename T>
    bool prepareMessage(T& message);

    void openNewFile(const std::string& filename, bool append = false);
    void close();

    size_t getMessageCount() const;
    const std::string& getFilename() const;
    bool isOpen() const;

private:
    void flush(); // Private - called automatically by writeMessage()
};

// Generic prepare method that works with any SBE message type - ACQUIRES LOCK
template<typename T>
bool SBEBinaryWriter::prepareMessage(T& message) {
    try {
        // Acquire lock - will be held until writeMessage() completes
        writeMutex_.lock();

        // Clear buffer
        std::fill(buffer_.begin(), buffer_.end(), 0);

        // Create and encode message header
        com::liversedge::messages::MessageHeader hdr;
        const size_t headerSize = hdr.encodedLength();

        // Wrap header at the beginning of buffer
        hdr.wrap(buffer_.data(), 0, 0, buffer_.size())
            .blockLength(message.sbeBlockLength())
            .templateId(message.sbeTemplateId())
            .schemaId(message.sbeSchemaId())
            .version(message.sbeSchemaVersion());

        // Wrap message after the header
        message.wrapForEncode(buffer_.data(), headerSize, buffer_.size() - headerSize);
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Error preparing message: {}", e.what());
        writeMutex_.unlock(); // Release lock on error
        return false;
    }
}

// Generic write method that works with any SBE message type - RELEASES LOCK
template<typename T>
bool SBEBinaryWriter::writeMessage(T& message) {
    try {
        // Calculate total encoded size (header + message payload)
        com::liversedge::messages::MessageHeader hdr;
        size_t totalSize = hdr.encodedLength() + message.encodedLength();

        // Ensure we don't exceed buffer size
        if (totalSize > buffer_.size()) {
            spdlog::error("Message too large for buffer. Size: {}, Buffer: {}", totalSize, buffer_.size());
            writeMutex_.unlock(); // Release lock on error
            return false;
        }

        // Write to file
        file_.write(buffer_.data(), totalSize);
        if (!file_.good()) {
            spdlog::error("Error writing to file");
            writeMutex_.unlock(); // Release lock on error
            return false;
        }

        // Flush to disk while lock is held
        flush();

        messageCount_++;
        writeMutex_.unlock(); // Release lock on success
        return true;

    }
    catch (const std::exception& e) {
        spdlog::error("Error writing message: {}", e.what());
        writeMutex_.unlock(); // Release lock on exception
        return false;
    }
}

