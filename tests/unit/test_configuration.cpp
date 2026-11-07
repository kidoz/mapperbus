#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "app/async_configuration_save.hpp"
#include "app/configuration.hpp"
#include "core/binary_file.hpp"

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

TEST_CASE("Per-game device assignments preserve global choices and shared controls",
          "[app][configuration][game-assignments]") {
    MapperBusConfiguration config;
    const std::string first(64, 'a'), second(64, 'b');
    const auto globals = gamepad_assignments_for_game(config, {});
    auto a = globals, b = globals;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        a[player].gamepad_index = 5 - player;
        a[player].enabled = player >= 2;
        a[player].identity = {.guid = "03000000341200000100000000000000",
                              .serial = "pad \"" + std::to_string(player) + "\"; #serial\\value",
                              .name = "Saved device " + std::to_string(player),
                              .ambiguous = player % 2 == 0};
        b[player].gamepad_index = player + 10;
    }
    config.game_gamepads[first] = a;
    config.game_gamepads[second] = b;
    auto active = config.input.gamepad_profile(3);
    platform::apply_gamepad_assignment(active, a[3]);
    active.gamepad_index = 17;
    active.identity.serial = "discovered serial";
    active.axis_deadzone = 19000;
    REQUIRE(platform::apply_gamepad_mapping(active, "a=west,b=north"));
    remember_gamepad_profile(config, first, 3, active);
    a[3] = platform::gamepad_assignment(active);
    CHECK(gamepad_assignments_for_game(config, {}) == globals);
    CHECK(gamepad_assignments_for_game(config, first) == a);
    CHECK(gamepad_assignments_for_game(config, second) == b);
    CHECK(config.input.gamepad_profile(3).bindings == active.bindings);
    CHECK(config.input.gamepad_profile(3).axis_deadzone == 19000);
    CHECK(gamepad_assignments_for_game(config, std::string(64, 'c')) == globals);
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.game_gamepads == config.game_gamepads);
    CHECK(gamepad_assignments_for_game(restored, {}) == globals);
    CHECK(restored.input.gamepad_profile(3).bindings == active.bindings);
    CHECK(restored.input.gamepad_profile(3).axis_deadzone == active.axis_deadzone);
    auto discovered = a[3];
    discovered.gamepad_index = 1;
    discovered.identity.ambiguous = true;
    remember_gamepad_assignment(restored, first, 3, discovered);
    CHECK(gamepad_assignments_for_game(restored, {}) == globals);
    CHECK(gamepad_assignments_for_game(restored, second) == b);
    CHECK(gamepad_assignments_for_game(restored, first)[3] == discovered);
    restored.game_gamepads.erase(first);
    REQUIRE(save_mapperbus_configuration_to_file(restored, path));
    restored = load_mapperbus_configuration_from_file(path);
    CHECK(gamepad_assignments_for_game(restored, first) == globals);
    CHECK(gamepad_assignments_for_game(restored, second) == b);
    CHECK(restored.version == kMapperBusConfigurationVersion);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE(
    "Partial game assignments inherit final globals and ignore controls and malformed records",
    "[app][configuration][game-assignments]") {
    const std::string valid(64, 'a'), invalid(64, 'b');
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "[game_gamepads." << valid << ".player3]\nindex=2\na=west\ndeadzone=20000\n"
         << "[game_gamepads." << valid << ".player1]\nguid=invalid\nserial=orphan\nambiguous=true\n"
         << "[game_gamepads." << invalid << ".player1]\nindex=-1\nenabled=broken\nunknown=ignored\n"
         << "[game_gamepads." << invalid << ".player7]\nindex=2\n"
         << "[game_gamepads.invalid.player1]\nindex=3\n"
         << "[input.player3.gamepad]\nindex=9\nenabled=false\ndeadzone=12345\n"
         << "guid=03000000341200000100000000000000\nserial=global\n";
    file.close();
    auto config = load_mapperbus_configuration_from_file(path);
    REQUIRE(config.game_gamepads.size() == 1);
    auto assignments = gamepad_assignments_for_game(config, valid);
    CHECK(assignments[2].gamepad_index == 2);
    CHECK_FALSE(assignments[2].enabled);
    CHECK(assignments[2].identity == config.input.gamepad_profile(2).identity);
    CHECK(assignments[0].identity == platform::GamepadIdentity{});
    CHECK(config.input.gamepad_profile(2).gamepad_index == 9);
    CHECK(config.input.gamepad_profile(2).axis_deadzone == 12345);
    CHECK(config.input.gamepad_profile(2).bindings ==
          MapperBusInputConfiguration{}.gamepad_profile(2).bindings);
    REQUIRE(save_mapperbus_configuration_to_file(config, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.game_gamepads == config.game_gamepads);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Per-game controller hardware survives saves without changing defaults or host inputs",
          "[app][configuration][game-controllers]") {
    MapperBusConfiguration configuration;
    const std::string first(64, 'a');
    const std::string second(64, 'b');
    const auto global = controller_hardware(configuration.input);
    const ControllerHardwareSettings hori{core::ControllerMode::HoriFourPlayer,
                                          core::ControllerWiring::RfFamicom,
                                          core::ControllerTiming::RfFamicom,
                                          3};
    const ControllerHardwareSettings external{core::ControllerMode::Standard,
                                              core::ControllerWiring::RfFamicom,
                                              core::ControllerTiming::NesAvFamicom,
                                              1};
    remember_controller_hardware(configuration, first, hori);
    remember_controller_hardware(configuration, second, external);
    remember_controller_hardware(configuration, "invalid", hori);
    REQUIRE(configuration.game_controllers.size() == 2);
    CHECK(controller_hardware(configuration.input) == global);
    CHECK(controller_hardware_for_game(configuration, first) == hori);
    CHECK(controller_hardware_for_game(configuration, second) == external);
    CHECK(controller_hardware_for_game(configuration, std::string(64, 'c')) == global);
    CHECK(controller_hardware_for_game(configuration, "") == global);

    // Explicit zeros are real overrides, and CLI options replace only their field.
    ControllerHardwareOverrides cli;
    cli.mode = core::ControllerMode::Standard;
    cli.expansion_controllers = 0;
    auto expected = hori;
    expected.mode = core::ControllerMode::Standard;
    expected.expansion_controllers = 0;
    CHECK(controller_hardware_for_game(configuration, first, cli) == expected);
    cli.wiring = core::ControllerWiring::Nes;
    cli.timing = core::ControllerTiming::NesAvFamicom;
    CHECK(controller_hardware_for_game(configuration, first, cli) == global);
    CHECK(controller_hardware_for_game(configuration, first) == hori);

    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        configuration.input.keyboard_profile(player)[0] = 70 + player;
        configuration.input.gamepad_profile(player).gamepad_index = 5 - player;
        configuration.input.gamepad_profile(player).axis_deadzone = 1000 + player;
    }
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.version == kMapperBusConfigurationVersion);
    CHECK(restored.game_controllers == configuration.game_controllers);
    CHECK(controller_hardware(restored.input) == global);
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        CHECK(restored.input.keyboard_profile(player) ==
              configuration.input.keyboard_profile(player));
        const auto& actual = restored.input.gamepad_profile(player);
        const auto& expected_gamepad = configuration.input.gamepad_profile(player);
        CHECK(actual.enabled == expected_gamepad.enabled);
        CHECK(actual.gamepad_index == expected_gamepad.gamepad_index);
        CHECK(actual.axis_deadzone == expected_gamepad.axis_deadzone);
        CHECK(actual.bindings == expected_gamepad.bindings);
        CHECK(actual.identity == expected_gamepad.identity);
    }

    // Removing one game returns it to current global settings, including after restart.
    auto new_global = global;
    new_global.mode = core::ControllerMode::FourScore;
    set_controller_hardware(restored.input, new_global);
    restored.game_controllers.erase(first);
    REQUIRE(save_mapperbus_configuration_to_file(restored, path));
    restored = load_mapperbus_configuration_from_file(path);
    CHECK(controller_hardware_for_game(restored, first) == new_global);
    CHECK(controller_hardware_for_game(restored, second) == external);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Partial game controller records inherit globals and reject malformed fields",
          "[app][configuration][game-controllers]") {
    const std::string first(64, 'a');
    const std::string invalid_only(64, 'b');
    const std::string zero(64, 'c');
    const auto path = temporary_config_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    // Globals appear later deliberately; missing fields inherit their final values.
    file << "[game_controllers." << first << "]\ncontroller_mode=hori-4-player\n"
         << "controller_wiring=broken\ncontroller_timing=broken\nexpansion_controllers=3\n"
         << "unknown=ignored\n[game_controllers." << invalid_only << "]\n"
         << "controller_mode=broken\nunknown=ignored\n"
         << "[game_controllers.invalid]\ncontroller_mode=four-score\n"
         << "[game_controllers." << zero << "]\nexpansion_controllers=none\n"
         << "[input]\ncontroller_wiring=rf-famicom\ncontroller_timing=rf-famicom\n"
         << "expansion1_attached=true\nexpansion2_attached=true\n";
    file.close();
    auto restored = load_mapperbus_configuration_from_file(path);
    REQUIRE(restored.game_controllers.size() == 2);
    auto expected = controller_hardware(restored.input);
    expected.mode = core::ControllerMode::HoriFourPlayer;
    CHECK(controller_hardware_for_game(restored, first) == expected);
    CHECK_FALSE(restored.game_controllers.contains(invalid_only));
    CHECK(controller_hardware_for_game(restored, zero).expansion_controllers == 0);
    // Invalid in-memory keys and empty overrides cannot create durable records.
    restored.game_controllers["invalid"] = {.mode = core::ControllerMode::FourScore};
    restored.game_controllers[invalid_only] = {};
    REQUIRE(save_mapperbus_configuration_to_file(restored, path));
    const auto reread = load_mapperbus_configuration_from_file(path);
    CHECK(reread.game_controllers.size() == 2);
    CHECK_FALSE(reread.game_controllers.at(first).wiring.has_value());
    CHECK(controller_hardware_for_game(reread, first) == expected);
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Gamepad identities roundtrip with quoted serials and legacy defaults",
          "[app][configuration][identity]") {
    MapperBusConfiguration configuration;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        configuration.input.gamepad_profile(player).identity = {
            .guid = "03000000341200000100000000000000",
            .serial = "pad \"" + std::to_string(player) + "\"; #serial\\value",
            .name = "Saved controller " + std::to_string(player),
            .ambiguous = player % 2 == 0};
    }
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    for (int player = 0; player < platform::kInputProfileCount; ++player)
        CHECK(restored.input.gamepad_profile(player).identity ==
              configuration.input.gamepad_profile(player).identity);
    configuration.input.configure_four_hori_gamepads();
    CHECK(configuration.input.gamepad.identity == restored.input.gamepad.identity);
    for (int player = 2; player < platform::kInputProfileCount; ++player)
        CHECK(configuration.input.gamepad_profile(player).identity == platform::GamepadIdentity{});
    {
        std::ofstream file(path);
        file << "[input.gamepad]\nindex=3\nguid=invalid\n"
                "[input.player2.gamepad]\nindex=1\n";
    }
    const auto legacy = load_mapperbus_configuration_from_file(path);
    CHECK(legacy.input.gamepad.identity.guid.empty());
    CHECK(legacy.input.gamepad.gamepad_index == 3);
    CHECK(legacy.input.player2_gamepad.identity == platform::GamepadIdentity{});
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("Four Hori gamepads preset preserves bindings and persists device assignments",
          "[app][configuration][hori]") {
    MapperBusConfiguration configuration;
    configuration.input.expansion_controllers = 1;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        auto& pad = configuration.input.gamepad_profile(player);
        pad.enabled = false;
        pad.gamepad_index = 10 + player;
        pad.axis_deadzone = 12000 + player;
        REQUIRE(
            platform::apply_gamepad_mapping(pad, "a=west,turbo_a=north,microphone=rightshoulder"));
        configuration.input.keyboard_profile(player)[platform::button_index(core::Button::A)] =
            40 + player;
    }
    const auto before = configuration.input;
    configuration.input.configure_four_hori_gamepads();
    configuration.input.configure_four_hori_gamepads(); // Reapplying is safe.
    const auto path = temporary_config_path();
    REQUIRE(save_mapperbus_configuration_to_file(configuration, path));
    const auto restored = load_mapperbus_configuration_from_file(path);
    CHECK(restored.input.controller_mode == before.controller_mode);
    CHECK(restored.input.controller_timing == before.controller_timing);
    CHECK(restored.input.controller_wiring == before.controller_wiring);
    CHECK(restored.input.expansion_controllers == before.expansion_controllers);
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        CAPTURE(player);
        const auto& actual = restored.input.gamepad_profile(player);
        const auto& original = before.gamepad_profile(player);
        CHECK(actual.enabled == (player >= 2));
        CHECK(actual.gamepad_index == (player >= 2 ? player - 2 : original.gamepad_index));
        CHECK(actual.axis_deadzone == original.axis_deadzone);
        CHECK(restored.input.keyboard_profile(player) == before.keyboard_profile(player));
        for (const auto action : platform::input_actions())
            CHECK(platform::gamepad_control_token(
                      platform::gamepad_control_for_button(actual, action)) ==
                  platform::gamepad_control_token(
                      platform::gamepad_control_for_button(original, action)));
    }
    std::filesystem::remove_all(path.parent_path());
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

