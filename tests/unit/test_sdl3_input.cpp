#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../support/test_rom.hpp"
#include "../support/virtual_gamepads.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/gamepad_assignment.hpp"
#include "frontends/sdl3/sdl3_input.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/video/null_video.hpp"

namespace mapperbus::frontend {
namespace {

TEST_CASE("SDL game layouts retain process mappings and save discovery to the active game",
          "[sdl3][input][game-assignments]") {
    tests::IdentityGamepads pads;
    for (int pad = 0; pad < 4; ++pad)
        pads.attach(pad, pad);
    app::MapperBusConfiguration config;
    Sdl3SixPlayerInputConfig profiles;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        config.input.gamepad_profile(player).enabled = false;
        profiles.players[player] = config.input.gamepad_profile(player);
        profiles.keyboards[player].fill(0);
    }
    REQUIRE(platform::apply_gamepad_mapping(profiles.players[0], "a=west"));
    profiles.players[0].axis_deadzone = 21000;
    const auto globals = app::gamepad_assignments_for_game(config, {});
    const std::string first(64, 'a'), second(64, 'b');
    auto a = globals, b = globals;
    a[0].enabled = b[0].enabled = true;
    a[0].gamepad_index = 3;
    b[0].gamepad_index = 1;
    config.game_gamepads[first] = a;
    config.game_gamepads[second] = b;
    auto input = std::make_unique<Sdl3Input>(profiles);
    for (const auto& identity : {first, second, first}) {
        input->set_gamepad_assignments(app::gamepad_assignments_for_game(config, identity));
        for (int pad = 0; pad < 4; ++pad)
            REQUIRE(SDL_SetJoystickVirtualButton(
                pads.joysticks[pad], SDL_GAMEPAD_BUTTON_WEST, pad == 3));
        input->poll();
        CHECK(input->is_button_pressed(0, core::Button::A) == (identity == first));
        CHECK(input->gamepad_config(0).bindings == profiles.players[0].bindings);
        CHECK(input->gamepad_config(0).axis_deadzone == 21000);
        for (int player = 0; player < platform::kInputProfileCount; ++player)
            app::remember_gamepad_assignment(
                config,
                identity,
                player,
                platform::gamepad_assignment(input->gamepad_config(player)));
        CHECK(app::gamepad_assignments_for_game(config, {}) == globals);
    }
    const auto path = std::filesystem::temp_directory_path() /
                      ("mapperbus-sdl-layouts-" + std::to_string(SDL_GetTicksNS()) + ".conf");
    REQUIRE(app::save_mapperbus_configuration_to_file(config, path));
    config = app::load_mapperbus_configuration_from_file(path);
    input.reset();
    input = std::make_unique<Sdl3Input>(profiles);
    input->set_gamepad_assignments(app::gamepad_assignments_for_game(config, second));
    input->poll();
    CHECK(input->gamepad_config(0).identity == config.game_gamepads.at(second)[0].identity);
    input->set_gamepad_assignments(app::gamepad_assignments_for_game(config, {}));
    input->poll();
    CHECK_FALSE(input->is_button_pressed(0, core::Button::A));
    CHECK_FALSE(input->gamepad_config(0).enabled);
    CHECK(input->gamepad_config(0).bindings == profiles.players[0].bindings);
    std::filesystem::remove(path);
}

TEST_CASE("Four-controller capture requires fresh distinct presses and release before finish",
          "[sdl3][input][assignment]") {
    tests::IdentityGamepads pads;
    for (int pad = 0; pad < 4; ++pad)
        pads.attach(pad, 0); // Four indistinguishable models still have distinct instance IDs.
    pads.press(3, true);
    Sdl3GamepadAssignment capture({"Hori 1", "Hori 2", "Hori 3", "Hori 4"});
    capture.poll();
    CHECK(capture.assigned_count() == 0); // A button held on opening is ignored.
    pads.press(3, false);
    capture.poll();
    pads.press(0, true);
    capture.poll();
    REQUIRE(capture.assigned_count() == 1);
    capture.poll();
    CHECK(capture.assigned_count() == 1); // Holding does not advance another step.
    pads.press(0, false);
    capture.poll();
    pads.press(0, true);
    capture.poll();
    CHECK(capture.assigned_count() == 1);
    CHECK(capture.status().find("already Hori 1") != std::string::npos);
    pads.press(0, false);
    capture.poll();
    pads.press(1, true);
    pads.press(2, true);
    capture.poll();
    CHECK(capture.assigned_count() == 1);
    CHECK(capture.status().find("one controller") != std::string::npos);
    pads.press(1, false);
    pads.press(2, false);
    capture.poll();
    for (int pad : {2, 1, 3}) {
        pads.press(pad, true);
        capture.poll();
        CHECK_FALSE(capture.ready());
        pads.press(pad, false);
        capture.poll();
    }
    REQUIRE(capture.ready());
    const std::array expected = {pads.ids[0], pads.ids[2], pads.ids[1], pads.ids[3]};
    CHECK(std::ranges::equal(capture.assignments(), expected));
    pads.detach(2);
    capture.poll();
    CHECK_FALSE(capture.ready());
    CHECK(capture.assigned_count() == 1);
    CHECK(capture.status().find("disconnected") != std::string::npos);
    pads.attach(2, 0);
    capture.poll();
    CHECK(capture.assigned_count() == 1);
    pads.press(2, true);
    capture.poll();
    CHECK(capture.assigned_count() == 2);
}

TEST_CASE("Two and three controller captures use their role names and exact completion count",
          "[sdl3][input][assignment]") {
    for (const int count : {2, 3}) {
        DYNAMIC_SECTION(count << " controller roles") {
            tests::IdentityGamepads pads;
            for (int pad = 0; pad < 4; ++pad)
                pads.attach(pad, 0);
            std::vector<std::string> roles{"Player 1", "Player 2"};
            if (count == 3)
                roles.push_back("External 2");
            Sdl3GamepadAssignment capture(roles);
            CHECK(capture.status().find("Player 1") != std::string::npos);
            for (int role = 0; role < count; ++role) {
                const int pad = 3 - role;
                pads.press(pad, true);
                capture.poll();
                REQUIRE(capture.assigned_count() == static_cast<std::size_t>(role + 1));
                CHECK_FALSE(capture.ready());
                CHECK(capture.role_name(role) == roles[role]);
                pads.press(pad, false);
                capture.poll();
                CHECK(capture.ready() == (role == count - 1));
            }
            pads.detach(2); // Rewind the second role, clearing any later choice.
            capture.poll();
            CHECK_FALSE(capture.ready());
            CHECK(capture.assigned_count() == 1);
            CHECK(capture.status().find("Player 2") != std::string::npos);
            pads.attach(2, 0);
            pads.press(2, true); // A held button on connection is primed too.
            capture.poll();
            CHECK(capture.assigned_count() == 1);
            pads.press(2, false);
            capture.poll();
            pads.press(2, true);
            capture.poll();
            CHECK(capture.assigned_count() == 2);
        }
    }
    Sdl3GamepadAssignment empty({});
    CHECK_FALSE(empty.ready());
    CHECK(empty.assignments().empty());
    CHECK(empty.status().find("No controller roles") != std::string::npos);
}

TEST_CASE("Gamepad serial identity and ambiguous model matching are explicit",
          "[sdl3][input][identity]") {
    const std::array<Sdl3GamepadCandidate, 2> devices = {
        {{1, 0, {.guid = "model", .serial = "alpha", .name = "Same pad"}},
         {2, 1, {.guid = "model", .serial = "beta", .name = "Same pad"}}}};
    platform::GamepadIdentity saved{.guid = "model", .serial = "beta", .name = "Saved"};
    CHECK(unique_gamepad_match(saved, devices) == 1);
    saved.serial = "missing";
    CHECK_FALSE(unique_gamepad_match(saved, devices));
    saved.serial.clear();
    CHECK_FALSE(unique_gamepad_match(saved, devices));
    CHECK(unique_gamepad_match(saved, std::span(devices).first(1)) == 0);
    saved.ambiguous = true;
    CHECK_FALSE(unique_gamepad_match(saved, std::span(devices).first(1)));
}

TEST_CASE("Saved four-gamepad identities survive reordered startup and middle reconnect",
          "[sdl3][input][identity]") {
    tests::IdentityGamepads pads;
    for (int pad = 0; pad < 4; ++pad)
        pads.attach(pad, pad);
    app::MapperBusConfiguration saved;
    saved.input.gamepad.enabled = false;
    saved.input.player2_gamepad.enabled = false;
    saved.input.configure_four_hori_gamepads();
    Sdl3SixPlayerInputConfig profiles;
    for (int player = 0; player < 6; ++player)
        profiles.players[player] = saved.input.gamepad_profile(player);
    auto input = std::make_unique<Sdl3Input>(profiles);
    input->poll();
    REQUIRE(input->consume_gamepad_assignment_changes());
    for (int player = 0; player < 6; ++player)
        saved.input.gamepad_profile(player) = input->gamepad_config(player);
    for (int player = 2; player < 6; ++player) {
        CHECK_FALSE(saved.input.gamepad_profile(player).identity.guid.empty());
        CHECK_FALSE(saved.input.gamepad_profile(player).identity.ambiguous);
    }
    const auto path = std::filesystem::temp_directory_path() /
                      ("mapperbus-pad-identity-" + std::to_string(SDL_GetTicksNS()) + ".conf");
    REQUIRE(app::save_mapperbus_configuration_to_file(saved, path));
    input.reset();
    for (int pad = 0; pad < 4; ++pad)
        pads.detach(pad);
    for (int pad : {3, 1, 0, 2})
        pads.attach(pad, pad);
    const auto restored = app::load_mapperbus_configuration_from_file(path);
    for (int player = 0; player < 6; ++player)
        profiles.players[player] = restored.input.gamepad_profile(player);
    input = std::make_unique<Sdl3Input>(profiles);
    for (int active = 0; active < 4; ++active) {
        for (int pad = 0; pad < 4; ++pad)
            pads.press(pad, pad == active);
        input->poll();
        for (int pad = 0; pad < 4; ++pad)
            CHECK(input->is_button_pressed(pad + 2, core::Button::A) == (pad == active));
    }
    for (int pad = 0; pad < 4; ++pad)
        pads.press(pad, true);
    input->poll();
    pads.detach(1);
    input->poll();
    for (int pad = 0; pad < 4; ++pad)
        CHECK(input->is_button_pressed(pad + 2, core::Button::A) == (pad != 1));
    pads.attach(1, 1);
    pads.press(1, true);
    input->poll();
    for (int pad = 0; pad < 4; ++pad)
        CHECK(input->is_button_pressed(pad + 2, core::Button::A));
    input.reset();
    for (int pad = 0; pad < 4; ++pad)
        pads.detach(pad);
    pads.attach(0, 99);
    pads.press(0, true);
    input = std::make_unique<Sdl3Input>(profiles);
    input->poll();
    for (int pad = 0; pad < 4; ++pad)
        CHECK_FALSE(input->is_button_pressed(pad + 2, core::Button::A));
    std::filesystem::remove(path);
}

TEST_CASE("Identical controllers require explicit reassignment after disconnect or restart",
          "[sdl3][input][identity]") {
    tests::IdentityGamepads pads;
    pads.attach(0, 0);
    platform::GamepadInputConfig config;
    Sdl3GamepadSelection selection;
    selection.poll(config); // Legacy index configuration captures the chosen pad.
    REQUIRE(selection.gamepad() != nullptr);
    CHECK_FALSE(config.identity.ambiguous);
    pads.attach(1, 0);
    selection.poll(config);
    REQUIRE(selection.gamepad() != nullptr);
    REQUIRE(config.identity.ambiguous);
    auto persisted = config;
    Sdl3GamepadSelection restarted;
    restarted.poll(persisted);
    CHECK(restarted.gamepad() == nullptr);
    pads.detach(0);
    selection.poll(config);
    CHECK(selection.gamepad() == nullptr); // Remaining identical peer is not adopted.
    REQUIRE(config.identity.ambiguous);
    pads.attach(0, 0);
    selection.poll(config);
    CHECK(selection.gamepad() == nullptr);
    selection.assign(config, 1);
    REQUIRE(selection.gamepad() != nullptr);
    CHECK(SDL_GetGamepadID(selection.gamepad()) == pads.ids[0]);
    CHECK(config.identity.ambiguous);
}

TEST_CASE("Turbo gamepad mappings accept actions and explicit unbinding", "[sdl3][input][turbo]") {
    Sdl3InputConfig config;
    CHECK(gamepad_control_for_button(config, platform::InputAction::Microphone).kind ==
          GamepadControlKind::None);
    REQUIRE(apply_gamepad_mapping(config, "mic=rightshoulder"));
    CHECK(gamepad_control_for_button(config, platform::InputAction::Microphone).button ==
          platform::GamepadButton::RightShoulder);
    REQUIRE(apply_gamepad_mapping(config, "turbo_a=leftshoulder,turbo_b=none,a=east"));
    CHECK(gamepad_control_for_button(config, platform::InputAction::TurboA).button ==
          platform::GamepadButton::LeftShoulder);
    const auto& none = gamepad_control_for_button(config, platform::InputAction::TurboB);
    CHECK(none.kind == GamepadControlKind::None);
    CHECK(platform::gamepad_control_token(none) == "none");
    CHECK(gamepad_control_for_button(config, core::Button::A).button ==
          platform::GamepadButton::East);
}

TEST_CASE("Virtual SDL gamepads deliver turbo and microphone through a running ROM",
          "[sdl3][input][turbo][microphone][functional]") {
    // Exercise physical gamepad polling -> actions -> session pulses -> serial
    // NES ports -> emulated 6502 code -> CPU RAM, without a real gamepad.
    struct VirtualPads {
        std::array<SDL_JoystickID, 2> ids{};
        std::array<SDL_Joystick*, 2> joysticks{};
        ~VirtualPads() {
            for (auto* joystick : joysticks)
                if (joystick)
                    SDL_CloseJoystick(joystick);
            for (auto id : ids)
                if (id)
                    SDL_DetachVirtualJoystick(id);
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    } pads;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "MapperBus turbo test";
    Sdl3TwoPlayerInputConfig config;
    for (int player = 0; player < 2; ++player) {
        pads.ids[player] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(pads.ids[player] != 0);
        REQUIRE(SDL_IsGamepad(pads.ids[player]));
        pads.joysticks[player] = SDL_OpenJoystick(pads.ids[player]);
        REQUIRE(pads.joysticks[player] != nullptr);
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        REQUIRE(ids != nullptr);
        auto* found = std::find(ids, ids + count, pads.ids[player]);
        const int index = static_cast<int>(found - ids);
        SDL_free(ids);
        REQUIRE(index < count);
        (player == 0 ? config.player1 : config.player2).gamepad_index = index;
    }
    REQUIRE(apply_gamepad_mapping(config.player1, "turbo_a=leftshoulder"));
    REQUIRE(
        apply_gamepad_mapping(config.player2, "turbo_b=rightshoulder,microphone=rightshoulder"));
    core::register_builtin_mappers();
    // Continuously latch and read A/B for both players into RAM. Repeated
    // reads within one frame must not change the generated signal.
    constexpr std::uint8_t kProgram[] = {
        0xA9, 0x01, 0x8D, 0x16, 0x40,                                     // LDA #1; STA $4016
        0xA9, 0x00, 0x8D, 0x16, 0x40,                                     // LDA #0; STA $4016
        0xAD, 0x16, 0x40, 0x29, 0x01, 0x8D, 0x00, 0x02,                   // P1 A -> $0200
        0xAD, 0x16, 0x40, 0x29, 0x01, 0x8D, 0x01, 0x02,                   // P1 B -> $0201
        0xAD, 0x17, 0x40, 0x29, 0x01, 0x8D, 0x10, 0x02,                   // P2 A -> $0210
        0xAD, 0x17, 0x40, 0x29, 0x01, 0x8D, 0x11, 0x02,                   // P2 B -> $0211
        0xAD, 0x16, 0x40, 0x29, 0x04, 0x0D, 0x02, 0x02, 0x8D, 0x02, 0x02, // observed high
        0xAD, 0x16, 0x40, 0x49, 0x04, 0x29, 0x04, 0x0D, 0x03, 0x02, 0x8D, 0x03, 0x02, // low
        0x4C, 0x00, 0x80                                                              // JMP $8000
    };
    const auto rom = tests::build_ines_rom("turbo-functional", 0, 1, 1, kProgram);
    // Load independently persisted profiles before driving the actual NES ports.
    const auto config_path = std::filesystem::path(rom.string() + ".conf");
    app::MapperBusConfiguration saved;
    saved.input.gamepad = config.player1;
    saved.input.player2_gamepad = config.player2;
    REQUIRE(app::save_mapperbus_configuration_to_file(saved, config_path));
    const auto loaded = app::load_mapperbus_configuration_from_file(config_path);
    auto input = std::make_unique<Sdl3Input>(
        Sdl3TwoPlayerInputConfig{loaded.input.gamepad, loaded.input.player2_gamepad},
        loaded.input.keyboard_bindings,
        loaded.input.player2_keyboard_bindings);
    std::filesystem::remove(config_path);
    auto* backend = input.get();
    app::EmulationSession session(std::make_unique<platform::NullVideo>(),
                                  std::make_unique<platform::NullAudio>(),
                                  std::move(input));
    REQUIRE(session.load_rom(rom.string()));
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[0], SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true));
    for (const auto region : {core::Region::NTSC, core::Region::PAL, core::Region::Dendy}) {
        session.set_region(region);
        session.reset();
        for (int frame = 0; frame < 12; ++frame) {
            CAPTURE(region, frame);
            REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
            CHECK(backend->is_action_pressed(0, platform::InputAction::TurboA));
            CHECK(backend->is_action_pressed(1, platform::InputAction::TurboB));
            CHECK(backend->is_action_pressed(1, platform::InputAction::Microphone));
            CHECK(session.emulator().microphone_active());
            CHECK((session.emulator().read_cpu(0x0202) & 4U) == 4);
            CHECK((session.emulator().read_cpu(0x0203) & 4U) == 4);
            const int on = frame % 4 < 2 ? 1 : 0;
            CHECK(session.emulator().read_cpu(0x0200) == on);
            CHECK(session.emulator().read_cpu(0x0201) == 0);
            CHECK(session.emulator().read_cpu(0x0210) == 0);
            CHECK(session.emulator().read_cpu(0x0211) == on);
        }
    }
    // Disconnect while held: no stale button remains active.
    SDL_CloseJoystick(pads.joysticks[0]);
    pads.joysticks[0] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[0]));
    pads.ids[0] = 0;
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK_FALSE(backend->is_action_pressed(0, platform::InputAction::TurboA));
    CHECK(session.emulator().read_cpu(0x0200) == 0);
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false));
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK_FALSE(session.emulator().microphone_active());
    std::filesystem::remove(rom);
}

