#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include "core/binary_file.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"

using namespace mapperbus::core;

TEST_CASE("Public Blargg OAM and DMC DMA timing diagnostics", "[cpu][dma][diagnostic][external]") {
    register_builtin_mappers();
    const char* root = std::getenv("MAPPERBUS_CPU_DMA_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_CPU_DMA_DIAGNOSTICS to the pinned public ROM directory");
    // christopherpow/nes-test-roms at 97720008e51db15dd281a2a1e64d4c65cf1bca4c.
    for (const auto& [name, hash] :
         {std::pair{"sprdma_and_dmc_dma.nes",
                    "db3199bc1b0bdc07a316b3ab999d8fd8bb361456d2154e364c132cb06a26a10f"},
          {"sprdma_and_dmc_dma_512.nes",
           "3789f5134b0561b4344e3f4ce08b4d2a416f67435e083917a80d87fdb9d3583c"}}) {
        CAPTURE(name);
        const auto path = std::filesystem::path(root) / name;
        auto bytes = read_binary_file_capped(path.string(), kMaxRomBytes, "DMA diagnostic");
        REQUIRE(bytes);
        REQUIRE(sha256_hex(rom_sha256(*bytes)) == hash);
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(path.string()));
        emulator.reset();
        bool started = false;
        bool done = false;
        while (emulator.cpu().cycles() < 8000000) {
            emulator.step_instruction();
            if (emulator.read_cpu(0x6001) != 0xDE || emulator.read_cpu(0x6002) != 0xB0 ||
                emulator.read_cpu(0x6003) != 0x61)
                continue;
            const auto result = emulator.read_cpu(0x6000);
            started |= result == 0x80;
            if (started && result < 0x80) {
                done = true;
                break;
            }
        }
        std::string output;
        for (Address address = 0x6004; address < 0x6800; ++address) {
            const auto byte = emulator.read_cpu(address);
            if (byte == 0)
                break;
            output.push_back(static_cast<char>(byte));
        }
        INFO(output);
        REQUIRE(done);
        CHECK(emulator.read_cpu(0x6000) == 0);
        CHECK(output.contains("Passed"));
    }
}

namespace {
struct AccuracyCoinMachine {
    Emulator emulator;

    explicit AccuracyCoinMachine(const std::filesystem::path& path,
                                 ControllerTiming timing = ControllerTiming::NesAvFamicom) {
        // 100thCoin/AccuracyCoin at 66e8711292d432cf009b2831f3ea1a055d75c9ba.
        auto bytes = read_binary_file_capped(path.string(), kMaxRomBytes, "DMA diagnostic");
        REQUIRE(bytes);
        REQUIRE(sha256_hex(rom_sha256(*bytes)) ==
                "9332c3735dd7dea2f4711e670d6478d8b0085edeacdd4f9f300fb04a8bbe3f88");
        register_builtin_mappers();
        REQUIRE(emulator.load_cartridge(path.string()));
        REQUIRE(emulator.region() == Region::NTSC);
        emulator.set_controller_timing(timing);
        emulator.reset();
        // The original ROM's documented boot progress and DMA/open-bus pretest.
        while (emulator.read_cpu(0xEC) != 10 && emulator.cpu().cycles() < 3000000)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0xEC) == 10);
        REQUIRE(emulator.read_cpu(0x12) == 1);
    }

    void advance(uint64_t cycles) {
        const auto end = emulator.cpu().cycles() + cycles;
        while (emulator.cpu().cycles() < end)
            emulator.step_instruction();
    }

    void press(Button button) {
        emulator.controller().set_button_state(0, button, true);
        advance(180000);
        emulator.controller().set_button_state(0, button, false);
        advance(180000);
    }

    void select(Byte page, Byte row) {
        if (emulator.read_cpu(0x14) != page) {
            for (int count = 0; emulator.read_cpu(0x16) != 0xFF && count < 16; ++count)
                press(Button::Up);
            REQUIRE(emulator.read_cpu(0x16) == 0xFF);
            for (int count = 0; emulator.read_cpu(0x14) != page && count < 32; ++count)
                press(emulator.read_cpu(0x14) < page ? Button::Right : Button::Left);
        }
        REQUIRE(emulator.read_cpu(0x14) == page);
        for (int count = 0; emulator.read_cpu(0x16) != row && count < 16; ++count)
            press(emulator.read_cpu(0x16) == 0xFF || emulator.read_cpu(0x16) < row ? Button::Down
                                                                                   : Button::Up);
        REQUIRE(emulator.read_cpu(0x16) == row);
    }

    void run_test(Address result, Byte expected) {
        press(Button::A);
        const auto end = emulator.cpu().cycles() + 8000000;
        while ((emulator.read_cpu(result) == 0 || emulator.read_cpu(result) == 3) &&
               emulator.cpu().cycles() < end)
            emulator.step_instruction();
        CAPTURE(result, expected, emulator.read_cpu(result));
        REQUIRE(emulator.read_cpu(result) == expected);
    }
};
} // namespace

