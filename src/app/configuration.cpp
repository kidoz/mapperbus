#include "app/configuration.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "core/logger.hpp"

namespace mapperbus::app {
namespace {

[[nodiscard]] int default_keyboard_code_for_button(platform::InputAction button) {
    switch (button) {
    case platform::InputAction::Microphone:
        return 16; // M
    case platform::InputAction::TurboA:
        return 22; // S
    case platform::InputAction::TurboB:
        return 4; // A
    case platform::InputAction::A:
        return 27; // NodalKit X / USB HID X
    case platform::InputAction::B:
        return 29; // NodalKit Z / USB HID Z
    case platform::InputAction::Select:
        return 229; // Right Shift
    case platform::InputAction::Start:
        return 40; // Return
    case platform::InputAction::Up:
        return 82;
    case platform::InputAction::Down:
        return 81;
    case platform::InputAction::Left:
        return 80;
    case platform::InputAction::Right:
        return 79;
    }

    return 0;
}

[[nodiscard]] bool parse_bool(std::string_view value, bool fallback) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

[[nodiscard]] std::optional<int> parse_int_value(std::string_view value) {
    int parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [cursor, error] = std::from_chars(begin, end, parsed);
    if (error == std::errc{} && cursor == end) {
        return parsed;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> parse_double_value(std::string_view value) {
    double parsed = 0.0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [cursor, error] = std::from_chars(begin, end, parsed);
    if (error == std::errc{} && cursor == end) {
        return parsed;
    }
    return std::nullopt;
}

[[nodiscard]] int parse_int(std::string_view value, int fallback, std::string_view key) {
    if (auto parsed = parse_int_value(value)) {
        return *parsed;
    }
    core::logger::warn("Invalid integer for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] double parse_double(std::string_view value, double fallback, std::string_view key) {
    if (auto parsed = parse_double_value(value)) {
        return *parsed;
    }
    core::logger::warn("Invalid decimal for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] bool parse_bool_setting(std::string_view value, bool fallback, std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on" ||
        normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return parse_bool(value, fallback);
    }
    core::logger::warn("Invalid boolean for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] core::ResamplingMode parse_resampling_mode(std::string_view value,
                                                         core::ResamplingMode fallback,
                                                         std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "blip" || normalized == "blipbuffer" || normalized == "blip-buffer") {
        return core::ResamplingMode::BlipBuffer;
    }
    if (normalized == "cubic" || normalized == "cubichermite" || normalized == "cubic-hermite") {
        return core::ResamplingMode::CubicHermite;
    }
    core::logger::warn("Invalid resampling mode for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] core::FilterMode parse_filter_mode(std::string_view value,
                                                 core::FilterMode fallback,
                                                 std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "accurate" || normalized == "hardwareaccurate" ||
        normalized == "hardware-accurate") {
        return core::FilterMode::HardwareAccurate;
    }
    if (normalized == "enhanced") {
        return core::FilterMode::Enhanced;
    }
    if (normalized == "unfiltered" || normalized == "off") {
        return core::FilterMode::Unfiltered;
    }
    core::logger::warn("Invalid filter mode for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] core::FilterProfile parse_filter_profile(std::string_view value,
                                                       core::FilterProfile fallback,
                                                       std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "nes") {
        return core::FilterProfile::NES;
    }
    if (normalized == "famicom") {
        return core::FilterProfile::Famicom;
    }
    core::logger::warn("Invalid filter profile for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] core::StereoMode parse_stereo_mode(std::string_view value,
                                                 core::StereoMode fallback,
                                                 std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "mono") {
        return core::StereoMode::Mono;
    }
    if (normalized == "stereo" || normalized == "pseudostereo" || normalized == "pseudo-stereo") {
        return core::StereoMode::PseudoStereo;
    }
    core::logger::warn("Invalid stereo mode for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] core::ExpansionMixingMode parse_expansion_mixing_mode(
    std::string_view value, core::ExpansionMixingMode fallback, std::string_view key) {
    const std::string normalized = platform::normalized_token(value);
    if (normalized == "simple" || normalized == "simplesum" || normalized == "simple-sum") {
        return core::ExpansionMixingMode::SimpleSum;
    }
    if (normalized == "resistance" || normalized == "resistancemodeled" ||
        normalized == "resistance-modeled") {
        return core::ExpansionMixingMode::ResistanceModeled;
    }
    core::logger::warn(
        "Invalid expansion mixing mode for configuration key '{}': '{}'", key, value);
    return fallback;
}

[[nodiscard]] std::string resampling_mode_token(core::ResamplingMode mode) {
    return mode == core::ResamplingMode::CubicHermite ? "cubic" : "blip";
}

[[nodiscard]] std::string filter_mode_token(core::FilterMode mode) {
    switch (mode) {
    case core::FilterMode::HardwareAccurate:
        return "accurate";
    case core::FilterMode::Enhanced:
        return "enhanced";
    case core::FilterMode::Unfiltered:
    default:
        return "unfiltered";
    }
}

[[nodiscard]] std::string filter_profile_token(core::FilterProfile profile) {
    return profile == core::FilterProfile::Famicom ? "famicom" : "nes";
}

[[nodiscard]] std::string stereo_mode_token(core::StereoMode mode) {
    return mode == core::StereoMode::PseudoStereo ? "pseudo-stereo" : "mono";
}

[[nodiscard]] std::string expansion_mixing_token(core::ExpansionMixingMode mode) {
    return mode == core::ExpansionMixingMode::ResistanceModeled ? "resistance" : "simple";
}

[[nodiscard]] RegionPreference parse_region_preference(std::string_view value) {
    const std::string token = platform::normalized_token(value);
    if (token == "ntsc") {
        return RegionPreference::Ntsc;
    }
    if (token == "pal") {
        return RegionPreference::Pal;
    }
    if (token == "dendy") {
        return RegionPreference::Dendy;
    }
    if (token != "auto" && token != "automatic" && !token.empty()) {
        core::logger::warn("Invalid region preference '{}'; using automatic", value);
    }
    return RegionPreference::Automatic;
}

[[nodiscard]] const char* region_preference_token(RegionPreference preference) {
    switch (preference) {
    case RegionPreference::Ntsc:
        return "ntsc";
    case RegionPreference::Pal:
        return "pal";
    case RegionPreference::Dendy:
        return "dendy";
    case RegionPreference::Automatic:
        break;
    }
    return "auto";
}

[[nodiscard]] std::string scoped_key(std::string_view section, std::string_view key) {
    if (section.empty()) {
        return std::string(key);
    }
    return std::string(section) + "." + std::string(key);
}

void apply_setting(MapperBusConfiguration& configuration,
                   std::string_view section,
                   std::string_view key,
                   std::string_view value) {
    if (section == "state_slots") {
        const auto slot = parse_int_value(value);
        if (valid_game_identity(key) && slot && *slot >= 0 && *slot < kSaveStateSlotCount) {
            remember_state_slot(configuration, key, *slot);
        }
        return;
    }
    if (section == "save_states") {
        if (key == "mode") {
            configuration.save_states.mode =
                value == "portable" ? StateStorageMode::Portable : StateStorageMode::UserData;
        } else if (key == "directory") {
            const std::filesystem::path directory(value);
            if (directory.empty() || directory.is_absolute()) {
                configuration.save_states.directory = directory;
            } else {
                core::logger::warn("Ignoring relative save-state directory: {}", value);
            }
        }
        return;
    }
    const std::string full_key = scoped_key(section, key);

    if (full_key == "mapperbus.format" || full_key == "format") {
        return;
    }
    if (full_key == "mapperbus.version" || full_key == "version") {
        configuration.version = parse_int(value, kMapperBusConfigurationVersion, full_key);
        return;
    }
    if (full_key == "frontend.preview_scale" || full_key == "video.preview_scale") {
        configuration.frontend.preview_scale_index =
            std::clamp(parse_int(value, 0, full_key), 0, 3);
        return;
    }
    if (full_key == "frontend.ui_density" || full_key == "ui.density") {
        configuration.frontend.ui_density_index = std::clamp(parse_int(value, 0, full_key), 0, 3);
        return;
    }
    if (full_key == "frontend.audio_muted" || full_key == "audio.muted") {
        configuration.frontend.audio_muted = parse_bool_setting(value, false, full_key);
        return;
    }
    if (full_key == "frontend.settings_page") {
        const int page = parse_int(value, 0, full_key);
        configuration.frontend.settings_page_index = page >= 0 && page <= 3 ? page : 0;
        return;
    }
    if (full_key == "frontend.audio_volume") {
        configuration.frontend.audio_volume_percent =
            std::clamp(parse_int(value, 100, full_key), 0, 100);
        return;
    }
    if (full_key == "frontend.region") {
        configuration.frontend.region = parse_region_preference(value);
        return;
    }
    if (full_key == "fds.bios_path") {
        configuration.fds_bios_path = value;
        return;
    }
    if (full_key == "input.controller_timing") {
        const auto timing = parse_controller_timing(value);
        if (!timing)
            core::logger::warn("Invalid controller timing '{}'; using NES/AV Famicom", value);
        configuration.input.controller_timing =
            timing.value_or(core::ControllerTiming::NesAvFamicom);
        return;
    }
    if (full_key == "input.controller_wiring") {
        const auto wiring = parse_controller_wiring(value);
        if (!wiring)
            core::logger::warn("Invalid controller wiring '{}'; using NES", value);
        configuration.input.controller_wiring = wiring.value_or(core::ControllerWiring::Nes);
        return;
    }
    if (full_key == "microphone.enabled") {
        configuration.microphone.enabled = parse_bool_setting(value, false, full_key);
        return;
    }
    if (full_key == "microphone.device") {
        configuration.microphone.device = value;
        return;
    }
    if (full_key == "microphone.sensitivity") {
        configuration.microphone.sensitivity = std::clamp(parse_int(value, 50, full_key), 1, 100);
        return;
    }
    if (full_key.starts_with("recent.rom_")) {
        const std::string suffix(full_key.substr(std::strlen("recent.rom_")));
        const auto slot = parse_int_value(suffix);
        if (slot.has_value() && *slot >= 0 && *slot < 5) {
            auto& entries = configuration.recent_roms;
            if (entries.size() <= static_cast<std::size_t>(*slot)) {
                entries.resize(static_cast<std::size_t>(*slot) + 1);
            }
            entries[static_cast<std::size_t>(*slot)].path = std::filesystem::path(value);
        }
        return;
    }
    if (full_key.starts_with("recent.member_")) {
        const auto slot = parse_int_value(full_key.substr(std::strlen("recent.member_")));
        if (slot && *slot >= 0 && *slot < 5) {
            auto& entries = configuration.recent_roms;
            if (entries.size() <= static_cast<std::size_t>(*slot)) {
                entries.resize(static_cast<std::size_t>(*slot) + 1);
            }
            entries[static_cast<std::size_t>(*slot)].archive_member = value;
        }
        return;
    }
    if (full_key.starts_with("recent.opened_")) {
        const std::string suffix(full_key.substr(std::strlen("recent.opened_")));
        const auto slot = parse_int_value(suffix);
        const auto stamp = parse_int_value(value);
        if (slot.has_value() && stamp.has_value() && *slot >= 0 && *slot < 5) {
            auto& entries = configuration.recent_roms;
            if (entries.size() <= static_cast<std::size_t>(*slot)) {
                entries.resize(static_cast<std::size_t>(*slot) + 1);
            }
            entries[static_cast<std::size_t>(*slot)].last_opened_unix = *stamp;
        }
        return;
    }
    if (full_key == "audio.sample_rate") {
        configuration.audio.sample_rate =
            std::clamp(parse_int(value, 96000, full_key), 8000, 192000);
        return;
    }
    if (full_key == "audio.buffer_size") {
        configuration.audio.buffer_size_samples =
            std::clamp(parse_int(value, 2048, full_key), 128, 32768);
        return;
    }
    if (full_key == "audio.resampling") {
        configuration.audio.resampling =
            parse_resampling_mode(value, core::ResamplingMode::BlipBuffer, full_key);
        return;
    }
    if (full_key == "audio.filter_mode") {
        configuration.audio.filter_mode =
            parse_filter_mode(value, core::FilterMode::HardwareAccurate, full_key);
        return;
    }
    if (full_key == "audio.filter_profile") {
        configuration.audio.filter_profile =
            parse_filter_profile(value, core::FilterProfile::NES, full_key);
        return;
    }
    if (full_key == "audio.stereo") {
        configuration.audio.stereo_mode =
            parse_stereo_mode(value, core::StereoMode::Mono, full_key);
        return;
    }
    if (full_key == "audio.dithering") {
        configuration.audio.dithering_enabled = parse_bool_setting(value, false, full_key);
        return;
    }
    if (full_key == "audio.expansion_mixing") {
        configuration.audio.expansion_mixing =
            parse_expansion_mixing_mode(value, core::ExpansionMixingMode::SimpleSum, full_key);
        return;
    }
    if (full_key == "audio.drc_target_fill_ratio") {
        configuration.audio.drc_target_fill_ratio =
            static_cast<float>(std::clamp(parse_double(value, 0.5, full_key), 0.0, 1.0));
        return;
    }
    if (full_key == "audio.drc_deadzone") {
        configuration.audio.drc_deadzone =
            static_cast<float>(std::clamp(parse_double(value, 0.05, full_key), 0.0, 1.0));
        return;
    }
    if (full_key == "audio.drc_rate_adjustment" || full_key == "audio.drc_delta") {
        configuration.audio.drc_rate_adjustment =
            std::clamp(parse_double(value, 0.005, full_key), 0.0, 0.05);
        return;
    }
    if (full_key == "input.turbo.period_frames") {
        const int period = parse_int(value, 4, full_key);
        configuration.input.turbo_period_frames = period == 2 || period == 6 ? period : 4;
        return;
    }
    for (int player = 0; player < 2; ++player) {
        auto& keyboard = player == 0 ? configuration.input.keyboard_bindings
                                     : configuration.input.player2_keyboard_bindings;
        auto& gamepad =
            player == 0 ? configuration.input.gamepad : configuration.input.player2_gamepad;
        const std::string prefix = player == 0 ? "input." : "input.player2.";
        if (full_key == prefix + "gamepad.enabled") {
            gamepad.enabled = parse_bool_setting(value, true, full_key);
            return;
        }
        if (full_key == prefix + "gamepad.index") {
            gamepad.gamepad_index = std::max(0, parse_int(value, player, full_key));
            return;
        }
        if (full_key == prefix + "gamepad.deadzone") {
            gamepad.axis_deadzone =
                static_cast<std::int16_t>(std::clamp(parse_int(value, 12000, full_key), 1, 32767));
            return;
        }

        const auto keyboard_prefix = prefix + "keyboard.";
        const auto gamepad_prefix = prefix + "gamepad.";
        for (const auto button : platform::input_actions()) {
            const std::string button_name = controller_button_name(button);
            if (full_key == keyboard_prefix + button_name) {
                keyboard[platform::button_index(button)] = parse_int(
                    value, player == 0 ? default_keyboard_code_for_button(button) : 0, full_key);
                return;
            }
            if (full_key == gamepad_prefix + button_name) {
                auto control = platform::parse_gamepad_control(value);
                if (control) {
                    gamepad.bindings[platform::button_index(button)] = *control;
                } else {
                    core::logger::warn("Invalid gamepad control for configuration key '{}': '{}'",
                                       full_key,
                                       value);
                }
                return;
            }
        }
    }
}

/// Recent ROM paths are user-controlled filenames. Quote them so a newline,
/// '#', or ';' cannot split the INI record or start a comment.
[[nodiscard]] std::string quote_config_value(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char character : text) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out.push_back(character);
            break;
        }
    }
    out.push_back('"');
    return out;
}

/// Parses a quoted value and optional trailing comment. Returns nullopt when
/// the quotes or escapes are not closed.
[[nodiscard]] std::optional<std::string> parse_quoted_config_value(std::string_view text) {
    if (text.size() < 2 || text.front() != '"') {
        return std::nullopt;
    }

    std::string out;
    for (std::size_t index = 1; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '\\') {
            if (index + 1 >= text.size()) {
                return std::nullopt;
            }
            const char escaped = text[++index];
            switch (escaped) {
            case '\\':
                out.push_back('\\');
                break;
            case '"':
                out.push_back('"');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            default:
                return std::nullopt;
            }
            continue;
        }
        if (character == '"') {
            const std::string rest = platform::trim_copy(text.substr(index + 1));
            if (rest.empty() || rest.front() == '#' || rest.front() == ';') {
                return out;
            }
            return std::nullopt;
        }
        out.push_back(character);
    }
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path home_directory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return home;
    }
    if (const char* profile = std::getenv("USERPROFILE");
        profile != nullptr && profile[0] != '\0') {
        return profile;
    }
    return std::filesystem::current_path();
}

[[nodiscard]] std::filesystem::path configuration_directory() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
        return std::filesystem::path(appdata) / "MapperBus";
    }
    return home_directory() / "AppData" / "Roaming" / "MapperBus";