TEST_CASE("SDL3 gamepad control parser accepts buttons, aliases, and axes", "[sdl3][input]") {
    auto east = parse_gamepad_control("east");
    REQUIRE(east);
    REQUIRE(east->kind == GamepadControlKind::Button);
    REQUIRE(east->button == platform::GamepadButton::East);

    auto dpad_up = parse_gamepad_control("dpad-up");
    REQUIRE(dpad_up);
    REQUIRE(dpad_up->kind == GamepadControlKind::Button);
    REQUIRE(dpad_up->button == platform::GamepadButton::DpadUp);

    auto left = parse_gamepad_control("leftx-");
    REQUIRE(left);
    REQUIRE(left->kind == GamepadControlKind::AxisNegative);
    REQUIRE(left->axis == platform::GamepadAxis::LeftX);

    auto right = parse_gamepad_control("+leftx");
    REQUIRE(right);
    REQUIRE(right->kind == GamepadControlKind::AxisPositive);
    REQUIRE(right->axis == platform::GamepadAxis::LeftX);
}

TEST_CASE("Four SDL gamepads reach separate D0 and D1 reports through a running ROM",
          "[sdl3][input][expansion][functional]") {
    struct VirtualPads {
        std::array<SDL_JoystickID, 4> ids{};
        std::array<SDL_Joystick*, 4> joysticks{};
        ~VirtualPads() {
            for (auto* joystick : joysticks)
                if (joystick)
                    SDL_CloseJoystick(joystick);
            for (auto id : ids)
                if (id)
                    SDL_DetachVirtualJoystick(id);
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    } pads;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "MapperBus external pad test";
    Sdl3FourPlayerInputConfig config;
    for (int player = 0; player < 4; ++player) {
        desc.vendor_id = 0x1234;
        desc.product_id = static_cast<Uint16>(player + 1);
        pads.ids[player] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(pads.ids[player] != 0);
        pads.joysticks[player] = SDL_OpenJoystick(pads.ids[player]);
        REQUIRE(pads.joysticks[player] != nullptr);
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        REQUIRE(ids != nullptr);
        const int index = static_cast<int>(std::find(ids, ids + count, pads.ids[player]) - ids);
        SDL_free(ids);
        REQUIRE(index < count);
        config.players[player].gamepad_index = index;
    }
    core::register_builtin_mappers();
    constexpr std::uint8_t kProgram[] = {
        0xA9, 0x01, 0x8D, 0x16, 0x40,       // LDA #1; STA $4016
        0xA9, 0x00, 0x8D, 0x16, 0x40,       // LDA #0; STA $4016
        0xA2, 0x00,                         // LDX #0
        0xAD, 0x16, 0x40, 0x9D, 0x00, 0x06, // LDA $4016; STA $0600,X
        0xAD, 0x17, 0x40, 0x9D, 0x40, 0x06, // LDA $4017; STA $0640,X
        0xE8, 0xE0, 0x18, 0xD0, 0xEF,       // INX; CPX #24; BNE loop
        0x4C, 0x00, 0x80                    // JMP $8000
    };
    const auto rom = tests::build_ines_rom("expansion-functional", 0, 1, 1, kProgram);
    const auto config_path = std::filesystem::path(rom.string() + ".conf");
    app::MapperBusConfiguration saved;
    saved.input.expansion_controllers = 3;
    for (int player = 0; player < 4; ++player)
        saved.input.gamepad_profile(player) = config.players[player];
    REQUIRE(app::save_mapperbus_configuration_to_file(saved, config_path));
    const auto loaded = app::load_mapperbus_configuration_from_file(config_path);
    for (int player = 0; player < 4; ++player) {
        config.players[player] = loaded.input.gamepad_profile(player);
        config.keyboards[player] = loaded.input.keyboard_profile(player);
    }
    auto input = std::make_unique<Sdl3Input>(config);
    auto* backend = input.get();
    app::EmulationSession session(std::make_unique<platform::NullVideo>(),
                                  std::make_unique<platform::NullAudio>(),
                                  std::move(input));
    REQUIRE(session.load_rom(rom.string()));
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    session.set_controller_timing(core::ControllerTiming::RfFamicom);
    session.set_expansion_controllers(loaded.input.expansion_controllers);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[0], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_SOUTH, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_START, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[2], SDL_GAMEPAD_BUTTON_BACK, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[2], SDL_GAMEPAD_BUTTON_START, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_DPAD_UP, true));
    const auto check_reports = [&](core::Byte external2, core::Byte built_in2 = 2) {
        REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
        for (unsigned bit = 0; bit < 8; ++bit) {
            CAPTURE(bit);
            CHECK((session.emulator().read_cpu(0x0600 + bit) & 3U) ==
                  ((0x01U >> bit & 1U) | (0x0CU >> bit & 1U) << 1U));
            CHECK((session.emulator().read_cpu(0x0640 + bit) & 3U) ==
                  ((built_in2 >> bit & 1U) | (external2 >> bit & 1U) << 1U));
        }
    };
    check_reports(0x11);
    session.set_controller_mode(core::ControllerMode::FourScore);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    const uint32_t four_score_reports[] = {0x080C01, 0x04110A};
    for (unsigned bit = 0; bit < 24; ++bit)
        for (int port = 0; port < 2; ++port)
            CHECK((session.emulator().read_cpu((port == 0 ? 0x0600 : 0x0640) + bit) & 3U) ==
                  (four_score_reports[port] >> bit & 1U));
    session.set_controller_mode(core::ControllerMode::Standard);
    check_reports(0x11);
    SDL_CloseJoystick(pads.joysticks[3]);
    pads.joysticks[3] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[3]));
    pads.ids[3] = 0;
    check_reports(0);
    CHECK_FALSE(backend->is_button_pressed(3, core::Button::A));
    pads.ids[3] = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(pads.ids[3] != 0);
    pads.joysticks[3] = SDL_OpenJoystick(pads.ids[3]);
    REQUIRE(pads.joysticks[3] != nullptr);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_EAST, true));
    check_reports(1);
    // Reconnect before the next poll: the replacement ID is new, not a peer
    // which should be suppressed when processing the queued removal event.
    SDL_CloseJoystick(pads.joysticks[3]);
    pads.joysticks[3] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[3]));
    pads.ids[3] = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(pads.ids[3] != 0);
    pads.joysticks[3] = SDL_OpenJoystick(pads.ids[3]);
    REQUIRE(pads.joysticks[3] != nullptr);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_EAST, true));
    check_reports(1);
    // A middle removal compacts SDL's indices. Profile two must not adopt
    // external pad one, which is still holding Select and Start.
    SDL_CloseJoystick(pads.joysticks[1]);
    pads.joysticks[1] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[1]));
    pads.ids[1] = 0;
    check_reports(1, 0);
    check_reports(1, 0);
    std::filesystem::remove(config_path);
    std::filesystem::remove(rom);
}

