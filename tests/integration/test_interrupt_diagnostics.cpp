#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include "core/binary_file.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"

using namespace mapperbus::core;

TEST_CASE("Public Blargg CPU IRQ and NMI timing diagnostics",
          "[cpu][interrupt][diagnostic][external]") {
    register_builtin_mappers();
    const char* root = std::getenv("MAPPERBUS_CPU_INTERRUPT_DIAGNOSTICS");
    if (root == nullptr || *root == '\0')
        SKIP("Set MAPPERBUS_CPU_INTERRUPT_DIAGNOSTICS to the pinned public ROM directory");
    // christopherpow/nes-test-roms at 97720008e51db15dd281a2a1e64d4c65cf1bca4c.
    for (const auto& [name, hash] :
         {std::pair{"1-cli_latency.nes",
                    "e402d36118f77dcbbe8ddca90c15fc76a46bcb30b25cb028c383e4a621de5fc0"},
          {"2-nmi_and_brk.nes", "6e6bf6205930afcfebdc213c583df53986a688a8b36f8856b805ef4c1853e6eb"},
          {"3-nmi_and_irq.nes", "3008a9524d174a8aca562ff0361eba81da53e38cf1ebb5125322fe151f14d945"},
          {"4-irq_and_dma.nes", "6d7b4c1947ada64679af56cf0c227286b2408afe1747dfaa4dc7363d57ff87f6"},
          {"5-branch_delays_irq.nes",
           "f9e10b4a24d8f3cd3e51fb7457c72858aab96a6467fdbbd806d0661c2d32fdc7"}}) {
        CAPTURE(name);
        const auto path = std::filesystem::path(root) / name;
        auto bytes = read_binary_file_capped(path.string(), kMaxRomBytes, "interrupt diagnostic");
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
