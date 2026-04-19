#pragma once

#include <array>
#include <filesystem>

#include "core/types.hpp"
#include "platform/input/gamepad_config.hpp"

namespace mapperbus::app {

inline constexpr int kMapperBusConfigurationVersion = 1;

struct MapperBusInputConfiguration {
    std::array<int, 8> keyboard_bindings{};
    platform::GamepadInputConfig gamepad;

    MapperBusInputConfiguration();
};

struct MapperBusFrontendConfiguration {
    int preview_scale_index = 0;
    int ui_density_index = 0;
    bool audio_muted = false;
};

struct MapperBusConfiguration {
    int version = kMapperBusConfigurationVersion;
    MapperBusInputConfiguration input;
    MapperBusFrontendConfiguration frontend;
};

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
