#pragma once

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/emulator.hpp"

namespace mapperbus::app {

inline constexpr std::uintmax_t kMaxArchiveBytes = 256ULL * 1024 * 1024;
inline constexpr std::size_t kMaxArchiveEntries = 1024;

enum class ArchiveReadPhase { Open, Header, Data, Skip, Close };
struct ArchiveReadProgress {
    ArchiveReadPhase phase;
    std::size_t entries;
    std::size_t bytes;
};
/// Observers execute on the reader thread and must own thread-safe state, never UI references.
using ArchiveReadObserver = std::function<void(ArchiveReadProgress)>;
struct RomReadControl {
    std::stop_token stop;
    ArchiveReadObserver observe;
};
inline constexpr std::string_view kRomLoadCancelled = "Opening ROM cancelled";

[[nodiscard]] bool is_rom_archive(const std::filesystem::path& path);
/// Lists regular .nes and .fds members. Names are identifiers, never extraction paths.
[[nodiscard]] core::Result<std::vector<std::string>> list_archive_roms(
    const std::string& path, const RomReadControl& control = {});
/// Loads raw media or one ZIP/7z member. With no member, exactly one candidate
/// is required. Returns the selected member (empty for a raw ROM). Any failure
/// leaves the emulator's current cartridge intact.
/// An optional persistence root replaces the platform data directory for FDS
/// BIOS fallback/disk changes and archived battery saves. State slots are separate.
[[nodiscard]] core::Result<std::string> load_rom_source(
    core::Emulator& emulator,
    const std::string& path,
    std::string_view member = {},
    const std::string& fds_bios_path = {},
    const std::filesystem::path& persistence_directory = {},
    const RomReadControl& control = {});

} // namespace mapperbus::app
