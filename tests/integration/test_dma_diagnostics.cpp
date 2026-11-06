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