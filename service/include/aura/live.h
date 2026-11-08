#ifndef AURA_LIVE_H
#define AURA_LIVE_H

#include <json/json.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aura {

// 回放录制的复数立方体，经 C 管线输出实时数据
class LiveReplay {
public:
    LiveReplay(
        const std::filesystem::path& cubePath,
        int channels,
        int bins,
        double sampleRate,
        double speed
    );
    ~LiveReplay();

    LiveReplay(const LiveReplay&) = delete;
    LiveReplay& operator=(const LiveReplay&) = delete;

    bool valid() const;
    void start();
    void stop();
    Json::Value snapshot() const;

private:
    void run();

    std::filesystem::path cubePath_;
    int channels_ = 0;
    int bins_ = 0;
    double sampleRate_ = 100.0;
    double speed_ = 1.0;
    bool valid_ = false;

    std::vector<float> cube_;
    std::size_t frameFloats_ = 0;
    std::size_t frameCount_ = 0;
    std::size_t frameIndex_ = 0;
    unsigned long long emittedFrame_ = 0;

    void* pipeline_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    Json::Value latest_{Json::nullValue};
    unsigned long long startedAtMs_ = 0;
};

}  // namespace aura

#endif  // AURA_LIVE_H
