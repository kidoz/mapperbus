#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/async_disk_save.hpp"
#include "app/background_task.hpp"
#include "core/emulator.hpp"

namespace mapperbus::app {

struct StateLoadRequest {
    std::filesystem::path primary;
    // Empty for archived ROMs and FDS, which cannot import sibling states.
    std::filesystem::path legacy;
};

struct LoadedState {
    std::string path;
    std::vector<core::Byte> data;
    bool legacy = false;
};

[[nodiscard]] core::Result<LoadedState> read_state(const StateLoadRequest& request);
/// Caller must pause and stop accessing the machine until completion.
[[nodiscard]] core::Result<void> write_state(core::Emulator& emulator, const std::string& path);

class AsyncStateIO {
  public:
    enum class Phase { Idle, Loading, Saving };
    using Read = std::function<core::Result<LoadedState>(const StateLoadRequest&)>;
    using Write = std::function<core::Result<void>(core::Emulator&, const std::string&)>;

    explicit AsyncStateIO(Read read = read_state, Write write = write_state);
    [[nodiscard]] core::Result<void> load(StateLoadRequest request);
    /// Saving owns the paused machine until completion, including through teardown.
    [[nodiscard]] core::Result<void> save(std::shared_ptr<core::Emulator> emulator,
                                          std::string path,
                                          PersistenceFence fence = {});
    [[nodiscard]] std::optional<core::Result<LoadedState>> poll();
    bool cancel();
    [[nodiscard]] Phase phase() const {
        return phase_;
    }

  private:
    Read read_;
    Write write_;
    BackgroundTask<LoadedState> task_;
    Phase phase_ = Phase::Idle;
};

} // namespace mapperbus::app