TEST_CASE("SDL3 gamepad mapping parser updates NES bindings", "[sdl3][input]") {
    Sdl3InputConfig config;

    auto result = apply_gamepad_mapping(config, "a=east,b=south,up=lefty-,down=lefty+,select=back");
    REQUIRE(result);

    const auto& a = gamepad_control_for_button(config, core::Button::A);
    REQUIRE(a.kind == GamepadControlKind::Button);
    REQUIRE(a.button == platform::GamepadButton::East);

    const auto& b = gamepad_control_for_button(config, core::Button::B);
    REQUIRE(b.kind == GamepadControlKind::Button);
    REQUIRE(b.button == platform::GamepadButton::South);

    const auto& up = gamepad_control_for_button(config, core::Button::Up);
    REQUIRE(up.kind == GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    const auto& down = gamepad_control_for_button(config, core::Button::Down);
    REQUIRE(down.kind == GamepadControlKind::AxisPositive);
    REQUIRE(down.axis == platform::GamepadAxis::LeftY);

    const auto& select = gamepad_control_for_button(config, core::Button::Select);
    REQUIRE(select.kind == GamepadControlKind::Button);
    REQUIRE(select.button == platform::GamepadButton::Back);
}

TEST_CASE("Six SDL gamepads reach built-in and Hori reports through a running ROM",
          "[sdl3][input][hori][functional]") {
    struct VirtualPads {
        std::array<SDL_JoystickID, 6> ids{};
        std::array<SDL_Joystick*, 6> joysticks{};
        ~VirtualPads() {
            for (auto* joystick : joysticks)
                if (joystick)
                    SDL_CloseJoystick(joystick);
            for (auto id : ids)
                if (id)
                    SDL_DetachVirtualJoystick(id);
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    } pads;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "MapperBus Hori six profile test";
    Sdl3SixPlayerInputConfig profiles;
    for (int player = 0; player < 6; ++player) {
        pads.ids[player] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(pads.ids[player] != 0);
        pads.joysticks[player] = SDL_OpenJoystick(pads.ids[player]);
        REQUIRE(pads.joysticks[player] != nullptr);
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        REQUIRE(ids != nullptr);
        const int index = static_cast<int>(std::find(ids, ids + count, pads.ids[player]) - ids);
        SDL_free(ids);
        REQUIRE(index < count);
        profiles.players[player].gamepad_index = index;
    }
    REQUIRE(apply_gamepad_mapping(profiles.players[5], "microphone=rightshoulder"));
    constexpr std::uint8_t kProgram[] = {0xA9, 1,    0x8D, 0x16, 0x40, 0xA9, 0,    0x8D,
                                         0x16, 0x40, 0xA2, 0,    0xAD, 0x16, 0x40, 0x9D,
                                         0x00, 0x06, 0xAD, 0x17, 0x40, 0x9D, 0x40, 0x06,
                                         0xE8, 0xE0, 0x18, 0xD0, 0xEF, 0x4C, 0,    0x80};
    core::register_builtin_mappers();
    const auto rom = tests::build_ines_rom("hori-functional", 0, 1, 1, kProgram);
    const auto config_path = std::filesystem::path(rom.string() + ".conf");
    app::MapperBusConfiguration saved;
    saved.input.controller_mode = core::ControllerMode::HoriFourPlayer;
    for (int player = 0; player < 6; ++player)
        saved.input.gamepad_profile(player) = profiles.players[player];
    REQUIRE(app::save_mapperbus_configuration_to_file(saved, config_path));
    const auto loaded = app::load_mapperbus_configuration_from_file(config_path);
    for (int player = 0; player < 6; ++player) {
        profiles.players[player] = loaded.input.gamepad_profile(player);
        profiles.keyboards[player] = loaded.input.keyboard_profile(player);
    }
    auto input = std::make_unique<Sdl3Input>(profiles);
    auto* backend = input.get();
    app::EmulationSession session(std::make_unique<platform::NullVideo>(),
                                  std::make_unique<platform::NullAudio>(),
                                  std::move(input));
    REQUIRE(session.load_rom(rom.string()));
    session.set_controller_mode(loaded.input.controller_mode);
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    session.set_controller_timing(core::ControllerTiming::RfFamicom);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[0], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_BACK, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_START, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_DPAD_UP, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[2], SDL_GAMEPAD_BUTTON_SOUTH, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_START, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[4], SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[5], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[5], SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true));
    const auto check_reports = [&](bool sixth_connected) {
        REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
        CHECK(session.emulator().microphone_active() == sixth_connected);
        const uint32_t reports[] = {0x048002U, sixth_connected ? 0x080108U : 0x080008U};
        const unsigned built_in[] = {0x01, 0x10}; // RF built-in 2 masks Start/Select
        for (unsigned bit = 0; bit < 24; ++bit)
            for (int port = 0; port < 2; ++port) {
                const auto d0 = bit < 8 ? built_in[port] >> bit & 1U : 1U;
                const auto d1 = reports[port] >> bit & 1U;
                CHECK((session.emulator().read_cpu((port == 0 ? 0x0600 : 0x0640) + bit) & 3U) ==
                      (d0 | d1 << 1U));
            }
    };
    check_reports(true);
    SDL_CloseJoystick(pads.joysticks[5]);
    pads.joysticks[5] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[5]));
    pads.ids[5] = 0;
    check_reports(false);
    CHECK_FALSE(backend->is_button_pressed(5, core::Button::A));
    CHECK(backend->is_button_pressed(4, core::Button::Right));
    std::filesystem::remove(config_path);
    std::filesystem::remove(rom);
}

