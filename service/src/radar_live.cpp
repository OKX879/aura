#include "aura/radar.h"

#include "rs6240_capture.h"
#include "rs6240_spidev.h"

#include <aura_pipeline.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

// 雷达帧周期，单位毫秒
constexpr int kFramePeriodMs = 10;

// 数据就绪 GPIO 默认引脚，负数表示禁用
constexpr int kDefaultReadyGpio = 510;

int readGpioLevel(int fd) {
    char value = 0;
    if (lseek(fd, 0, SEEK_SET) != 0) return -1;
    if (read(fd, &value, 1) != 1) return -1;
    return value == '1' ? 1 : 0;
}

int openReadyGpio() {
    const char* env = std::getenv("AURA_RADAR_READY_GPIO");
    int pin = (env && *env) ? std::atoi(env) : kDefaultReadyGpio;
    if (pin < 0) return -1;
    const std::string base = "/sys/class/gpio/gpio" + std::to_string(pin);
    if (access(base.c_str(), F_OK) != 0) {
        int exportFd = open("/sys/class/gpio/export", O_WRONLY);
        if (exportFd >= 0) {
            const std::string line = std::to_string(pin);
            if (write(exportFd, line.data(), line.size()) < 0) {
                std::cerr << "RadarLive: gpio export failed\n";
            }
            close(exportFd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    int dirFd = open((base + "/direction").c_str(), O_WRONLY);
    if (dirFd >= 0) {
        if (write(dirFd, "in", 2) < 0) {
            std::cerr << "RadarLive: gpio direction failed\n";
        }
        close(dirFd);
    }
    // 用边沿中断代替轮询，避免占用 USB 带宽
    int edgeFd = open((base + "/edge").c_str(), O_WRONLY);
    if (edgeFd >= 0) {
        if (write(edgeFd, "rising", 6) < 0) {
            std::cerr << "RadarLive: gpio edge failed\n";
        }
        close(edgeFd);
    }
    int fd = open((base + "/value").c_str(), O_RDONLY);
    if (fd >= 0) {
        // 清除可能残留的就绪事件
        char buf[8];
        (void)lseek(fd, 0, SEEK_SET);
        if (read(fd, buf, sizeof(buf)) < 0) {
            std::cerr << "RadarLive: gpio clear failed\n";
        }
    }
    return fd;
}

bool waitFrameReady(int fd, int timeoutMs) {
    if (fd < 0) return false;
    // 已有数据排队时先看电平，避免漏掉本次就绪
    if (readGpioLevel(fd) == 1) return true;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLPRI | POLLERR;
    pfd.revents = 0;
    const int rc = poll(&pfd, 1, timeoutMs);
    if (rc > 0) {
        char buf[8];
        (void)lseek(fd, 0, SEEK_SET);
        if (read(fd, buf, sizeof(buf)) < 0) {
            return false;
        }
        return true;
    }
    return false;
}

unsigned long long nowMs() {
    using namespace std::chrono;
    return static_cast<unsigned long long>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

Json::Value number(double value) {
    if (!std::isfinite(value)) return Json::Value(Json::nullValue);
    return Json::Value(value);
}

int ioWrite(void* context, const std::uint8_t* data, std::size_t size) {
    auto* device = static_cast<rs6240_spidev_t*>(context);
    std::uint8_t discard[4096];
    if (!device || !data || size == 0u || size > sizeof(discard)) {
        return -1;
    }
    return rs6240_spidev_transfer(device, data, discard, size)
        ? static_cast<int>(size) : -1;
}

int ioRead(void* context, std::uint8_t* data, std::size_t capacity, std::uint32_t) {
    auto* device = static_cast<rs6240_spidev_t*>(context);
    std::uint8_t dummy[4096];
    if (!device || !data || capacity == 0u || capacity > 65535u) {
        return -1;
    }
    if (capacity > sizeof(dummy)) {
        return rs6240_spidev_read_large(device, data, capacity)
            ? static_cast<int>(capacity) : -1;
    }
    std::memset(dummy, 0xFF, capacity);
    return rs6240_spidev_transfer(device, dummy, data, capacity)
        ? static_cast<int>(capacity) : -1;
}

void ioDelay(void*, std::uint32_t delayMs) {
    if (delayMs > 0u) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

void ioDelayUs(void*, std::uint32_t delayUs) {
    if (delayUs > 0u) {
        std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
    }
}

// 全局就绪线句柄，保持 C 回调签名不变
int g_readyFd = -1;

// 等待设备就绪；1 就绪，0 超时，-1 无就绪线
int ioWaitReady(void*, std::uint32_t timeoutUs) {
    if (g_readyFd < 0) return -1;
    // 就绪线已保证一帧就绪，这里只留 2 毫秒兜底
    const std::uint32_t budgetUs = timeoutUs < 2000u ? 2000u : timeoutUs;
    int timeoutMs = static_cast<int>((budgetUs + 999u) / 1000u);
    if (timeoutMs <= 0) timeoutMs = 1;
    return waitFrameReady(g_readyFd, timeoutMs) ? 1 : 0;
}

// 拉高 DTR/RTS 使雷达进入命令模式
void assertModemLines(int fd) {
    int bits = TIOCM_DTR | TIOCM_RTS;
    if (ioctl(fd, TIOCMBIS, &bits) != 0) {
        std::cerr << "RadarLive: cannot raise DTR/RTS\n";
    }
}

// 发送空行清空串口行缓冲
void clearUartLine(int fd) {
    const char newline[] = "\r\n\r\n";
    if (write(fd, newline, sizeof(newline) - 1) < 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    char scratch[256];
    while (read(fd, scratch, sizeof(scratch)) > 0) {
    }
}

bool sendUart(int fd, const char* command, unsigned int waitMs) {
    // 命令需回 Command Success，失败重试一次
    unsigned int wait = waitMs;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string line = std::string(command) + "\r\n";
        if (write(fd, line.data(), line.size()) != static_cast<ssize_t>(line.size())) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        std::string reply;
        char buffer[512];
        ssize_t got;
        while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
            reply.append(buffer, static_cast<std::size_t>(got));
        }
        if (reply.find("Command Success") != std::string::npos) {
            return true;
        }
        wait *= 2u;
    }
    std::cerr << "RadarLive: no reply to '" << command << "'\n";
    return false;
}

// 执行一次独立的串口命令
bool uartCommand(const std::string& path, const char* command,
                 unsigned int waitMs) {
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    struct termios config {};
    if (fd < 0) return false;
    if (tcgetattr(fd, &config) != 0) { close(fd); return false; }
    cfmakeraw(&config);
    config.c_cflag |= CLOCAL | CREAD;
    config.c_cc[VMIN] = 0;
    config.c_cc[VTIME] = 0;
    cfsetispeed(&config, B115200);
    cfsetospeed(&config, B115200);
    if (tcsetattr(fd, TCSANOW, &config) != 0) { close(fd); return false; }
    assertModemLines(fd);
    tcflush(fd, TCIOFLUSH);
    clearUartLine(fd);
    const bool ok = sendUart(fd, command, waitMs);
    close(fd);
    return ok;
}

}  // namespace

namespace aura {

RadarLive::RadarLive(
    const std::string& spidevPath,
    const std::string& uartPath,
    unsigned int spiHz
) : spidevPath_(spidevPath), uartPath_(uartPath), spiHz_(spiHz) {
    device_ = rs6240_spidev_open(spidevPath_.c_str(), spiHz_);
    if (!device_) {
        std::cerr << "RadarLive: cannot open " << spidevPath_ << " at "
                  << spiHz_ << " Hz\n";
        return;
    }
    auto* capture = new rs6240_capture_t();
    std::memset(capture, 0, sizeof(*capture));
    rs6240_io_t io{};
    io.context = device_;
    io.write = ioWrite;
    io.read = ioRead;
    io.delay = ioDelay;
    io.delay_us = ioDelayUs;
    io.wait_ready = ioWaitReady;
    if (!rs6240_capture_init(capture, &io, 8192u)) {
        std::cerr << "RadarLive: capture init failed\n";
        delete capture;
        rs6240_spidev_close(static_cast<rs6240_spidev_t*>(device_));
        device_ = nullptr;
        return;
    }
    rs6240_capture_set_poll_command_delay(capture, 1u);
    // HOST_READ 后的稳定延时，默认 1 毫秒
    const char* pollUsEnv = std::getenv("AURA_RADAR_POLL_DELAY_US");
    const unsigned int pollUs =
        (pollUsEnv && *pollUsEnv) ? std::strtoul(pollUsEnv, nullptr, 10) : 1000u;
    rs6240_capture_set_poll_command_delay_us(capture, pollUs);
    capture_ = reinterpret_cast<rs6240_capture*>(capture);
    valid_ = true;
    std::cout << "RadarLive: spidev=" << spidevPath_ << " uart=" << uartPath_
              << " spi=" << spiHz_ << " Hz\n";
}

RadarLive::~RadarLive() {
    stop();
    if (capture_) {
        rs6240_capture_destroy(reinterpret_cast<rs6240_capture_t*>(capture_));
        delete reinterpret_cast<rs6240_capture_t*>(capture_);
        capture_ = nullptr;
    }
    if (device_) {
        rs6240_spidev_close(static_cast<rs6240_spidev_t*>(device_));
        device_ = nullptr;
    }
    if (pipeline_) {
        aura_pipeline_destroy(static_cast<AuraPipeline*>(pipeline_));
        pipeline_ = nullptr;
    }
}

bool RadarLive::valid() const {
    return valid_;
}

void RadarLive::start() {
    if (!valid_ || running_.exchange(true)) return;
    startedAtMs_ = nowMs();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        procStop_ = false;
    }
    procWorker_ = std::thread(&RadarLive::processWorker, this);
    worker_ = std::thread(&RadarLive::run, this);
}

void RadarLive::stop() {
    if (worker_.joinable()) {
        // 先让雷达停止，再退出采集线程
        (void)disarmUart();
        running_ = false;
        worker_.join();
    } else {
        running_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        procStop_ = true;
        queue_.clear();
    }
    queueCv_.notify_all();
    if (procWorker_.joinable()) {
        procWorker_.join();
    }
}

bool RadarLive::disarmUart() {
    int fd = open(uartPath_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    struct termios config {};
    if (fd < 0) return false;
    if (tcgetattr(fd, &config) != 0) {
        close(fd);
        return false;
    }
    cfmakeraw(&config);
    config.c_cflag |= CLOCAL | CREAD;
    config.c_cc[VMIN] = 0;
    config.c_cc[VTIME] = 0;
    cfsetispeed(&config, B115200);
    cfsetospeed(&config, B115200);
    if (tcsetattr(fd, TCSANOW, &config) != 0) {
        close(fd);
        return false;
    }
    assertModemLines(fd);
    tcflush(fd, TCIOFLUSH);
    clearUartLine(fd);
    bool ok = true;
    ok = sendUart(fd, "mmwc stop", 250) && ok;
    ok = sendUart(fd, "mmwc roi disable", 200) && ok;
    ok = sendUart(fd, "mmwc report disable", 250) && ok;
    close(fd);
    std::cout << "RadarLive: radar disarmed on shutdown\n";
    return ok;
}

Json::Value RadarLive::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json::Value data(Json::objectValue);
    data["source"] = "radar";
    data["spidev"] = spidevPath_;
    data["uart"] = uartPath_;
    data["spi_hz"] = static_cast<Json::UInt>(spiHz_);
    data["running"] = running_.load();
    data["started_at_ms"] = static_cast<Json::UInt64>(startedAtMs_);
    data["frames"] = static_cast<Json::UInt64>(framesTotal_);
    data["lost"] = static_cast<Json::UInt64>(lostFrames_);
    data["errors"] = static_cast<Json::UInt64>(errors_);
    data["channels"] = channels_;
    data["bins"] = bins_;
    data["fs"] = sampleRate_;
    data["latest"] = latest_;
    return data;
}

bool RadarLive::armUart(bool hardReset) {
    int fd = open(uartPath_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "RadarLive: cannot open " << uartPath_ << '\n';
        return false;
    }
    struct termios config {};
    if (tcgetattr(fd, &config) != 0) {
        close(fd);
        return false;
    }
    cfmakeraw(&config);
    config.c_cflag |= CLOCAL | CREAD;
    config.c_cc[VMIN] = 0;
    config.c_cc[VTIME] = 0;
    cfsetispeed(&config, B115200);
    cfsetospeed(&config, B115200);
    if (tcsetattr(fd, TCSANOW, &config) != 0) {
        close(fd);
        return false;
    }
    assertModemLines(fd);
    tcflush(fd, TCIOFLUSH);
    clearUartLine(fd);

    bool ok = true;
    ok = sendUart(fd, "mmwc stop", 250) && ok;
    // 先回到命令模式，唤醒后再开启精简模式
    ok = sendUart(fd, "mmwc hif_simplex 0", 200) && ok;
    if (hardReset) {
        // 硬复位：关闭并重新打开雷达会话
        ok = sendUart(fd, "mmwc close", 600) && ok;
        ok = sendUart(fd, "mmwc open", 1200) && ok;
    }
    ok = sendUart(fd, "mmwc report disable", 250) && ok;
    ok = sendUart(fd, "mmwc roi disable", 200) && ok;
    ok = sendUart(fd, "mmwc mode 4 1", 350) && ok;
    ok = sendUart(fd, "mmwc freq 57000 66000", 250) && ok;
    ok = sendUart(fd, "mmwc range 3072 24", 250) && ok;
    ok = sendUart(fd, "mmwc frame 10 0", 250) && ok;
    ok = sendUart(fd, "mmwc start", 800) && ok;
    ok = sendUart(fd, "mmwc stop", 250) && ok;
    ok = sendUart(fd, "mmwc comtype 1", 250) && ok;
    ok = sendUart(fd, "mmwc report points 1", 500) && ok;
    ok = sendUart(fd, "mmwc report cube 1", 600) && ok;
    ok = sendUart(fd, "mmwc report disable", 400) && ok;
    ok = sendUart(fd, "mmwc roi 8 16", 150) && ok;
    ok = sendUart(fd, "mmwc start", 100) && ok;
    close(fd);
    std::cout << "RadarLive: HIF switched to SPI, ROI 8+16 armed\n";
    return ok;
}

bool RadarLive::doWake() {
    rs6240_capture_status_t status =
        rs6240_capture_wake(reinterpret_cast<rs6240_capture_t*>(capture_), 50u, 5u);
    if (status != RS6240_CAPTURE_OK) {
        std::cerr << "RadarLive: HIF wake failed: " << static_cast<int>(status)
                  << " attempts=" << reinterpret_cast<rs6240_capture_t*>(capture_)->wake_attempts
                  << '\n';
        return false;
    }
    std::cout << "RadarLive: HIF wake=0x79\n";
    return true;
}

void RadarLive::enqueueFrame() {
    auto* capture = reinterpret_cast<rs6240_capture_t*>(capture_);
    const unsigned int tx = capture->roi.tx_count ? capture->roi.tx_count : 2u;
    const unsigned int rx = capture->roi.rx_count ? capture->roi.rx_count : 4u;
    const unsigned int bins =
        capture->roi.range_count ? capture->roi.range_count : 16u;
    const unsigned int channels = tx * rx;
    const unsigned int frameIndex = capture->roi.frame_index;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (haveLastFrame_ && frameIndex > lastFrameIndex_) {
            const unsigned int delta = frameIndex - lastFrameIndex_;
            if (delta > 1u) lostFrames_ += delta - 1u;
        }
        lastFrameIndex_ = frameIndex;
        haveLastFrame_ = true;
        ++framesTotal_;
    }

    PendingFrame frame;
    frame.channels = channels;
    frame.bins = bins;
    frame.frameIndex = frameIndex;
    frame.overrun = capture->roi.queue_overrun;
    frame.reads = capture->last_poll_reads;
    frame.msgLen = capture->last_message_length;
    frame.msgId = capture->last_message_id;
    const unsigned int total = channels * bins;
    frame.re.assign(total, 0.0f);
    frame.im.assign(total, 0.0f);
    const unsigned int points = capture->roi.point_count < total
        ? capture->roi.point_count : total;
    for (unsigned int i = 0; i < points; ++i) {
        frame.re[i] = static_cast<float>(capture->roi.points[i].real);
        frame.im[i] = static_cast<float>(capture->roi.points[i].imag);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        // 队列满时丢弃最旧帧，避免阻塞采集
        if (queue_.size() >= 512u) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(frame));
    }
    queueCv_.notify_one();
}

void RadarLive::processWorker() {
    for (;;) {
        PendingFrame frame;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return procStop_ || !queue_.empty(); });
            if (procStop_) return;
            frame = std::move(queue_.front());
            queue_.pop_front();
        }
        processFrame(frame);
    }
}

