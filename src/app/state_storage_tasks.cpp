#include "app/state_storage_tasks.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace mapperbus::app {
namespace {
core::Result<void> check_writable(const std::filesystem::path& directory) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 4; ++attempt) {
        struct Probe {
            std::filesystem::path path;
            bool owned = false;
            ~Probe() {
                if (owned) {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
            }
        } probe{directory / (".mapperbus-write-check-" + std::to_string(nonce) + "-" +
                             std::to_string(attempt))};
        // C++23 exclusive creation: never truncate an existing user's file.
        std::ofstream file(probe.path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (!file)
            continue;
        probe.owned = true;
        file.put('\0');
        file.close();
        if (!file)
            return std::unexpected("Cannot write to save-state folder: " + directory.string());
        std::error_code error;
        std::filesystem::remove(probe.path, error);
        if (error)
            return std::unexpected("Cannot remove a write check in save-state folder: " +
                                   error.message());
        probe.owned = false;
        return {};
    }
    return std::unexpected("Cannot write to save-state folder: " + directory.string());
}
} // namespace

core::Result<StateSlotStatuses> scan_state_slots(const StateSlotScanRequest& request) {
    StateSlotStatuses result{};
    for (int slot = 0; slot < kSaveStateSlotCount; ++slot) {
        const auto primary = request.directory / ("slot-" + std::to_string(slot) + ".state");
        const auto legacy =
            request.allow_legacy ? legacy_state_path(request.rom, slot) : std::filesystem::path{};
        auto path = resolve_state_load_path(primary, legacy);
        auto& status = result[static_cast<std::size_t>(slot)];
        if (!path) {
            status = StateSlotStatus::Unavailable;
            continue;
        }
        std::error_code error;
        const auto file = std::filesystem::status(path->path, error);
        status = error == std::errc::no_such_file_or_directory ||
                         (!error && !std::filesystem::exists(file))
                     ? StateSlotStatus::Empty
                 : error || !std::filesystem::is_regular_file(file) ? StateSlotStatus::Unavailable
                                                                    : StateSlotStatus::Saved;
    }
    return result;
}

core::Result<PreparedStateStorage> prepare_state_storage(const StateStorageSettings& settings,
                                                         const std::filesystem::path& rom,
                                                         const std::string& identity) {
    if (!settings.directory.empty() && !settings.directory.is_absolute())
        return std::unexpected("Choose an absolute save-state folder path");
    if (!valid_game_identity(identity))
        return std::unexpected("No valid game identity for save-state storage");
    auto directory = checked_state_directory_for_game(settings, rom, identity);
    if (!directory)
        return std::unexpected(directory.error());
    if (auto created = ensure_state_directory(*directory); !created)
        return std::unexpected(created.error());
    if (auto writable = check_writable(*directory); !writable)
        return std::unexpected(writable.error());
    return PreparedStateStorage{settings, std::move(*directory)};
}

core::Result<void> open_state_directory(const std::filesystem::path& directory) {
    if (auto created = ensure_state_directory(directory); !created)
        return created;
#if defined(_WIN32)
    const auto initialized =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized))
        return std::unexpected("Unable to initialize the states-folder opener");
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    CoUninitialize();
    if (result <= 32)
        return std::unexpected("Unable to open states folder: " + directory.string());
#else
#if defined(__APPLE__)
    std::string opener = "open";
    char** environment = *_NSGetEnviron();
#else
    std::string opener = "xdg-open";
    char** environment = environ;
#endif
    auto path = directory.string();
    std::array<char*, 3> arguments{opener.data(), path.data(), nullptr};
    pid_t pid = 0;
    const int result =
        posix_spawnp(&pid, opener.c_str(), nullptr, nullptr, arguments.data(), environment);
    if (result != 0)
        return std::unexpected("Unable to open states folder: " +
                               std::string(std::strerror(result)));
    int status = 0;
    pid_t waited = 0;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return std::unexpected("Unable to open states folder: " + path);
#endif
    return {};
}

StateStorageTasks::StateStorageTasks(Scan scan, Prepare prepare, Open open)
    : scan_(std::move(scan)), prepare_(std::move(prepare)), open_(std::move(open)) {}

core::Result<void> StateStorageTasks::scan(StateSlotScanRequest request) {
    (void)scan_task_.cancel();
    auto started = scan_task_.start(
        [scan = scan_, request = std::move(request)] { return scan(request); }, true);
    scanning_ = started.has_value();
    return started;
}

core::Result<void> StateStorageTasks::prepare(StateStorageSettings settings,
                                              std::filesystem::path rom,
                                              std::string identity) {
    if (preparing_)
        return std::unexpected("A storage location is already being checked");
    auto started = prepare_task_.start(
        [prepare = prepare_,
         settings = std::move(settings),
         rom = std::move(rom),
         identity = std::move(identity)] { return prepare(settings, rom, identity); },
        true);
    preparing_ = started.has_value();
    return started;
}

core::Result<void> StateStorageTasks::open(std::filesystem::path directory) {
    auto started = open_task_.start(
        [open = open_, directory = std::move(directory)] { return open(directory); }, false);
    if (started)
        opening_ = true;
    return started;
}

void StateStorageTasks::cancel_chooser() {
    (void)scan_task_.cancel();
    (void)prepare_task_.cancel();
    scanning_ = false;
    preparing_ = false;
}

std::optional<core::Result<StateSlotStatuses>> StateStorageTasks::poll_slots() {
    auto result = scan_task_.poll();
    if (result)
        scanning_ = false;
    return result;
}

std::optional<core::Result<PreparedStateStorage>> StateStorageTasks::poll_location() {
    auto result = prepare_task_.poll();
    if (result)
        preparing_ = false;
    return result;
}

std::optional<core::Result<void>> StateStorageTasks::poll_open() {
    auto result = open_task_.poll();
    if (result)
        opening_ = false;
    return result;
}

} // namespace mapperbus::app
