#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/background_task.hpp"
#include "app/state_storage.hpp"
#include "core/emulator.hpp"

namespace mapperbus::app {

struct RomLoadRequest {
    std::string path;
    std::string member;
    std::string fds_bios_path;
    core::AudioSettings audio_settings;
    StateStorageSettings state_storage;
    std::filesystem::path persistence_directory;
};

struct PreparedRom {
    // Null only when an archive requires a member choice (or after saving).
    std::shared_ptr<core::Emulator> emulator;
    std::string path;
    std::string member;
    std::filesystem::path state_directory;
    std::vector<std::string> members;
};

[[nodiscard]] core::Result<PreparedRom> prepare_rom(const RomLoadRequest& request);

/// Owns bounded, isolated I/O jobs. Cancellation/destruction never joins a blocked
/// file operation. Workers own their inputs/results and never call the frontend.
class AsyncRomLoader {
  public:
    enum class Phase { Idle, Saving, Loading };
    using Prepare = std::function<core::Result<PreparedRom>(const RomLoadRequest&)>;

    explicit AsyncRomLoader(Prepare prepare = prepare_rom);
    ~AsyncRomLoader() = default;
    AsyncRomLoader(const AsyncRomLoader&) = delete;
    AsyncRomLoader& operator=(const AsyncRomLoader&) = delete;

    [[nodiscard]] core::Result<void> start(RomLoadRequest request);
    /// Caller pauses the machine and stops accessing it until poll completes.
    /// Saving cannot be cancelled; the handle keeps it alive during shutdown.
    [[nodiscard]] core::Result<void> save_current(std::shared_ptr<core::Emulator> current);
    [[nodiscard]] std::optional<core::Result<PreparedRom>> poll();
    bool cancel();
    [[nodiscard]] Phase phase() const {
        return phase_;
    }

  private:
    [[nodiscard]] core::Result<void> launch(std::function<core::Result<PreparedRom>()> task,
                                            Phase phase);
    Prepare prepare_;
    BackgroundTask<PreparedRom> task_;
    Phase phase_ = Phase::Idle;
};

} // namespace mapperbus::app
