#include "aura/database.h"

#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace aura {
namespace {

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(statement_); }
    sqlite3_stmt* get() const noexcept { return statement_; }
private:
    sqlite3_stmt* statement_ = nullptr;
};

const std::unordered_map<std::string, std::string> kColumnNames = {
    {"user_id", "userId"}, {"device_id", "deviceId"}, {"room_id", "roomId"},
    {"employee_id", "employeeId"}, {"department_id", "departmentId"},
    {"serial_number", "serialNumber"}, {"firmware_version", "firmwareVersion"},
    {"last_seen_at", "lastSeenAt"}, {"created_at", "createdAt"},
    {"updated_at", "updatedAt"}, {"recorded_at", "recordedAt"},
    {"focus_score", "focusScore"}, {"stress_level", "stressLevel"},
    {"fatigue_level", "fatigueLevel"}, {"heart_rate", "heartRate"},
    {"respiration_rate", "respirationRate"}, {"nn_heart_rate", "nnHeartRate"},
    {"nn_respiration_rate", "nnRespirationRate"}, {"nn_focus_score", "nnFocusScore"},
    {"c_heart_rate", "v4HeartRate"}, {"ppi_heart_rate", "ppiHeartRate"},
    {"median_ppi_ms", "medianPpiMs"}, {"ppi_beat_count", "ppiBeatCount"},
    {"c_respiration_rate", "cRespirationRate"}, {"c_focus_score", "cFocusScore"},
    {"nn_quality", "nnQuality"}, {"gwo_vmd_heart_rate", "gwoVmdHeartRate"},
    {"gwo_vmd_quality", "gwoVmdQuality"}, {"gwo_vmd_k", "gwoVmdK"},
    {"gwo_vmd_alpha", "gwoVmdAlpha"}, {"gwo_vmd_processing_ms", "gwoVmdProcessingMs"},
    {"gwo_vmd_harmonic_risk", "gwoVmdHarmonicRisk"}, {"processing_mode", "processingMode"},
    {"research_diagnostics", "researchDiagnostics"}, {"is_resolved", "isResolved"},
    {"resolved_at", "resolvedAt"}, {"password_hash", "passwordHash"},
    {"endpoint_url", "endpointUrl"}, {"room_name", "roomName"},
    {"relax_score", "relaxScore"}
};

std::string outputName(const std::string& column) {
    const auto found = kColumnNames.find(column);
    return found == kColumnNames.end() ? column : found->second;
}

Json::Value parseJsonObject(const std::string& text) {
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream input(text);
    if (Json::parseFromStream(builder, input, &value, &errors) && value.isObject()) return value;
    return Json::nullValue;
}

Json::Value rowToJson(sqlite3_stmt* statement) {
    Json::Value row(Json::objectValue);
    const int count = sqlite3_column_count(statement);
    for (int i = 0; i < count; ++i) {
        const std::string rawName = sqlite3_column_name(statement, i);
        const std::string name = outputName(rawName);
        switch (sqlite3_column_type(statement, i)) {
            case SQLITE_INTEGER:
                if (rawName == "is_resolved") row[name] = sqlite3_column_int(statement, i) != 0;
                else row[name] = Json::Int64(sqlite3_column_int64(statement, i));
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(statement, i);
                break;
            case SQLITE_TEXT: {
                const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, i));
                const std::string text = value ? value : "";
                row[name] = rawName == "research_diagnostics" ? parseJsonObject(text) : Json::Value(text);
                break;
            }
            case SQLITE_NULL:
            default:
                row[name] = Json::nullValue;
                break;
        }
    }
    return row;
}

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void bindNullableJson(sqlite3_stmt* statement, int index, const Json::Value& value) {
    if (value.isNull()) sqlite3_bind_null(statement, index);
    else if (value.isString()) bindText(statement, index, value.asString());
    else if (value.isInt64() || value.isUInt64()) sqlite3_bind_int64(statement, index, value.asInt64());
    else if (value.isNumeric()) sqlite3_bind_double(statement, index, value.asDouble());
    else {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        bindText(statement, index, Json::writeString(writer, value));
    }
}

const Json::Value& field(const Json::Value& object, const char* name) {
    static const Json::Value nullValue;
    return object.isMember(name) ? object[name] : nullValue;
}

}  // namespace

