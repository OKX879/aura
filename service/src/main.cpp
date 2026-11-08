#include "aura/api.h"
#include "aura/auth.h"
#include "aura/database.h"
#ifdef AURA_HAS_LIVE_CORE
#include "aura/live.h"
#endif
#ifdef AURA_HAS_RADAR
#include "aura/radar.h"
#endif

#include <drogon/drogon.h>
#include <json/json.h>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::string environment(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

// 局域网发现：监听 UDP 37020，收到探测就回复设备信息
void startDiscoveryResponder(const std::string& serial,
                             const std::string& name,
                             const std::string& model,
                             const std::string& endpoint,
                             int httpPort) {
    constexpr int kDiscoveryPort = 37020;
    std::thread([serial, name, model, endpoint, httpPort] {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;
        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        int broadcast = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(kDiscoveryPort);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ::close(fd);
            return;
        }

        Json::Value payload(Json::objectValue);
        payload["proto"] = "aura-discovery";
        payload["serial"] = serial;
        payload["name"] = name;
        payload["model"] = model;
        payload["endpoint"] = endpoint;
        payload["port"] = httpPort;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string reply = Json::writeString(builder, payload);

        std::thread([fd, reply] {
            while (true) {
                sockaddr_in target{};
                target.sin_family = AF_INET;
                target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
                target.sin_port = htons(kDiscoveryPort);
                ::sendto(fd, reply.data(), reply.size(), 0,
                         reinterpret_cast<sockaddr*>(&target), sizeof(target));
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();

        char buffer[512];
        while (true) {
            sockaddr_in from{};
            socklen_t length = sizeof(from);
            const ssize_t received = ::recvfrom(fd, buffer, sizeof(buffer) - 1, 0,
                                                reinterpret_cast<sockaddr*>(&from), &length);
            if (received <= 0) continue;
            buffer[received] = '\0';
            if (std::strncmp(buffer, "AURA_PROBE", 10) != 0) continue;
            ::sendto(fd, reply.data(), reply.size(), 0,
                     reinterpret_cast<sockaddr*>(&from), length);
        }
    }).detach();
}

std::filesystem::path findDatabase() {
    const std::string configured = environment("AURA_DB_PATH");
    if (!configured.empty()) return std::filesystem::absolute(configured);
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "backend" / "database" / "aura.db",
        std::filesystem::current_path() / ".." / "backend" / "database" / "aura.db",
        std::filesystem::current_path() / ".." / ".." / "backend" / "database" / "aura.db"
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) return std::filesystem::weakly_canonical(candidate);
    }
    throw std::runtime_error("Cannot find backend/database/aura.db; set AURA_DB_PATH");
}

std::filesystem::path findUiRoot() {
    const std::string configured = environment("AURA_UI_ROOT");
    if (!configured.empty()) {
        const std::filesystem::path candidate = std::filesystem::absolute(configured);
        if (std::filesystem::exists(candidate / "index.html")) return candidate;
    }
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "cpp_desktop" / "build" / "Release" / "ui",
        std::filesystem::current_path() / ".." / ".." / "cpp_desktop" / "build" / "Release" / "ui",
        std::filesystem::current_path() / "ui"
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / "index.html")) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }
    return {};
}