void RadarLive::processFrame(const PendingFrame& frame) {
    const unsigned int channels = frame.channels;
    const unsigned int bins = frame.bins;

    if (!pipeline_ || channels_ != channels || bins_ != bins) {
        if (pipeline_) {
            aura_pipeline_destroy(static_cast<AuraPipeline*>(pipeline_));
            pipeline_ = nullptr;
        }
        // 按采样率换算窗口与步长，保持时间尺度正确
        const int windowN = static_cast<int>(10.24 * sampleRate_ + 0.5);
        const int stepN = static_cast<int>(sampleRate_ + 0.5);
        pipeline_ = aura_pipeline_create(static_cast<int>(channels),
            static_cast<int>(bins), sampleRate_, windowN, stepN);
        channels_ = channels;
        bins_ = bins;
        std::cout << "RadarLive: pipeline channels=" << channels
                  << " bins=" << bins << " fs=" << sampleRate_
                  << " window=" << windowN << " hop=" << stepN << '\n';
        if (!pipeline_) return;
    }

    aura_pipeline_push(static_cast<AuraPipeline*>(pipeline_),
                       frame.re.data(), frame.im.data());

    AuraSnapshot snap{};
    if (aura_pipeline_snapshot(static_cast<AuraPipeline*>(pipeline_), &snap) != 0
        || snap.frame_id <= 0) {
        return;
    }
    Json::Value value(Json::objectValue);
    value["t"] = static_cast<Json::UInt64>(nowMs());
    value["frame"] = snap.frame_id;
    value["radar_frame"] = frame.frameIndex;
    value["reads"] = frame.reads;
    value["overrun"] = frame.overrun;
    value["msg_len"] = frame.msgLen;
    value["msg_id"] = frame.msgId;
    value["hr"] = number(snap.hr_bpm);
    value["hr_fft"] = number(snap.hr_fft);
    value["hr_acf"] = number(snap.hr_acf);
    value["hr_ppi"] = number(snap.hr_ppi);
    value["hr_fused"] = number(snap.hr_fused);
    value["conf"] = number(snap.hr_conf);
    value["confident"] = snap.hr_confident;
    value["consistent"] = snap.hr_consistent;
    value["resp_hz"] = number(snap.resp_hz);
    value["ppi_source"] = snap.ppi_source;
    value["n_beats"] = snap.n_beats;
    value["bin"] = snap.selected_bin;
    value["presence"] = snap.presence;
    value["motion"] = snap.motion;
    value["amp"] = number(snap.fused_amp);
    value["coh"] = number(snap.mean_coherence);
    value["motion_level"] = number(snap.motion_level_rad);
    value["motion_excursion"] = number(snap.motion_excursion_rad);
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = value;
}

