#pragma once

#include <functional>
#include <json/json.h>
#include <memory>
#include <string>

namespace aura {

class Database;
class AuthService;

void registerApiRoutes(
    const std::shared_ptr<Database>& database,
    const std::shared_ptr<AuthService>& auth,
    const std::function<Json::Value()>& liveSnapshot = {}
);

// 配对码：首次生成后写入 .pair-code，重启后复用
std::string ensurePairingCode();

}  // namespace aura
