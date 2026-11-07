#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>

#include "app/rom_source.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"

using namespace mapperbus::core;

namespace {
void frames(Emulator& emulator, int count) {
    for (int frame = 0; frame < count; ++frame)
        emulator.step_frame();
}

void input(Emulator& emulator, int profile, Button button, int count) {
    auto& controller = profile < 2   ? emulator.controller()
                       : profile < 4 ? emulator.expansion_controller()
                                     : emulator.additional_expansion_controller();
    controller.set_button_state(profile % 2, button, true);
    frames(emulator, count);
    controller.set_button_state(profile % 2, button, false);
}

std::array<Byte, 3> player_x(Emulator& emulator) {
    // Observed horizontal positions in this hash-pinned Japanese revision.
    return {emulator.read_cpu(0x0064), emulator.read_cpu(0x0065), emulator.read_cpu(0x0066)};
}

std::array<Byte, 4> wits_inputs(Emulator& emulator) {
    // Game-decoded controller bytes in this hash-pinned Japanese revision.
    return {emulator.read_cpu(0x006C),
            emulator.read_cpu(0x006D),
            emulator.read_cpu(0x006E),
            emulator.read_cpu(0x006F)};
}

std::array<bool, 4> changed_quadrants(const FrameBuffer& before, const FrameBuffer& after) {
    std::array<bool, 4> result{};
    // Exclude the score/life displays. During the first 60 playable frames,
    // each rider and its trail remain inside a separate board quadrant.
    for (int y = 32; y < 216; ++y) {
        for (int x = 16; x < 240; ++x) {
            const auto offset = y * kScreenWidth + x;
            if (before.pixels[offset] != after.pixels[offset])
                result[(y >= 124 ? 2 : 0) + (x >= 128 ? 1 : 0)] = true;
        }
    }
    return result;
}
} // namespace

TEST_CASE("Japanese Bomberman II ZIP supports three independent players",
          "[controller][compatibility][archive][external]") {
    const auto* root = std::getenv("MAPPERBUS_CONTROLLER_TEST_ASSETS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_CONTROLLER_TEST_ASSETS to your licensed controller game directory");
    const auto path = std::filesystem::path(root) / "Bomberman II (Japan).zip";
    register_builtin_mappers();
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        CAPTURE(timing);
        Emulator emulator;
        emulator.set_controller_timing(timing);
        emulator.set_controller_wiring(ControllerWiring::RfFamicom);
        emulator.set_expansion_controllers(3);
        const auto opened = mapperbus::app::load_rom_source(emulator, path.string());
        INFO((opened ? *opened : opened.error()));
        REQUIRE(opened);
        REQUIRE(*opened == "Bomberman II (Japan).nes");
        REQUIRE(sha256_hex(emulator.content_hash()) ==
                "9bc9051b174ec4743df78a3086df6577c55258d705c9c9c451be63152c9badf1");
        emulator.reset();

        // Recorded host inputs: skip the introduction, open the mode menu,
        // choose Battle (three players), then a one-win match. The second
        // Start follows the match-screen transition, which ignores early input.
        frames(emulator, 180);
        input(emulator, 0, Button::Start, 1);
        frames(emulator, 300);
        input(emulator, 0, Button::Select, 1);
        frames(emulator, 20);
        input(emulator, 0, Button::Select, 1);
        frames(emulator, 20);
        input(emulator, 0, Button::Start, 1);
        frames(emulator, 180);
        input(emulator, 0, Button::Down, 1);
        frames(emulator, 15);
        input(emulator, 0, Button::Down, 1);
        frames(emulator, 15);
        input(emulator, 0, Button::Start, 1);
        frames(emulator, 240);
        input(emulator, 0, Button::Start, 3);
        frames(emulator, 600);
        REQUIRE(emulator.read_cpu(0x003B) == 2); // Battle, rather than Normal or VS.
        const std::array<Byte, 3> initial_x = {0x18, 0xD8, 0x78};
        REQUIRE(player_x(emulator) == initial_x);
        const auto arena = emulator.save_state();

        for (const auto mode : {ControllerMode::Standard, ControllerMode::HoriFourPlayer}) {
            CAPTURE(mode);
            REQUIRE(emulator.load_state(arena));
            emulator.set_controller_mode(mode);
            const auto checkpoint = emulator.save_state();
            frames(emulator, 12);
            const auto idle_pixels = emulator.frame_buffer().pixels;
            REQUIRE(player_x(emulator) == initial_x);
            for (int profile = 0; profile < 6; ++profile) {
                CAPTURE(profile);
                REQUIRE(emulator.load_state(checkpoint));
                const auto button = profile == 1 ? Button::Left : Button::Right;
                input(emulator, profile, button, 12);
                // Capture before read_cpu probes, which also update bus open bus.
                const auto after_input = emulator.save_state();
                auto expected_x = initial_x;
                if (profile < 3)
                    expected_x[profile] += profile == 1 ? -12 : 12;
                CHECK(player_x(emulator) == expected_x);
                if (profile < 3)
                    CHECK(emulator.frame_buffer().pixels != idle_pixels);
                else
                    CHECK(emulator.frame_buffer().pixels == idle_pixels);

                // Replay the real game's third-controller input after restoring.
                if (profile == 2) {
                    const auto pixels = emulator.frame_buffer().pixels;
                    REQUIRE(emulator.load_state(checkpoint));
                    input(emulator, profile, button, 12);
                    const auto actual = emulator.save_state();
                    const auto mismatch = std::ranges::mismatch(actual, after_input);
                    INFO("First state difference at " << mismatch.in1 - actual.begin());
                    CHECK((actual == after_input));
                    CHECK(emulator.frame_buffer().pixels == pixels);
                }
            }
        }
    }
}

