#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "app/state_storage.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/types.hpp"
#include "platform/input/gamepad_config.hpp"

namespace mapperbus::app {

inline constexpr int kMapperBusConfigurationVersion = 1;

struct MapperBusInputConfiguration {
    std::array<int, platform::kInputActionCount> keyboard_bindings{};
    platform::GamepadInputConfig gamepad;
    /// Player two starts with unbound keyboard controls and gamepad index 1.
    std::array<int, platform::kInputActionCount> player2_keyboard_bindings{};
    platform::GamepadInputConfig player2_gamepad;
    int turbo_period_frames = 4;

    MapperBusInputConfiguration();
};

/// Cartridge region used while playing. Automatic keeps the region the ROM
/// header selects.
enum class RegionPreference {
    Automatic,
    Ntsc,
    Pal,
    Dendy,
};

struct MapperBusFrontendConfiguration {
    int preview_scale_index = 0;
    int ui_density_index = 0;
    /// Last Settings pane: Input (0), Video (1), or Audio (2).
    int settings_page_index = 0;
    bool audio_muted = false;
    /// Speaker loudness from 0 to 100. Mute is a separate switch.
    int audio_volume_percent = 100;
    RegionPreference region = RegionPreference::Automatic;
};

struct RecentRomEntry {
    std::filesystem::path path;
    std::int64_t last_opened_unix = 0;
    std::string archive_member = {};
};

struct MapperBusConfiguration {
    int version = kMapperBusConfigurationVersion;
    std::string fds_bios_path;
    core::AudioSettings audio;
    MapperBusInputConfiguration input;
    MapperBusFrontendConfiguration frontend;
    /// Recently opened ROMs, newest first, at most five entries.
    std::vector<RecentRomEntry> recent_roms;
    StateStorageSettings save_states;
    /// Selected zero-based state slot, keyed by the ROM content SHA-256.
    std::map<std::string, int> state_slots;
};

[[nodiscard]] int selected_state_slot(const MapperBusConfiguration& configuration,
                                      std::string_view identity);
void remember_state_slot(MapperBusConfiguration& configuration,
                         std::string_view identity,
                         int slot);

/// Record a successful open: dedupe by normalized path and member, move to front, stamp
/// the time, and trim to the newest five entries.
void touch_recent_rom(MapperBusConfiguration& configuration,
                      const std::filesystem::path& path,
                      std::string_view archive_member = {});

/// Forget one recently opened ROM (matched by normalized path and member, like
/// touch_recent_rom). Used to prune entries whose file can no longer be
/// opened. Never touches game or save files.
void remove_recent_rom(MapperBusConfiguration& configuration,
                       const std::filesystem::path& path,
                       std::string_view archive_member = {});

/// Forget all recently opened ROMs. Never touches game or save files.
void clear_recent_roms(MapperBusConfiguration& configuration);

[[nodiscard]] std::string controller_button_name(platform::InputAction button);
[[nodiscard]] inline std::string controller_button_name(core::Button button) {
    return controller_button_name(platform::input_action(button));
}
[[nodiscard]] std::filesystem::path mapperbus_configuration_path();
[[nodiscard]] MapperBusConfiguration load_mapperbus_configuration();
[[nodiscard]] MapperBusConfiguration load_mapperbus_configuration_from_file(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> save_mapperbus_configuration(
    const MapperBusConfiguration& configuration);
[[nodiscard]] core::Result<void> save_mapperbus_configuration_to_file(
    const MapperBusConfiguration& configuration, const std::filesystem::path& path);

} // namespace mapperbus::app
