#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "app/configuration.hpp"

namespace mapperbus::app {
namespace {

#if defined(__linux__)
TEST_CASE("Linux configuration ignores invalid XDG paths", "[app][configuration]") {
    struct RestoreXdgConfigHome {
        std::optional<std::string> original;
        RestoreXdgConfigHome() {
            if (const auto* value = std::getenv("XDG_CONFIG_HOME")) {
                original = value;
            }
        }
        ~RestoreXdgConfigHome() {
            if (original) {
                (void)setenv("XDG_CONFIG_HOME", original->c_str(), 1);
            } else {
                (void)unsetenv("XDG_CONFIG_HOME");
            }
        }
    } restore;

    REQUIRE(unsetenv("XDG_CONFIG_HOME") == 0);
    const auto fallback = mapperbus_configuration_path();
    REQUIRE(setenv("XDG_CONFIG_HOME", "", 1) == 0);
    REQUIRE(mapperbus_configuration_path() == fallback);
    REQUIRE(setenv("XDG_CONFIG_HOME", "relative/config", 1) == 0);
    REQUIRE(mapperbus_configuration_path() == fallback);
    const auto absolute = std::filesystem::temp_directory_path() / "mapperbus-xdg-test";
    REQUIRE(setenv("XDG_CONFIG_HOME", absolute.c_str(), 1) == 0);
    REQUIRE(mapperbus_configuration_path() == absolute / "mapperbus" / "mapperbus.conf");
}
#endif

[[nodiscard]] std::filesystem::path temporary_config_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("mapperbus-config-test-" + std::to_string(stamp)) / "mapperbus.conf";
}

TEST_CASE("External controller attachments and host profiles persist independently",
          "[app][configuration][expansion]") {
    MapperBusConfiguration configuration;
    CHECK(configuration.input.expansion_controllers == 0);
    for (const int player : {2, 3}) {
        CHECK(configuration.input.gamepad_profile(player).gamepad_index == player);
        for (const auto key : configuration.input.keyboard_profile(player))
            CHECK(key == 0);
        configuration.input.keyboard_profile(player)[platform::button_index(core::Button::A)] =
            40 + player;
        auto& gamepad = configuration.input.gamepad_profile(player);
        gamepad.gamepad_index = 7 + player;
        gamepad.axis_deadzone = 14000 + player;
        gamepad.enabled = player == 2;
        REQUIRE(platform::apply_gamepad_mapping(
            gamepad, player == 2 ? "a=west,turbo_a=north" : "a=leftx-,microphone=rightshoulder"));
    }
    configuration.input.expansion_controllers = 2;
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.input.expansion_controllers == 2);
    for (const int player : {0, 1, 2, 3}) {
        CHECK(restored.input.keyboard_profile(player) ==
              configuration.input.keyboard_profile(player));
        const auto& expected = configuration.input.gamepad_profile(player);
        const auto& actual = restored.input.gamepad_profile(player);
        CHECK(actual.gamepad_index == expected.gamepad_index);
        CHECK(actual.enabled == expected.enabled);
        CHECK(actual.axis_deadzone == expected.axis_deadzone);
        for (std::size_t action = 0; action < platform::kInputActionCount; ++action)
            CHECK(platform::gamepad_control_token(actual.bindings[action]) ==
                  platform::gamepad_control_token(expected.bindings[action]));
    }
    CHECK(parse_expansion_controllers("none") == 0);
    CHECK(parse_expansion_controllers("1") == 1);
    CHECK(parse_expansion_controllers("2") == 2);
    CHECK(parse_expansion_controllers("both") == 3);
    CHECK_FALSE(parse_expansion_controllers("3"));
    CHECK_FALSE(parse_expansion_controllers("invalid"));
    {
        std::ofstream file(path);
        file << "[input]\nexpansion1_attached=invalid\nexpansion2_attached=true\n";
    }
    CHECK(load_mapperbus_configuration_from_file(path).input.expansion_controllers == 2);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Controller mode persists independently and invalid values select Standard",
          "[app][configuration][four-score]") {
    MapperBusConfiguration config;
    CHECK(config.input.controller_mode == core::ControllerMode::Standard);
    config.input.controller_mode = core::ControllerMode::FourScore;
    config.input.expansion_controllers = 3;
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.input.controller_mode == core::ControllerMode::FourScore);
    CHECK(restored.input.expansion_controllers == 3);
    CHECK(parse_controller_mode("standard") == core::ControllerMode::Standard);
    CHECK(parse_controller_mode("four-score") == core::ControllerMode::FourScore);
    CHECK_FALSE(parse_controller_mode("four_score"));
    CHECK(std::string(controller_mode_token(restored.input.controller_mode)) == "four-score");
    {
        std::ofstream file(path);
        file << "[input]\ncontroller_mode=invalid\n";
    }
    CHECK(load_mapperbus_configuration_from_file(path).input.controller_mode ==
          core::ControllerMode::Standard);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Hori mode and all six input profiles roundtrip independently",
          "[app][configuration][hori]") {
    MapperBusConfiguration configuration;
    configuration.input.controller_mode = core::ControllerMode::HoriFourPlayer;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        auto& pad = configuration.input.gamepad_profile(player);
        CHECK(pad.gamepad_index == player);
        if (player >= 2)
            for (const auto key : configuration.input.keyboard_profile(player))
                CHECK(key == 0);
        configuration.input.keyboard_profile(player)[platform::button_index(core::Button::A)] =
            44 + player;
        pad.gamepad_index = player + 6;
        pad.enabled = player % 2 == 0;
        pad.axis_deadzone = 12000 + player;
    }
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.input.controller_mode == core::ControllerMode::HoriFourPlayer);
    CHECK(parse_controller_mode("hori-4-player") == core::ControllerMode::HoriFourPlayer);
    CHECK(std::string(controller_mode_token(restored.input.controller_mode)) == "hori-4-player");
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        CHECK(restored.input.keyboard_profile(player) ==
              configuration.input.keyboard_profile(player));
        CHECK(restored.input.gamepad_profile(player).gamepad_index == player + 6);
        CHECK(restored.input.gamepad_profile(player).enabled == (player % 2 == 0));
        CHECK(restored.input.gamepad_profile(player).axis_deadzone == 12000 + player);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Content-identified slot selections persist independently of recent games",
          "[app][configuration][state]") {
    MapperBusConfiguration configuration;
    const auto path = temporary_config_path();
    const std::string game(64, 'a');
    const std::string other(64, 'b');
    REQUIRE(selected_state_slot(configuration, game) == 0);
    remember_state_slot(configuration, game, 9);
    remember_state_slot(configuration, other, 3);
    clear_recent_roms(configuration);
    remember_state_slot(configuration, game, -1);
    remember_state_slot(configuration, game, 10);
    remember_state_slot(configuration, "not a hash", 2);
    REQUIRE(configuration.state_slots.size() == 2);
    configuration.state_slots["invalid\n[frontend]\npreview_scale=3"] = 1;
    configuration.save_states.mode = StateStorageMode::Portable;
    configuration.save_states.directory = path.parent_path() / "states = #;[x]\n\"";
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    auto restored = load_mapperbus_configuration_from_file(path);
    REQUIRE(restored.recent_roms.empty());
    REQUIRE(restored.state_slots.size() == 2);
    REQUIRE(restored.frontend.preview_scale_index == 0);
    REQUIRE(selected_state_slot(restored, game) == 9);
    REQUIRE(selected_state_slot(restored, other) == 3);
    REQUIRE(restored.save_states.mode == StateStorageMode::Portable);
    REQUIRE(restored.save_states.directory == configuration.save_states.directory);
    remember_state_slot(restored, game, 0);
    REQUIRE(save_mapperbus_configuration_to_file(restored, path));
    REQUIRE(selected_state_slot(load_mapperbus_configuration_from_file(path), game) == 0);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Malformed state storage records use defaults", "[app][configuration][state]") {
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path);
        file << "[state_slots]\nzz=2\n"
             << std::string(64, 'a') << "=-1\n"
             << std::string(64, 'b') << "=10\n"
             << std::string(64, 'c') << "=nope\n"
             << std::string(64, 'd')
             << "=9\n[save_states]\nmode=unknown\ndirectory=relative/path\n";
    }
    const auto configuration = load_mapperbus_configuration_from_file(path);
    REQUIRE(configuration.state_slots.size() == 1);
    REQUIRE(selected_state_slot(configuration, std::string(64, 'd')) == 9);
    REQUIRE(configuration.save_states.mode == StateStorageMode::UserData);
    REQUIRE(configuration.save_states.directory.empty());
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Both input profiles persist independently", "[app][configuration][input]") {
    const auto path = temporary_config_path();
    MapperBusConfiguration config;
    const auto a = platform::button_index(platform::InputAction::A);
    CHECK(config.input.player2_gamepad.gamepad_index == 1);
    for (const auto key : config.input.player2_keyboard_bindings) {
        CHECK(key == 0);
    }
    config.input.keyboard_bindings[a] = 6;
    config.input.player2_keyboard_bindings[a] = 13;
    config.input.player2_keyboard_bindings[platform::button_index(platform::InputAction::TurboB)] =
        14;
    config.input.gamepad.enabled = false;
    config.input.player2_gamepad.enabled = true;
    config.input.player2_gamepad.gamepad_index = 3;
    config.input.player2_gamepad.axis_deadzone = 16000;
    REQUIRE(platform::apply_gamepad_mapping(config.input.player2_gamepad,
                                            "a=leftx+,b=west,turbo_a=none,turbo_b=leftshoulder"));
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    const auto loaded = load_mapperbus_configuration_from_file(path);
    CHECK(loaded.version == 1);
    CHECK(loaded.input.keyboard_bindings == config.input.keyboard_bindings);
    CHECK(loaded.input.player2_keyboard_bindings == config.input.player2_keyboard_bindings);
    CHECK_FALSE(loaded.input.gamepad.enabled);
    CHECK(loaded.input.gamepad.bindings == config.input.gamepad.bindings);
    CHECK(loaded.input.player2_gamepad.enabled);
    CHECK(loaded.input.player2_gamepad.gamepad_index == 3);
    CHECK(loaded.input.player2_gamepad.axis_deadzone == 16000);
    CHECK(loaded.input.player2_gamepad.bindings == config.input.player2_gamepad.bindings);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Controller timing configuration has tolerant defaults and strict option tokens",
          "[app][configuration][input]") {
    CHECK_FALSE(parse_controller_timing("invalid"));
    CHECK_FALSE(parse_controller_timing(""));
    const auto path = temporary_config_path();
    CHECK(load_mapperbus_configuration_from_file(path).input.controller_timing ==
          core::ControllerTiming::NesAvFamicom);
    std::filesystem::create_directories(path.parent_path());
    for (const auto token : {"nes-av-famicom", "rf-famicom", "invalid"}) {
        {
            std::ofstream file(path);
            file << "[input]\ncontroller_timing=" << token << '\n';
        }
        const auto loaded = load_mapperbus_configuration_from_file(path);
        CHECK(loaded.input.controller_timing ==
              parse_controller_timing(token).value_or(core::ControllerTiming::NesAvFamicom));
        REQUIRE(save_mapperbus_configuration_to_file(loaded, path));
        CHECK(load_mapperbus_configuration_from_file(path).input.controller_timing ==
              loaded.input.controller_timing);
        CHECK(parse_controller_timing(controller_timing_token(loaded.input.controller_timing)) ==
              loaded.input.controller_timing);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Controller wiring configuration keeps timing independent and defaults to NES",
          "[app][configuration][input]") {
    CHECK_FALSE(parse_controller_wiring("invalid"));
    CHECK_FALSE(parse_controller_wiring(""));
    const auto path = temporary_config_path();
    CHECK(load_mapperbus_configuration_from_file(path).input.controller_wiring ==
          core::ControllerWiring::Nes);
    std::filesystem::create_directories(path.parent_path());
    for (const auto token : {"nes", "rf-famicom", "invalid"}) {
        {
            std::ofstream file(path);
            file << "[input]\ncontroller_timing=rf-famicom\ncontroller_wiring=" << token << '\n';
        }
        const auto loaded = load_mapperbus_configuration_from_file(path);
        CHECK(loaded.input.controller_wiring ==
              parse_controller_wiring(token).value_or(core::ControllerWiring::Nes));
        CHECK(loaded.input.controller_timing == core::ControllerTiming::RfFamicom);
        REQUIRE(save_mapperbus_configuration_to_file(loaded, path));
        CHECK(load_mapperbus_configuration_from_file(path).input.controller_wiring ==
              loaded.input.controller_wiring);
        CHECK(parse_controller_wiring(controller_wiring_token(loaded.input.controller_wiring)) ==
              loaded.input.controller_wiring);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Older input files keep player one settings and independent player two defaults",
          "[app][configuration][input]") {
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path);
        file << "[input.keyboard]\na=6\n[input.gamepad]\nindex=2\na=west\n";
    }
    const auto loaded = load_mapperbus_configuration_from_file(path);
    const MapperBusInputConfiguration defaults;
    CHECK(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 6);
    CHECK(loaded.input.gamepad.gamepad_index == 2);
    CHECK(loaded.input.player2_keyboard_bindings == defaults.player2_keyboard_bindings);
    CHECK(loaded.input.player2_gamepad.gamepad_index == 1);
    CHECK(loaded.input.player2_gamepad.bindings == defaults.player2_gamepad.bindings);
    {
        std::ofstream file(path);
        file << "[input.player2.keyboard]\na=invalid\nb=0\n"
                "[input.player2.gamepad]\nindex=invalid\ndeadzone=99999\n"
                "enabled=invalid\na=invalid\nb=north\n";
    }
    const auto malformed = load_mapperbus_configuration_from_file(path);
    CHECK(malformed.input.keyboard_bindings == defaults.keyboard_bindings);
    CHECK(malformed.input.player2_keyboard_bindings == defaults.player2_keyboard_bindings);
    CHECK(malformed.input.player2_gamepad.gamepad_index == 1);
    CHECK(malformed.input.player2_gamepad.axis_deadzone == 32767);
    CHECK(malformed.input.player2_gamepad.enabled);
    CHECK(malformed.input.player2_gamepad.bindings[platform::button_index(core::Button::A)] ==
          defaults.player2_gamepad.bindings[platform::button_index(core::Button::A)]);
    CHECK(malformed.input.player2_gamepad
              .bindings[platform::button_index(platform::InputAction::TurboA)]
              .kind == platform::GamepadControlKind::None);
    CHECK(malformed.input.gamepad.bindings == defaults.gamepad.bindings);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Turbo bindings and speed persist with optional version one keys",
          "[app][configuration][turbo]") {
    const auto path = temporary_config_path();
    MapperBusConfiguration config;
    const auto ta = platform::button_index(platform::InputAction::TurboA);
    const auto tb = platform::button_index(platform::InputAction::TurboB);
    REQUIRE(config.input.keyboard_bindings[ta] == 22);
    REQUIRE(config.input.keyboard_bindings[tb] == 4);
    REQUIRE(config.input.gamepad.bindings[ta].button == platform::GamepadButton::North);
    REQUIRE(config.input.gamepad.bindings[tb].button == platform::GamepadButton::West);
    REQUIRE(config.input.turbo_period_frames == 4);
    for (const int period : {2, 4, 6}) {
        config.input.turbo_period_frames = period;
        config.input.keyboard_bindings[ta] = 0; // explicitly unbound
        config.input.keyboard_bindings[tb] = 6;
        config.input.gamepad.bindings[ta] = {.kind = platform::GamepadControlKind::None};
        config.input.gamepad.bindings[tb] = platform::gamepad_axis_control(
            platform::GamepadAxis::RightTrigger, platform::GamepadControlKind::AxisPositive);
        REQUIRE(save_mapperbus_configuration_to_file(config, path));
        const auto loaded = load_mapperbus_configuration_from_file(path);
        CHECK(loaded.version == 1);
        CHECK(loaded.input.keyboard_bindings == config.input.keyboard_bindings);
        CHECK(loaded.input.gamepad.bindings == config.input.gamepad.bindings);
        CHECK(loaded.input.turbo_period_frames == period);
    }
    for (const auto* value : {"0", "-2", "3", "1000000", "garbage"}) {
        std::ofstream file(path);
        file << "[input.keyboard]\na=44\n[input.turbo]\nperiod_frames=" << value << '\n';
        file.close();
        const auto loaded = load_mapperbus_configuration_from_file(path);
        CHECK(loaded.input.turbo_period_frames == 4);
        CHECK(loaded.input.keyboard_bindings[ta] == 22);
        CHECK(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 44);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("New turbo defaults do not overlap existing custom bindings",
          "[app][configuration][turbo]") {
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "[input.keyboard]\na=22\nleft=4\n[input.gamepad]\nb=north\nup=west\n";
    file.close();
    auto loaded = load_mapperbus_configuration_from_file(path);
    for (auto action : {platform::InputAction::TurboA, platform::InputAction::TurboB}) {
        const auto index = platform::button_index(action);
        CHECK(loaded.input.keyboard_bindings[index] == 0);
        CHECK(loaded.input.gamepad.bindings[index].kind == platform::GamepadControlKind::None);
    }
    CHECK(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 22);
    CHECK(loaded.input.gamepad.bindings[platform::button_index(core::Button::B)].button ==
          platform::GamepadButton::North);
    // An explicit duplicate is retained rather than silently remapped.
    std::ofstream explicit_file(path, std::ios::app);
    explicit_file << "[input.keyboard]\nturbo_a=22\n[input.gamepad]\nturbo_a=north\n";
    explicit_file.close();
    loaded = load_mapperbus_configuration_from_file(path);
    CHECK(loaded.input.keyboard_bindings[platform::button_index(platform::InputAction::TurboA)] ==
          22);
    CHECK(loaded.input.gamepad.bindings[platform::button_index(platform::InputAction::TurboA)]
              .button == platform::GamepadButton::North);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Microphone bindings persist and legacy keys keep their meaning",
          "[configuration][microphone]") {
    const auto mic = platform::button_index(platform::InputAction::Microphone);
    const auto path = temporary_config_path();
    MapperBusConfiguration config;
    CHECK(config.input.keyboard_bindings[mic] == 16);
    CHECK(config.input.player2_keyboard_bindings[mic] == 0);
    CHECK(config.input.gamepad.bindings[mic].kind == platform::GamepadControlKind::None);
    config.input.player2_keyboard_bindings[mic] = 17;
    config.input.player2_gamepad.bindings[mic] =
        platform::gamepad_button_control(platform::GamepadButton::RightShoulder);
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    auto loaded = load_mapperbus_configuration_from_file(path);
    CHECK(loaded.input.player2_keyboard_bindings[mic] == 17);
    CHECK(loaded.input.player2_gamepad.bindings[mic] == config.input.player2_gamepad.bindings[mic]);
    for (const auto prefix : {"input", "input.player2"}) {
        {
            std::ofstream file(path);
            file << '[' << prefix << ".keyboard]\na=16\n";
        }
        loaded = load_mapperbus_configuration_from_file(path);
        CHECK(loaded.input.keyboard_bindings[mic] == 0);
    }
    {
        std::ofstream file(path);
        file << "[input.keyboard]\na=16\nmicrophone=16\n";
    }
    loaded = load_mapperbus_configuration_from_file(path);
    CHECK(loaded.input.keyboard_bindings[mic] == 16);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Live microphone defaults stay disabled and sensitivity is bounded",
          "[configuration][microphone]") {
    const auto path = temporary_config_path();
    const auto defaults = load_mapperbus_configuration_from_file(path).microphone;
    CHECK_FALSE(defaults.enabled);
    CHECK(defaults.device.empty());
    CHECK(defaults.sensitivity == 50);
    CHECK_FALSE(defaults.audio_enabled);
    CHECK(defaults.volume_percent == 25);
    std::filesystem::create_directories(path.parent_path());
    struct Scenario {
        const char* token;
        int sensitivity;
        int volume;
    };
    for (const auto& [token, expected, volume] :
         {Scenario{"bad", 50, 25}, {"-1", 1, 0}, {"0", 1, 0}, {"101", 100, 100}, {"72", 72, 72}}) {
        {
            std::ofstream file(path);
            file << "[microphone]\nenabled=invalid\naudio_enabled=invalid\nsensitivity=" << token
                 << "\nvolume=" << token << '\n';
        }
        const auto loaded = load_mapperbus_configuration_from_file(path);
        CHECK_FALSE(loaded.microphone.enabled);
        CHECK(loaded.microphone.sensitivity == expected);
        CHECK(loaded.microphone.device.empty());
        CHECK_FALSE(loaded.microphone.audio_enabled);
        CHECK(loaded.microphone.volume_percent == volume);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("MapperBus configuration round-trips universal settings", "[app][configuration]") {
    // Capture preferences are host settings, separate from machine snapshots.
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
    configuration.frontend.settings_page_index = 3;
    configuration.frontend.audio_muted = true;
    configuration.frontend.audio_volume_percent = 40;
    configuration.frontend.region = RegionPreference::Pal;
    configuration.microphone = {.enabled = true, .device = "Input #1; \"USB\"", .sensitivity = 72};
    configuration.microphone.audio_enabled = true;
    configuration.microphone.volume_percent = 63;
    configuration.input.controller_timing = core::ControllerTiming::RfFamicom;
    configuration.input.controller_wiring = core::ControllerWiring::RfFamicom;
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
    REQUIRE(loaded.frontend.settings_page_index == 3);
    REQUIRE(loaded.frontend.audio_muted);
    REQUIRE(loaded.frontend.audio_volume_percent == 40);
    REQUIRE(loaded.frontend.region == RegionPreference::Pal);
    CHECK(loaded.microphone.enabled);
    CHECK(loaded.microphone.device == configuration.microphone.device);
    CHECK(loaded.microphone.sensitivity == 72);
    CHECK(loaded.microphone.audio_enabled);
    CHECK(loaded.microphone.volume_percent == 63);
    REQUIRE(loaded.input.controller_timing == core::ControllerTiming::RfFamicom);
    REQUIRE(loaded.input.controller_wiring == core::ControllerWiring::RfFamicom);
    REQUIRE(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 44);
    REQUIRE_FALSE(loaded.input.gamepad.enabled);
    REQUIRE(loaded.input.gamepad.gamepad_index == 2);
    REQUIRE(loaded.input.gamepad.axis_deadzone == 16000);

    const auto& up = loaded.input.gamepad.bindings[platform::button_index(core::Button::Up)];
    REQUIRE(up.kind == platform::GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Settings pane defaults safely for missing and invalid preferences",
          "[app][configuration]") {
    CHECK(MapperBusConfiguration{}.frontend.settings_page_index == 0);
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    for (const auto* value : {"0", "1", "2", "3", "-1", "4", "invalid"}) {
        {
            std::ofstream file(path);
            file << "[frontend]\nsettings_page=" << value << '\n';
        }
        const auto loaded = load_mapperbus_configuration_from_file(path);
        const std::string pane(value);
        const int expected = pane == "1" ? 1 : pane == "2" ? 2 : pane == "3" ? 3 : 0;
        CHECK(loaded.frontend.settings_page_index == expected);
    }
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Recent ROM entries can be pruned individually", "[app][configuration]") {
    MapperBusConfiguration configuration;
    const auto existing = std::filesystem::temp_directory_path() / "mapperbus-recent-kept.nes";
    const auto missing = std::filesystem::temp_directory_path() / "mapperbus-recent-deleted.nes";
    {
        std::ofstream file(existing);
        file << "not a real rom";
    }
    std::filesystem::remove(missing);

    touch_recent_rom(configuration, existing);
    touch_recent_rom(configuration, missing);
    REQUIRE(configuration.recent_roms.size() == 2);

    // Pruning a missing file (e.g. after a failed open) removes only that
    // entry and matches through path normalization.
    remove_recent_rom(configuration, missing);
    REQUIRE(configuration.recent_roms.size() == 1);
    REQUIRE(configuration.recent_roms.front().path == existing);

    // Removing a path that was never recorded is a no-op.
    remove_recent_rom(configuration,
                      std::filesystem::temp_directory_path() / "mapperbus-recent-unknown.nes");
    REQUIRE(configuration.recent_roms.size() == 1);

    std::filesystem::remove(existing);
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
    REQUIRE(loaded.frontend.audio_volume_percent == 100);
    REQUIRE(loaded.frontend.region == RegionPreference::Automatic);
    REQUIRE(loaded.input.keyboard_bindings[platform::button_index(core::Button::A)] == 44);
    REQUIRE_FALSE(loaded.input.gamepad.enabled);
    REQUIRE(loaded.input.gamepad.gamepad_index == 1);
    REQUIRE(loaded.input.gamepad.axis_deadzone == 22000);

    const auto& up = loaded.input.gamepad.bindings[platform::button_index(core::Button::Up)];
    REQUIRE(up.kind == platform::GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Frontend volume and region preferences clamp invalid values", "[app][configuration]") {
    const auto path = temporary_config_path();
    {
        std::ofstream file(path);
        file << "[frontend]\n";
        file << "audio_volume=400\n";
        file << "region=nope\n";
    }

    const auto loaded = load_mapperbus_configuration_from_file(path);
    REQUIRE(loaded.frontend.audio_volume_percent == 100);
    REQUIRE(loaded.frontend.region == RegionPreference::Automatic);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Recent ROM paths round-trip without injecting INI records", "[app][configuration]") {
    MapperBusConfiguration configuration;
    const std::string nasty =
        std::string("/tmp/mapperbus/a#b;c") + "\n[frontend]\naudio_volume=0.nes";
    configuration.recent_roms.push_back(RecentRomEntry{std::filesystem::path(nasty), 42});

    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));

    const auto loaded = load_mapperbus_configuration_from_file(path);
    REQUIRE(loaded.recent_roms.size() == 1);
    REQUIRE(loaded.recent_roms.front().path.string() == nasty);
    REQUIRE(loaded.recent_roms.front().last_opened_unix == 42);
    REQUIRE(loaded.frontend.audio_volume_percent == 100);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Unquoted configuration values still honor inline comments", "[app][configuration]") {
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path);
        file << "[frontend]\n";
        file << "audio_volume=40 # loud\n";
        file << "[recent]\n";
        file << "rom_0=/tmp/plain.nes # note\n";
        file << "opened_0=7\n";
    }

    const auto loaded = load_mapperbus_configuration_from_file(path);
    REQUIRE(loaded.frontend.audio_volume_percent == 40);
    REQUIRE(loaded.recent_roms.size() == 1);
    REQUIRE(loaded.recent_roms.front().path == std::filesystem::path("/tmp/plain.nes"));
    REQUIRE(loaded.recent_roms.front().last_opened_unix == 7);

    std::filesystem::remove_all(path.parent_path());
}

} // namespace
} // namespace mapperbus::app

