#include "app/async_rom_loader.hpp"

#include <utility>

#include "app/rom_source.hpp"

namespace mapperbus::app {

core::Result<PreparedRom> prepare_rom(const RomLoadRequest& request) {
    PreparedRom media;
    media.path = request.path;
    media.member = request.member;
    if (is_rom_archive(request.path) && request.member.empty()) {
        auto members = list_archive_roms(request.path);
        if (!members)
            return std::unexpected(members.error());
        if (members->size() > 1) {
            media.members = std::move(*members);
            return media;
        }
        if (members->size() == 1)
            media.member = members->front();
    }
    media.emulator = std::make_shared<core::Emulator>(request.audio_settings);
    media.emulator->set_persistence_enabled(false);
    auto loaded = load_rom_source(*media.emulator,
                                  request.path,
                                  media.member,
                                  request.fds_bios_path,
                                  request.persistence_directory);
    if (!loaded)
        return std::unexpected(loaded.error());
    media.member = std::move(*loaded);
    media.emulator->reset();
    media.state_directory = state_directory_for_game(
        request.state_storage, request.path, core::sha256_hex(media.emulator->content_hash()));
    return media;
}

AsyncRomLoader::AsyncRomLoader(Prepare prepare) : prepare_(std::move(prepare)) {}

core::Result<void> AsyncRomLoader::launch(std::function<core::Result<PreparedRom>()> task,
                                          Phase phase) {
    auto started = task_.start(std::move(task), phase == Phase::Loading);
    if (started)
        phase_ = phase;
    return started;
}

core::Result<void> AsyncRomLoader::start(RomLoadRequest request) {
    return launch([prepare = prepare_, request = std::move(request)] { return prepare(request); },
                  Phase::Loading);
}

core::Result<void> AsyncRomLoader::save_current(std::shared_ptr<core::Emulator> current) {
    return launch(
        [current = std::move(current)]() -> core::Result<PreparedRom> {
            if (auto saved = current->flush_persistent_data(); !saved)
                return std::unexpected("Cannot save the current game: " + saved.error());
            return PreparedRom{};
        },
        Phase::Saving);
}

std::optional<core::Result<PreparedRom>> AsyncRomLoader::poll() {
    auto result = task_.poll();
    if (result)
        phase_ = Phase::Idle;
    return result;
}

bool AsyncRomLoader::cancel() {
    if (!task_.cancel())
        return false;
    phase_ = Phase::Idle;
    return true;
}

} // namespace mapperbus::app
