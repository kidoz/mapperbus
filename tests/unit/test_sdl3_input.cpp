#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../support/test_rom.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/sdl3_input.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/video/null_video.hpp"

namespace mapperbus::frontend {
namespace {

TEST_CASE("Turbo gamepad mappings accept actions and explicit unbinding", "[sdl3][input][turbo]") {
    Sdl3InputConfig config;
    REQUIRE(apply_gamepad_mapping(config, "turbo_a=leftshoulder,turbo_b=none,a=east"));
    CHECK(gamepad_control_for_button(config, platform::InputAction::TurboA).button ==
          platform::GamepadButton::LeftShoulder);
    const auto& none = gamepad_control_for_button(config, platform::InputAction::TurboB);
    CHECK(none.kind == GamepadControlKind::None);
    CHECK(platform::gamepad_control_token(none) == "none");
    CHECK(gamepad_control_for_button(config, core::Button::A).button ==
          platform::GamepadButton::East);
}

TEST_CASE("Virtual SDL gamepads deliver turbo through a running NES ROM",
          "[sdl3][input][turbo][functional]") {
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
    REQUIRE(apply_gamepad_mapping(config.player2, "turbo_b=rightshoulder"));
    core::register_builtin_mappers();
    // Continuously latch and read A/B for both players into RAM. Repeated
    // reads within one frame must not change the generated signal.
    constexpr std::uint8_t kProgram[] = {
        0xA9, 0x01, 0x8D, 0x16, 0x40,                   // LDA #1; STA $4016
        0xA9, 0x00, 0x8D, 0x16, 0x40,                   // LDA #0; STA $4016
        0xAD, 0x16, 0x40, 0x29, 0x01, 0x8D, 0x00, 0x02, // P1 A -> $0200
        0xAD, 0x16, 0x40, 0x29, 0x01, 0x8D, 0x01, 0x02, // P1 B -> $0201
        0xAD, 0x17, 0x40, 0x29, 0x01, 0x8D, 0x10, 0x02, // P2 A -> $0210
        0xAD, 0x17, 0x40, 0x29, 0x01, 0x8D, 0x11, 0x02, // P2 B -> $0211
        0x4C, 0x00, 0x80                                // JMP $8000
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