namespace {
struct ConfigurationFixture {
    std::filesystem::path path = temporary_config_path();
    ~ConfigurationFixture() {
        std::error_code error;
        std::filesystem::remove_all(path.parent_path(), error);
    }
};
struct ConfigurationGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool finished = false;
    void block() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [this] { return released; });
    }
    bool wait(bool ConfigurationGate::* flag) {
        std::unique_lock lock(mutex);
        return changed.wait_for(
            lock, std::chrono::seconds(5), [this, flag] { return this->*flag; });
    }
    void release() {
        const std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
    void finish() {
        const std::lock_guard lock(mutex);
        finished = true;
        changed.notify_all();
    }
};
struct ReleaseConfiguration {
    std::shared_ptr<ConfigurationGate> gate;
    ~ReleaseConfiguration() {
        gate->release();
    }
};
ConfigurationSaveCompletion wait_configuration(AsyncConfigurationSave& saving) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = saving.poll())
            return std::move(*result);
        std::this_thread::yield();
    }
    return {0, std::unexpected("Timed out waiting for configuration save")};
}
} // namespace

TEST_CASE("Background settings saves own inputs and coalesce rapid edits to the latest revision",
          "[app][configuration][async]") {
    ConfigurationFixture fixture;
    auto gate = std::make_shared<ConfigurationGate>();
    const ReleaseConfiguration cleanup{gate};
    auto written = std::make_shared<std::vector<int>>();
    AsyncConfigurationSave saving(fixture.path,
                                  [gate, written](const auto& config, const auto& path) {
                                      if (written->empty())
                                          gate->block();
                                      written->push_back(config.frontend.audio_volume_percent);
                                      return save_mapperbus_configuration_to_file(config, path);
                                  });
    MapperBusConfiguration config;
    config.frontend.audio_volume_percent = 10;
    const auto first = saving.submit(config);
    REQUIRE(gate->wait(&ConfigurationGate::entered));
    config.frontend.audio_volume_percent = 20;
    (void)saving.submit(config);
    config.frontend.audio_volume_percent = 30;
    config.state_slots[std::string(64, 'b')] = 8;
    const auto latest = saving.submit(config);
    config.frontend.audio_volume_percent = 99;
    CHECK(saving.busy());
    CHECK(saving.unsaved());
    CHECK_FALSE(saving.poll());
    gate->release();
    auto old = wait_configuration(saving);
    REQUIRE(old.result);
    CHECK(old.revision == first);
    CHECK(saving.busy());
    CHECK(saving.unsaved());
    auto final = wait_configuration(saving);
    REQUIRE(final.result);
    CHECK(final.revision == latest);
    CHECK_FALSE(saving.busy());
    CHECK_FALSE(saving.unsaved());
    CHECK(saving.error().empty());
    CHECK(*written == std::vector<int>{10, 30});
    const auto loaded = load_mapperbus_configuration_from_file(fixture.path);
    CHECK(loaded.frontend.audio_volume_percent == 30);
    CHECK(loaded.state_slots.at(std::string(64, 'b')) == 8);
}

