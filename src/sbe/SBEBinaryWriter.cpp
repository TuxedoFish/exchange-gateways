#include "../../include/sbe/SBEBinaryWriter.h"

SBEBinaryWriter::SBEBinaryWriter()
    : messageCount_(0), buffer_(BUFFER_SIZE) {
}

SBEBinaryWriter::~SBEBinaryWriter() {
    close();
}

void SBEBinaryWriter::openNewFile(const std::string& filename, bool append) {
    close();
    filename_ = filename;

    // Create directories if they don't exist
    boost::filesystem::path filepath(filename);
    if (filepath.has_parent_path()) {
        boost::filesystem::create_directories(filepath.parent_path());
    }

    std::ios::openmode mode = std::ios::binary;
    if (append) {
        mode |= std::ios::app;
    } else {
        mode |= std::ios::trunc;
    }

    file_.open(filename, mode);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filename_);
    }
    spdlog::info("{} binary file: {}", (append ? "Opened" : "Created"), filename_);
}

// Flush and close file
void SBEBinaryWriter::close() {
    writeMutex_.lock();
    if (file_.is_open()) {
        file_.flush();
        file_.close();
        spdlog::info("Closed file {} after writing {} messages", filename_, messageCount_);
    }
    writeMutex_.unlock();
}

// Get stats
size_t SBEBinaryWriter::getMessageCount() const { return messageCount_; }
const std::string& SBEBinaryWriter::getFilename() const { return filename_; }

// Check if file is open and ready
bool SBEBinaryWriter::isOpen() const { return file_.is_open(); }

// Private flush to disk - called only while mutex is held
void SBEBinaryWriter::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}