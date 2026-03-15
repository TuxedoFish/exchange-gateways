#include "../../include/util/SimpleConfig.h"

// Get string value
std::string SimpleConfig::getString(const std::string& key) const {
    auto it = config_values_.find(key);
    if (it == config_values_.end()) {
        throw std::runtime_error("Configuration key not found: " + key);
    }
    return it->second;
}

// Get string with default value
std::string SimpleConfig::getString(const std::string& key, const std::string& default_value) const {
    auto it = config_values_.find(key);
    return (it != config_values_.end()) ? it->second : default_value;
}

// Get integer value
int SimpleConfig::getInt(const std::string& key) const {
    return std::stoi(getString(key));
}

int SimpleConfig::getInt(const std::string& key, int default_value) const {
    try {
        return std::stoi(getString(key));
    }
    catch (...) {
        return default_value;
    }
}

// Get double value
double SimpleConfig::getDouble(const std::string& key) const {
    return std::stod(getString(key));
}

double SimpleConfig::getDouble(const std::string& key, double default_value) const {
    try {
        return std::stod(getString(key));
    }
    catch (...) {
        return default_value;
    }
}

// Get boolean value
bool SimpleConfig::getBool(const std::string& key) const {
    std::string value = getString(key);
    // Convert to lowercase for comparison
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "1" || value == "yes";
}

bool SimpleConfig::getBool(const std::string& key, bool default_value) const {
    try {
        return getBool(key);
    }
    catch (...) {
        return default_value;
    }
}

// Check if key exists
bool SimpleConfig::hasKey(const std::string& key) const {
    return config_values_.find(key) != config_values_.end();
}

void SimpleConfig::loadFromFile() {
    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        spdlog::error("Cannot open configuration file: {}", config_file_path_);
        throw std::runtime_error("Cannot open configuration file: " + config_file_path_);
    }

    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        spdlog::info("{}", line);
        line_number++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Find the equals sign
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            spdlog::error("Warning: Invalid line {} in config file: {}", line_number, line);
            continue;
        }

        // Extract key and value
        std::string key = trim(line.substr(0, equals_pos));
        std::string value = trim(line.substr(equals_pos + 1));

        // Remove quotes if present
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }

        config_values_[key] = value;
    }
    spdlog::info("Loaded configuration from: {}", config_file_path_);
}

// Utility function to trim whitespace
std::string SimpleConfig::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
