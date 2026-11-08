#pragma once

#include <json/json.h>
#include <sqlite3.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace aura {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    const std::string& path() const noexcept { return path_; }
    bool healthy(std::string* detail = nullptr);

    std::optional<Json::Value> findUserByEmail(const std::string& email, bool includePassword = false);
    std::optional<Json::Value> findUserById(const std::string& id);
    Json::Value devices();
    std::optional<Json::Value> findDeviceById(const std::string& id);
    // data 可选字段：名称、序列号、型号、房间、固件、地址、状态
    std::optional<Json::Value> createDevice(const Json::Value& data);
    bool deleteDevice(const std::string& id);
    // 设备心跳：刷新在线时间，不存在时自动补建
    bool heartbeatDevice(
        const std::string& serialNumber,
        const std::string& name,
        const std::string& model,
        const std::string& firmwareVersion,
        const std::string& endpointUrl);
    // 体征入库，按序列号与邮箱解析外键
    bool insertVitalRecord(
        const std::string& serialNumber,
        const std::string& ownerEmail,
        double heartRate,
        double respirationRate,
        double stress,
        double fatigue,
        double focus);
    Json::Value alerts(const std::string& userId, bool isAdmin, bool unresolvedOnly, int limit = 1000);
    Json::Value latestRecords(const std::string& userId, bool isAdmin, int limit);
    Json::Value mobileDashboard(const std::string& userId);
    int ingestRecords(
        const Json::Value& records,
        const std::string& authenticatedUserId,
        bool isAdmin,
        std::string& error
    );
    // 疲劳预警：连续高疲劳超时则生成未解决告警
    void createFatigueAlerts();
    // 疲劳预警配置（阈值与时长）
    Json::Value fatigueAlertConfig();
    void setFatigueAlertConfig(double threshold, double minutes);

    // ---- 用户管理 ----
    Json::Value users();
    int userCount();
    bool userEmailExists(const std::string& email);
    bool userEmployeeIdExists(const std::string& employeeId);
    // data 可选字段：姓名、工号、部门、角色、邮箱、电话、状态、密码
    std::optional<Json::Value> createUser(const Json::Value& data);
    std::optional<Json::Value> updateUser(const std::string& id, const Json::Value& data);
    bool deleteUser(const std::string& id);
    bool updateUserPassword(const std::string& id, const std::string& passwordHash);

    // ---- 告警 ----
    std::optional<Json::Value> findAlertById(const std::string& id);
    std::optional<Json::Value> resolveAlert(const std::string& id);

    // ---- 焦点数据 ----
    Json::Value personalStats(const std::string& userId, const std::string& range);
    Json::Value eventStats(const std::string& userId, const std::string& range);

private:
    using Binder = std::function<void(sqlite3_stmt*)>;

    Json::Value queryArray(const std::string& sql, const Binder& binder = {});
    std::optional<Json::Value> queryOne(const std::string& sql, const Binder& binder = {});
    int execute(const std::string& sql, const Binder& binder = {});
    void configure();

    sqlite3* db_ = nullptr;
    std::string path_;
    std::mutex mutex_;
};

}  // namespace aura