Database::Database(const std::string& path) : path_(path) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open AURA database: " + message);
    }
    configure();
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::configure() {
    char* error = nullptr;
    constexpr const char* sql =
        "PRAGMA foreign_keys=ON;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA busy_timeout=5000;";
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "SQLite configuration failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
    // 旧库缺少设备地址列时补建，失败可忽略
    sqlite3_exec(db_, "ALTER TABLE devices ADD COLUMN endpoint_url TEXT",
                 nullptr, nullptr, nullptr);
    // 旧库缺少头像列时补建，失败可忽略
    sqlite3_exec(db_, "ALTER TABLE users ADD COLUMN avatar TEXT",
                 nullptr, nullptr, nullptr);
    // 提高忙等待时间，避免并发写库报错
    sqlite3_exec(db_, "PRAGMA busy_timeout=5000", nullptr, nullptr, nullptr);
}

bool Database::healthy(std::string* detail) {
    try {
        const auto row = queryOne("SELECT 1 AS ok");
        return row && (*row)["ok"].asInt() == 1;
    } catch (const std::exception& error) {
        if (detail) *detail = error.what();
        return false;
    }
}

Json::Value Database::queryArray(const std::string& sql, const Binder& binder) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement statement(db_, sql);
    if (binder) binder(statement.get());
    Json::Value rows(Json::arrayValue);
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) rows.append(rowToJson(statement.get()));
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return rows;
}

std::optional<Json::Value> Database::queryOne(const std::string& sql, const Binder& binder) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement statement(db_, sql);
    if (binder) binder(statement.get());
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) return rowToJson(statement.get());
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return std::nullopt;
}

int Database::execute(const std::string& sql, const Binder& binder) {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement statement(db_, sql);
    if (binder) binder(statement.get());
    const int result = sqlite3_step(statement.get());
    if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return sqlite3_changes(db_);
}

std::optional<Json::Value> Database::findUserByEmail(const std::string& email, bool includePassword) {
    const std::string columns = includePassword
        ? "id,name,employee_id,department_id,role,email,phone,status,avatar,password_hash,created_at,updated_at"
        : "id,name,employee_id,department_id,role,email,phone,status,avatar,created_at,updated_at";
    return queryOne("SELECT " + columns + " FROM users WHERE lower(email)=lower(?) LIMIT 1", [&](sqlite3_stmt* s) {
        bindText(s, 1, email);
    });
}

std::optional<Json::Value> Database::findUserById(const std::string& id) {
    return queryOne(
        "SELECT id,name,employee_id,department_id,role,email,phone,status,avatar,created_at,updated_at "
        "FROM users WHERE id=? LIMIT 1",
        [&](sqlite3_stmt* s) { bindText(s, 1, id); }
    );
}

Json::Value Database::devices() {
    // 在线状态按最近 30 秒心跳推导
    return queryArray(
        "SELECT d.id,d.name,d.serial_number,d.model,d.room_id,r.name AS room_name,"
        "CASE WHEN d.last_seen_at IS NOT NULL AND "
        "datetime(replace(replace(d.last_seen_at,'T',' '),'Z','')) >= datetime('now','-30 seconds') "
        "THEN 'online' ELSE 'offline' END AS status,"
        "d.firmware_version,d.last_seen_at,d.endpoint_url,d.created_at,d.updated_at "
        "FROM devices d LEFT JOIN rooms r ON r.id=d.room_id "
        "ORDER BY d.created_at DESC"
    );
}

std::optional<Json::Value> Database::findDeviceById(const std::string& id) {
    return queryOne(
        "SELECT d.id,d.name,d.serial_number,d.model,d.room_id,r.name AS room_name,"
        "CASE WHEN d.last_seen_at IS NOT NULL AND "
        "datetime(replace(replace(d.last_seen_at,'T',' '),'Z','')) >= datetime('now','-30 seconds') "
        "THEN 'online' ELSE 'offline' END AS status,"
        "d.firmware_version,d.last_seen_at,d.endpoint_url,d.created_at,d.updated_at "
        "FROM devices d LEFT JOIN rooms r ON r.id=d.room_id WHERE d.id=? LIMIT 1",
        [&](sqlite3_stmt* s) { bindText(s, 1, id); }
    );
}

bool Database::insertVitalRecord(
    const std::string& serialNumber,
    const std::string& ownerEmail,
    double heartRate,
    double respirationRate,
    double stress,
    double fatigue,
    double focus
) {
    const std::string id = drogon::utils::getUuid();
    return execute(
        "INSERT INTO focus_records("
        "id,user_id,device_id,room_id,focus_score,stress_level,fatigue_level,"
        "heart_rate,respiration_rate,recorded_at) "
        "VALUES(?,"
        "(SELECT id FROM users WHERE email=? LIMIT 1),"
        "(SELECT id FROM devices WHERE serial_number=? LIMIT 1),"
        "(SELECT room_id FROM devices WHERE serial_number=? LIMIT 1),"
        "?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",
        [&](sqlite3_stmt* s) {
            int i = 1;
            bindText(s, i++, id);
            bindText(s, i++, ownerEmail);
            bindText(s, i++, serialNumber);
            bindText(s, i++, serialNumber);
            sqlite3_bind_double(s, i++, focus);
            sqlite3_bind_double(s, i++, stress);
            sqlite3_bind_double(s, i++, fatigue);
            sqlite3_bind_double(s, i++, heartRate);
            sqlite3_bind_double(s, i++, respirationRate);
        }
    ) > 0;
}