TEST_CASE("Recent archive members round-trip and deduplicate independently",
          "[app][configuration]") {
    using namespace mapperbus::app;
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-archive-recents-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "collection.zip";
    {
        std::ofstream file(path);
    }
    MapperBusConfiguration configuration;
    touch_recent_rom(configuration, path, "one.nes");
    touch_recent_rom(configuration, path, "folder/two#;\n.nes");
    touch_recent_rom(configuration, path, "one.nes");
    REQUIRE(configuration.recent_roms.size() == 2);
    CHECK(configuration.recent_roms.front().archive_member == "one.nes");
    REQUIRE(save_mapperbus_configuration_to_file(configuration, root / "mapperbus.conf"));
    auto loaded = load_mapperbus_configuration_from_file(root / "mapperbus.conf");
    REQUIRE(loaded.recent_roms.size() == 2);
    CHECK(loaded.recent_roms.back().archive_member == "folder/two#;\n.nes");
    remove_recent_rom(loaded, path, "one.nes");
    REQUIRE(loaded.recent_roms.size() == 1);
    CHECK(loaded.recent_roms.front().archive_member == "folder/two#;\n.nes");
    std::filesystem::remove_all(root);
}

namespace mapperbus::app {
TEST_CASE("FDS BIOS configuration round trips quoted paths", "[app][configuration][fds]") {
    const auto path = temporary_config_path();
    MapperBusConfiguration config;
    config.fds_bios_path = "/bios = #;[x] \"disk\".bin";
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    CHECK(load_mapperbus_configuration_from_file(path).fds_bios_path == config.fds_bios_path);
    std::filesystem::remove_all(path.parent_path());
}
} // namespace mapperbus::app
