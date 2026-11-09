#include "aura/api.h"
#include "aura/auth.h"
#include "aura/database.h"

#include <drogon/drogon.h>
#include <bcrypt/BCrypt.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace aura {
namespace {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

drogon::HttpResponsePtr response(Json::Value body, drogon::HttpStatusCode status = drogon::k200OK) {
    auto result = drogon::HttpResponse::newHttpJsonResponse(body);
    result->setStatusCode(status);
    result->addHeader("Cache-Control", "no-store");
    result->addHeader("Access-Control-Allow-Origin", "*");
    result->addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
    result->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    return result;
}

drogon::HttpResponsePtr success(const Json::Value& data, drogon::HttpStatusCode status = drogon::k200OK) {
    Json::Value body(Json::objectValue);
    body["success"] = true;
    body["data"] = data;
    return response(body, status);
}

drogon::HttpResponsePtr failure(
    const std::string& message,
    const std::string& code,
    drogon::HttpStatusCode status
) {
    Json::Value error(Json::objectValue);
    error["message"] = message;
    error["code"] = code;
    Json::Value body(Json::objectValue);
    body["success"] = false;
    body["error"] = error;
    return response(body, status);
}

std::optional<AuthenticatedUser> requireUser(
    const drogon::HttpRequestPtr& request,
    const std::shared_ptr<AuthService>& auth,
    Callback& callback
) {
    std::string errorCode;
    auto user = auth->authenticate(request, errorCode);
    if (user) return user;
    if (errorCode == "UNAUTHORIZED") {
        callback(failure("未提供访问令牌", errorCode, drogon::k401Unauthorized));
    } else if (errorCode == "ACCOUNT_INACTIVE") {
        callback(failure("账号不存在或已停用", errorCode, drogon::k403Forbidden));
    } else {
        // 令牌失效返回 401，客户端据此跳转登录
        callback(failure("令牌无效或已过期", "TOKEN_INVALID", drogon::k401Unauthorized));
    }
    return std::nullopt;
}

int integerParameter(const drogon::HttpRequestPtr& request, const std::string& name, int fallback) {
    try {
        const std::string value = request->getParameter(name);
        return value.empty() ? fallback : std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

Json::Value safeLatest(const std::shared_ptr<Database>& database, const std::string& userId) {
    const Json::Value values = database->latestRecords(userId, false, 1);
    return values.empty() ? Json::Value(Json::nullValue) : values[0];
}

std::string ruleReply(const std::string& message, const Json::Value& latest) {
    if (latest.isNull()) return "暂时没有生命体征记录。请先确认雷达已连接并开始监测。";
    if (message.find("心率") != std::string::npos || message.find("heart") != std::string::npos) {
        if (latest["heartRate"].isNull()) return "当前没有可用的心率估计。";
        return "当前雷达心率估计为 " + latest["heartRate"].asString()
            + " BPM。该值来自非接触式机械信号估计，不等同于 ECG，也不能用于诊断。";
    }
    if (message.find("呼吸") != std::string::npos) {
        if (latest["respirationRate"].isNull()) return "当前没有可用的呼吸率估计。";
        return "当前雷达呼吸率估计为 " + latest["respirationRate"].asString()
            + " 次/分。请同时关注运动状态和信号质量。";
    }
    return "最新记录：心率 " + latest["heartRate"].asString()
        + " BPM，呼吸率 " + latest["respirationRate"].asString()
        + " 次/分，专注度 " + latest["focusScore"].asString()
        + "。这些是工程估计值，不是医疗诊断结论。";
}

/* 注册限流：同一 IP 每小时最多 5 次 */
bool signupAllowed(const std::string& ip) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::vector<std::chrono::steady_clock::time_point>> history;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex);
    auto& times = history[ip];
    times.erase(std::remove_if(times.begin(), times.end(),
        [&](const std::chrono::steady_clock::time_point& t) {
            return now - t > std::chrono::hours(1);
        }), times.end());
    if (times.size() >= 5) return false;
    times.push_back(now);
    return true;
}

}  // namespace

