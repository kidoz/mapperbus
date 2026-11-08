#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/async_disk_save.hpp"
#include "app/background_task.hpp"
#include "app/rom_source.hpp"
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
    ArchiveReadObserver archive_observer = {};
    /// Owned reader-thread diagnostic carrying the resolved member (empty for raw media).
    std::function<void(std::string)> on_fds_bios_error = {};
};

struct PreparedRom {
    // Null only when an archive requires a member choice (or after saving).
    std::shared_ptr<core::Emulator> emulator;
    std::string path;
    std::string member;
    std::filesystem::path state_directory;
    std::vector<std::string> members;
};

[[nodiscard]] core::Result<PreparedRom> prepare_rom(const RomLoadRequest& request,
                                                    std::stop_token stop = {});

/// Owns bounded, isolated I/O jobs. Cancellation/destruction never joins a blocked
/// file operation. Workers own their inputs/results and never call the frontend.
class AsyncRomLoader {
  public:
    enum class Phase { Idle, Saving, Loading };
    using Prepare = std::function<core::Result<PreparedRom>(const RomLoadRequest&)>;
    using CancellablePrepare =
        std::function<core::Result<PreparedRom>(const RomLoadRequest&, std::stop_token)>;

    explicit AsyncRomLoader(CancellablePrepare prepare = prepare_rom);
    /// Compatibility for injected readers that cannot cooperate with cancellation.
    explicit AsyncRomLoader(Prepare prepare);
    ~AsyncRomLoader() = default;
    AsyncRomLoader(const AsyncRomLoader&) = delete;
    AsyncRomLoader& operator=(const AsyncRomLoader&) = delete;

    [[nodiscard]] core::Result<void> start(RomLoadRequest request);
    /// Caller pauses the machine and stops accessing it until poll completes.
    /// Saving cannot be cancelled; the handle keeps it alive during shutdown.
    [[nodiscard]] core::Result<void> save_current(std::shared_ptr<core::Emulator> current,
                                                  PersistenceFence fence = {});
    [[nodiscard]] std::optional<core::Result<PreparedRom>> poll();
    /// Latest coalesced archive checkpoint for this job, never an abandoned job.
    /// A busy mailbox returns no update rather than blocking the caller.
    [[nodiscard]] std::optional<ArchiveReadProgress> archive_progress() const;
    /// BIOS failure from the most recently polled load, including its resolved member.
    [[nodiscard]] const std::optional<std::string>& fds_bios_error_member() const {
        return fds_bios_error_member_;
    }
    bool cancel();
    [[nodiscard]] Phase phase() const {
        return phase_;
    }

  private:
    [[nodiscard]] core::Result<void> launch(BackgroundTask<PreparedRom>::CancellableTask task,
                                            Phase phase);
    CancellablePrepare prepare_;
    BackgroundTask<PreparedRom> task_;
    struct ProgressState;
    std::shared_ptr<ProgressState> progress_;
    std::optional<std::string> fds_bios_error_member_;
    Phase phase_ = Phase::Idle;
};

} // namespace mapperbus::app
