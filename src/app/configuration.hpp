#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/live_microphone.hpp"
#include "app/state_storage.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/types.hpp"
#include "platform/input/gamepad_config.hpp"

namespace mapperbus::core {
class Emulator;
} // namespace mapperbus::core

namespace mapperbus::app {

inline constexpr int kMapperBusConfigurationVersion = 1;

struct MapperBusInputConfiguration {
    core::ControllerTiming controller_timing = core::ControllerTiming::NesAvFamicom;
    core::ControllerWiring controller_wiring = core::ControllerWiring::Nes;
    std::array<int, platform::kInputActionCount> keyboard_bindings{};
    platform::GamepadInputConfig gamepad;
    /// Player two starts with unbound keyboard controls and gamepad index 1.
    std::array<int, platform::kInputActionCount> player2_keyboard_bindings{};
    platform::GamepadInputConfig player2_gamepad;
    /// External pads 1/2 use host profiles 3/4. Bit 0/1 attaches each D1 input.
    core::Byte expansion_controllers = 0;
    core::ControllerMode controller_mode = core::ControllerMode::Standard;
    std::array<std::array<int, platform::kInputActionCount>, platform::kInputProfileCount - 2>
        expansion_keyboard_bindings{};
    std::array<platform::GamepadInputConfig, platform::kInputProfileCount - 2> expansion_gamepads{};
    int turbo_period_frames = 4;

    MapperBusInputConfiguration();
    /// Assigns and enables devices 0..3 for Hori 1..4, retaining all bindings.
    void configure_four_hori_gamepads();
    auto& keyboard_profile(int player) {
        if (player == 0)
            return keyboard_bindings;
        if (player == 1)
            return player2_keyboard_bindings;
        return expansion_keyboard_bindings.at(player - 2);
    }
    const auto& keyboard_profile(int player) const {
        if (player == 0)
            return keyboard_bindings;
        if (player == 1)
            return player2_keyboard_bindings;
        return expansion_keyboard_bindings.at(player - 2);
    }
    auto& gamepad_profile(int player) {
        if (player == 0)
            return gamepad;
        if (player == 1)
            return player2_gamepad;
        return expansion_gamepads.at(player - 2);
    }
    const auto& gamepad_profile(int player) const {
        if (player == 0)
            return gamepad;
        if (player == 1)
            return player2_gamepad;
        return expansion_gamepads.at(player - 2);
    }
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
    /// Last Settings pane: Input (0), Video (1), Audio (2), or General (3).
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

/// Machine controller hardware only; host bindings and devices stay shared.
struct ControllerHardwareSettings {
    core::ControllerMode mode = core::ControllerMode::Standard;
    core::ControllerWiring wiring = core::ControllerWiring::Nes;
    core::ControllerTiming timing = core::ControllerTiming::NesAvFamicom;
    core::Byte expansion_controllers = 0;
    bool operator==(const ControllerHardwareSettings&) const = default;
};

/// Missing or invalid fields inherit defaults. Also used for process-local CLI overrides.
struct ControllerHardwareOverrides {
    std::optional<core::ControllerMode> mode = std::nullopt;
    std::optional<core::ControllerWiring> wiring = std::nullopt;
    std::optional<core::ControllerTiming> timing = std::nullopt;
    std::optional<core::Byte> expansion_controllers = std::nullopt;
    [[nodiscard]] bool empty() const;
    void apply_to(ControllerHardwareSettings& settings) const;
    bool operator==(const ControllerHardwareOverrides&) const = default;
};

struct MapperBusConfiguration {
    int version = kMapperBusConfigurationVersion;
    std::string fds_bios_path;
    core::AudioSettings audio;
    LiveMicrophoneSettings microphone;
    MapperBusInputConfiguration input;
    MapperBusFrontendConfiguration frontend;
    /// Recently opened ROMs, newest first, at most five entries.
    std::vector<RecentRomEntry> recent_roms;
    StateStorageSettings save_states;
    /// Selected zero-based state slot, keyed by the ROM content SHA-256.
    std::map<std::string, int> state_slots;
    /// Optional hardware preferences keyed by the same content identity as states.
    std::map<std::string, ControllerHardwareOverrides> game_controllers;
    /// All six host device choices per game; bindings and deadzones stay in input.
    std::map<std::string, platform::GamepadAssignments> game_gamepads;
};

[[nodiscard]] platform::GamepadAssignments gamepad_assignments_for_game(
    const MapperBusConfiguration& configuration, std::string_view identity);
void remember_gamepad_assignment(MapperBusConfiguration& configuration,
                                 std::string_view identity,
                                 int player,
                                 const platform::GamepadAssignment& assignment);
/// Save shared controls/deadzones, routing only the device choice to its active scope.
void remember_gamepad_profile(MapperBusConfiguration& configuration,
                              std::string_view identity,
                              int player,
                              const platform::GamepadInputConfig& profile);

[[nodiscard]] ControllerHardwareSettings controller_hardware(
    const MapperBusInputConfiguration& input);
[[nodiscard]] ControllerHardwareSettings controller_hardware(const core::Emulator& emulator);
void set_controller_hardware(MapperBusInputConfiguration& input,
                             const ControllerHardwareSettings& settings);
void apply_controller_hardware(core::Emulator& emulator,
                               const ControllerHardwareSettings& settings);
[[nodiscard]] ControllerHardwareSettings controller_hardware_for_game(
    const MapperBusConfiguration& configuration,
    std::string_view identity,
    const ControllerHardwareOverrides& overrides = {});
void remember_controller_hardware(MapperBusConfiguration& configuration,
                                  std::string_view identity,
                                  const ControllerHardwareSettings& settings);

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
[[nodiscard]] std::optional<core::ControllerTiming> parse_controller_timing(std::string_view value);
[[nodiscard]] const char* controller_timing_token(core::ControllerTiming timing);
[[nodiscard]] std::optional<core::ControllerWiring> parse_controller_wiring(std::string_view value);
[[nodiscard]] const char* controller_wiring_token(core::ControllerWiring wiring);
[[nodiscard]] std::optional<core::ControllerMode> parse_controller_mode(std::string_view value);
[[nodiscard]] const char* controller_mode_token(core::ControllerMode mode);
[[nodiscard]] std::optional<core::Byte> parse_expansion_controllers(std::string_view value);
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