// 配对码保存在数据库同级 .pair-code，权限 600
std::string ensurePairingCode() {
    static const std::string code = [] {
        const char* configured = std::getenv("AURA_DB_PATH");
        const std::filesystem::path directory = (configured && *configured)
            ? std::filesystem::path(configured).parent_path()
            : std::filesystem::path(".");
        const std::filesystem::path path = directory / ".pair-code";
        {
            std::ifstream input(path);
            std::string value;
            if (input && std::getline(input, value)) {
                value.erase(
                    std::remove_if(value.begin(), value.end(),
                                   [](unsigned char c) { return std::isspace(c) != 0; }),
                    value.end());
                if (value.size() == 6) return value;
            }
        }
        std::random_device device;
        std::mt19937 generator(device());
        std::uniform_int_distribution<int> distribution(100000, 999999);
        const std::string generated = std::to_string(distribution(generator));
        std::ofstream output(path, std::ios::trunc);
        output << generated << '\n';
        output.close();
        std::error_code error;
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, error);
        return generated;
    }();
    return code;
}

void registerApiRoutes(
    const std::shared_ptr<Database>& database,
    const std::shared_ptr<AuthService>& auth,
    const std::function<Json::Value()>& liveSnapshot
) {
    using namespace drogon;
    // 实时快照含健康数据，必须登录后才能读取
    if (liveSnapshot) {
        app().registerHandler("/api/v1/live/snapshot",
            [liveSnapshot, auth](const HttpRequestPtr& request, Callback&& callback) {
                if (!requireUser(request, auth, callback)) return;
                callback(success(liveSnapshot()));
            }, {Get});
    }

    // 健康检查公开，只暴露运行计数
    struct LiveHealth {
        std::mutex mutex;
        std::uint64_t frames = 0;
        std::chrono::steady_clock::time_point changedAt = std::chrono::steady_clock::now();
    };
    auto liveHealth = std::make_shared<LiveHealth>();

    app().registerHandler("/api/v1/health",
        [database, liveSnapshot, liveHealth](const HttpRequestPtr&, Callback&& callback) {
            std::string detail;
            const bool databaseHealthy = database->healthy(&detail);

            Json::Value data(Json::objectValue);
            data["status"] = "ok";
            data["service"] = "aura-drogon";
            data["version"] = "1.1.0";
            data["database"] = databaseHealthy ? "ok" : detail;

            bool pipelineHealthy = true;
            if (liveSnapshot) {
                const Json::Value snapshot = liveSnapshot();
                const std::uint64_t frames = snapshot["frames"].asUInt64();
                const bool running = snapshot["running"].asBool();
                long long stalledMs = 0;
                {
                    std::lock_guard<std::mutex> lock(liveHealth->mutex);
                    const auto now = std::chrono::steady_clock::now();
                    if (frames != liveHealth->frames) {
                        liveHealth->frames = frames;
                        liveHealth->changedAt = now;
                    }
                    stalledMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - liveHealth->changedAt).count();
                }
                data["radar_running"] = running;
                data["radar_frames"] = frames;
                data["radar_stalled_ms"] = static_cast<Json::Int64>(stalledMs);
                data["radar_fs"] = snapshot["fs"];
                data["radar_errors"] = snapshot["errors"];
                // 雷达运行但 30 秒无新帧视为异常
                pipelineHealthy = !running || stalledMs < 30000;
            }

            const bool healthy = databaseHealthy && pipelineHealthy;
            data["status"] = healthy ? "ok" : "degraded";
            callback(success(data, healthy ? k200OK : k503ServiceUnavailable));
        }, {Get});

    // 设备配对：公开接口，按 IP 限制每分钟 10 次
    struct PairLimiter {
        std::mutex mutex;
        std::unordered_map<std::string,
                           std::pair<std::chrono::steady_clock::time_point, int>> buckets;

        bool allow(const std::string& key) {
            std::lock_guard<std::mutex> lock(mutex);
            const auto now = std::chrono::steady_clock::now();
            auto& bucket = buckets[key];
            if (bucket.second == 0 ||
                std::chrono::duration_cast<std::chrono::minutes>(now - bucket.first).count() >= 1) {
                bucket.first = now;
                bucket.second = 0;
            }
            if (bucket.second >= 10) return false;
            ++bucket.second;
            return true;
        }
    };
    auto pairLimiter = std::make_shared<PairLimiter>();

    app().registerHandler("/api/v1/pair/verify",
        [pairLimiter](const HttpRequestPtr& request, Callback&& callback) {
            const auto json = request->getJsonObject();
            if (!json || !(*json)["code"].isString()) {
                callback(failure("请输入配对码", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            if (!pairLimiter->allow(request->peerAddr().toIp())) {
                callback(failure("配对尝试过于频繁，请稍后再试", "TOO_MANY_ATTEMPTS",
                                 k403Forbidden));
                return;
            }
            if ((*json)["code"].asString() != ensurePairingCode()) {
                callback(failure("配对码不正确", "PAIR_CODE_INVALID", k403Forbidden));
                return;
            }
            const auto envOr = [](const char* key, const std::string& fallback) {
                const char* value = std::getenv(key);
                return (value && *value) ? std::string(value) : fallback;
            };
            Json::Value data(Json::objectValue);
            data["serial"] = envOr("AURA_DEVICE_SERIAL", "AURA-6240-001");
            data["model"] = envOr("AURA_DEVICE_MODEL", "MRS6240-P2512");
            data["name"] = envOr("AURA_DEVICE_NAME", "AURA 雷达");
            data["endpoint"] = envOr("AURA_DEVICE_ENDPOINT", "");
            data["firmwareVersion"] = envOr("AURA_DEVICE_FIRMWARE", "1.0.0");
            callback(success(data));
        }, {Post});

    app().registerHandler("/api/v1/auth/login",
        [auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto json = request->getJsonObject();
            if (!json || !(*json)["email"].isString() || !(*json)["password"].isString()) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            const Json::Value login = auth->login((*json)["email"].asString(), (*json)["password"].asString());
            if (!login["ok"].asBool()) {
                callback(failure("邮箱或密码错误", "UNAUTHORIZED", k401Unauthorized));
                return;
            }
            Json::Value data(Json::objectValue);
            data["token"] = login["token"];
            data["user"] = login["user"];
            callback(success(data));
        }, {Post});

    app().registerHandler("/api/v1/auth/register",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            // 仅管理员可创建账号
            const auto actor = requireUser(request, auth, callback);
            if (!actor) return;
            if (!actor->isAdmin()) {
                callback(failure("仅管理员可创建账号", "FORBIDDEN", k403Forbidden));
                return;
            }
            const auto json = request->getJsonObject();
            if (!json || !(*json)["name"].isString() || !(*json)["employeeId"].isString()
                || !(*json)["email"].isString() || !(*json)["password"].isString()
                || (*json)["name"].asString().empty() || (*json)["employeeId"].asString().empty()
                || (*json)["email"].asString().empty() || (*json)["password"].asString().size() < 8) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            if (database->userEmailExists((*json)["email"].asString())) {
                callback(failure("邮箱已被注册", "EMAIL_EXISTS", k409Conflict));
                return;
            }
            if (database->userEmployeeIdExists((*json)["employeeId"].asString())) {
                callback(failure("工号已被使用", "EMPLOYEE_ID_EXISTS", k409Conflict));
                return;
            }
            Json::Value data(Json::objectValue);
            data["name"] = (*json)["name"].asString();
            data["employeeId"] = (*json)["employeeId"].asString();
            data["departmentId"] = (*json)["departmentId"];
            data["email"] = (*json)["email"].asString();
            data["phone"] = (*json)["phone"];
            data["status"] = "active";
            data["role"] = database->userCount() == 0 ? "admin" : "employee";
            data["passwordHash"] = BCrypt::generateHash((*json)["password"].asString(), 10);
            const auto user = database->createUser(data);
            if (!user) {
                callback(failure("注册失败", "REGISTRATION_FAILED", k500InternalServerError));
                return;
            }
            Json::Value payload(Json::objectValue);
            payload["token"] = auth->createToken((*user)["id"].asString(), (*user)["role"].asString());
            payload["user"] = *user;
            callback(success(payload, k201Created));
        }, {Post});

    app().registerHandler("/api/v1/auth/signup",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            if (!signupAllowed(request->peerAddr().toIp())) {
                callback(failure("注册过于频繁，请稍后再试", "RATE_LIMITED", k429TooManyRequests));
                return;
            }
            const auto json = request->getJsonObject();
            if (!json || !(*json)["name"].isString() || !(*json)["email"].isString()
                || !(*json)["password"].isString()
                || (*json)["name"].asString().empty()
                || (*json)["email"].asString().empty()
                || (*json)["password"].asString().size() < 8) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            const std::string email = (*json)["email"].asString();
            if (email.find('@') == std::string::npos) {
                callback(failure("邮箱格式不正确", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            if (database->userEmailExists(email)) {
                callback(failure("邮箱已被注册", "EMAIL_EXISTS", k409Conflict));
                return;
            }
            Json::Value data(Json::objectValue);
            data["name"] = (*json)["name"].asString();
            data["employeeId"] = "U" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            data["email"] = email;
            data["status"] = "active";
            data["role"] = database->userCount() == 0 ? "admin" : "employee";
            data["passwordHash"] = BCrypt::generateHash((*json)["password"].asString(), 10);
            const auto user = database->createUser(data);
            if (!user) {
                callback(failure("注册失败", "REGISTRATION_FAILED", k500InternalServerError));
                return;
            }
            Json::Value payload(Json::objectValue);
            payload["token"] = auth->createToken((*user)["id"].asString(), (*user)["role"].asString());
            payload["user"] = *user;
            callback(success(payload, k201Created));
        }, {Post});

    app().registerHandler("/api/v1/auth/change-password",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const auto json = request->getJsonObject();
            if (!json || !(*json)["currentPassword"].isString() || !(*json)["newPassword"].isString()
                || (*json)["newPassword"].asString().size() < 8) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            const auto full = database->findUserByEmail(user->profile["email"].asString(), true);
            if (!full || !(*full)["passwordHash"].isString()
                || !BCrypt::validatePassword((*json)["currentPassword"].asString(), (*full)["passwordHash"].asString())) {
                callback(failure("当前密码错误", "CURRENT_PASSWORD_INVALID", k403Forbidden));
                return;
            }
            if (!database->updateUserPassword(user->id, BCrypt::generateHash((*json)["newPassword"].asString(), 12))) {
                callback(failure("修改失败", "PASSWORD_CHANGE_FAILED", k500InternalServerError));
                return;
            }
            Json::Value result(Json::objectValue);
            result["changed"] = true;
            callback(success(result));
        }, {Post});

    app().registerHandler("/api/v1/auth/me",
        [auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (user) callback(success(user->profile));
        }, {Get});

    // 用户自助修改资料，只允许改昵称与头像
    app().registerHandler("/api/v1/auth/profile",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const auto json = request->getJsonObject();
            if (!json || !json->isObject()) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }

            Json::Value patch(Json::objectValue);
            if ((*json).isMember("name")) {
                if (!(*json)["name"].isString()) {
                    callback(failure("昵称格式无效", "VALIDATION_ERROR", k400BadRequest));
                    return;
                }
                const std::string name = (*json)["name"].asString();
                if (name.empty() || name.size() > 40) {
                    callback(failure("昵称长度需在 1-40 个字符之间", "VALIDATION_ERROR",
                                     k400BadRequest));
                    return;
                }
                patch["name"] = name;
            }
            if ((*json).isMember("avatar")) {
                const Json::Value& avatar = (*json)["avatar"];
                if (avatar.isNull()) {
                    patch["avatar"] = Json::Value(Json::nullValue);
                } else if (avatar.isString()) {
                    // 头像以 Base64 存库，限制 512KB
                    if (avatar.asString().size() > 512 * 1024) {
                        callback(failure("头像图片过大，请重新选择", "PAYLOAD_TOO_LARGE",
                                         k400BadRequest));
                        return;
                    }
                    patch["avatar"] = avatar;
                } else {
                    callback(failure("头像格式无效", "VALIDATION_ERROR", k400BadRequest));
                    return;
                }
            }
            if (patch.empty()) {
                callback(failure("没有需要更新的字段", "VALIDATION_ERROR", k400BadRequest));
                return;
            }

            const auto updated = database->updateUser(user->id, patch);
            if (!updated) {
                callback(failure("资料更新失败", "UPDATE_FAILED", k500InternalServerError));
                return;
            }
            callback(success(*updated));
        }, {Post});

    app().registerHandler("/api/v1/devices",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            if (!requireUser(request, auth, callback)) return;
            callback(success(database->devices()));
        }, {Get});

    // 添加设备：注册到服务器，所有客户端可见
    app().registerHandler("/api/v1/devices",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            if (!requireUser(request, auth, callback)) return;
            const auto json = request->getJsonObject();
            if (!json || !(*json)["name"].isString() || (*json)["name"].asString().empty()) {
                callback(failure("设备名称不能为空", "VALIDATION_ERROR", drogon::k400BadRequest));
                return;
            }
            Json::Value data(Json::objectValue);
            data["name"] = (*json)["name"].asString();
            for (const char* key : {"serialNumber", "model", "roomId", "firmwareVersion", "endpointUrl"}) {
                if ((*json).isMember(key)) data[key] = (*json)[key];
            }
            const auto device = database->createDevice(data);
            if (!device) {
                callback(failure("注册失败，序列号可能已被使用", "DEVICE_CREATE_FAILED",
                                 drogon::k409Conflict));
                return;
            }
            callback(success(*device, drogon::k201Created));
        }, {Post});

    app().registerHandler("/api/v1/devices/{1}",
        [database, auth](const HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            if (!requireUser(request, auth, callback)) return;
            if (!database->findDeviceById(id)) {
                callback(failure("设备不存在", "NOT_FOUND", drogon::k404NotFound));
                return;
            }
            database->deleteDevice(id);
            Json::Value out(Json::objectValue);
            out["deleted"] = true;
            callback(success(out));
        }, {Delete});

    // 部门接口为空实现，仅满足前端读取
    app().registerHandler("/api/v1/departments",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            if (!requireUser(request, auth, callback)) return;
            callback(success(Json::Value(Json::arrayValue)));
        }, {Get});

    app().registerHandler("/api/v1/departments/building-diagram",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            if (!requireUser(request, auth, callback)) return;
            Json::Value data(Json::objectValue);
            data["floors"] = Json::Value(Json::arrayValue);
            callback(success(data));
        }, {Get});

    // 用户管理接口，仅管理员可用
    app().registerHandler("/api/v1/users",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            if (!user->isAdmin()) {
                callback(failure("权限不足", "FORBIDDEN", k403Forbidden));
                return;
            }
            if (request->method() == Get) {
                callback(success(database->users()));
                return;
            }
            const auto json = request->getJsonObject();
            if (!json || !(*json)["name"].isString() || !(*json)["employeeId"].isString()
                || !(*json)["email"].isString()) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            if (database->userEmailExists((*json)["email"].asString())) {
                callback(failure("邮箱已存在", "EMAIL_EXISTS", k409Conflict));
                return;
            }
            if (database->userEmployeeIdExists((*json)["employeeId"].asString())) {
                callback(failure("工号已存在", "EMPLOYEE_ID_EXISTS", k409Conflict));
                return;
            }
            Json::Value data(Json::objectValue);
            for (const char* key : {"name", "employeeId", "departmentId", "role", "email", "phone", "status"}) {
                if ((*json).isMember(key)) data[key] = (*json)[key];
            }
            if ((*json).isMember("password") && (*json)["password"].isString()) {
                data["passwordHash"] = BCrypt::generateHash((*json)["password"].asString(), 10);
            }
            const auto created = database->createUser(data);
            if (!created) {
                callback(failure("创建失败", "CREATE_FAILED", k500InternalServerError));
                return;
            }
            callback(success(*created, k201Created));
        }, {Get, Post});

    app().registerHandler("/api/v1/users/{1}",
        [database, auth](const HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            const auto actor = requireUser(request, auth, callback);
            if (!actor) return;
            const auto existing = database->findUserById(id);
            if (!existing) {
                callback(failure("用户不存在", "NOT_FOUND", k404NotFound));
                return;
            }
            if (request->method() == Delete) {
                if (!actor->isAdmin()) {
                    callback(failure("权限不足", "FORBIDDEN", k403Forbidden));
                    return;
                }
                database->deleteUser(id);
                Json::Value out(Json::objectValue);
                out["deleted"] = true;
                callback(success(out));
                return;
            }
            // 修改用户
            const auto json = request->getJsonObject();
            if (!json) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            if (!actor->isAdmin() && actor->id != id) {
                callback(failure("权限不足", "FORBIDDEN", k403Forbidden));
                return;
            }
            Json::Value data(Json::objectValue);
            for (const char* key : {"name", "employeeId", "departmentId", "role", "email", "phone", "status"}) {
                if ((*json).isMember(key)) data[key] = (*json)[key];
            }
            if (actor->isAdmin() && (*json).isMember("password") && (*json)["password"].isString()) {
                data["passwordHash"] = BCrypt::generateHash((*json)["password"].asString(), 12);
            }
            const auto updated = database->updateUser(id, data);
            callback(success(updated ? *updated : *existing));
        }, {Patch, Delete});

    // ---- 告警 ----
    app().registerHandler("/api/v1/alerts/{1}/resolve",
        [database, auth](const HttpRequestPtr& request, Callback&& callback, const std::string& id) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const auto existing = database->findAlertById(id);
            if (!existing) {
                callback(failure("告警不存在", "NOT_FOUND", k404NotFound));
                return;
            }
            if (!user->isAdmin() && existing->isMember("userId")
                && (*existing)["userId"].isString() && (*existing)["userId"].asString() != user->id) {
                callback(failure("权限不足", "FORBIDDEN", k403Forbidden));
                return;
            }
            const auto resolved = database->resolveAlert(id);
            callback(success(resolved ? *resolved : *existing));
        }, {Post});

    // ---- 专注数据 ----
    app().registerHandler("/api/v1/focus-data/personal-stats",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const std::string range = request->getParameter("range");
            callback(success(database->personalStats(user->id, range)));
        }, {Get});

    app().registerHandler("/api/v1/focus-data/event-stats",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const std::string range = request->getParameter("range");
            callback(success(database->eventStats(user->id, range)));
        }, {Get});

    app().registerHandler("/api/v1/focus-data/latest",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const int limit = std::clamp(integerParameter(request, "limit", 100), 1, 1000);
            callback(success(database->latestRecords(user->id, user->isAdmin(), limit)));
        }, {Get});

    const auto ingest = [database, auth](const HttpRequestPtr& request, Callback&& callback, bool bulk) {
        const auto user = requireUser(request, auth, callback);
        if (!user) return;
        const auto json = request->getJsonObject();
        if (!json) {
            callback(failure("请求体必须是 JSON", "VALIDATION_ERROR", k400BadRequest));
            return;
        }
        Json::Value records(Json::arrayValue);
        if (bulk) {
            if (!(*json)["records"].isArray()) {
                callback(failure("Expected { records: [...] }", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            records = (*json)["records"];
        } else {
            records.append(*json);
        }
        std::string error;
        const int inserted = database->ingestRecords(records, user->id, user->isAdmin(), error);
        if (inserted < 0) {
            callback(failure(error, "INGEST_FAILED", k400BadRequest));
            return;
        }
        if (inserted > 0) database->createFatigueAlerts();
        Json::Value data(Json::objectValue);
        data[bulk ? "inserted" : "created"] = bulk ? inserted : inserted > 0;
        callback(success(data, k201Created));
    };
    app().registerHandler("/api/v1/focus-data/ingest",
        [ingest](const HttpRequestPtr& request, Callback&& callback) { ingest(request, std::move(callback), false); }, {Post});
    app().registerHandler("/api/v1/focus-data/bulk-ingest",
        [ingest](const HttpRequestPtr& request, Callback&& callback) { ingest(request, std::move(callback), true); }, {Post});

    app().registerHandler("/api/v1/alerts",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            callback(success(database->alerts(user->id, user->isAdmin(), false)));
        }, {Get});

    app().registerHandler("/api/v1/alerts/unresolved",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            callback(success(database->alerts(user->id, user->isAdmin(), true)));
        }, {Get});

    // ---- 疲劳预警配置 ----
    app().registerHandler("/api/v1/settings/fatigue-alert",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            if (request->method() == Post) {
                if (!user->isAdmin()) {
                    callback(failure("权限不足", "FORBIDDEN", k403Forbidden));
                    return;
                }
                const auto json = request->getJsonObject();
                if (!json || !(*json)["threshold"].isNumeric() || !(*json)["minutes"].isNumeric()) {
                    callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                    return;
                }
                const double threshold = (*json)["threshold"].asDouble();
                const double minutes = (*json)["minutes"].asDouble();
                if (threshold < 1.0 || threshold > 100.0 || minutes < 1.0 || minutes > 1440.0) {
                    callback(failure("参数超出范围", "VALIDATION_ERROR", k400BadRequest));
                    return;
                }
                database->setFatigueAlertConfig(threshold, minutes);
                callback(success(database->fatigueAlertConfig()));
                return;
            }
            callback(success(database->fatigueAlertConfig()));
        }, {Get, Post});

    app().registerHandler("/api/v1/mobile/dashboard",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            callback(success(database->mobileDashboard(user->id)));
        }, {Get});

    app().registerHandler("/api/v1/mobile/chat",
        [database, auth](const HttpRequestPtr& request, Callback&& callback) {
            const auto user = requireUser(request, auth, callback);
            if (!user) return;
            const auto json = request->getJsonObject();
            if (!json || !(*json)["message"].isString() || (*json)["message"].asString().empty()) {
                callback(failure("请求参数无效", "VALIDATION_ERROR", k400BadRequest));
                return;
            }
            Json::Value data(Json::objectValue);
            data["reply"] = ruleReply((*json)["message"].asString(), safeLatest(database, user->id));
            callback(success(data));
        }, {Post});
}

}  // namespace aura