#elif defined(__APPLE__)
    return home_directory() / "Library" / "Application Support" / "MapperBus";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        const std::filesystem::path base(xdg);
        if (base.is_absolute()) {
            return base / "mapperbus";
        }
    }
    return home_directory() / ".config" / "mapperbus";
#endif
}

} // namespace

std::optional<core::ControllerTiming> parse_controller_timing(std::string_view value) {
    if (value == "nes-av-famicom")
        return core::ControllerTiming::NesAvFamicom;
    if (value == "rf-famicom")
        return core::ControllerTiming::RfFamicom;
    return std::nullopt;
}

const char* controller_timing_token(core::ControllerTiming timing) {
    return timing == core::ControllerTiming::RfFamicom ? "rf-famicom" : "nes-av-famicom";
}

std::optional<core::ControllerWiring> parse_controller_wiring(std::string_view value) {
    if (value == "nes")
        return core::ControllerWiring::Nes;
    if (value == "rf-famicom")
        return core::ControllerWiring::RfFamicom;
    return std::nullopt;
}

const char* controller_wiring_token(core::ControllerWiring wiring) {
    return wiring == core::ControllerWiring::RfFamicom ? "rf-famicom" : "nes";
}

MapperBusInputConfiguration::MapperBusInputConfiguration() {
    player2_gamepad.gamepad_index = 1;
    for (const auto button : platform::input_actions()) {
        keyboard_bindings[platform::button_index(button)] =
            default_keyboard_code_for_button(button);
    }
}

