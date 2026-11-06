#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/emulator.hpp"

namespace mapperbus::app {

inline constexpr std::uintmax_t kMaxArchiveBytes = 256ULL * 1024 * 1024;
inline constexpr std::size_t kMaxArchiveEntries = 1024;

[[nodiscard]] bool is_rom_archive(const std::filesystem::path& path);
/// Lists regular .nes members. Names are identifiers, never extraction paths.
[[nodiscard]] core::Result<std::vector<std::string>> list_archive_roms(const std::string& path);
/// Loads raw media or one ZIP/7z member. With no member, exactly one candidate
/// is required. Returns the selected member (empty for a raw ROM). Any failure
/// leaves the emulator's current cartridge intact.
[[nodiscard]] core::Result<std::string> load_rom_source(core::Emulator& emulator,
                                                        const std::string& path,
                                                        std::string_view member = {});

} // namespace mapperbus::app