TEST_CASE("Failed settings writes preserve the old file and Retry uses newer edits",
          "[app][configuration][async]") {
    ConfigurationFixture fixture;
    MapperBusConfiguration config;
    REQUIRE(save_mapperbus_configuration_to_file(config, fixture.path));
    auto original = core::read_binary_file_capped(fixture.path.string(), 100000, "config");
    REQUIRE(original);
    auto gate = std::make_shared<ConfigurationGate>();
    const ReleaseConfiguration cleanup{gate};
    auto calls = std::make_shared<int>(0);
    AsyncConfigurationSave saving(
        fixture.path, [gate, calls](const auto& proposed, const auto& path) -> core::Result<void> {
            if (++*calls == 1) {
                gate->block();
                return std::unexpected("Injected settings write failure");
            }
            return save_mapperbus_configuration_to_file(proposed, path);
        });
    config.frontend.audio_volume_percent = 10;
    const auto first = saving.submit(config);
    REQUIRE(gate->wait(&ConfigurationGate::entered));
    config.frontend.audio_volume_percent = 20;
    (void)saving.submit(config);
    gate->release();
    auto failed = wait_configuration(saving);
    REQUIRE_FALSE(failed.result);
    CHECK(failed.revision == first);
    CHECK_FALSE(saving.busy());
    CHECK(saving.unsaved());
    CHECK_FALSE(saving.error().empty());
    auto retained = core::read_binary_file_capped(fixture.path.string(), 100000, "config");
    REQUIRE(retained);
    CHECK(*retained == *original);
    config.frontend.audio_volume_percent = 30;
    const auto latest = saving.submit(config);
    CHECK_FALSE(saving.busy()); // Failure stays latched until explicit Retry.
    CHECK_FALSE(saving.poll());
    saving.retry();
    auto retried = wait_configuration(saving);
    REQUIRE(retried.result);
    CHECK(retried.revision == latest);
    CHECK_FALSE(saving.unsaved());
    CHECK(load_mapperbus_configuration_from_file(fixture.path).frontend.audio_volume_percent == 30);
    CHECK(*calls == 2);
}

