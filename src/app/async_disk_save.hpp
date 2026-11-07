#pragma once

#include <functional>
#include <optional>
#include <string>

#include "app/background_task.hpp"
#include "core/emulator.hpp"

namespace mapperbus::app {

using PersistenceFence = std::function<void()>;

/// One immutable disk write at a time, plus at most one coalesced newer snapshot.
/// Poll/submit/handoff run on the emulation thread; workers never access a machine.
class AsyncDiskSave {
  public:
    using Write = std::function<core::Result<void>(const core::DiskSaveSnapshot&)>;
    explicit AsyncDiskSave(Write write = write_snapshot);
    [[nodiscard]] core::Result<void> submit(core::DiskSaveSnapshot snapshot);
    [[nodiscard]] std::optional<core::Result<core::DiskSaveSnapshot>> poll();
    [[nodiscard]] bool busy() const {
        return running_;
    }
    [[nodiscard]] const std::string& error() const {
        return error_;
    }
    void clear_error() {
        error_.clear();
    }

    /// Drop the coalesced copy (the exclusive writer will capture current RAM).
    /// Wait on the returned fence in that worker, then finish the handoff on UI.
    [[nodiscard]] PersistenceFence begin_handoff();
    void finish_handoff(const core::Result<void>& result);
    void cancel_handoff();
    [[nodiscard]] static core::Result<void> write_snapshot(const core::DiskSaveSnapshot& snapshot);

  private:
    [[nodiscard]] core::Result<void> start(core::DiskSaveSnapshot snapshot);
    Write write_;
    BackgroundTask<core::DiskSaveSnapshot> task_;
    std::optional<core::DiskSaveSnapshot> next_;
    std::string error_;
    bool running_ = false;
    bool handed_off_ = false;
};

} // namespace mapperbus::app
