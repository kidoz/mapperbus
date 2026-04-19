#include "frontends/nodalkit/gui_audio_backend.hpp"

#include "platform/audio/null_audio.hpp"

#if defined(MAPPERBUS_HAVE_SDL3_AUDIO)
#include "frontends/sdl3/sdl3_audio.hpp"
#endif

namespace mapperbus::frontend {

GuiAudioBackend::GuiAudioBackend() : fallback_(std::make_unique<platform::NullAudio>()) {
#if defined(MAPPERBUS_HAVE_SDL3_AUDIO)
    primary_ = std::make_unique<Sdl3Audio>();
#endif
}

GuiAudioBackend::~GuiAudioBackend() {
    shutdown();
}

bool GuiAudioBackend::initialize(int sample_rate, int buffer_size, int channels) {
    shutdown();

    if (primary_ && primary_->initialize(sample_rate, buffer_size, channels)) {
        active_ = primary_.get();
        using_fallback_ = false;
        return true;
    }

    if (fallback_ && fallback_->initialize(sample_rate, buffer_size, channels)) {
        active_ = fallback_.get();
        using_fallback_ = true;
        return true;
    }

    active_ = nullptr;
    using_fallback_ = false;
    return false;
}

void GuiAudioBackend::queue_samples(std::span<const float> samples) {
    if (active_ && !muted_) {
        active_->queue_samples(samples);
    }
}

void GuiAudioBackend::shutdown() {
    if (primary_) {
        primary_->shutdown();
    }
    if (fallback_) {
        fallback_->shutdown();
    }
    active_ = nullptr;
    using_fallback_ = false;
}

int GuiAudioBackend::queued_samples() const {
    if (!active_) {
        return 0;
    }
    return active_->queued_samples();
}

std::string_view GuiAudioBackend::status_text() const {
#if defined(MAPPERBUS_HAVE_SDL3_AUDIO)
    if (muted_) {
        return "Audio: muted";
    }
    if (active_ == nullptr) {
        return "Audio: SDL3 if available";
    }
    return using_fallback_ ? "Audio: silent fallback" : "Audio: SDL3";
#else
    return "Audio: disabled";
#endif
}

bool GuiAudioBackend::is_muted() const {
    return muted_;
}

bool GuiAudioBackend::uses_realtime_output() const {
    return active_ != nullptr && !using_fallback_ && !muted_;
}

void GuiAudioBackend::set_muted(bool muted) {
    muted_ = muted;
}

} // namespace mapperbus::frontend
