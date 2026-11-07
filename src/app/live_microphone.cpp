#include "app/live_microphone.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/logger.hpp"

namespace mapperbus::app {

LiveMicrophone::~LiveMicrophone() {
    suspend();
}

void LiveMicrophone::set_backend(std::unique_ptr<platform::MicrophoneBackend> backend) {
    suspend();
    backend_ = std::move(backend);
    error_.clear();
}

void LiveMicrophone::configure(LiveMicrophoneSettings settings) {
    settings.sensitivity = std::clamp(settings.sensitivity, 1, 100);
    if (settings.enabled != settings_.enabled || settings.device != settings_.device)
        suspend();
    settings_ = std::move(settings);
    error_.clear();
}

void LiveMicrophone::suspend() {
    if (backend_ && recording_)
        backend_->stop();
    recording_ = false;
    active_ = false;
}

void LiveMicrophone::fail(const std::string& error) {
    suspend();
    error_ = error.empty() ? "Capture failed" : error;
    core::logger::warn("Live microphone: {}", error_);
}

void LiveMicrophone::retry() {
    suspend();
    error_.clear();
}

void LiveMicrophone::discard() {
    active_ = false;
    if (recording_) {
        if (auto result = backend_->discard(); !result)
            fail(result.error());
    }
}

bool LiveMicrophone::poll(bool eligible) {
    if (!settings_.enabled || !eligible) {
        suspend();
        return false;
    }
    if (!backend_ || !error_.empty())
        return false;
    if (!recording_) {
        auto started = backend_->start(settings_.device);
        if (!started) {
            backend_->stop();
            fail(started.error());
            return false;
        }
        recording_ = true;
    }
    std::array<float, 2048> samples{};
    auto read = backend_->read(samples);
    if (!read || *read > samples.size()) {
        fail(read ? "Invalid capture sample count" : read.error());
        return false;
    }
    if (*read == 0) {
        active_ = false;
        return false;
    }
    double energy = 0;
    for (std::size_t index = 0; index < *read; ++index) {
        const auto sample =
            std::isfinite(samples[index]) ? std::clamp(samples[index], -1.0F, 1.0F) : 0.0F;
        energy += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const float threshold =
        0.2F * std::pow(0.01F, static_cast<float>(settings_.sensitivity) / 100.0F);
    const double level = std::sqrt(energy / static_cast<double>(*read));
    active_ = level >= static_cast<double>(threshold * (active_ ? 0.6F : 1.0F));
    return active_;
}

core::Result<std::vector<std::string>> LiveMicrophone::devices() {
    if (!backend_)
        return std::unexpected("Live capture is unavailable in this build");
    return backend_->devices();
}

std::string LiveMicrophone::status() const {
    if (!settings_.enabled)
        return "Live microphone: off";
    if (!backend_)
        return "Live microphone: unavailable in this build";
    if (!error_.empty())
        return "Live microphone: " + error_;
    if (recording_)
        return active_ ? "Live microphone: recording · activity detected"
                       : "Live microphone: recording · quiet";
    return "Live microphone: waiting for an unpaused RF Famicom game";
}

} // namespace mapperbus::app
