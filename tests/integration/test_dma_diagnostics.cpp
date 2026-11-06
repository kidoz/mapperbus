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

TEST_CASE("AccuracyCoin explicit and implicit DMC DMA abort diagnostics",
          "[cpu][dma][diagnostic][external]") {
    const char* root = std::getenv("MAPPERBUS_DMC_STOP_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_DMC_STOP_DIAGNOSTICS to the pinned AccuracyCoin directory");
    // 100thCoin/AccuracyCoin at 66e8711292d432cf009b2831f3ea1a055d75c9ba.
    const auto path = std::filesystem::path(root) / "AccuracyCoin.nes";
    auto bytes = read_binary_file_capped(path.string(), kMaxRomBytes, "DMC stop diagnostic");
    REQUIRE(bytes);
    REQUIRE(sha256_hex(rom_sha256(*bytes)) ==
            "9332c3735dd7dea2f4711e670d6478d8b0085edeacdd4f9f300fb04a8bbe3f88");
    register_builtin_mappers();
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(path.string()));
    REQUIRE(emulator.region() == Region::NTSC);
    emulator.reset();
    // Run the unmodified ROM through its own boot and controller menu. $EC is
    // its documented boot progress byte; $14/$16 are page/cursor indices.
    while (emulator.read_cpu(0xEC) != 10 && emulator.cpu().cycles() < 3000000)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0xEC) == 10);
    REQUIRE(emulator.read_cpu(0x12) == 1); // ROM's DMA/open-bus synchronization pretest
    const auto advance = [&](uint64_t cycles) {
        const auto end = emulator.cpu().cycles() + cycles;
        while (emulator.cpu().cycles() < end)
            emulator.step_instruction();
    };
    const auto press = [&](Button button) {
        emulator.controller().set_button_state(0, button, true);
        advance(180000);
        emulator.controller().set_button_state(0, button, false);
        advance(180000);
    };
    for (int page = 0; page < 12; ++page)
        press(Button::Right);
    REQUIRE(emulator.read_cpu(0x14) == 12);
    REQUIRE(emulator.read_cpu(0x16) == 0xFF);
    for (int row = 0; row < 9; ++row)
        press(Button::Down);
    REQUIRE(emulator.read_cpu(0x16) == 8);
    const auto run_test = [&](Address result, Byte expected) {
        press(Button::A);
        const auto end = emulator.cpu().cycles() + 8000000;
        while ((emulator.read_cpu(result) == 0 || emulator.read_cpu(result) == 3) &&
               emulator.cpu().cycles() < end)
            emulator.step_instruction();
        CAPTURE(result, expected, emulator.read_cpu(result));
        REQUIRE(emulator.read_cpu(result) == expected);
    };
    run_test(0x479, 1); // Explicit DMA Abort: ordinary PASS
    constexpr std::array<Byte, 16> kExplicit = {4, 4, 4, 4, 4, 4, 3, 4, 1, 1, 0, 0, 0, 0, 0, 0};
    for (std::size_t index = 0; index < kExplicit.size(); ++index) {
        CAPTURE(index);
        CHECK(emulator.read_cpu(static_cast<Address>(0x50 + index)) == kExplicit[index]);
    }
    press(Button::Down);
    REQUIRE(emulator.read_cpu(0x16) == 9);
    run_test(0x478, 5); // Implicit DMA Abort: late-2A03G/2A03H alternate PASS
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
