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
    settings.volume_percent = std::clamp(settings.volume_percent, 0, 100);
    if (settings.enabled != settings_.enabled || settings.device != settings_.device)
        suspend();
    if (settings.audio_enabled != settings_.audio_enabled ||
        settings.volume_percent != settings_.volume_percent)
        clear_mix_audio();
    settings_ = std::move(settings);
    error_.clear();
}

void LiveMicrophone::suspend() {
    if (backend_ && recording_)
        backend_->stop();
    recording_ = false;
    active_ = false;
    clear_mix_audio();
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
    clear_mix_audio();
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
        samples[index] = sample;
    }
    if (settings_.audio_enabled && settings_.volume_percent > 0)
        append_mix_audio({samples.data(), *read});
    const float threshold =
        0.2F * std::pow(0.01F, static_cast<float>(settings_.sensitivity) / 100.0F);
    const double level = std::sqrt(energy / static_cast<double>(*read));
    active_ = level >= static_cast<double>(threshold * (active_ ? 0.6F : 1.0F));
    return active_;
}

void LiveMicrophone::clear_mix_audio() {
    mix_audio_.fill(0.0F);
    mix_head_ = 0;
    mix_size_ = 0;
    mix_phase_ = 0;
}

void LiveMicrophone::append_mix_audio(std::span<const float> samples) {
    // Keep at most 128 ms; replace a stale backlog with the latest packet.
    if (samples.size() + mix_size_ > mix_audio_.size())
        clear_mix_audio();
    for (const auto sample : samples) {
        mix_audio_[(mix_head_ + mix_size_) % mix_audio_.size()] = sample;
        ++mix_size_;
    }
}

void LiveMicrophone::mix_into(std::span<float> samples, int sample_rate, int channels) {
    if (!recording_ || !settings_.audio_enabled || settings_.volume_percent == 0 ||
        sample_rate < 8000 || sample_rate > 192000 || (channels != 1 && channels != 2))
        return;
    const auto channel_count = static_cast<std::size_t>(channels);
    const double step = 16000.0 / static_cast<double>(sample_rate);
    const float gain = static_cast<float>(settings_.volume_percent) / 100.0F;
    for (std::size_t frame = 0; frame + channel_count <= samples.size() && mix_size_ > 0;
         frame += channel_count) {
        const auto next = (mix_head_ + (mix_size_ > 1 ? 1 : 0)) % mix_audio_.size();
        const float sound =
            std::lerp(mix_audio_[mix_head_], mix_audio_[next], static_cast<float>(mix_phase_)) *
            gain;
        for (std::size_t channel = 0; channel < channel_count; ++channel)
            samples[frame + channel] = std::clamp(samples[frame + channel] + sound, -1.0F, 1.0F);
        mix_phase_ += step;
        const auto consumed = static_cast<std::size_t>(mix_phase_);
        if (consumed >= mix_size_) {
            clear_mix_audio();
        } else {
            for (std::size_t index = 0; index < consumed; ++index)
                mix_audio_[(mix_head_ + index) % mix_audio_.size()] = 0.0F;
            mix_head_ = (mix_head_ + consumed) % mix_audio_.size();
            mix_size_ -= consumed;
            mix_phase_ -= static_cast<double>(consumed);
        }
    }
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
