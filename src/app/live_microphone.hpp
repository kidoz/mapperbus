#pragma once

#include <memory>
#include <string>

#include "platform/audio/microphone_backend.hpp"

namespace mapperbus::app {

struct LiveMicrophoneSettings {
    bool enabled = false;
    /// Empty selects the system default at capture start; other names match exactly.
    std::string device;
    int sensitivity = 50;
};

/// Host input policy; raw capture/device state never enters machine snapshots.
class LiveMicrophone {
  public:
    ~LiveMicrophone();
    void set_backend(std::unique_ptr<platform::MicrophoneBackend> backend);
    void configure(LiveMicrophoneSettings settings);
    void suspend();
    void discard();
    void retry();
    [[nodiscard]] bool poll(bool eligible);
    [[nodiscard]] core::Result<std::vector<std::string>> devices();
    [[nodiscard]] std::string status() const;
    [[nodiscard]] const LiveMicrophoneSettings& settings() const {
        return settings_;
    }
    [[nodiscard]] bool recording() const {
        return recording_;
    }

  private:
    void fail(const std::string& error);
    LiveMicrophoneSettings settings_;
    std::unique_ptr<platform::MicrophoneBackend> backend_;
    std::string error_;
    bool recording_ = false;
    bool active_ = false;
};

} // namespace mapperbus::app