TEST_CASE("SDL3 gamepad mapping parser reports invalid entries", "[sdl3][input]") {
    Sdl3InputConfig config;

    auto unknown_button = apply_gamepad_mapping(config, "jump=a");
    REQUIRE_FALSE(unknown_button);

    auto missing_direction = apply_gamepad_mapping(config, "up=lefty");
    REQUIRE_FALSE(missing_direction);
    REQUIRE(missing_direction.error().find("+ or - direction") != std::string::npos);

    auto unknown_control = apply_gamepad_mapping(config, "a=launch");
    REQUIRE_FALSE(unknown_control);
}

TEST_CASE("SDL3 scaler hotkeys map to runtime commands", "[sdl3][input]") {
    auto native = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_0);
    REQUIRE(native.kind == Sdl3ScalerCommandKind::Native);

    auto factor = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_4);
    REQUIRE(factor.kind == Sdl3ScalerCommandKind::SetFactor);
    REQUIRE(factor.factor == 4);

    auto cycle = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_F9);
    REQUIRE(cycle.kind == Sdl3ScalerCommandKind::CycleMode);

    auto ignored = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_A);
    REQUIRE_FALSE(ignored);
}

TEST_CASE("SDL3 save-state hotkeys map to session commands", "[sdl3][input]") {
    auto save = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F5, false, 0);
    REQUIRE(save.kind == Sdl3SessionCommandKind::SaveState);
    REQUIRE(save.slot == 0);

    auto load = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F7, false, 0);
    REQUIRE(load.kind == Sdl3SessionCommandKind::LoadState);

    // Shift+F5 targets slot 1.
    auto save_slot1 = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F5, true, 0);
    REQUIRE(save_slot1.kind == Sdl3SessionCommandKind::SaveState);
    REQUIRE(save_slot1.slot == 1);

    // F9 belongs to the scaler hotkeys and must stay out of the session set.
    REQUIRE_FALSE(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F9, false, 0));
    REQUIRE_FALSE(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_A, false, 0));
}

