#pragma once

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

#include "app/background_task.hpp"
#include "app/rom_source.hpp"

namespace mapperbus::app {

/// Read-only, bounded BIOS checks. Workers own paths/readers, never frontend state.
class AsyncFdsBiosValidation {
  public:
    explicit AsyncFdsBiosValidation(FdsBiosRead read = read_fds_bios)
        : read_(std::make_shared<const FdsBiosRead>(std::move(read))) {}

    [[nodiscard]] core::Result<void> start(std::string path) {
        return task_.start(
            [read = read_,
             path = std::move(path)](std::stop_token stop) -> core::Result<std::string> {
                if (stop.stop_requested())
                    return std::unexpected("Checking BIOS cancelled.");
                auto bios = (*read)(path);
                if (stop.stop_requested())
                    return std::unexpected("Checking BIOS cancelled.");
                if (!bios)
                    return std::unexpected(bios.error());
                if (bios->size() != 8192)
                    return std::unexpected("FDS BIOS must contain exactly 8192 bytes.");
                return path;
            },
            true);
    }
    [[nodiscard]] std::optional<core::Result<std::string>> poll() {
        return task_.poll();
    }
    bool cancel() {
        return task_.cancel();
    }

  private:
    std::shared_ptr<const FdsBiosRead> read_;
    BackgroundTask<std::string> task_;
};

} // namespace mapperbus::app
