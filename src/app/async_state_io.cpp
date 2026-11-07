#include "app/async_state_io.hpp"

#include <utility>

#include "app/state_storage.hpp"
#include "core/binary_file.hpp"

namespace mapperbus::app {

core::Result<LoadedState> read_state(const StateLoadRequest& request) {
    auto resolved = resolve_state_load_path(request.primary, request.legacy);
    if (!resolved)
        return std::unexpected(resolved.error());
    auto data = core::read_binary_file_capped(
        resolved->path, core::Emulator::kMaxStateBytes, "save-state file");
    if (!data)
        return std::unexpected(data.error());
    return LoadedState{std::move(resolved->path), std::move(*data), resolved->legacy};
}

core::Result<void> write_state(core::Emulator& emulator, const std::string& path) {
    if (auto directory = ensure_state_directory(std::filesystem::path(path).parent_path());
        !directory)
        return directory;
    if (auto saved = emulator.flush_disk_changes(); !saved)
        return saved;
    return emulator.save_state_to_file(path);
}

AsyncStateIO::AsyncStateIO(Read read, Write write)
    : read_(std::move(read)), write_(std::move(write)) {}

core::Result<void> AsyncStateIO::load(StateLoadRequest request) {
    auto started =
        task_.start([read = read_, request = std::move(request)] { return read(request); }, true);
    if (started)
        phase_ = Phase::Loading;
    return started;
}

core::Result<void> AsyncStateIO::save(std::shared_ptr<core::Emulator> emulator, std::string path) {
    auto started = task_.start(
        [write = write_,
         emulator = std::move(emulator),
         path = std::move(path)]() -> core::Result<LoadedState> {
            if (auto saved = write(*emulator, path); !saved)
                return std::unexpected(saved.error());
            return LoadedState{path, {}, false};
        },
        false);
    if (started)
        phase_ = Phase::Saving;
    return started;
}

std::optional<core::Result<LoadedState>> AsyncStateIO::poll() {
    auto result = task_.poll();
    if (result)
        phase_ = Phase::Idle;
    return result;
}

bool AsyncStateIO::cancel() {
    if (!task_.cancel())
        return false;
    phase_ = Phase::Idle;
    return true;
}

} // namespace mapperbus::app
