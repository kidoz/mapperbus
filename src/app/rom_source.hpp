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
    /// Reader-thread diagnostic for an inaccessible/invalid BIOS; own state, never UI references.
    std::function<void()> on_fds_bios_error = {};
};
inline constexpr std::string_view kRomLoadCancelled = "Opening ROM cancelled";

[[nodiscard]] bool is_rom_archive(const std::filesystem::path& path);
using FdsBiosRead = std::function<core::Result<std::vector<core::Byte>>(const std::string&)>;
/// Reads a regular file capped at 8 KiB and requires exactly 8192 bytes.
[[nodiscard]] core::Result<std::vector<core::Byte>> read_fds_bios(const std::string& path);
/// Raw PRG or a PRG-only iNES image; the normalized chip is 16..128 KiB.
[[nodiscard]] core::Result<std::vector<core::Byte>> read_dcs_option_rom(const std::string& path);
/// Lists regular .nes and .fds members. Names are identifiers, never extraction paths.
[[nodiscard]] core::Result<std::vector<std::string>> list_archive_roms(
    const std::string& path, const RomReadControl& control = {});
/// Loads raw media or one ZIP/7z member. With no member, exactly one candidate
/// is required. Returns the selected member (empty for a raw ROM). Any failure
/// leaves the emulator's current cartridge intact.
/// An optional persistence root replaces the platform data directory for FDS
/// BIOS fallback/disk changes, archived battery saves and shared Datach reader
/// storage. A Datach path override selects that device file explicitly.
/// State slots are separate.
[[nodiscard]] core::Result<std::string> load_rom_source(
    core::Emulator& emulator,
    const std::string& path,
    std::string_view member = {},
    const std::string& fds_bios_path = {},
    const std::filesystem::path& persistence_directory = {},
    const RomReadControl& control = {},
    const std::string& dcs_option_path = {},
    const std::string& datach_save_path = {});

} // namespace mapperbus::app
