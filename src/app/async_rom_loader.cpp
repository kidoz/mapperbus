#include "app/async_rom_loader.hpp"

#include <mutex>
#include <utility>

#include "app/rom_source.hpp"

namespace mapperbus::app {

struct AsyncRomLoader::ProgressState {
    std::mutex mutex;
    std::optional<ArchiveReadProgress> latest;
};

core::Result<PreparedRom> prepare_rom(const RomLoadRequest& request, std::stop_token stop) {
    const auto cancelled = [&] { return std::unexpected(std::string(kRomLoadCancelled)); };
    if (stop.stop_requested())
        return cancelled();
    const RomReadControl control{stop, request.archive_observer};
    PreparedRom media;
    media.path = request.path;
    media.member = request.member;
    if (is_rom_archive(request.path) && request.member.empty()) {
        auto members = list_archive_roms(request.path, control);
        if (!members)
            return std::unexpected(members.error());
        if (stop.stop_requested())
            return cancelled();
        if (members->size() > 1) {
            media.members = std::move(*members);
            return media;
        }
        if (members->size() == 1)
            media.member = members->front();
    }
    if (stop.stop_requested())
        return cancelled();
    media.emulator = std::make_shared<core::Emulator>(request.audio_settings);
    media.emulator->set_persistence_enabled(false);
    auto loaded = load_rom_source(*media.emulator,
                                  request.path,
                                  media.member,
                                  request.fds_bios_path,
                                  request.persistence_directory,
                                  control);
    if (!loaded)
        return std::unexpected(loaded.error());
    if (stop.stop_requested())
        return cancelled();
    media.member = std::move(*loaded);
    media.emulator->reset();
    media.state_directory = state_directory_for_game(
        request.state_storage, request.path, core::sha256_hex(media.emulator->content_hash()));
    if (stop.stop_requested())
        return cancelled();
    return media;
}

AsyncRomLoader::AsyncRomLoader(CancellablePrepare prepare) : prepare_(std::move(prepare)) {}

AsyncRomLoader::AsyncRomLoader(Prepare prepare)
    : AsyncRomLoader([prepare = std::move(prepare)](const RomLoadRequest& request,
                                                    std::stop_token) { return prepare(request); }) {
}

core::Result<void> AsyncRomLoader::launch(BackgroundTask<PreparedRom>::CancellableTask task,
                                          Phase phase) {
    auto started = task_.start(std::move(task), phase == Phase::Loading);
    if (started)
        phase_ = phase;
    return started;
}

core::Result<void> AsyncRomLoader::start(RomLoadRequest request) {
    auto progress = std::make_shared<ProgressState>();
    request.archive_observer =
        [progress, observer = std::move(request.archive_observer)](ArchiveReadProgress update) {
            {
                const std::lock_guard lock(progress->mutex);
                progress->latest = update;
            }
            if (observer)
                observer(update);
        };
    auto started = launch([prepare = prepare_, request = std::move(request)](
                              std::stop_token stop) { return prepare(request, stop); },
                          Phase::Loading);
    if (started)
        progress_ = std::move(progress);
    return started;
}

core::Result<void> AsyncRomLoader::save_current(std::shared_ptr<core::Emulator> current,
                                                PersistenceFence fence) {
    return launch(
        [current = std::move(current),
         fence = std::move(fence)](std::stop_token) -> core::Result<PreparedRom> {
            if (fence)
                fence();
            if (auto saved = current->flush_persistent_data(); !saved)
                return std::unexpected("Cannot save the current game: " + saved.error());
            return PreparedRom{};
        },
        Phase::Saving);
}

std::optional<core::Result<PreparedRom>> AsyncRomLoader::poll() {
    auto result = task_.poll();
    if (result) {
        phase_ = Phase::Idle;
        progress_.reset();
    }
    return result;
}

bool AsyncRomLoader::cancel() {
    if (!task_.cancel())
        return false;
    phase_ = Phase::Idle;
    progress_.reset();
    return true;
}

std::optional<ArchiveReadProgress> AsyncRomLoader::archive_progress() const {
    if (phase_ != Phase::Loading || !progress_)
        return std::nullopt;
    const std::unique_lock lock(progress_->mutex, std::try_to_lock);
    return lock.owns_lock() ? progress_->latest : std::nullopt;
}

} // namespace mapperbus::app
