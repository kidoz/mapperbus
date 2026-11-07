#include "frontends/sdl3/sdl3_microphone.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <limits>

#if defined(__APPLE__)
#include "frontends/sdl3/macos/microphone_permission.hpp"
#endif

namespace mapperbus::frontend {
namespace {
void SDLCALL trim_capture(void*, SDL_AudioStream* stream, int, int) {
    // Bound pending PCM even while a native dialog blocks the UI thread.
    if (SDL_GetAudioStreamAvailable(stream) > 2048 * static_cast<int>(sizeof(float)))
        (void)SDL_ClearAudioStream(stream);
}
} // namespace

Sdl3Microphone::Sdl3Microphone(std::unique_ptr<platform::MicrophonePermission> permission)
    : permission_(std::move(permission)) {
#if defined(__APPLE__)
    if (!permission_) {
        permission_ = make_macos_microphone_permission();
        native_permission_ = true;
    }
#endif
}

core::Result<void> Sdl3Microphone::check_permission(bool request_if_needed) {
    if (!permission_)
        return {};
    // Dummy/disk drivers do not use hardware and must not prompt during tests.
    const auto* driver = SDL_GetCurrentAudioDriver();
    if (native_permission_ && driver && std::string_view(driver) != "coreaudio")
        return {};
    using platform::MicrophoneAccess;
    switch (permission_->status()) {
    case MicrophoneAccess::Authorized:
        return {};
    case MicrophoneAccess::NotDetermined:
        if (!request_if_needed)
            return std::unexpected("Microphone permission changed; choose Retry to request access");
        permission_->request();
        return std::unexpected("Answer the macOS microphone permission prompt, then choose Retry");
    case MicrophoneAccess::Pending:
        return std::unexpected(
            "Waiting for macOS microphone permission; answer the prompt, then Retry");
    case MicrophoneAccess::Denied:
        return std::unexpected("Microphone access denied. Enable MapperBus in System Settings > "
                               "Privacy & Security > Microphone. Reopen if macOS asks, then Retry");
    case MicrophoneAccess::Restricted:
        return std::unexpected("Microphone access is restricted by macOS policy. Contact your "
                               "administrator; bound input remains available");
    }
    return std::unexpected("Microphone permission is unavailable");
}

Sdl3Microphone::~Sdl3Microphone() {
    stop();
    if (initialized_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

core::Result<void> Sdl3Microphone::initialize() {
    if (!initialized_) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return std::unexpected(std::string(SDL_GetError()));
        initialized_ = true;
    }
    return {};
}

core::Result<std::vector<std::string>> Sdl3Microphone::devices() {
    if (auto result = initialize(); !result)
        return std::unexpected(result.error());
    int count = 0;
    auto* ids = SDL_GetAudioRecordingDevices(&count);
    if (!ids)
        return std::unexpected(std::string(SDL_GetError()));
    std::vector<std::string> names;
    for (int index = 0; index < count; ++index) {
        const auto* name = SDL_GetAudioDeviceName(ids[index]);
        if (name && std::ranges::find(names, name) == names.end())
            names.emplace_back(name);
    }
    SDL_free(ids);
    return names;
}

core::Result<void> Sdl3Microphone::start(std::string_view device) {
    stop();
    if (auto result = initialize(); !result)
        return result;
    std::string name(device);
    if (name.empty()) {
        const auto* current = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
        if (!current)
            return std::unexpected(std::string(SDL_GetError()));
        name = current;
        system_default_ = true;
        default_name_ = current;
    }
    int count = 0;
    auto* ids = SDL_GetAudioRecordingDevices(&count);
    if (!ids)
        return std::unexpected(std::string(SDL_GetError()));
    initial_devices_.assign(ids, ids + count);
    std::ranges::sort(initial_devices_);
    for (int index = 0; index < count; ++index) {
        const auto* candidate = SDL_GetAudioDeviceName(ids[index]);
        if (candidate && name == candidate) {
            physical_ = ids[index];
            break;
        }
    }
    SDL_free(ids);
    if (!physical_)
        return std::unexpected("Selected recording device is unavailable: " + name);
    if (auto result = check_permission(true); !result) {
        stop();
        return result;
    }
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 1, .freq = 16000};
    stream_ =
        SDL_OpenAudioDeviceStream(system_default_ ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : physical_,
                                  &spec,
                                  trim_capture,
                                  nullptr);
    if (!stream_) {
        physical_ = 0;
        return std::unexpected(std::string(SDL_GetError()));
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        const std::string error(SDL_GetError());
        stop();
        return std::unexpected(error);
    }
    return {};
}

void Sdl3Microphone::stop() {
    if (stream_)
        SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    physical_ = 0;
    system_default_ = false;
    initial_devices_.clear();
    default_name_.clear();
}

core::Result<void> Sdl3Microphone::check_connected() {
    if (!stream_)
        return std::unexpected("Capture is not running");
    if (auto result = check_permission(false); !result) {
        stop();
        return result;
    }
    int count = 0;
    auto* ids = SDL_GetAudioRecordingDevices(&count);
    if (!ids)
        return std::unexpected(std::string(SDL_GetError()));
    bool connected = std::find(ids, ids + count, physical_) != ids + count;
    if (system_default_) {
        std::sort(ids, ids + count);
        const auto* name = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
        connected =
            std::equal(initial_devices_.begin(), initial_devices_.end(), ids, ids + count) &&
            name && default_name_ == name;
    }
    SDL_free(ids);
    if (!connected) {
        stop();
        return std::unexpected("Recording device disconnected; choose a device or retry");
    }
    return {};
}

core::Result<std::size_t> Sdl3Microphone::read(std::span<float> samples) {
    if (auto result = check_connected(); !result)
        return std::unexpected(result.error());
    if (samples.empty())
        return 0;
    const int available = SDL_GetAudioStreamAvailable(stream_);
    if (available < 0)
        return std::unexpected(std::string(SDL_GetError()));
    if (samples.size_bytes() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::unexpected("Capture read is too large");
    if (static_cast<std::size_t>(available) > samples.size_bytes()) {
        // Drop an old backlog in constant time instead of replaying stale speech.
        if (auto result = discard(); !result)
            return std::unexpected(result.error());
        return 0;
    }
    const int bytes =
        SDL_GetAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size_bytes()));
    if (bytes < 0)
        return std::unexpected(std::string(SDL_GetError()));
    if (bytes % sizeof(float) != 0)
        return std::unexpected("Invalid capture sample size");
    return static_cast<std::size_t>(bytes) / sizeof(float);
}

core::Result<void> Sdl3Microphone::discard() {
    if (auto result = check_connected(); !result)
        return result;
    if (!SDL_ClearAudioStream(stream_))
        return std::unexpected(std::string(SDL_GetError()));
    return {};
}

} // namespace mapperbus::frontend