TEST_CASE("Japanese Wit's detects Hori and controls all four players",
          "[controller][compatibility][archive][external][hori][wits]") {
    const auto* root = std::getenv("MAPPERBUS_CONTROLLER_TEST_ASSETS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_CONTROLLER_TEST_ASSETS to your licensed controller game directory");
    const auto path = std::filesystem::path(root) / "Wit's (Japan).zip";
    register_builtin_mappers();
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        CAPTURE(timing);
        Emulator emulator;
        emulator.set_controller_timing(timing);
        emulator.set_controller_wiring(ControllerWiring::RfFamicom);
        emulator.set_controller_mode(ControllerMode::HoriFourPlayer);
        emulator.set_expansion_controllers(3);
        const auto opened = mapperbus::app::load_rom_source(emulator, path.string());
        INFO((opened ? *opened : opened.error()));
        REQUIRE(opened);
        REQUIRE(*opened == "Wit's (Japan).nes");
        REQUIRE(sha256_hex(emulator.content_hash()) ==
                "34352cb9090fd00d1d677308dd3adbdfd4e76622de21bb48dffbffe19df9f195");
        emulator.reset();
        frames(emulator, 180);
        input(emulator, 0, Button::Start, 3);
        frames(emulator, 90);
        const auto title = emulator.save_state();

        // The unmodified game decodes Hori's serial signatures as $20/$10
        // (first read in bit 7), and selects its Hori input path with $C0.
        // Standard is the negative control; Four Score selects a different path.
        for (const auto mode : {ControllerMode::Standard,
                                ControllerMode::FourScore,
                                ControllerMode::HoriFourPlayer}) {
            CAPTURE(mode);
            REQUIRE(emulator.load_state(title));
            emulator.set_controller_mode(mode);
            frames(emulator, 3);
            const Byte expected = mode == ControllerMode::Standard    ? 0
                                  : mode == ControllerMode::FourScore ? 0x30
                                                                      : 0xC0;
            REQUIRE(emulator.read_cpu(0x0078) == expected);
            if (mode == ControllerMode::HoriFourPlayer) {
                CHECK(emulator.read_cpu(0x0074) == 0x20);
                CHECK(emulator.read_cpu(0x0075) == 0x10);
            } else if (mode == ControllerMode::FourScore) {
                CHECK(emulator.read_cpu(0x0076) == 0x10);
                CHECK(emulator.read_cpu(0x0077) == 0x20);
            }
        }

        REQUIRE(emulator.load_state(title));
        for (int down = 0; down < 4; ++down) {
            input(emulator, 0, Button::Down, 3);
            frames(emulator, 20);
        }
        input(emulator, 0, Button::Start, 3); // MULTI VS MODE.
        frames(emulator, 300);
        input(emulator, 0, Button::Start, 3);
        frames(emulator, 240); // First round, before riders reach each other.
        REQUIRE(emulator.read_cpu(0x0078) == 0xC0);
        const auto arena = emulator.save_state();
        frames(emulator, 60);
        const auto idle = emulator.frame_buffer();

        // Built-ins alias players 1/2 in this game. Hori pads 1..4 decode to
        // four independent bytes, and turn only their own on-screen rider.
        constexpr std::array kPlayerForProfile = {0, 1, 0, 1, 2, 3};
        constexpr std::array kQuadrantForPlayer = {2, 3, 0, 1};
        for (int profile = 0; profile < 6; ++profile) {
            CAPTURE(profile);
            REQUIRE(emulator.load_state(arena));
            input(emulator, profile, Button::Right, 60);
            std::array<Byte, 4> expected_inputs{};
            expected_inputs[kPlayerForProfile[profile]] = 1; // Game's Right bit.
            CHECK(wits_inputs(emulator) == expected_inputs);
            std::array<bool, 4> expected_changes{};
            expected_changes[kQuadrantForPlayer[kPlayerForProfile[profile]]] = true;
            CHECK(changed_quadrants(idle, emulator.frame_buffer()) == expected_changes);
        }

        // Restore a running four-player round with simultaneous, distinct input.
        REQUIRE(emulator.load_state(arena));
        constexpr std::array kButtons = {Button::Right, Button::Left, Button::Up, Button::Down};
        for (int pad = 0; pad < 4; ++pad) {
            auto& pair = pad < 2 ? emulator.expansion_controller()
                                 : emulator.additional_expansion_controller();
            pair.set_button_state(pad % 2, kButtons[pad], true);
        }
        frames(emulator, 7);
        const auto checkpoint = emulator.save_state();
        frames(emulator, 13);
        const auto expected_state = emulator.save_state();
        const auto expected_pixels = emulator.frame_buffer().pixels;
        const std::array<Byte, 4> expected_inputs = {1, 2, 8, 4};
        CHECK(wits_inputs(emulator) == expected_inputs);
        REQUIRE(emulator.load_state(checkpoint));
        frames(emulator, 13);
        CHECK((emulator.save_state() == expected_state));
        CHECK(emulator.frame_buffer().pixels == expected_pixels);
        CHECK(wits_inputs(emulator) == expected_inputs);
        CHECK(emulator.controller_mode() == ControllerMode::HoriFourPlayer);
    }
}
