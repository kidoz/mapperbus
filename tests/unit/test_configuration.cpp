#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "app/configuration.hpp"

namespace mapperbus::app {
namespace {

[[nodiscard]] std::filesystem::path temporary_config_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("mapperbus-config-test-" + std::to_string(stamp)) / "mapperbus.conf";
}

TEST_CASE("MapperBus configuration round-trips universal settings", "[app][configuration]") {
    MapperBusConfiguration configuration;
    configuration.audio.sample_rate = 48000;
    configuration.audio.resampling = core::ResamplingMode::CubicHermite;
    configuration.audio.filter_mode = core::FilterMode::Enhanced;
    configuration.audio.filter_profile = core::FilterProfile::Famicom;
    configuration.audio.stereo_mode = core::StereoMode::PseudoStereo;
    configuration.audio.dithering_enabled = true;
    configuration.audio.expansion_mixing = core::ExpansionMixingMode::ResistanceModeled;
    configuration.audio.drc_target_fill_ratio = 0.6f;
    configuration.audio.drc_deadzone = 0.08f;
    configuration.audio.drc_rate_adjustment = 0.01;
    configuration.frontend.preview_scale_index = 2;
    configuration.frontend.ui_density_index = 3;
    configuration.frontend.audio_muted = true;
    configuration.input.keyboard_bindings[platform::button_index(core::Button::A)] = 44;
    configuration.input.gamepad.enabled = false;
    configuration.input.gamepad.gamepad_index = 2;
    configuration.input.gamepad.axis_deadzone = 16000;
    configuration.input.gamepad.bindings[platform::button_index(core::Button::Up)] =
        platform::gamepad_axis_control(platform::GamepadAxis::LeftY,
                                       platform::GamepadControlKind::AxisNegative);

    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));

    const auto loaded = load_mapperbus_configuration_from_file(path);
    REQUIRE(loaded.version == kMapperBusConfigurationVersion);
    REQUIRE(loaded.audio.sample_rate == 48000);
    REQUIRE(loaded.audio.resampling == core::ResamplingMode::CubicHermite);
    REQUIRE(loaded.audio.filter_mode == core::FilterMode::Enhanced);
    REQUIRE(loaded.audio.filter_profile == core::FilterProfile::Famicom);
    REQUIRE(loaded.audio.stereo_mode == core::StereoMode::PseudoStereo);
    REQUIRE(loaded.audio.dithering_enabled);
    REQUIRE(loaded.audio.expansion_mixing == core::ExpansionMixingMode::ResistanceModeled);
    REQUIRE(loaded.audio.drc_target_fill_ratio == 0.6f);
    REQUIRE(loaded.audio.drc_deadzone == 0.08f);
    REQUIRE(loaded.audio.drc_rate_adjustment == 0.01);
    REQUIRE(loaded.frontend.preview_scale_index == 2);
    REQUIRE(loaded.frontend.ui_density_index == 3);
    REQUIRE(loaded.frontend.audio_muted);
    REQUIRE(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 44);
    REQUIRE_FALSE(loaded.input.gamepad.enabled);
    REQUIRE(loaded.input.gamepad.gamepad_index == 2);
    REQUIRE(loaded.input.gamepad.axis_deadzone == 16000);

    const auto& up = loaded.input.gamepad.bindings[platform::button_index(core::Button::Up)];
    REQUIRE(up.kind == platform::GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("MapperBus configuration accepts legacy flat settings", "[app][configuration]") {
    const auto path = temporary_config_path().parent_path() / "config.ini";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "video.preview_scale=3\n";
    file << "ui.density=2\n";
    file << "audio.muted=true\n";
    file << "audio.sample_rate=44100\n";
    file << "audio.resampling=cubic\n";
    file << "audio.filter_mode=accurate\n";
    file << "audio.filter_profile=famicom\n";
    file << "audio.stereo=pseudo-stereo\n";
    file << "audio.dithering=true\n";
    file << "audio.expansion_mixing=resistance\n";
    file << "audio.drc_target_fill_ratio=0.4\n";
    file << "audio.drc_deadzone=0.03\n";
    file << "audio.drc_delta=0.002\n";
    file << "input.keyboard.a=44\n";
    file << "input.gamepad.enabled=false\n";
    file << "input.gamepad.index=1\n";
    file << "input.gamepad.deadzone=22000\n";
    file << "input.gamepad.up=lefty-\n";
    file.close();

    const auto loaded = load_mapperbus_configuration_from_file(path);
    REQUIRE(loaded.audio.sample_rate == 44100);
    REQUIRE(loaded.audio.resampling == core::ResamplingMode::CubicHermite);
    REQUIRE(loaded.audio.filter_mode == core::FilterMode::HardwareAccurate);
    REQUIRE(loaded.audio.filter_profile == core::FilterProfile::Famicom);
    REQUIRE(loaded.audio.stereo_mode == core::StereoMode::PseudoStereo);
    REQUIRE(loaded.audio.dithering_enabled);
    REQUIRE(loaded.audio.expansion_mixing == core::ExpansionMixingMode::ResistanceModeled);
    REQUIRE(loaded.audio.drc_target_fill_ratio == 0.4f);
    REQUIRE(loaded.audio.drc_deadzone == 0.03f);
    REQUIRE(loaded.audio.drc_rate_adjustment == 0.002);
    REQUIRE(loaded.frontend.preview_scale_index == 3);
    REQUIRE(loaded.frontend.ui_density_index == 2);
    REQUIRE(loaded.frontend.audio_muted);
    REQUIRE(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 44);
    REQUIRE_FALSE(loaded.input.gamepad.enabled);
    REQUIRE(loaded.input.gamepad.gamepad_index == 1);
    REQUIRE(loaded.input.gamepad.axis_deadzone == 22000);

    const auto& up = loaded.input.gamepad.bindings[platform::button_index(core::Button::Up)];
    REQUIRE(up.kind == platform::GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    std::filesystem::remove_all(path.parent_path());
}

} // namespace
} // namespace mapperbus::app
