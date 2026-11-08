#include "aura/auth.h"
#include "aura/database.h"

#include <bcrypt/BCrypt.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

int main() {
    // 校验与已有数据库密码哈希的兼容性
    assert(BCrypt::validatePassword(
        "AURA-Cpp-Test-2026!",
        "$2b$10$P9Cfh6nCEOIHjca1c4b/4OdhEqqoxgEDjrBW34uC46QQfXkNvb0dq"));
    assert(!BCrypt::validatePassword(
        "wrong-password",
        "$2b$10$P9Cfh6nCEOIHjca1c4b/4OdhEqqoxgEDjrBW34uC46QQfXkNvb0dq"));

    const auto path = std::filesystem::temp_directory_path()
        / ("aura-auth-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
    {
        aura::Database database(path.string());
        aura::AuthService auth(database, "test-secret-that-is-at-least-thirty-two-characters");
        const std::string token = auth.createToken("user-1", "employee", 60);
        const auto payload = auth.verifyToken(token);
        assert(payload);
        assert((*payload)["userId"].asString() == "user-1");
        assert((*payload)["role"].asString() == "employee");
        assert(!auth.verifyToken(token + "x"));
        assert(!auth.verifyToken("not-a-token"));
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::cout << "aura_auth_tests: passed\n";
    return 0;
}
