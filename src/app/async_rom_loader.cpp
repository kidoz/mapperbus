#include "app/async_rom_loader.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include "app/rom_source.hpp"
#include "core/mappers/mapper_registry.hpp"

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

struct AsyncRomLoader::Job {
    std::mutex mutex;
    bool abandoned = false;
    std::optional<core::Result<PreparedRom>> result;
};

struct AsyncRomLoader::Workers {
    std::atomic<int> count{0};
    std::shared_ptr<const core::MapperRegistry> registry = core::MapperRegistry::retain();
};

AsyncRomLoader::AsyncRomLoader(Prepare prepare)
    : prepare_(std::move(prepare)), workers_(std::make_shared<Workers>()) {}

AsyncRomLoader::~AsyncRomLoader() {
    if (job_) {
        const std::lock_guard lock(job_->mutex);
        job_->abandoned = true;
    }
}

core::Result<void> AsyncRomLoader::launch(std::function<core::Result<PreparedRom>()> task,
                                          Phase phase) {
    if (job_)
        return std::unexpected("A ROM operation is already in progress");
    // Stuck OS reads cannot be forcibly interrupted portably. Bound abandoned
    // jobs so repeated Cancel/Open cannot create unbounded threads or buffers.
    auto job = std::make_shared<Job>();
    constexpr int kMaxWorkers = 4;
    if (workers_->count.fetch_add(1) >= kMaxWorkers) {
        --workers_->count;
        return std::unexpected("Previous file operations are still waiting. Try again later.");
    }
    try {
        std::thread([job, workers = workers_, task = std::move(task)] {
            auto result = [&]() -> core::Result<PreparedRom> {
                try {
                    return task();
                } catch (const std::exception& error) {
                    return std::unexpected("ROM operation failed: " + std::string(error.what()));
                } catch (...) {
                    return std::unexpected("ROM operation failed unexpectedly");
                }
            }();
            {
                const std::lock_guard lock(job->mutex);
                if (!job->abandoned)
                    job->result = std::move(result);
            }
            --workers->count;
        }).detach();
    } catch (const std::exception& error) {
        --workers_->count;
        return std::unexpected("Cannot start ROM operation: " + std::string(error.what()));
    }
    job_ = std::move(job);
    phase_ = phase;
    return {};
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
    if (!job_)
        return std::nullopt;
    std::optional<core::Result<PreparedRom>> result;
    {
        const std::lock_guard lock(job_->mutex);
        if (!job_->result)
            return std::nullopt;
        result = std::move(job_->result);
    }
    job_.reset();
    phase_ = Phase::Idle;
    return result;
}

bool AsyncRomLoader::cancel() {
    if (phase_ != Phase::Loading)
        return false;
    {
        const std::lock_guard lock(job_->mutex);
        job_->abandoned = true;
    }
    job_.reset();
    phase_ = Phase::Idle;
    return true;
}

} // namespace mapperbus::app
