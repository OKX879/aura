#include "aura/auth.h"

#include "aura/database.h"

#include <bcrypt/BCrypt.hpp>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace aura {
namespace {

std::string base64UrlEncode(const unsigned char* data, std::size_t size) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(4 * ((size + 2) / 3));
    std::uint32_t buffer = 0;
    int bits = 0;
    for (std::size_t i = 0; i < size; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            encoded.push_back(alphabet[(buffer >> bits) & 0x3f]);
        }
    }
    if (bits > 0) encoded.push_back(alphabet[(buffer << (6 - bits)) & 0x3f]);
    while (encoded.size() % 4 != 0) encoded.push_back('=');
    while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
    for (char& value : encoded) {
        if (value == '+') value = '-';
        else if (value == '/') value = '_';
    }
    return encoded;
}

std::optional<std::string> base64UrlDecode(std::string encoded) {
    for (char& value : encoded) {
        if (value == '-') value = '+';
        else if (value == '_') value = '/';
    }
    if (encoded.empty()) return std::nullopt;
    std::string decoded;
    decoded.reserve(encoded.size() * 3 / 4);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const unsigned char value : encoded) {
        if (value == '=') break;
        int digit = -1;
        if (value >= 'A' && value <= 'Z') digit = value - 'A';
        else if (value >= 'a' && value <= 'z') digit = value - 'a' + 26;
        else if (value >= '0' && value <= '9') digit = value - '0' + 52;
        else if (value == '+') digit = 62;
        else if (value == '/') digit = 63;
        if (digit < 0) return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<char>((buffer >> bits) & 0xff));
        }
    }
    return decoded;
}

std::string jsonCompact(const Json::Value& value) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

std::optional<Json::Value> parseJson(const std::string& value) {
    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream input(value);
    if (!Json::parseFromStream(builder, input, &json, &errors)) return std::nullopt;
    return json;
}

std::string sign(const std::string& message, const std::string& secret) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    unsigned char output[32]{};
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        status = BCryptHash(
            algorithm,
            reinterpret_cast<PUCHAR>(const_cast<char*>(secret.data())),
            static_cast<ULONG>(secret.size()),
            reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
            static_cast<ULONG>(message.size()),
            output,
            static_cast<ULONG>(sizeof(output)));
    }
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Windows HMAC-SHA256 failed");
#else
    unsigned char output[32]{};
    unsigned int length = 0;
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(message.data()),
         message.size(), output, &length);
    if (length != sizeof(output)) throw std::runtime_error("HMAC-SHA256 failed");
#endif
    return base64UrlEncode(output, sizeof(output));
}

std::string randomSecret() {
    unsigned char bytes[48]{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(sizeof(bytes)),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("Secure random generation failed");
    }
#else
    if (RAND_bytes(bytes, static_cast<int>(sizeof(bytes))) != 1) {
        throw std::runtime_error("Secure random generation failed");
    }
#endif
    return base64UrlEncode(bytes, sizeof(bytes));
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    }
    return difference == 0;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> secretFromDotEnv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string line;
    constexpr std::string_view prefix = "JWT_SECRET=";
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.starts_with(prefix)) continue;
        std::string value = trim(line.substr(prefix.size()));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
            || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (value.size() >= 32) return value;
    }
    return std::nullopt;
}

}  // namespace

AuthService::AuthService(Database& database, std::string secret)
    : database_(database), secret_(std::move(secret)) {
    if (secret_.size() < 32) throw std::runtime_error("JWT secret must contain at least 32 characters");
}

Json::Value AuthService::login(const std::string& email, const std::string& password) {
    Json::Value result(Json::objectValue);
    const auto user = database_.findUserByEmail(email, true);
    if (!user || !(*user)["passwordHash"].isString()
        || !BCrypt::validatePassword(password, (*user)["passwordHash"].asString())) {
        result["ok"] = false;
        return result;
    }
    Json::Value profile = *user;
    profile.removeMember("passwordHash");
    result["ok"] = true;
    result["token"] = createToken(profile["id"].asString(), profile["role"].asString());
    result["user"] = profile;
    return result;
}

