#include "app/async_disk_save.hpp"

#include <utility>

#include "core/binary_file.hpp"

namespace mapperbus::app {

AsyncDiskSave::AsyncDiskSave(Write write) : write_(std::move(write)) {}

core::Result<void> AsyncDiskSave::write_snapshot(const core::DiskSaveSnapshot& snapshot) {
    return core::write_binary_file_atomic(snapshot.path, snapshot.data, "FDS disk save");
}

core::Result<void> AsyncDiskSave::start(core::DiskSaveSnapshot snapshot) {
    auto started = task_.start(
        [write = write_,
         snapshot = std::move(snapshot)]() mutable -> core::Result<core::DiskSaveSnapshot> {
            if (auto saved = write(snapshot); !saved)
                return std::unexpected(saved.error());
            return std::move(snapshot);
        },
        false);
    running_ = started.has_value();
    if (!started)
        error_ = started.error();
    return started;
}

core::Result<void> AsyncDiskSave::submit(core::DiskSaveSnapshot snapshot) {
    if (handed_off_)
        return std::unexpected("Disk persistence belongs to another save operation");
    if (running_) {
        next_ = std::move(snapshot);
        return {};
    }
    if (!error_.empty())
        return std::unexpected(error_);
    return start(std::move(snapshot));
}

std::optional<core::Result<core::DiskSaveSnapshot>> AsyncDiskSave::poll() {
    if (handed_off_)
        return std::nullopt;
    auto result = task_.poll();
    if (!result)
        return std::nullopt;
    running_ = false;
    if (!*result) {
        error_ = result->error();
        next_.reset(); // RAM retains the latest data; Retry takes a fresh copy.
    } else {
        error_.clear();
        if (next_) {
            auto next = std::move(*next_);
            next_.reset();
            (void)start(std::move(next));
        }
    }
    return result;
}

PersistenceFence AsyncDiskSave::begin_handoff() {
    handed_off_ = true;
    next_.reset();
    return task_.completion_fence();
}

void AsyncDiskSave::finish_handoff(const core::Result<void>& result) {
    // The exclusive worker waited for the fence before accessing the machine.
    (void)task_.poll();
    running_ = false;
    handed_off_ = false;
    error_ = result ? std::string{} : result.error();
}

void AsyncDiskSave::cancel_handoff() {
    // Starting the exclusive worker failed: keep tracking the original writer.
    handed_off_ = false;
}

} // namespace mapperbus::app