TEST_CASE("AccuracyCoin explicit and implicit DMC DMA abort diagnostics",
          "[cpu][dma][diagnostic][external]") {
    const char* root = std::getenv("MAPPERBUS_DMC_STOP_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_DMC_STOP_DIAGNOSTICS to the pinned AccuracyCoin directory");
    AccuracyCoinMachine fixture(std::filesystem::path(root) / "AccuracyCoin.nes");
    auto& emulator = fixture.emulator;
    fixture.select(12, 8);
    fixture.run_test(0x479, 1); // Explicit DMA Abort: ordinary PASS
    constexpr std::array<Byte, 16> kExplicit = {4, 4, 4, 4, 4, 4, 3, 4, 1, 1, 0, 0, 0, 0, 0, 0};
    for (std::size_t index = 0; index < kExplicit.size(); ++index) {
        CAPTURE(index);
        CHECK(emulator.read_cpu(static_cast<Address>(0x50 + index)) == kExplicit[index]);
    }
    fixture.select(12, 9);
    fixture.run_test(0x478, 5); // Implicit DMA Abort: late-2A03G/2A03H alternate PASS
    constexpr std::array<Byte, 16> kImplicit = {0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 1, 1, 0, 0, 0, 0};
    constexpr std::array<Byte, 16> kWrite = {0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 1, 0, 0, 0, 0, 0};
    constexpr std::array<Byte, 16> kLoop = {0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 4};
    for (std::size_t index = 0; index < kImplicit.size(); ++index) {
        CAPTURE(index);
        CHECK(emulator.read_cpu(static_cast<Address>(0x500 + index)) == kImplicit[index]);
        CHECK(emulator.read_cpu(static_cast<Address>(0x520 + index)) == kWrite[index]);
        CHECK(emulator.read_cpu(static_cast<Address>(0x540 + index)) == kLoop[index]);
    }
}

TEST_CASE("AccuracyCoin NTSC DMA register conflicts and controller read edges",
          "[cpu][dma][controller][diagnostic][external]") {
    const char* root = std::getenv("MAPPERBUS_DMA_CONFLICT_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_DMA_CONFLICT_DIAGNOSTICS to the pinned AccuracyCoin directory");
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        CAPTURE(timing);
        AccuracyCoinMachine fixture(std::filesystem::path(root) / "AccuracyCoin.nes", timing);
        const Byte controller_pass = timing == ControllerTiming::RfFamicom ? 9 : 5;
        struct Diagnostic {
            Byte page;
            Byte row;
            Address result;
            Byte expected;
        };
        for (const auto& test : std::array{
                 Diagnostic{.page = 12, .row = 4, .result = 0x45D, .expected = 1}, // DMA + $4015
                 Diagnostic{.page = 12,
                            .row = 5,
                            .result = 0x45E,
                            .expected = controller_pass}, // DMA + $4016
                 Diagnostic{
                     .page = 12, .row = 6, .result = 0x46B, .expected = 1}, // DMC bus conflicts
                 Diagnostic{.page = 13,
                            .row = 6,
                            .result = 0x45C,
                            .expected = 9}, // APU activation, pad clocks
                 Diagnostic{
                     .page = 13, .row = 7, .result = 0x45F, .expected = 1}, // Controller strobing
                 Diagnostic{.page = 13, .row = 8, .result = 0x47A, .expected = controller_pass}
                 // Controller clocking
             }) {
            CAPTURE(test.page, test.row);
            fixture.select(test.page, test.row);
            fixture.run_test(test.result, test.expected);
        }
    }
}
