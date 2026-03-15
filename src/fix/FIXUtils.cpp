#include "../../include/fix/FIXUtils.h"

using encoding_t = unsigned char const*;

std::uint64_t FixUtils::convertFIXTimeToNanos(const FIX::UtcTimeStamp& fixTime)
{
    auto timePoint = std::chrono::system_clock::from_time_t(fixTime.getTimeT());

    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
        timePoint.time_since_epoch()
    ).count();

    nanoseconds += fixTime.getMillisecond() * 1000000;

    return static_cast<std::uint64_t>(nanoseconds);
}

void FixUtils::logFixMessage(const std::string& prefix, const FIX::Message& message)
{
    std::string fixString = message.toString();
    std::replace(fixString.begin(), fixString.end(), '\001', '|');
    spdlog::info("{}: {}", prefix, fixString);
}

void FixUtils::addDeribitAuth(FIX::Message& message, const SimpleConfig& config)
{
    std::string user = config.getString("deribit_client_id");
    std::string secret = config.getString("deribit_client_secret");

    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::string timestamp_in_ms = std::to_string(ms.count());

    constexpr int nonce_to_encode_size = 32;
    unsigned char nonce_to_encode[nonce_to_encode_size];
    if (!RAND_bytes(nonce_to_encode, nonce_to_encode_size)) {
        throw(std::runtime_error("Impossible to create random data to login"));
    }
    encoding_t nonce = reinterpret_cast<encoding_t>(nonce_to_encode);
    std::string nonce64 = AuthHandler::base64_encode(nonce, nonce_to_encode_size);

    std::stringstream raw_data_stream;
    raw_data_stream << timestamp_in_ms << "." << nonce64;
    std::string raw_data = raw_data_stream.str();
    std::string raw_and_secret = raw_data + secret;
    spdlog::info("Logging on with timestamp={} nonce64={} raw_data={}raw_and_secret={}", timestamp_in_ms, nonce64, raw_data, raw_and_secret);

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, raw_and_secret.c_str(), raw_and_secret.size());
    SHA256_Final(hash, &sha256);
    std::string password_sha_base64 = AuthHandler::base64_encode(hash, SHA256_DIGEST_LENGTH);

    message.setField(FIX::Username(user));
    message.setField(FIX::RawData(raw_data));
    message.setField(FIX::Password(password_sha_base64));
    spdlog::info("Sending logon with credentials");
}
