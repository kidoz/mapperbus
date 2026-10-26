#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/apu/audio_settings.hpp"
#include "core/types.hpp"
#include "platform/input/gamepad_config.hpp"

namespace mapperbus::app {

inline constexpr int kMapperBusConfigurationVersion = 1;

struct MapperBusInputConfiguration {
    std::array<int, 8> keyboard_bindings{};
    platform::GamepadInputConfig gamepad;

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
    bool audio_muted = false;
    /// Speaker loudness from 0 to 100. Mute is a separate switch.
    int audio_volume_percent = 100;
    RegionPreference region = RegionPreference::Automatic;
};

struct RecentRomEntry {
    std::filesystem::path path;
    std::int64_t last_opened_unix = 0;
};

struct MapperBusConfiguration {
    int version = kMapperBusConfigurationVersion;
    core::AudioSettings audio;
    MapperBusInputConfiguration input;
    MapperBusFrontendConfiguration frontend;
    /// Recently opened ROMs, newest first, at most five entries.
    std::vector<RecentRomEntry> recent_roms;
};

/// Record a successful open: dedupe by normalized path, move to front, stamp
/// the time, and trim to the newest five entries.
void touch_recent_rom(MapperBusConfiguration& configuration, const std::filesystem::path& path);

/// Forget one recently opened ROM (matched by normalized path, like
/// touch_recent_rom). Used to prune entries whose file can no longer be
/// opened. Never touches game or save files.
void remove_recent_rom(MapperBusConfiguration& configuration, const std::filesystem::path& path);

/// Forget all recently opened ROMs. Never touches game or save files.
void clear_recent_roms(MapperBusConfiguration& configuration);

[[nodiscard]] std::string controller_button_name(core::Button button);
[[nodiscard]] std::filesystem::path mapperbus_configuration_path();
[[nodiscard]] MapperBusConfiguration load_mapperbus_configuration();
[[nodiscard]] MapperBusConfiguration load_mapperbus_configuration_from_file(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> save_mapperbus_configuration(
    const MapperBusConfiguration& configuration);
[[nodiscard]] core::Result<void> save_mapperbus_configuration_to_file(
    const MapperBusConfiguration& configuration, const std::filesystem::path& path);

} // namespace mapperbus::app