std::string controller_button_name(platform::InputAction button) {
    switch (button) {
    case platform::InputAction::Microphone:
        return "microphone";
    case platform::InputAction::TurboA:
        return "turbo_a";
    case platform::InputAction::TurboB:
        return "turbo_b";
    case platform::InputAction::A:
        return "a";
    case platform::InputAction::B:
        return "b";
    case platform::InputAction::Select:
        return "select";
    case platform::InputAction::Start:
        return "start";
    case platform::InputAction::Up:
        return "up";
    case platform::InputAction::Down:
        return "down";
    case platform::InputAction::Left:
        return "left";
    case platform::InputAction::Right:
        return "right";
    }

    return "unknown";
}

std::filesystem::path mapperbus_configuration_path() {
    return configuration_directory() / "mapperbus.conf";
}

MapperBusConfiguration load_mapperbus_configuration() {
    auto configuration = load_mapperbus_configuration_from_file(mapperbus_configuration_path());

    const auto legacy_path = configuration_directory() / "config.ini";
    if (configuration.version == kMapperBusConfigurationVersion &&
        !std::filesystem::exists(mapperbus_configuration_path()) &&
        std::filesystem::exists(legacy_path)) {
        configuration = load_mapperbus_configuration_from_file(legacy_path);
    }

    return configuration;
}