TEST_CASE("SDL3 FDS hotkeys map to disk controls", "[sdl3][input][fds]") {
    CHECK(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F6, false, 0).kind ==
          Sdl3SessionCommandKind::NextDiskSide);
    CHECK(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F6, true, 0).kind ==
          Sdl3SessionCommandKind::EjectDisk);
    CHECK(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F8, false, 0).kind ==
          Sdl3SessionCommandKind::ToggleDiskProtection);
}

TEST_CASE("SDL3 selects ten state slots without consuming scaler number keys", "[sdl3][input]") {
    for (int slot = 0; slot < 10; ++slot) {
        const auto key = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + slot);
        const auto select = Sdl3Input::session_command_for_scancode(key, false, 0, true);
        REQUIRE(select.kind == Sdl3SessionCommandKind::SlotSelect);
        REQUIRE(select.slot == slot);
        REQUIRE_FALSE(Sdl3Input::session_command_for_scancode(key, false, 0));
        const auto save = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F5, false, slot);
        const auto load = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F7, false, slot);
        REQUIRE(save.slot == slot);
        REQUIRE(load.slot == slot);
    }
    REQUIRE(Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_3).kind ==
            Sdl3ScalerCommandKind::SetFactor);
}

TEST_CASE("SDL3 window hotkeys map to fullscreen/vsync", "[sdl3][input]") {
    auto fullscreen = Sdl3Input::window_command_for_scancode(SDL_SCANCODE_F11);
    REQUIRE(fullscreen.kind == Sdl3WindowCommandKind::ToggleFullscreen);

    auto vsync = Sdl3Input::window_command_for_scancode(SDL_SCANCODE_V);
    REQUIRE(vsync.kind == Sdl3WindowCommandKind::ToggleVsync);

    REQUIRE_FALSE(Sdl3Input::window_command_for_scancode(SDL_SCANCODE_A));
}

} // namespace
} // namespace mapperbus::frontend