int integerEnvironment(const char* name, int fallback) {
    try {
        return std::stoi(environment(name, std::to_string(fallback)));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main() {
    try {
        const std::filesystem::path databasePath = findDatabase();
        auto database = std::make_shared<aura::Database>(databasePath.string());
        auto auth = std::make_shared<aura::AuthService>(
            *database, aura::loadOrCreateSecret(databasePath.string())
        );

        std::shared_ptr<aura::LiveReplay> replay;
#ifdef AURA_HAS_LIVE_CORE
        const std::string cube = environment("AURA_LIVE_CUBE");
        if (!cube.empty()) {
            int channels = 8;
            int bins = 30;
            double fs = 100.0;
            const std::string meta = environment("AURA_LIVE_META");
            if (!meta.empty()) {
                std::ifstream input(meta, std::ios::binary);
                Json::Value json;
                Json::CharReaderBuilder builder;
                std::string errors;
                if (input && Json::parseFromStream(builder, input, &json, &errors)) {
                    if (json["channels"].isInt()) channels = json["channels"].asInt();
                    if (json["bins"].isInt()) bins = json["bins"].asInt();
                    if (json["fs"].isNumeric()) fs = json["fs"].asDouble();
                }
            }
            double speed = 1.0;
            try {
                speed = std::stod(environment("AURA_LIVE_SPEED", "1.0"));
            } catch (...) {
                speed = 1.0;
            }
            replay = std::make_shared<aura::LiveReplay>(
                std::filesystem::absolute(cube), channels, bins, fs, speed
            );
            if (replay->valid()) {
                const std::string radarFlag = environment("AURA_RADAR_ENABLE");
                const bool radarRequested = (radarFlag == "1" || radarFlag == "true");
                if (radarRequested) {
                    // 硬件雷达优先，避免两条管线同时运行
                    std::cout << "  live:     replay disabled (hardware radar enabled)\n";
                    replay.reset();
                } else {
                    replay->start();
                }
            } else {
                replay.reset();
            }
        }
#endif

        std::shared_ptr<aura::RadarLive> radar;
#ifdef AURA_HAS_RADAR
        const std::string radarEnabled = environment("AURA_RADAR_ENABLE");
        if (radarEnabled == "1" || radarEnabled == "true") {
            radar = std::make_shared<aura::RadarLive>(
                environment("AURA_RADAR_SPIDEV", "/dev/spidev8.0"),
                environment("AURA_RADAR_UART", "/dev/ttyACM0"),
                static_cast<unsigned int>(integerEnvironment("AURA_RADAR_SPI_HZ", 8000000))
            );
            if (radar->valid()) radar->start();
            else radar.reset();
        }
#endif

        std::function<Json::Value()> liveProducer;
        if (radar) {
            liveProducer = [radar] { return radar->snapshot(); };
        } else if (replay) {
            liveProducer = [replay] { return replay->snapshot(); };
        }
        aura::registerApiRoutes(database, auth, liveProducer);

        // 设备心跳：采样计数增长时每 5 秒刷新在线时间
        if (liveProducer) {
            const std::string serial = environment("AURA_DEVICE_SERIAL", "AURA-6240-001");
            const std::string deviceName = environment("AURA_DEVICE_NAME", "AURA 雷达（本机）");
            const std::string deviceModel = environment("AURA_DEVICE_MODEL", "MRS6240-P2512");
            const std::string deviceFirmware =
                environment("AURA_DEVICE_FIRMWARE", "r3_databox_v2.0.6");
            const std::string deviceEndpoint = environment("AURA_DEVICE_ENDPOINT");
            std::thread([database, liveProducer, serial, deviceName, deviceModel,
                         deviceFirmware, deviceEndpoint] {
                unsigned long long lastFrames = 0;
                bool haveFrames = false;
                auto lastAdvance = std::chrono::steady_clock::now();
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    unsigned long long frames = 0;
                    bool valid = false;
                    try {
                        const Json::Value snapshot = liveProducer();
                        const Json::Value& value = snapshot["frames"];
                        if (value.isUInt64() || value.isUInt() || value.isInt()) {
                            frames = value.asUInt64();
                            valid = true;
                        }
                    } catch (...) {
                        valid = false;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    if (valid && (!haveFrames || frames != lastFrames)) {
                        lastFrames = frames;
                        haveFrames = true;
                        lastAdvance = now;
                    }
                    const auto silent = std::chrono::duration_cast<std::chrono::seconds>(
                        now - lastAdvance).count();
                    if (haveFrames && silent < 15) {
                        database->heartbeatDevice(
                            serial, deviceName, deviceModel, deviceFirmware, deviceEndpoint);
                    }
                }
            }).detach();
            std::cout << "  device:   heartbeat serial=" << serial << '\n';

            // 体征落库：每 10 分钟写一条历史记录
            const std::string ownerEmail = environment("AURA_OWNER_EMAIL", "mobile@aura.local");
            // 落库间隔，可缩短便于验证
            const int vitalsIntervalMin = std::max(1, integerEnvironment("AURA_VITALS_INTERVAL_MIN", 10));
            std::thread([database, liveProducer, serial, ownerEmail, vitalsIntervalMin] {
                constexpr double kBaselineBpm = 68.0;
                while (true) {
                    std::this_thread::sleep_for(std::chrono::minutes(vitalsIntervalMin));
                    try {
                        const Json::Value snapshot = liveProducer();
                        const Json::Value& latest = snapshot["latest"];
                        const double hr = latest["hr"].asDouble();
                        if (hr <= 0.0) {
                            std::cerr << "vitals skip: no valid heart rate\n";
                            continue;
                        }
                        const double respHz = latest["resp_hz"].asDouble();
                        const double motion = latest["motion_level"].asDouble();
                        const double coherence = latest["coh"].asDouble();
                        const double confidence = latest["conf"].asDouble();
                        const double delta = hr - kBaselineBpm;
                        const double stress = std::clamp(
                            48.0 + delta * 1.8 + motion * 34.0 +
                                (1.0 - std::clamp(coherence, 0.0, 1.0)) * 18.0,
                            0.0, 100.0);
                        const double fatigue = std::clamp(
                            30.0 + delta * 1.1 + motion * 12.0, 0.0, 100.0);
                        const double focus = std::clamp(
                            86.0 - std::abs(delta) * 1.2 - motion * 46.0 -
                                (1.0 - std::clamp(confidence, 0.0, 1.0)) * 22.0,
                            0.0, 100.0);
                        const double respBpm = respHz > 0.0 ? respHz * 60.0 : 0.0;
                        database->insertVitalRecord(
                            serial, ownerEmail, hr, respBpm, stress, fatigue, focus);
                        std::cerr << "vitals recorded hr=" << hr
                                  << " resp=" << respBpm << '\n';
                    } catch (const std::exception& error) {
                        std::cerr << "vitals write failed: " << error.what() << '\n';
                    } catch (...) {
                        std::cerr << "vitals write failed: unknown error\n";
                    }
                }
            }).detach();
            std::cout << "  device:   vitals writer owner=" << ownerEmail << '\n';
        }

        const std::string bind = environment("AURA_BIND", "127.0.0.1");
        const int port = integerEnvironment("AURA_PORT", 3000);
        const std::filesystem::path uiRoot = findUiRoot();
        std::cout << "AURA Drogon service\n"
                  << "  database: " << databasePath.string() << '\n'
                  << "  listen:   http://" << bind << ':' << port << '\n'
                  << "  frontend: " << (uiRoot.empty() ? "not found" : uiRoot.string()) << "\n";

        // 配对码：App 添加设备时需输入的 6 位数字
        std::cout << "  pairing:  " << aura::ensurePairingCode()
                  << "  (文件 " << (databasePath.parent_path() / ".pair-code").string() << ")\n"
                  << std::flush;

        startDiscoveryResponder(
            environment("AURA_DEVICE_SERIAL", "AURA-6240-001"),
            environment("AURA_DEVICE_NAME", "AURA 雷达（本机）"),
            environment("AURA_DEVICE_MODEL", "MRS6240-P2512"),
            environment("AURA_DEVICE_ENDPOINT"),
            port);

        auto& application = drogon::app();
        if (!uiRoot.empty()) {
            application
                .setDocumentRoot(uiRoot.string())
                .setStaticFilesCacheTime(0)
                .setStaticFileHeaders({{"Cache-Control", "no-cache"}});
        }
        application.addListener(bind, static_cast<std::uint16_t>(port))
            .setThreadNum(2)
            .setClientMaxBodySize(4 * 1024 * 1024)
            .setLogLevel(trantor::Logger::kWarn)
            .run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AuraService startup failed: " << error.what() << '\n';
        return 1;
    }
}
