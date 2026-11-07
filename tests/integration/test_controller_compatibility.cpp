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
