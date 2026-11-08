#include "aura/live.h"

#include <aura_pipeline.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

namespace {

unsigned long long nowMs() {
    using namespace std::chrono;
    return static_cast<unsigned long long>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

Json::Value finiteNumber(double value) {
    if (!std::isfinite(value)) return Json::Value(Json::nullValue);
    return Json::Value(value);
}

}  // namespace

namespace aura {

LiveReplay::LiveReplay(
    const std::filesystem::path& cubePath,
    int channels,
    int bins,
    double sampleRate,
    double speed
) : cubePath_(cubePath), channels_(channels), bins_(bins),
    sampleRate_(sampleRate > 0 ? sampleRate : 100.0), speed_(speed > 0 ? speed : 1.0) {
    if (channels_ <= 0 || bins_ <= 0) return;
    frameFloats_ = static_cast<std::size_t>(channels_) * static_cast<std::size_t>(bins_) * 2u;

    std::ifstream input(cubePath_, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "LiveReplay: cannot open " << cubePath_.string() << '\n';
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(input.tellg());
    if (bytes < frameFloats_ * sizeof(float)) {
        std::cerr << "LiveReplay: cube too small\n";
        return;
    }
    input.seekg(0);
    cube_.resize(bytes / sizeof(float));
    if (!input.read(reinterpret_cast<char*>(cube_.data()), bytes)) {
        std::cerr << "LiveReplay: cube read failed\n";
        return;
    }
    frameCount_ = cube_.size() / frameFloats_;

    pipeline_ = aura_pipeline_create(channels_, bins_, sampleRate_, 1024, 100);
    if (!pipeline_) {
        std::cerr << "LiveReplay: pipeline allocation failed\n";
        return;
    }
    valid_ = true;
    std::cout << "LiveReplay: " << cubePath_.string() << " frames=" << frameCount_
              << " channels=" << channels_ << " bins=" << bins_
              << " fs=" << sampleRate_ << " speed=" << speed_ << '\n';
}

LiveReplay::~LiveReplay() {
    stop();
    if (pipeline_) {
        aura_pipeline_destroy(static_cast<AuraPipeline*>(pipeline_));
        pipeline_ = nullptr;
    }
}

bool LiveReplay::valid() const {
    return valid_;
}

void LiveReplay::start() {
    if (!valid_ || running_.exchange(true)) return;
    startedAtMs_ = nowMs();
    worker_ = std::thread(&LiveReplay::run, this);
}

void LiveReplay::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

Json::Value LiveReplay::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json::Value data(Json::objectValue);
    data["source"] = "replay";
    data["cube"] = cubePath_.string();
    data["frames"] = static_cast<Json::UInt64>(frameCount_);
    data["channels"] = channels_;
    data["bins"] = bins_;
    data["fs"] = sampleRate_;
    data["speed"] = speed_;
    data["running"] = running_.load();
    data["started_at_ms"] = static_cast<Json::UInt64>(startedAtMs_);
    data["latest"] = latest_;
    return data;
}

void LiveReplay::run() {
    using namespace std::chrono;
    AuraPipeline* pipeline = static_cast<AuraPipeline*>(pipeline_);
    std::vector<float> re(static_cast<std::size_t>(channels_) * bins_);
    std::vector<float> im(re.size());
    const auto period = microseconds(static_cast<long long>(
        1000000.0 / (sampleRate_ * speed_)));
    auto next = steady_clock::now();

    while (running_) {
        const std::size_t base = frameIndex_ * frameFloats_;
        for (std::size_t i = 0; i < re.size(); ++i) {
            re[i] = cube_[base + 2u * i];
            im[i] = cube_[base + 2u * i + 1u];
        }
        aura_pipeline_push(pipeline, re.data(), im.data());

        AuraSnapshot snap{};
        if (aura_pipeline_snapshot(pipeline, &snap) == 0 && snap.frame_id > 0
            && static_cast<unsigned long long>(snap.frame_id) != emittedFrame_) {
            emittedFrame_ = static_cast<unsigned long long>(snap.frame_id);
            Json::Value value(Json::objectValue);
            value["t"] = static_cast<Json::UInt64>(nowMs());
            value["frame"] = snap.frame_id;
            value["hr"] = finiteNumber(snap.hr_bpm);
            value["hr_fft"] = finiteNumber(snap.hr_fft);
            value["hr_acf"] = finiteNumber(snap.hr_acf);
            value["hr_ppi"] = finiteNumber(snap.hr_ppi);
            value["hr_fused"] = finiteNumber(snap.hr_fused);
            value["conf"] = finiteNumber(snap.hr_conf);
            value["confident"] = snap.hr_confident;
            value["consistent"] = snap.hr_consistent;
            value["resp_hz"] = finiteNumber(snap.resp_hz);
            value["ppi_source"] = snap.ppi_source;
            value["n_beats"] = snap.n_beats;
            value["bin"] = snap.selected_bin;
            value["presence"] = snap.presence;
            value["motion"] = snap.motion;
            value["amp"] = finiteNumber(snap.fused_amp);
            value["coh"] = finiteNumber(snap.mean_coherence);
            value["motion_level"] = finiteNumber(snap.motion_level_rad);
            value["motion_excursion"] = finiteNumber(snap.motion_excursion_rad);
            value["position"] = static_cast<Json::UInt64>(frameIndex_);
            std::lock_guard<std::mutex> lock(mutex_);
            latest_ = value;
        }

        ++frameIndex_;
        if (frameIndex_ >= frameCount_) frameIndex_ = 0;

        next += period;
        const auto now = steady_clock::now();
        if (now < next) {
            std::this_thread::sleep_until(next);
        } else if (now - next > milliseconds(200)) {
            next = now;
        }
    }
}

}  // namespace aura
