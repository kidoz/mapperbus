#include "app/configuration.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

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

[[nodiscard]] int parse_int(std::string_view value, int fallback) {
    try {
        std::size_t consumed = 0;
        const std::string text(value);
        const int parsed = std::stoi(text, &consumed);
        if (consumed == text.size()) {
            return parsed;
        }
    } catch (...) {
    }
    return fallback;
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
        configuration.version = parse_int(value, kMapperBusConfigurationVersion);
        return;
    }
    if (full_key == "frontend.preview_scale" || full_key == "video.preview_scale") {
        configuration.frontend.preview_scale_index = std::clamp(parse_int(value, 0), 0, 3);
        return;
    }
    if (full_key == "frontend.ui_density" || full_key == "ui.density") {
        configuration.frontend.ui_density_index = std::clamp(parse_int(value, 0), 0, 3);
        return;
    }
    if (full_key == "frontend.audio_muted" || full_key == "audio.muted") {
        configuration.frontend.audio_muted = parse_bool(value, false);
        return;
    }
    if (full_key == "input.gamepad.enabled") {
        configuration.input.gamepad.enabled = parse_bool(value, true);
        return;
    }
    if (full_key == "input.gamepad.index") {
        configuration.input.gamepad.gamepad_index = std::max(0, parse_int(value, 0));
        return;
    }
    if (full_key == "input.gamepad.deadzone") {
        configuration.input.gamepad.axis_deadzone =
            static_cast<std::int16_t>(std::clamp(parse_int(value, 12000), 1, 32767));
        return;
    }

    for (const auto button : platform::controller_buttons()) {
        const std::string button_name = controller_button_name(button);
        if (full_key == "input.keyboard." + button_name) {
            configuration.input.keyboard_bindings[platform::button_index(button)] =
                parse_int(value, default_keyboard_code_for_button(button));
            return;
        }
        if (full_key == "input.gamepad." + button_name) {
            auto control = platform::parse_gamepad_control(value);
            if (control) {
                configuration.input.gamepad.bindings[platform::button_index(button)] = *control;
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

    std::ofstream file(path, std::ios::trunc);
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

    return {};
}

} // namespace mapperbus::app
