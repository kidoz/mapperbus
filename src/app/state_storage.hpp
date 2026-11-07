#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace mapperbus::app {

inline constexpr int kSaveStateSlotCount = 10;

enum class StateStorageMode { UserData, Portable };

struct StateStorageSettings {
    StateStorageMode mode = StateStorageMode::UserData;
    /// Optional absolute root containing per-game folders. Takes precedence over mode.
    std::filesystem::path directory;
};

[[nodiscard]] std::filesystem::path mapperbus_data_directory();
[[nodiscard]] bool valid_game_identity(std::string_view identity);
/// Reuses an existing folder with this content identity, regardless of the ROM's name.
[[nodiscard]] std::filesystem::path state_directory_for_game(const StateStorageSettings& settings,
                                                             const std::filesystem::path& rom,
                                                             std::string_view identity);
struct StateLoadPath {
    std::string path;
    bool legacy = false;
};
/// A present/erroring primary never falls back. An empty legacy path disables import.
[[nodiscard]] core::Result<StateLoadPath> resolve_state_load_path(
    const std::filesystem::path& primary, const std::filesystem::path& legacy = {});
[[nodiscard]] core::Result<void> ensure_state_directory(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path legacy_state_path(const std::filesystem::path& rom, int slot);

} // namespace mapperbus::app