bool Database::heartbeatDevice(
    const std::string& serialNumber,
    const std::string& name,
    const std::string& model,
    const std::string& firmwareVersion,
    const std::string& endpointUrl
) {
    // 只刷新时间与地址，保留用户设置的设备名
    const int updated = execute(
        "UPDATE devices SET last_seen_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
        "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
        "endpoint_url=COALESCE(NULLIF(?,''),endpoint_url) WHERE serial_number=?",
        [&](sqlite3_stmt* s) {
            bindText(s, 1, endpointUrl);
            bindText(s, 2, serialNumber);
        }
    );
    if (updated > 0) return true;

    Json::Value data(Json::objectValue);
    data["name"] = name;
    data["serialNumber"] = serialNumber;
    data["model"] = model;
    data["firmwareVersion"] = firmwareVersion;
    data["endpointUrl"] = endpointUrl;
    return createDevice(data).has_value();
}

std::optional<Json::Value> Database::createDevice(const Json::Value& data) {
    const std::string id = drogon::utils::getUuid();
    std::string serial = data["serialNumber"].asString();
    if (serial.empty()) {
        // 序列号为空时自动生成
        serial = "AURA-" + id.substr(0, 8);
    }
    const std::string model = data["model"].asString().empty()
        ? std::string("MRS6240-P2512") : data["model"].asString();
    const std::string firmware = data["firmwareVersion"].asString().empty()
        ? std::string("1.0.0") : data["firmwareVersion"].asString();
    const std::string status = data["status"].asString().empty()
        ? std::string("online") : data["status"].asString();
    int changed = 0;
    try {
        changed = execute(
            "INSERT INTO devices(id,name,serial_number,model,room_id,status,firmware_version,"
            "last_seen_at,endpoint_url,created_at,updated_at) "
            "VALUES(?,?,?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'),?,"
            "strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now'))",
            [&](sqlite3_stmt* s) {
                int i = 1;
                bindText(s, i++, id);
                bindText(s, i++, data["name"].asString());
                bindText(s, i++, serial);
                bindText(s, i++, model);
                bindNullableJson(s, i++, data["roomId"]);
                bindText(s, i++, status);
                bindText(s, i++, firmware);
                bindNullableJson(s, i++, data["endpointUrl"]);
            }
        );
    } catch (const std::exception&) {
        // 序列号冲突按输入错误处理，由 API 返回 409
        return std::nullopt;
    }
    return changed > 0 ? findDeviceById(id) : std::nullopt;
}

bool Database::deleteDevice(const std::string& id) {
    return execute("DELETE FROM devices WHERE id=?", [&](sqlite3_stmt* s) {
        bindText(s, 1, id);
    }) > 0;
}

Json::Value Database::alerts(const std::string& userId, bool isAdmin, bool unresolvedOnly, int limit) {
    std::string sql =
        "SELECT id,type,severity,user_id,device_id,room_id,message,is_resolved,resolved_at,created_at FROM alerts WHERE 1=1";
    if (!isAdmin) sql += " AND user_id=?";
    if (unresolvedOnly) sql += " AND is_resolved=0";
    sql += " ORDER BY created_at DESC LIMIT ?";
    return queryArray(sql, [&](sqlite3_stmt* statement) {
        int index = 1;
        if (!isAdmin) bindText(statement, index++, userId);
        sqlite3_bind_int(statement, index, std::clamp(limit, 1, 1000));
    });
}

Json::Value Database::latestRecords(const std::string& userId, bool isAdmin, int limit) {
    std::string sql = "SELECT * FROM focus_records";
    if (!isAdmin) sql += " WHERE user_id=?";
    sql += " ORDER BY recorded_at DESC LIMIT ?";
    return queryArray(sql, [&](sqlite3_stmt* statement) {
        int index = 1;
        if (!isAdmin) bindText(statement, index++, userId);
        sqlite3_bind_int(statement, index, std::clamp(limit, 1, 1000));
    });
}

