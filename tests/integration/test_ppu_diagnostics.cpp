#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include "core/binary_file.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"

using namespace mapperbus::core;

TEST_CASE("Public Blargg NTSC PPU vblank and NMI timing diagnostics",
          "[ppu][timing][diagnostic][external]") {
    register_builtin_mappers();
    const char* root = std::getenv("MAPPERBUS_PPU_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_PPU_DIAGNOSTICS to the pinned public ROM directory");
    // christopherpow/nes-test-roms at 97720008e51db15dd281a2a1e64d4c65cf1bca4c.
    for (const auto& [name, hash] :
         {std::pair{"01-vbl_basics.nes",
                    "06aea5af4edab4e3141c939cd5ac9936f8758203b25dcaf84ae1a09db49e024a"},
          {"02-vbl_set_time.nes",
           "dd98856130078844e3aa4bd95a9be8ab501ea84c089f1d8ad49a1b20af4b3a80"},
          {"03-vbl_clear_time.nes",
           "787fdaa4dd6c5b6df5f4308fb6d55b57e2c2f69bd5ecdf8ad5c69735db4fcc72"},
          {"04-nmi_control.nes",
           "84722c75b896c47c8642f83220230fe14f0a31e55e26ecb83c400e6a26d91b32"},
          {"05-nmi_timing.nes", "72e515d689d7404ae5779b8c9c4c7b3563a755a94bd44864516f1b03df044482"},
          {"06-suppression.nes",
           "811dd5997bbf48c2e5687ab06845f17ea76b2be472786596c334137582cc72aa"},
          {"07-nmi_on_timing.nes",
           "1ed154363660b5775b112ae63ce9bb4e400ebde2afef4d0ac12fc433efda3702"},
          {"08-nmi_off_timing.nes",
           "1d2a4093091c8e58a7f99d6a3531bbc6346b52cfc59bcb17ca04c1f2376cf2fc"},
          {"09-even_odd_frames.nes",
           "1ac04283021ddd9294cc74ee709c55e20a350dc4815c15a8a93b3654837e858d"},
          {"10-even_odd_timing.nes",
           "7217d2d172ce11ad45c4da40c2f22201cf0eb758bc2cd8dd39d2cf0a7d4ca83e"}}) {
        CAPTURE(name);
        const auto path = std::filesystem::path(root) / name;
        auto bytes = read_binary_file_capped(path.string(), kMaxRomBytes, "PPU diagnostic");
        REQUIRE(bytes);
        REQUIRE(sha256_hex(rom_sha256(*bytes)) == hash);
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(path.string()));
        emulator.reset();
        bool started = false;
        bool done = false;
        while (emulator.cpu().cycles() < 20000000) {
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