void RadarLive::run() {
    const int readyFd = openReadyGpio();
    g_readyFd = readyFd;
    std::cout << "RadarLive: frame-ready GPIO "
              << (readyFd >= 0 ? "enabled" : "disabled") << '\n';
    if (!armUart()) {
        std::cerr << "RadarLive: UART arm failed; continuing with wake retries\n";
    }
    if (!doWake()) {
        errors_++;
    }
    // 设备自行推送报告时主机只负责读取时钟
    const char* simplexEnv = std::getenv("AURA_RADAR_SIMPLEX");
    const bool wantSimplex = !(simplexEnv && simplexEnv[0] == '0');
    bool simplex = false;
    if (wantSimplex && uartCommand(uartPath_, "mmwc hif_simplex 1", 400)) {
        rs6240_capture_set_simplex(
            reinterpret_cast<rs6240_capture_t*>(capture_), 1);
        simplex = true;
        std::cout << "RadarLive: HIF simplex enabled\n";
    } else {
        std::cerr << "RadarLive: HIF simplex disabled; polling mode\n";
    }
    unsigned int pollFailures = 0;
    unsigned int wakeFailures = 0;
    unsigned int pollCount = 0;
    auto lastRecovery = std::chrono::steady_clock::now() -
        std::chrono::seconds(10);
    // 单帧最大读取次数，精简模式下只需一次
    const unsigned int maxReads = simplex ? 1u : 64u;
    while (running_) {
        auto* capture = reinterpret_cast<rs6240_capture_t*>(capture_);
        // 就绪等待已放在采集循环内，这里不再重复
        const unsigned int timeoutMs = pollCount < 3u ? 1000u : 100u;
        rs6240_capture_status_t status =
            rs6240_capture_poll_roi(capture, timeoutMs, maxReads);
        ++pollCount;
        if (status == RS6240_CAPTURE_OK) {
            pollFailures = 0;
            // 只投递帧，采集线程不等待管线重算
            enqueueFrame();
        } else {
            if (!simplex) {
                ++errors_;
            } else {
                // 无数据时空读属正常
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ++pollFailures;
            if (pollFailures >= (simplex ? 200u : 40u)) {
                // 会话卡死需完整重新初始化，冷却期内不重复
                const auto now = std::chrono::steady_clock::now();
                if (now - lastRecovery >= std::chrono::seconds(5)) {
                    lastRecovery = now;
                    std::cerr << "RadarLive: no HIF frames, full re-arm\n";
                    (void)armUart(true);
                    if (!doWake()) {
                        ++wakeFailures;
                        if (wakeFailures % 20u == 0u) {
                            std::cerr << "RadarLive: wake still failing; "
                                         "power-cycle the radar module if "
                                         "this persists\n";
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }
                    } else {
                        wakeFailures = 0;
                    }
                    if (simplex) {
                        // 重新开启精简模式
                        (void)uartCommand(uartPath_, "mmwc hif_simplex 1", 200);
                    }
                }
                pollFailures = 0;
            }
        }
    }
    if (readyFd >= 0) {
        close(readyFd);
    }
}

}  // namespace aura