TEST_CASE("A settings proposal follows older saves and can be retried or discarded after failure",
          "[app][configuration][async][transaction]") {
    ConfigurationFixture fixture;
    MapperBusConfiguration live;
    live.frontend.audio_volume_percent = 60;
    auto gate = std::make_shared<ConfigurationGate>();
    const ReleaseConfiguration cleanup{gate};
    auto calls = std::make_shared<int>(0);
    AsyncConfigurationSave saving(
        fixture.path, [gate, calls](const auto& proposed, const auto& path) -> core::Result<void> {
            const auto call = ++*calls;
            if (call == 1)
                gate->block();
            if (call == 2 || call == 4)
                return std::unexpected("Rejected proposal");
            return save_mapperbus_configuration_to_file(proposed, path);
        });
    (void)saving.submit(live);
    REQUIRE(gate->wait(&ConfigurationGate::entered));
    auto proposed = live;
    proposed.input.controller_mode = core::ControllerMode::FourScore;
    proposed.save_states.directory = fixture.path.parent_path() / "new-states";
    const auto transaction = saving.submit(proposed);
    CHECK(live.input.controller_mode == core::ControllerMode::Standard);
    gate->release();
    REQUIRE(wait_configuration(saving).result);
    auto failed = wait_configuration(saving);
    CHECK(failed.revision == transaction);
    REQUIRE_FALSE(failed.result);
    CHECK(load_mapperbus_configuration_from_file(fixture.path).input.controller_mode ==
          core::ControllerMode::Standard);
    saving.retry();
    auto committed = wait_configuration(saving);
    REQUIRE(committed.result);
    CHECK(committed.revision == transaction);
    live = proposed; // The matching receipt is the UI's only apply seam.
    CHECK(load_mapperbus_configuration_from_file(fixture.path).save_states.directory ==
          live.save_states.directory);
    proposed.input.controller_mode = core::ControllerMode::Standard;
    (void)saving.submit(proposed);
    REQUIRE_FALSE(wait_configuration(saving).result);
    saving.replace_failed(live);
    REQUIRE(wait_configuration(saving).result);
    CHECK_FALSE(saving.unsaved());
    CHECK(load_mapperbus_configuration_from_file(fixture.path).input.controller_mode ==
          core::ControllerMode::FourScore);
}