MapperBusConfiguration load_mapperbus_configuration_from_file(const std::filesystem::path& path) {
    MapperBusConfiguration configuration;
    constexpr auto kExtraActions = std::array{platform::InputAction::TurboA,
                                              platform::InputAction::TurboB,
                                              platform::InputAction::Microphone};
    std::array<std::array<bool, kExtraActions.size()>, 2> explicit_extra_keyboard{};
    std::array<std::array<bool, kExtraActions.size()>, 2> explicit_extra_gamepad{};

    std::ifstream file(path);
    if (!file) {
        return configuration;
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = platform::trim_copy(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            section = platform::trim_copy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = platform::trim_copy(std::string_view(trimmed).substr(0, separator));
        std::string raw_value =
            platform::trim_copy(std::string_view(trimmed).substr(separator + 1));
        std::string value;
        if (!raw_value.empty() && raw_value.front() == '"') {
            auto parsed = parse_quoted_config_value(raw_value);
            if (!parsed) {
                core::logger::warn("Skipping configuration value with broken quotes for key '{}'",
                                   key);
                continue;
            }
            value = std::move(*parsed);
        } else {
            // Unquoted values still treat '#' and ';' as comments, so older
            // files keep their previous meaning.
            const std::size_t comment = raw_value.find_first_of("#;");
            if (comment != std::string::npos) {
                raw_value = platform::trim_copy(std::string_view(raw_value).substr(0, comment));
            }
            value = std::move(raw_value);
        }

        apply_setting(configuration, section, key, value);
        const auto full_key = scoped_key(section, key);
        for (int player = 0; player < 2; ++player) {
            const std::string prefix = player == 0 ? "input." : "input.player2.";
            for (std::size_t index = 0; index < kExtraActions.size(); ++index) {
                const auto name = controller_button_name(kExtraActions[index]);
                if (full_key == prefix + "keyboard." + name)
                    explicit_extra_keyboard[player][index] = true;
                if (full_key == prefix + "gamepad." + name)
                    explicit_extra_gamepad[player][index] = true;
            }
        }
    }

    // New defaults must not make an older custom binding activate a second
    // action. Explicitly configured duplicates remain the user's choice.
    for (int player = 0; player < 2; ++player) {
        auto& keyboard = player == 0 ? configuration.input.keyboard_bindings
                                     : configuration.input.player2_keyboard_bindings;
        auto& gamepad =
            player == 0 ? configuration.input.gamepad : configuration.input.player2_gamepad;
        for (std::size_t index = 0; index < kExtraActions.size(); ++index) {
            const auto action = kExtraActions[index];
            const auto target = platform::button_index(action);
            for (auto other : platform::input_actions()) {
                if (other == action)
                    continue;
                const auto source = platform::button_index(other);
                if (!explicit_extra_keyboard[player][index] && keyboard[target] != 0 &&
                    keyboard[target] == keyboard[source]) {
                    keyboard[target] = 0;
                }
                if (!explicit_extra_gamepad[player][index] &&
                    gamepad.bindings[target].kind != platform::GamepadControlKind::None &&
                    gamepad.bindings[target] == gamepad.bindings[source]) {
                    gamepad.bindings[target] = {.kind = platform::GamepadControlKind::None};
                }
            }
        }
    }
    // A keyboard Microphone binding in either profile drives one shared device.
    // Avoid adding it over an older binding in the other profile as well.
    const auto mic = platform::button_index(platform::InputAction::Microphone);
    for (int player = 0; player < 2; ++player) {
        auto& target = player == 0 ? configuration.input.keyboard_bindings
                                   : configuration.input.player2_keyboard_bindings;
        const auto& other = player == 0 ? configuration.input.player2_keyboard_bindings
                                        : configuration.input.keyboard_bindings;
        if (explicit_extra_keyboard[player][2] || target[mic] == 0)
            continue;
        for (const auto action : platform::input_actions())
            if (action != platform::InputAction::Microphone &&
                target[mic] == other[platform::button_index(action)])
                target[mic] = 0;
    }
    return configuration;
}

core::Result<void> save_mapperbus_configuration(const MapperBusConfiguration& configuration) {
    return save_mapperbus_configuration_to_file(configuration, mapperbus_configuration_path());
}

core::Result<void> save_mapperbus_configuration_to_file(const MapperBusConfiguration& configuration,
                                                        const std::filesystem::path& path) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return std::unexpected("failed to create configuration directory: " + error.message());
        }
    }

    const auto temp_path = path.parent_path() / (path.filename().string() + ".tmp");
    std::ofstream file(temp_path, std::ios::trunc);
    if (!file) {
        return std::unexpected("failed to open configuration file for writing");
    }

    file << "# MapperBus universal configuration\n";
    file << "# Format: mapperbus.conf v" << kMapperBusConfigurationVersion << "\n\n";
    file << "[mapperbus]\n";
    file << "format=mapperbus.conf\n";
    file << "version=" << kMapperBusConfigurationVersion << "\n\n";

    file << "[frontend]\n";
    file << "preview_scale=" << configuration.frontend.preview_scale_index << '\n';
    file << "ui_density=" << configuration.frontend.ui_density_index << '\n';
    file << "settings_page=" << configuration.frontend.settings_page_index << '\n';
    file << "audio_muted=" << (configuration.frontend.audio_muted ? "true" : "false") << '\n';
    file << "audio_volume=" << configuration.frontend.audio_volume_percent << '\n';
    file << "region=" << region_preference_token(configuration.frontend.region) << "\n\n";

    file << "[input]\n";
    file << "controller_timing=" << controller_timing_token(configuration.input.controller_timing)
         << '\n';
    file << "controller_wiring=" << controller_wiring_token(configuration.input.controller_wiring)
         << "\n\n";

    file << "[microphone]\n";
    file << "enabled=" << (configuration.microphone.enabled ? "true" : "false") << '\n';
    file << "device=" << quote_config_value(configuration.microphone.device) << '\n';
    file << "sensitivity=" << std::clamp(configuration.microphone.sensitivity, 1, 100) << "\n\n";

    file << "[audio]\n";
    file << "sample_rate=" << configuration.audio.sample_rate << '\n';
    file << "buffer_size=" << configuration.audio.buffer_size_samples << '\n';
    file << "resampling=" << resampling_mode_token(configuration.audio.resampling) << '\n';
    file << "filter_mode=" << filter_mode_token(configuration.audio.filter_mode) << '\n';
    file << "filter_profile=" << filter_profile_token(configuration.audio.filter_profile) << '\n';
    file << "stereo=" << stereo_mode_token(configuration.audio.stereo_mode) << '\n';
    file << "dithering=" << (configuration.audio.dithering_enabled ? "true" : "false") << '\n';
    file << "expansion_mixing=" << expansion_mixing_token(configuration.audio.expansion_mixing)
         << '\n';
    file << "drc_target_fill_ratio=" << configuration.audio.drc_target_fill_ratio << '\n';
    file << "drc_deadzone=" << configuration.audio.drc_deadzone << '\n';
    file << "drc_rate_adjustment=" << configuration.audio.drc_rate_adjustment << "\n\n";

    file << "[fds]\nbios_path=" << quote_config_value(configuration.fds_bios_path) << "\n\n";

    if (!configuration.recent_roms.empty()) {
        file << "[recent]\n";
        for (std::size_t index = 0; index < configuration.recent_roms.size(); ++index) {
            const auto& entry = configuration.recent_roms[index];
            file << "rom_" << index << "=" << quote_config_value(entry.path.string()) << '\n';
            if (!entry.archive_member.empty()) {
                file << "member_" << index << "=" << quote_config_value(entry.archive_member)
                     << '\n';
            }
            file << "opened_" << index << "=" << entry.last_opened_unix << '\n';
        }
        file << '\n';
    }

    file << "[save_states]\n";
    file << "mode="
         << (configuration.save_states.mode == StateStorageMode::Portable ? "portable"
                                                                          : "user-data")
         << '\n';
    file << "directory=" << quote_config_value(configuration.save_states.directory.string())
         << "\n\n";

    if (!configuration.state_slots.empty()) {
        file << "[state_slots]\n";
        for (const auto& [rom, slot] : configuration.state_slots) {
            if (valid_game_identity(rom) && slot >= 0 && slot < kSaveStateSlotCount) {
                file << rom << '=' << slot << '\n';
            }
        }
        file << '\n';
    }

    file << "[input.turbo]\nperiod_frames=" << configuration.input.turbo_period_frames << "\n\n";
    for (int player = 0; player < 2; ++player) {
        const auto& keyboard = player == 0 ? configuration.input.keyboard_bindings
                                           : configuration.input.player2_keyboard_bindings;
        const auto& gamepad =
            player == 0 ? configuration.input.gamepad : configuration.input.player2_gamepad;
        const auto* prefix = player == 0 ? "input." : "input.player2.";
        file << '[' << prefix << "keyboard]\n";
        for (const auto button : platform::input_actions()) {
            file << controller_button_name(button) << '='
                 << keyboard[platform::button_index(button)] << '\n';
        }
        file << '\n';

        file << '[' << prefix << "gamepad]\n";
        file << "enabled=" << (gamepad.enabled ? "true" : "false") << '\n';
        file << "index=" << gamepad.gamepad_index << '\n';
        file << "deadzone=" << gamepad.axis_deadzone << '\n';
        for (const auto button : platform::input_actions()) {
            file << controller_button_name(button) << '='
                 << platform::gamepad_control_token(
                        gamepad.bindings[platform::button_index(button)])
                 << '\n';
        }
        file << '\n';
    }

    if (!file) {
        return std::unexpected("failed to write configuration file");
    }

    file.close();
    if (!file) {
        return std::unexpected("failed to close configuration file");
    }

    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return std::unexpected("failed to replace configuration file: " + error.message());
    }

    return {};
}