Json::Value Database::mobileDashboard(const std::string& userId) {
    const auto user = findUserById(userId);
    if (!user) throw std::runtime_error("USER_NOT_FOUND");

    Json::Value dashboard(Json::objectValue);
    dashboard["user"] = *user;
    const Json::Value recent = queryArray(
        "SELECT * FROM focus_records WHERE user_id=? ORDER BY recorded_at DESC LIMIT 50",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    dashboard["latestRecord"] = recent.empty() ? Json::nullValue : recent[0];

    const auto summary = queryOne(
        "SELECT COALESCE(AVG(focus_score),0) averageFocus,COALESCE(MAX(focus_score),0) maxFocus,"
        "COALESCE(MIN(focus_score),0) minFocus,COUNT(*) recordCount,AVG(heart_rate) averageHeartRate,"
        "AVG(respiration_rate) averageRespirationRate,AVG(stress_level) averageStress,"
        "AVG(fatigue_level) averageFatigue,MAX(heart_rate) maxHeartRate,MIN(heart_rate) minHeartRate "
        "FROM focus_records WHERE user_id=? AND date(recorded_at,'localtime')=date('now','localtime')",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    dashboard["todaySummary"] = summary.value_or(Json::Value(Json::objectValue));

    // 趋势只取最近 24 小时数据
    Json::Value vitals = queryArray(
        "SELECT recorded_at AS time,focus_score AS focusScore,heart_rate AS heartRate,"
        "respiration_rate AS respirationRate,stress_level AS stressLevel,"
        "fatigue_level AS fatigueLevel "
        "FROM focus_records WHERE user_id=? AND "
        "datetime(replace(replace(recorded_at,'T',' '),'Z','')) >= datetime('now','-24 hours') "
        "ORDER BY recorded_at ASC LIMIT 240",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    dashboard["vitalsHistory"] = vitals;

    Json::Value trend = queryArray(
        "SELECT strftime('%Y-%m-%dT%H:00:00.000Z',recorded_at) time,"
        "ROUND(AVG(focus_score),1) averageFocus,COUNT(DISTINCT user_id) peopleCount "
        "FROM focus_records WHERE user_id=? AND recorded_at>=datetime('now','-12 hours') "
        "GROUP BY time ORDER BY time ASC",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    dashboard["recentTrend"] = trend;

    Json::Value userAlerts = alerts(userId, false, false, 20);
    int unresolved = 0;
    for (const auto& alert : userAlerts) if (!alert["isResolved"].asBool()) ++unresolved;
    Json::Value alertSummary(Json::objectValue);
    alertSummary["unresolvedCount"] = unresolved;
    Json::Value recentAlerts(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < std::min<Json::ArrayIndex>(5, userAlerts.size()); ++i) recentAlerts.append(userAlerts[i]);
    alertSummary["recent"] = recentAlerts;
    dashboard["alertSummary"] = alertSummary;

    Json::Value allDevices = devices();
    int online = 0;
    for (const auto& device : allDevices) if (device["status"].asString() == "online") ++online;
    Json::Value deviceSummary(Json::objectValue);
    deviceSummary["online"] = online;
    deviceSummary["total"] = static_cast<int>(allDevices.size());
    Json::Value recentDevices(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < std::min<Json::ArrayIndex>(5, allDevices.size()); ++i) recentDevices.append(allDevices[i]);
    deviceSummary["recent"] = recentDevices;
    dashboard["deviceSummary"] = deviceSummary;
    return dashboard;
}

int Database::ingestRecords(
    const Json::Value& records,
    const std::string& authenticatedUserId,
    bool isAdmin,
    std::string& error
) {
    if (!records.isArray() || records.empty() || records.size() > 500) {
        error = "records must contain 1 to 500 items";
        return -1;
    }

    static const std::vector<const char*> fields = {
        "deviceId", "roomId", "focusScore", "stressLevel", "fatigueLevel", "heartRate", "respirationRate",
        "nnHeartRate", "nnRespirationRate", "nnFocusScore", "v4HeartRate", "ppiHeartRate", "medianPpiMs",
        "ppiBeatCount", "cRespirationRate", "cFocusScore", "nnQuality", "gwoVmdHeartRate", "gwoVmdQuality",
        "gwoVmdK", "gwoVmdAlpha", "gwoVmdProcessingMs", "gwoVmdHarmonicRisk", "processingMode",
        "relaxScore", "researchDiagnostics", "recordedAt"
    };
    constexpr const char* sql =
        "INSERT OR IGNORE INTO focus_records (id,user_id,device_id,room_id,focus_score,stress_level,fatigue_level,"
        "heart_rate,respiration_rate,nn_heart_rate,nn_respiration_rate,nn_focus_score,c_heart_rate,ppi_heart_rate,"
        "median_ppi_ms,ppi_beat_count,c_respiration_rate,c_focus_score,nn_quality,gwo_vmd_heart_rate,gwo_vmd_quality,"
        "gwo_vmd_k,gwo_vmd_alpha,gwo_vmd_processing_ms,gwo_vmd_harmonic_risk,processing_mode,relax_score,"
        "research_diagnostics,recorded_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,COALESCE(?,strftime('%Y-%m-%dT%H:%M:%fZ','now')))";

    std::lock_guard<std::mutex> lock(mutex_);
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return -1;
    }
    int inserted = 0;
    try {
        Statement statement(db_, sql);
        for (const auto& record : records) {
            if (!record.isObject() || !record["deviceId"].isString()) {
                throw std::runtime_error("deviceId is required");
            }
            // 指标允许为空，缺证据时不拒收整条记录
            if (record["focusScore"].isNumeric()) {
                const double focus = record["focusScore"].asDouble();
                if (focus < 0.0 || focus > 100.0) throw std::runtime_error("focusScore must be between 0 and 100");
            }
            sqlite3_reset(statement.get());
            sqlite3_clear_bindings(statement.get());
            const std::string id = record.get("recordId", "").asString().empty()
                ? drogon::utils::getUuid() : record["recordId"].asString();
            const std::string requestedUser = record.get("userId", "").asString();
            const std::string userId = isAdmin && !requestedUser.empty() ? requestedUser : authenticatedUserId;
            bindText(statement.get(), 1, id);
            bindText(statement.get(), 2, userId);
            int index = 3;
            for (const char* name : fields) bindNullableJson(statement.get(), index++, field(record, name));
            const int result = sqlite3_step(statement.get());
            if (result != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
            inserted += sqlite3_changes(db_);
        }
        if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
        return inserted;
    } catch (const std::exception& exception) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        error = exception.what();
        return -1;
    }
}

void Database::createFatigueAlerts() {
    constexpr double kDefaultThreshold = 60.0;
    constexpr double kDefaultMinutes = 30.0;
    constexpr double kMaxGapSeconds = 15.0;      // 相邻记录超过 15 秒视为断档

    // 读取可配置的阈值与时长
    double threshold = kDefaultThreshold;
    double minutes = kDefaultMinutes;
    const Json::Value config = fatigueAlertConfig();
    if (config["threshold"].isNumeric()) threshold = config["threshold"].asDouble();
    if (config["minutes"].isNumeric()) minutes = config["minutes"].asDouble();
    const double kMinSeconds = std::max(1.0, minutes * 60.0);
    const auto asSec = [](double v) { return std::to_string(static_cast<long long>(std::round(v))); };

    // 只扫描近期出现过高疲劳的用户
    const Json::Value candidates = queryArray(
        "SELECT DISTINCT user_id AS uid FROM focus_records "
        "WHERE user_id IS NOT NULL AND fatigue_level >= " + asSec(threshold) + " "
        "AND recorded_at >= datetime('now','-" + asSec(kMinSeconds) + " seconds')"
    );
    for (const auto& candidate : candidates) {
        if (!candidate["uid"].isString() || candidate["uid"].asString().empty()) continue;
        const std::string userId = candidate["uid"].asString();

        // 已有未解决告警则跳过
        const bool hasPending = queryOne(
            "SELECT 1 AS ok FROM alerts WHERE type='fatigue' AND is_resolved=0 "
            "AND (user_id IS NULL OR user_id=?) LIMIT 1",
            [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
        ).has_value();
        if (hasPending) continue;

        // 按真实时间差累加连续高疲劳时长
        const Json::Value rows = queryArray(
            "SELECT julianday(recorded_at) AS ts FROM focus_records "
            "WHERE user_id=? AND fatigue_level >= " + asSec(threshold) + " "
            "AND recorded_at >= datetime('now','-" + asSec(kMinSeconds + 60.0) + " seconds') "
            "ORDER BY recorded_at DESC LIMIT 10000",
            [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
        );
        if (rows.empty()) continue;

        // 从最新记录往回累加，遇到断档即停止
        double spanSeconds = 0.0;
        double prevTs = rows[0]["ts"].asDouble();
        bool first = true;
        for (const auto& row : rows) {
            const double currTs = row["ts"].asDouble();
            if (!first) {
                const double gap = prevTs - currTs;
                if (gap > kMaxGapSeconds) break;  // 断档，连续段结束
                spanSeconds += gap;
            }
            first = false;
            prevTs = currTs;
        }
        // 连续时长达到阈值才生成告警
        if (spanSeconds < kMinSeconds) continue;

        // 生成未解决告警
        const std::string message =
            "已连续疲劳超过 " + std::to_string(static_cast<long long>(std::round(minutes)))
            + " 分钟，建议起身活动、休息放松，避免过度疲劳。";
        execute(
            "INSERT INTO alerts(id,type,severity,user_id,message,is_resolved,created_at) "
            "VALUES(?,?,?,?,?,0,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",
            [&](sqlite3_stmt* s) {
                bindText(s, 1, drogon::utils::getUuid());
                bindText(s, 2, "fatigue");
                bindText(s, 3, "medium");
                bindText(s, 4, userId);
                bindText(s, 5, message);
            }
        );
    }
}

Json::Value Database::fatigueAlertConfig() {
    constexpr double kDefaultThreshold = 60.0;
    constexpr double kDefaultMinutes = 30.0;
    // 确保 settings 表存在
    execute("CREATE TABLE IF NOT EXISTS settings ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT)");

    Json::Value result(Json::objectValue);
    result["threshold"] = kDefaultThreshold;
    result["minutes"] = kDefaultMinutes;

    const Json::Value rows = queryArray(
        "SELECT key AS k, value AS v FROM settings WHERE key IN ('fatigue_threshold','fatigue_minutes')"
    );
    for (const auto& row : rows) {
        if (!row["k"].isString() || !row["v"].isString()) continue;
        const std::string& key = row["k"].asString();
        try {
            const double value = std::stod(row["v"].asString());
            if (key == "fatigue_threshold" && value >= 1.0 && value <= 100.0) result["threshold"] = value;
            else if (key == "fatigue_minutes" && value >= 1.0 && value <= 1440.0) result["minutes"] = value;
        } catch (...) {
            // 非法数值按默认值处理
        }
    }
    return result;
}

void Database::setFatigueAlertConfig(double threshold, double minutes) {
    execute("CREATE TABLE IF NOT EXISTS settings ("
            "key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT)");
    const std::string updatedAt = std::string("strftime('%Y-%m-%dT%H:%M:%fZ','now')");
    const auto upsert = [&](const std::string& key, double value) {
        execute(
            "INSERT INTO settings(key,value,updated_at) VALUES(?,?," + updatedAt + ") "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value,updated_at=excluded.updated_at",
            [&](sqlite3_stmt* s) {
                bindText(s, 1, key);
                bindText(s, 2, std::to_string(value));
            }
        );
    };
    upsert("fatigue_threshold", threshold);
    upsert("fatigue_minutes", minutes);
}

// ---- 用户管理 ----

Json::Value Database::users() {
    return queryArray(
        "SELECT id,name,employee_id,department_id,role,email,phone,status,created_at,updated_at "
        "FROM users ORDER BY created_at ASC"
    );
}

int Database::userCount() {
    const auto row = queryOne("SELECT COUNT(*) AS count FROM users");
    return row ? (*row)["count"].asInt() : 0;
}

bool Database::userEmailExists(const std::string& email) {
    return queryOne(
        "SELECT 1 AS ok FROM users WHERE lower(email)=lower(?) LIMIT 1",
        [&](sqlite3_stmt* s) { bindText(s, 1, email); }
    ).has_value();
}

bool Database::userEmployeeIdExists(const std::string& employeeId) {
    return queryOne(
        "SELECT 1 AS ok FROM users WHERE employee_id=? LIMIT 1",
        [&](sqlite3_stmt* s) { bindText(s, 1, employeeId); }
    ).has_value();
}

std::optional<Json::Value> Database::createUser(const Json::Value& data) {
    const std::string id = drogon::utils::getUuid();
    const int changed = execute(
        "INSERT INTO users(id,name,employee_id,department_id,role,email,phone,status,"
        "created_at,updated_at,password_hash) "
        "VALUES(?,?,?,?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
        "strftime('%Y-%m-%dT%H:%M:%fZ','now'),?)",
        [&](sqlite3_stmt* s) {
            int i = 1;
            bindText(s, i++, id);
            bindText(s, i++, data["name"].asString());
            bindText(s, i++, data["employeeId"].asString());
            bindNullableJson(s, i++, data["departmentId"]);
            bindNullableJson(s, i++, data["role"].asString().empty() ? Json::Value("employee") : data["role"]);
            bindText(s, i++, data["email"].asString());
            bindNullableJson(s, i++, data["phone"]);
            bindNullableJson(s, i++, data["status"].asString().empty() ? Json::Value("active") : data["status"]);
            bindNullableJson(s, i++, data["passwordHash"]);
        }
    );
    return changed > 0 ? findUserById(id) : std::nullopt;
}

std::optional<Json::Value> Database::updateUser(const std::string& id, const Json::Value& data) {
    static const std::vector<std::pair<std::string, std::string>> updateable = {
        {"name", "name"}, {"employeeId", "employee_id"}, {"departmentId", "department_id"},
        {"role", "role"}, {"email", "email"}, {"phone", "phone"}, {"status", "status"},
        {"avatar", "avatar"}, {"passwordHash", "password_hash"}
    };
    std::string sql = "UPDATE users SET ";
    std::vector<std::string> sets;
    std::vector<std::string> keys;
    for (const auto& [jsonKey, column] : updateable) {
        if (!data.isMember(jsonKey)) continue;
        sets.push_back(column + "=?");
        keys.push_back(jsonKey);
    }
    if (sets.empty()) return findUserById(id);
    for (std::size_t i = 0; i < sets.size(); ++i) {
        if (i > 0) sql += ",";
        sql += sets[i];
    }
    sql += ",updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?";
    execute(sql, [&](sqlite3_stmt* s) {
        int i = 1;
        for (const auto& key : keys) bindNullableJson(s, i++, data[key]);
        bindText(s, i++, id);
    });
    return findUserById(id);
}

bool Database::deleteUser(const std::string& id) {
    return execute("DELETE FROM users WHERE id=?", [&](sqlite3_stmt* s) { bindText(s, 1, id); }) > 0;
}

bool Database::updateUserPassword(const std::string& id, const std::string& passwordHash) {
    return execute(
        "UPDATE users SET password_hash=?,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?",
        [&](sqlite3_stmt* s) {
            bindText(s, 1, passwordHash);
            bindText(s, 2, id);
        }
    ) > 0;
}

// ---- 告警 ----

std::optional<Json::Value> Database::findAlertById(const std::string& id) {
    return queryOne(
        "SELECT id,type,severity,user_id,device_id,room_id,message,is_resolved,resolved_at,created_at "
        "FROM alerts WHERE id=? LIMIT 1",
        [&](sqlite3_stmt* s) { bindText(s, 1, id); }
    );
}

std::optional<Json::Value> Database::resolveAlert(const std::string& id) {
    execute(
        "UPDATE alerts SET is_resolved=1,resolved_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?",
        [&](sqlite3_stmt* s) { bindText(s, 1, id); }
    );
    return findAlertById(id);
}

// ---- 数值辅助 ----

namespace {
Json::Value roundNumber(const Json::Value& value, int decimals = 1) {
    if (!value.isNumeric()) return Json::Value(0.0);
    const double factor = std::pow(10.0, decimals);
    return Json::Value(std::round(value.asDouble() * factor) / factor);
}
}  // namespace

// ---- 焦点数据 ----

Json::Value Database::personalStats(const std::string& userId, const std::string& range) {
    const std::string normalized = range == "week" ? "week" : (range == "month" ? "month" : "day");
    std::string sinceHours = "24 hours";
    // day 范围按 10 分钟分桶
    std::string bucketExpr = "strftime('%H:%M',datetime((strftime('%s',recorded_at)/600)*600,'unixepoch'))";
    int limit = 240;
    if (normalized == "week") { sinceHours = "7 days"; bucketExpr = "strftime('%Y-%m-%d',recorded_at)"; limit = 400; }
    else if (normalized == "month") { sinceHours = "30 days"; bucketExpr = "strftime('%Y-%m-%d',recorded_at)"; limit = 600; }

    const Json::Value rows = queryArray(
        "SELECT " + bucketExpr + " AS bucket, "
        "AVG(heart_rate) avg_hr, AVG(respiration_rate) avg_rr, AVG(focus_score) avg_focus, "
        "AVG(fatigue_level) avg_fatigue, AVG(COALESCE(relax_score,0)) avg_relax, COUNT(*) record_count "
        "FROM focus_records WHERE recorded_at >= datetime('now','-" + sinceHours + "') "
        "AND user_id=? AND heart_rate > 0 AND heart_rate < 250 GROUP BY bucket ORDER BY bucket ASC",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );

    Json::Value result(Json::objectValue);
    result["range"] = normalized;
    Json::Value labels(Json::arrayValue), heartRate(Json::arrayValue), respiration(Json::arrayValue),
        focus(Json::arrayValue), fatigue(Json::arrayValue), relax(Json::arrayValue);
    double sumHr = 0, sumRr = 0, sumFocus = 0, sumFatigue = 0, sumRelax = 0;
    int totalRecords = 0;
    const double count = rows.size();
    for (const auto& row : rows) {
        labels.append(row["bucket"]);
        heartRate.append(row["avg_hr"].isNumeric() ? roundNumber(row["avg_hr"]) : Json::Value(Json::nullValue));
        respiration.append(row["avg_rr"].isNumeric() ? roundNumber(row["avg_rr"]) : Json::Value(Json::nullValue));
        focus.append(row["avg_focus"].isNumeric() ? roundNumber(row["avg_focus"]) : Json::Value(Json::nullValue));
        fatigue.append(row["avg_fatigue"].isNumeric() ? roundNumber(row["avg_fatigue"]) : Json::Value(Json::nullValue));
        relax.append(row["avg_relax"].isNumeric() ? roundNumber(row["avg_relax"]) : Json::Value(Json::nullValue));
        if (row["avg_hr"].isNumeric()) sumHr += row["avg_hr"].asDouble();
        if (row["avg_rr"].isNumeric()) sumRr += row["avg_rr"].asDouble();
        if (row["avg_focus"].isNumeric()) sumFocus += row["avg_focus"].asDouble();
        if (row["avg_fatigue"].isNumeric()) sumFatigue += row["avg_fatigue"].asDouble();
        if (row["avg_relax"].isNumeric()) sumRelax += row["avg_relax"].asDouble();
        totalRecords += row["record_count"].isNumeric() ? row["record_count"].asInt() : 0;
    }
    result["labels"] = labels;
    result["heartRate"] = heartRate;
    result["respiration"] = respiration;
    result["focus"] = focus;
    result["fatigue"] = fatigue;
    result["relax"] = relax;
    Json::Value summary(Json::objectValue);
    summary["avgHr"] = count > 0 ? roundNumber(Json::Value(sumHr / count)) : 0.0;
    summary["avgRr"] = count > 0 ? roundNumber(Json::Value(sumRr / count)) : 0.0;
    summary["avgFocus"] = count > 0 ? roundNumber(Json::Value(sumFocus / count)) : 0.0;
    summary["avgFatigue"] = count > 0 ? roundNumber(Json::Value(sumFatigue / count)) : 0.0;
    summary["avgRelax"] = count > 0 ? roundNumber(Json::Value(sumRelax / count)) : 0.0;
    summary["totalRecords"] = totalRecords;
    result["summary"] = summary;

    // Bin 轨迹
    const Json::Value bins = queryArray(
        "SELECT recorded_at, json_extract(research_diagnostics,'$.targetBin') target_bin FROM ("
        "SELECT recorded_at, research_diagnostics FROM focus_records "
        "WHERE recorded_at >= datetime('now','-"+ sinceHours +"') AND user_id=?"
        " AND research_diagnostics IS NOT NULL ORDER BY recorded_at DESC LIMIT " + std::to_string(limit) +
        ") ORDER BY recorded_at ASC",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    Json::Value trajectory(Json::arrayValue);
    for (const auto& row : bins) {
        Json::Value point(Json::objectValue);
        point["time"] = row["recorded_at"];
        const bool hasBin = row["target_bin"].isNumeric();
        point["bin"] = hasBin ? row["target_bin"] : Json::Value(Json::nullValue);
        point["distanceCm"] = hasBin ? roundNumber(Json::Value(row["target_bin"].asDouble() * 2.4)) : Json::Value(Json::nullValue);
        trajectory.append(point);
    }
    result["binTrajectory"] = trajectory;
    return result;
}

Json::Value Database::eventStats(const std::string& userId, const std::string& range) {
    const std::string normalized = range == "week" ? "week" : (range == "month" ? "month" : "day");
    std::string sinceHours = "24 hours";
    if (normalized == "week") sinceHours = "7 days";
    else if (normalized == "month") sinceHours = "30 days";

    struct EventRow { std::string label; int count; };
    const Json::Value rows = queryArray(
        "SELECT json_extract(research_diagnostics,'$.eventLabel') event_label, COUNT(*) cnt "
        "FROM focus_records WHERE recorded_at >= datetime('now','-" + sinceHours + "') AND user_id=? "
        "AND research_diagnostics IS NOT NULL GROUP BY event_label ORDER BY cnt DESC",
        [&](sqlite3_stmt* s) { bindText(s, 1, userId); }
    );
    std::unordered_map<std::string, int> countByLabel;
    for (const auto& row : rows) countByLabel[row["cnt"].isNumeric() ? row["event_label"].asString() : ""] = row["cnt"].asInt();

    static const std::vector<std::pair<std::string, std::string>> eventTypes = {
        {"SITTING_QUIET", "静坐"}, {"BREATH_HOLD", "屏气"}, {"ABSENT", "离开"},
        {"LATERAL_MOTION", "左右移动"}, {"FORE_AFT_MOTION", "前后移动"},
        {"INTENSE_MOTION", "剧烈运动"}, {"DEEP_BREATH", "深呼吸"}
    };
    Json::Value stats(Json::arrayValue);
    for (const auto& [key, label] : eventTypes) {
        Json::Value item(Json::objectValue);
        item["key"] = key;
        item["label"] = label;
        item["count"] = countByLabel[key];
        item["duration_s"] = countByLabel[key] * 3;
        stats.append(item);
    }
    Json::Value result(Json::objectValue);
    result["range"] = normalized;
    result["events"] = stats;
    return result;
}

}  // namespace aura
