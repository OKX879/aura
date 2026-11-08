#ifndef AURA_RADAR_H
#define AURA_RADAR_H

#include <json/json.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct rs6240_capture;  // 前置声明，避免头文件暴露 C 内部结构

namespace aura {

// 通过串口配置雷达，经 spidev 采集 ROI 帧送入管线
class RadarLive {
public:
    RadarLive(
        const std::string& spidevPath,
        const std::string& uartPath,
        unsigned int spiHz
    );
    ~RadarLive();

    RadarLive(const RadarLive&) = delete;
    RadarLive& operator=(const RadarLive&) = delete;

    bool valid() const;
    void start();
    void stop();
    Json::Value snapshot() const;

private:
    // 一帧原始采集数据，交给管线线程处理
    struct PendingFrame {
        std::vector<float> re;
        std::vector<float> im;
        unsigned int channels = 0;
        unsigned int bins = 0;
        unsigned int frameIndex = 0;
        unsigned int overrun = 0;
        unsigned int reads = 0;
        unsigned int msgLen = 0;
        unsigned int msgId = 0;
    };

    void run();
    bool armUart(bool hardReset = false);
    bool disarmUart();
    bool doWake();
    void enqueueFrame();
    void processWorker();
    void processFrame(const PendingFrame& frame);

    std::string spidevPath_;
    std::string uartPath_;
    unsigned int spiHz_ = 8000000u;

    void* device_ = nullptr;
    rs6240_capture* capture_ = nullptr;
    void* pipeline_ = nullptr;
    bool valid_ = false;

    std::atomic<bool> running_{false};
    std::thread worker_;
    // 管线线程：消费采集线程投递的帧
    std::thread procWorker_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<PendingFrame> queue_;
    bool procStop_ = false;

    mutable std::mutex mutex_;
    Json::Value latest_{Json::nullValue};

    unsigned long long startedAtMs_ = 0;
    unsigned long long framesTotal_ = 0;
    unsigned long long lostFrames_ = 0;
    unsigned long long errors_ = 0;
    unsigned int lastFrameIndex_ = 0;
    bool haveLastFrame_ = false;
    unsigned int channels_ = 0;
    unsigned int bins_ = 0;
    // 实测稳定推送帧率，用于管线时间基准
    double sampleRate_ = 90.0;
};

}  // namespace aura

#endif  // AURA_RADAR_H