int selected_state_slot(const MapperBusConfiguration& configuration, std::string_view identity) {
    const auto entry = configuration.state_slots.find(std::string(identity));
    if (entry == configuration.state_slots.end() || entry->second < 0 ||
        entry->second >= kSaveStateSlotCount) {
        return 0;
    }
    return entry->second;
}

void remember_state_slot(MapperBusConfiguration& configuration,
                         std::string_view identity,
                         int slot) {
    if (valid_game_identity(identity) && slot >= 0 && slot < kSaveStateSlotCount) {
        configuration.state_slots[std::string(identity)] = slot;
    }
}

void touch_recent_rom(MapperBusConfiguration& configuration,
                      const std::filesystem::path& path,
                      std::string_view archive_member) {
    constexpr std::size_t kMaxRecentRoms = 5;
    auto normalized = std::filesystem::weakly_canonical(path);
    if (normalized.empty()) {
        normalized = path.lexically_normal();
    }

    std::erase_if(configuration.recent_roms,
                  [&normalized, archive_member](const RecentRomEntry& entry) {
                      return entry.archive_member == archive_member &&
                             (std::filesystem::weakly_canonical(entry.path) == normalized ||
                              entry.path.lexically_normal() == normalized);
                  });
    RecentRomEntry entry;
    entry.path = path;
    entry.archive_member = archive_member;
    entry.last_opened_unix = static_cast<std::int64_t>(std::time(nullptr));
    configuration.recent_roms.insert(configuration.recent_roms.begin(), std::move(entry));
    if (configuration.recent_roms.size() > kMaxRecentRoms) {
        configuration.recent_roms.resize(kMaxRecentRoms);
    }
}

void remove_recent_rom(MapperBusConfiguration& configuration,
                       const std::filesystem::path& path,
                       std::string_view archive_member) {
    auto normalized = std::filesystem::weakly_canonical(path);
    if (normalized.empty()) {
        normalized = path.lexically_normal();
    }

    std::erase_if(configuration.recent_roms,
                  [&normalized, archive_member](const RecentRomEntry& entry) {
                      return entry.archive_member == archive_member &&
                             (std::filesystem::weakly_canonical(entry.path) == normalized ||
                              entry.path.lexically_normal() == normalized);
                  });
}

void clear_recent_roms(MapperBusConfiguration& configuration) {
    configuration.recent_roms.clear();
}

} // namespace mapperbus::app