TEST_CASE("Configuration workers can outlive their owner without accessing live settings",
          "[app][configuration][async]") {
    ConfigurationFixture fixture;
    auto gate = std::make_shared<ConfigurationGate>();
    const ReleaseConfiguration cleanup{gate};
    MapperBusConfiguration live;
    live.frontend.audio_volume_percent = 35;
    {
        AsyncConfigurationSave saving(fixture.path, [gate](const auto& proposed, const auto& path) {
            gate->block();
            auto saved = save_mapperbus_configuration_to_file(proposed, path);
            gate->finish();
            return saved;
        });
        (void)saving.submit(live);
        REQUIRE(gate->wait(&ConfigurationGate::entered));
    }
    live.frontend.audio_volume_percent = 90;
    gate->release();
    REQUIRE(gate->wait(&ConfigurationGate::finished));
    CHECK(load_mapperbus_configuration_from_file(fixture.path).frontend.audio_volume_percent == 35);
}

TEST_CASE("Default background settings failures retain existing configuration bytes",
          "[app][configuration][async]") {
    ConfigurationFixture fixture;
    MapperBusConfiguration config;
    REQUIRE(save_mapperbus_configuration_to_file(config, fixture.path));
    const auto original = core::read_binary_file_capped(fixture.path.string(), 100000, "config");
    REQUIRE(original);
    std::filesystem::create_directory(fixture.path.string() + ".tmp");
    AsyncConfigurationSave saving(fixture.path);
    config.frontend.audio_volume_percent = 25;
    (void)saving.submit(config);
    REQUIRE_FALSE(wait_configuration(saving).result);
    auto retained = core::read_binary_file_capped(fixture.path.string(), 100000, "config");
    REQUIRE(retained);
    CHECK(*retained == *original);
    std::filesystem::remove(fixture.path.string() + ".tmp");
    saving.retry();
    REQUIRE(wait_configuration(saving).result);
    CHECK(load_mapperbus_configuration_from_file(fixture.path).frontend.audio_volume_percent == 25);
}
} // namespace mapperbus::app
