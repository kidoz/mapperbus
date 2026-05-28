#include "app/configuration.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "core/logger.hpp"

namespace mapperbus::app {
namespace {

[[nodiscard]] int default_keyboard_code_for_button(core::Button button) {
    switch (button) {
    case core::Button::A:
        return 27; // NodalKit X / USB HID X
    case core::Button::B:
        return 29; // NodalKit Z / USB HID Z
    case core::Button::Select:
        return 229; // Right Shift
    case core::Button::Start:
        return 40; // Return
    case core::Button::Up:
        return 82;
    case core::Button::Down:
        return 81;
    case core::Button::Left:
        return 80;
    case core::Button::Right:
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
            parse_filter_mode(value, core::FilterMode::Unfiltered, full_key);
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
    if (full_key == "input.gamepad.enabled") {
        configuration.input.gamepad.enabled = parse_bool_setting(value, true, full_key);
        return;
    }
    if (full_key == "input.gamepad.index") {
        configuration.input.gamepad.gamepad_index = std::max(0, parse_int(value, 0, full_key));
        return;
    }
    if (full_key == "input.gamepad.deadzone") {
        configuration.input.gamepad.axis_deadzone =
            static_cast<std::int16_t>(std::clamp(parse_int(value, 12000, full_key), 1, 32767));
        return;
    }

    for (const auto button : platform::controller_buttons()) {
        const std::string button_name = controller_button_name(button);
        if (full_key == "input.keyboard." + button_name) {
            configuration.input.keyboard_bindings[platform::button_index(button)] =
                parse_int(value, default_keyboard_code_for_button(button), full_key);
            return;
        }
        if (full_key == "input.gamepad." + button_name) {
            auto control = platform::parse_gamepad_control(value);
            if (control) {
                configuration.input.gamepad.bindings[platform::button_index(button)] = *control;
            } else {
                core::logger::warn(
                    "Invalid gamepad control for configuration key '{}': '{}'", full_key, value);
            }
            return;
        }
    }
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
        return std::filesystem::path(xdg) / "mapperbus";
    }
    return home_directory() / ".config" / "mapperbus";
#endif
}

} // namespace

MapperBusInputConfiguration::MapperBusInputConfiguration() {
    for (const auto button : platform::controller_buttons()) {
        keyboard_bindings[platform::button_index(button)] =
            default_keyboard_code_for_button(button);
    }
}

std::string controller_button_name(core::Button button) {
    switch (button) {
    case core::Button::A:
        return "a";
    case core::Button::B:
        return "b";
    case core::Button::Select:
        return "select";
    case core::Button::Start:
        return "start";
    case core::Button::Up:
        return "up";
    case core::Button::Down:
        return "down";
    case core::Button::Left:
        return "left";
    case core::Button::Right:
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

        const std::size_t comment = trimmed.find_first_of("#;");
        if (comment != std::string::npos) {
            trimmed = platform::trim_copy(std::string_view(trimmed).substr(0, comment));
        }

        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            section = platform::trim_copy(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        apply_setting(configuration,
                      section,
                      platform::trim_copy(std::string_view(trimmed).substr(0, separator)),
                      platform::trim_copy(std::string_view(trimmed).substr(separator + 1)));
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
    file << "audio_muted=" << (configuration.frontend.audio_muted ? "true" : "false") << "\n\n";

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

    file << "[input.keyboard]\n";
    for (const auto button : platform::controller_buttons()) {
        file << controller_button_name(button) << '='
             << configuration.input.keyboard_bindings[platform::button_index(button)] << '\n';
    }
    file << '\n';

    file << "[input.gamepad]\n";
    file << "enabled=" << (configuration.input.gamepad.enabled ? "true" : "false") << '\n';
    file << "index=" << configuration.input.gamepad.gamepad_index << '\n';
    file << "deadzone=" << configuration.input.gamepad.axis_deadzone << '\n';
    for (const auto button : platform::controller_buttons()) {
        file << controller_button_name(button) << '='
             << platform::gamepad_control_token(
                    configuration.input.gamepad.bindings[platform::button_index(button)])
             << '\n';
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

} // namespace mapperbus::app
