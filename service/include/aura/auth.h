#pragma once

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <optional>
#include <string>

namespace aura {

class Database;

struct AuthenticatedUser {
    std::string id;
    std::string role;
    Json::Value profile;

    bool isAdmin() const noexcept { return role == "admin"; }
};

class AuthService {
public:
    AuthService(Database& database, std::string secret);

    Json::Value login(const std::string& email, const std::string& password);
    std::optional<AuthenticatedUser> authenticate(
        const drogon::HttpRequestPtr& request,
        std::string& errorCode
    );

    std::string createToken(const std::string& userId, const std::string& role, long long ttlSeconds = 86400) const;
    std::optional<Json::Value> verifyToken(const std::string& token) const;

private:
    Database& database_;
    std::string secret_;
};

std::string loadOrCreateSecret(const std::string& databasePath);

}  // namespace aura