std::string AuthService::createToken(
    const std::string& userId,
    const std::string& role,
    long long ttlSeconds
) const {
    Json::Value header(Json::objectValue);
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Json::Value payload(Json::objectValue);
    payload["userId"] = userId;
    payload["role"] = role;
    payload["iat"] = Json::Int64(now);
    payload["exp"] = Json::Int64(now + ttlSeconds);
    const std::string headerText = jsonCompact(header);
    const std::string encodedHeader = base64UrlEncode(
        reinterpret_cast<const unsigned char*>(headerText.data()), headerText.size());
    const std::string payloadText = jsonCompact(payload);
    const std::string encodedPayload = base64UrlEncode(
        reinterpret_cast<const unsigned char*>(payloadText.data()), payloadText.size());
    const std::string unsignedToken = encodedHeader + "." + encodedPayload;
    return unsignedToken + "." + sign(unsignedToken, secret_);
}

std::optional<Json::Value> AuthService::verifyToken(const std::string& token) const {
    const auto first = token.find('.');
    const auto second = first == std::string::npos ? std::string::npos : token.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos || token.find('.', second + 1) != std::string::npos) {
        return std::nullopt;
    }
    const std::string unsignedToken = token.substr(0, second);
    const std::string suppliedSignature = token.substr(second + 1);
    const std::string expectedSignature = sign(unsignedToken, secret_);
    if (!constantTimeEqual(suppliedSignature, expectedSignature)) {
        return std::nullopt;
    }
    const auto headerText = base64UrlDecode(token.substr(0, first));
    const auto payloadText = base64UrlDecode(token.substr(first + 1, second - first - 1));
    if (!headerText || !payloadText) return std::nullopt;
    const auto header = parseJson(*headerText);
    const auto payload = parseJson(*payloadText);
    if (!header || !payload || (*header)["alg"].asString() != "HS256") return std::nullopt;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!(*payload)["exp"].isInt64() || (*payload)["exp"].asInt64() <= now
        || !(*payload)["userId"].isString()) return std::nullopt;
    return payload;
}

std::optional<AuthenticatedUser> AuthService::authenticate(
    const drogon::HttpRequestPtr& request,
    std::string& errorCode
) {
    const std::string authorization = request->getHeader("Authorization");
    constexpr const char* prefix = "Bearer ";
    if (!authorization.starts_with(prefix)) {
        errorCode = "UNAUTHORIZED";
        return std::nullopt;
    }
    const auto payload = verifyToken(authorization.substr(std::char_traits<char>::length(prefix)));
    if (!payload) {
        errorCode = "FORBIDDEN";
        return std::nullopt;
    }
    const auto user = database_.findUserById((*payload)["userId"].asString());
    if (!user || (*user)["status"].asString() != "active") {
        errorCode = "ACCOUNT_INACTIVE";
        return std::nullopt;
    }
    return AuthenticatedUser{(*user)["id"].asString(), (*user)["role"].asString(), *user};
}

std::string loadOrCreateSecret(const std::string& databasePath) {
    if (const char* configured = std::getenv("JWT_SECRET"); configured && std::char_traits<char>::length(configured) >= 32) {
        return configured;
    }
    const std::filesystem::path databaseDirectory = std::filesystem::path(databasePath).parent_path();
    if (const auto existing = secretFromDotEnv(databaseDirectory.parent_path() / ".env")) {
        return *existing;
    }
    const std::filesystem::path secretPath =
        databaseDirectory / ".aura-service-secret";
    {
        std::ifstream input(secretPath, std::ios::binary);
        std::string value;
        if (input && std::getline(input, value) && value.size() >= 32) return value;
    }
    std::filesystem::create_directories(secretPath.parent_path());
    const std::string value = randomSecret();
    std::ofstream output(secretPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot persist JWT secret next to database");
    output << value << '\n';
    return value;
}

}  // namespace aura
