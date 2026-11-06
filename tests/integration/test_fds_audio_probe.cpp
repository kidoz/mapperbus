#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/cpu/cpu.hpp"
#include "core/emulator.hpp"
#include "core/ppu/ppu.hpp"

using namespace mapperbus::core;

namespace {
std::vector<Byte> probe_image() {
    auto bytes = read_binary_file_capped(MAPPERBUS_FDS_AUDIO_PROBE_PATH, 65500, "audio probe");
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 65500);
    return *bytes;
}

void check_capture(const auto& read) {
    REQUIRE(read(0x05FF) == 0xA5);
    // These are expectations for the current approximation, not hardware
    // observations. Raw hardware captures must remain independent of these.
    constexpr std::array<Byte, 4> kSettledProduct = {3, 0xC1, 0xFC, 4};
    for (std::size_t page = 0; page < 4; ++page) {
        for (std::size_t phase = 0; phase < 32; ++phase) {
            INFO("page " << page << " phase " << phase);
            CHECK(read(static_cast<Address>(0xD000 + page * 64 + phase)) == kSettledProduct[page]);
        }
    }
    // Master speed zero runs both envelopes in this model. Master one is
    // slower, and master 255 cannot tick within the capture window.
    CHECK((read(0xD000 + 10 * 64 + 31) & 0x3FU) < 32);
    CHECK((read(0xD000 + 11 * 64 + 31) & 0x3FU) < (read(0xD000 + 10 * 64 + 31) & 0x3FU));
    for (std::size_t page : {14, 15}) {
        for (std::size_t phase = 0; phase < 32; ++phase) {
            CHECK((read(static_cast<Address>(0xD000 + page * 64 + phase)) & 0x3FU) == 32);
            CHECK((read(static_cast<Address>(0xD000 + page * 64 + 32 + phase)) & 0x3FU) == 16);
        }
    }
}

void artifact(Emulator& emulator) {
    const auto* output = std::getenv("MAPPERBUS_FDS_TEST_OUTPUT");
    if (output == nullptr || *output == '\0')
        return;
    std::filesystem::create_directories(output);
    const std::string header = "P6\n256 240\n255\n";
    std::vector<Byte> bytes(header.begin(), header.end());
    for (const auto pixel : emulator.frame_buffer().pixels) {
        bytes.push_back(static_cast<Byte>(pixel >> 16U));
        bytes.push_back(static_cast<Byte>(pixel >> 8U));
        bytes.push_back(static_cast<Byte>(pixel));
    }
    REQUIRE(write_binary_file_atomic(
        (std::filesystem::path(output) / "audio-probe.ppm").string(), bytes, "audio probe frame"));
    std::vector<Byte> capture(1024);
    for (std::size_t index = 0; index < capture.size(); ++index)
        capture[index] = emulator.read_cpu(static_cast<Address>(0xD000 + index));
    REQUIRE(write_binary_file_atomic(
        (std::filesystem::path(output) / "audio-probe-emulator.bin").string(),
        capture,
        "audio probe capture"));
    const std::string metadata = "{\"format\":\"mapperbus-fds-audio-capture\",\"version\":1,"
                                 "\"source\":\"mapperbus-emulator\",\"probe_sha256\":\"" +
                                 sha256_hex(rom_sha256(probe_image())) + R"(","capture_sha256":")" +
                                 sha256_hex(rom_sha256(capture)) +
                                 "\",\"data_file\":\"audio-probe-emulator.bin\"}\n";
    const std::vector<Byte> metadata_bytes(metadata.begin(), metadata.end());
    REQUIRE(write_binary_file_atomic(
        (std::filesystem::path(output) / "audio-probe-emulator.json").string(),
        metadata_bytes,
        "audio probe provenance"));
}
} // namespace

TEST_CASE("Generated FDS audio probe executes all capture pages without licensed assets",
          "[integration][fds][probe]") {
    const auto disk = probe_image();
    Fds drive;
    REQUIRE(drive.load_disk(disk));
    std::vector<Byte> bios(8192, 0xEA);
    for (std::size_t vector = 8192 - 6; vector < bios.size(); vector += 2) {
        bios[vector] = 0;
        bios[vector + 1] = 0x60;
    }
    auto loaded = Cartridge::from_fds_bios(bios, drive);
    REQUIRE(loaded);
    auto cartridge = std::move(*loaded);
    // Test bootstrap: copy the first original program file to adapter RAM.
    // The separate opt-in case validates actual disk boot with the supplied BIOS.
    REQUIRE(disk[58] == 3);
    const auto length =
        static_cast<std::size_t>(disk[71]) | (static_cast<std::size_t>(disk[72]) << 8U);
    REQUIRE(length > 0);
    REQUIRE(length < 0x7000);
    for (std::size_t index = 0; index < length; ++index)
        cartridge.write_prg(static_cast<Address>(0x6000 + index), disk[75 + index]);
    Ppu ppu;
    MemoryBus bus;
    bus.connect_cartridge(&cartridge);
    bus.connect_fds(&drive);
    bus.connect_ppu(&ppu);
    ppu.connect_cartridge(&cartridge);
    ppu.reset();
    Cpu cpu(bus);
    cpu.reset();
    std::uint64_t elapsed = 0;
    while (bus.read(0x05FF) != 0xA5 && elapsed < 8000000) {
        const auto clocks = cpu.step();
        REQUIRE_FALSE(cpu.jammed());
        ppu.step(clocks);
        drive.step(clocks);
        for (std::uint32_t cycle = 0; cycle < clocks; ++cycle)
            drive.clock_audio();
        elapsed += clocks;
    }
    check_capture([&](Address address) { return bus.read(address); });
}

TEST_CASE("Generated FDS audio probe boots with the supplied BIOS and displays captures",
          "[integration][fds][probe]") {
    const auto* bios_path = std::getenv("MAPPERBUS_FDS_TEST_BIOS");
    if (bios_path == nullptr || *bios_path == '\0')
        SKIP("Set MAPPERBUS_FDS_TEST_BIOS to validate real BIOS boot of the original probe");
    auto bios = read_binary_file_capped(bios_path, 8192, "FDS BIOS");
    REQUIRE(bios);
    Emulator emulator;
    REQUIRE(emulator.load_fds(*bios, probe_image()));
    emulator.reset();
    for (int frame = 0; frame < 600; ++frame) {
        emulator.step_frame();
        REQUIRE_FALSE(emulator.cpu().jammed());
        std::array<float, 4096> samples{};
        while (emulator.drain_audio(samples.data(), samples.size()) != 0) {
        }
    }
    INFO("PC " << emulator.cpu().pc() << " NMI flag "
               << static_cast<int>(emulator.read_cpu(0x100)));
    artifact(emulator);
    check_capture([&](Address address) { return emulator.read_cpu(address); });
    // The probe's own hex display should contain the first raw product byte.
    CHECK(emulator.ppu().vram()[8 * 32 + 3] == 1); // high digit 0
    CHECK(emulator.ppu().vram()[8 * 32 + 4] == 4); // low digit 3
    for (const auto button : {Button::Right, Button::Left}) {
        emulator.controller().set_button_state(0, button, true);
        for (int frame = 0; frame < 5; ++frame)
            emulator.step_frame();
        emulator.controller().set_button_state(0, button, false);
        for (int frame = 0; frame < 5; ++frame)
            emulator.step_frame();
        const Byte expected_page = button == Button::Right ? 1 : 0;
        CHECK(emulator.read_cpu(0x14) == expected_page);
        CHECK(emulator.ppu().vram()[3 * 32 + 8] == expected_page + 1);
    }
    CHECK(emulator.read_cpu(0x14) == 0); // page navigation returned to first page
    emulator.controller().set_button_state(0, Button::A, true);
    for (int frame = 0; frame < 5; ++frame)
        emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x05FF) == 0); // A began a fresh capture
    emulator.controller().set_button_state(0, Button::A, false);
    for (int frame = 0; frame < 300 && emulator.read_cpu(0x05FF) != 0xA5; ++frame) {
        emulator.step_frame();
        REQUIRE_FALSE(emulator.cpu().jammed());
        std::array<float, 4096> samples{};
        while (emulator.drain_audio(samples.data(), samples.size()) != 0) {
        }
    }
    check_capture([&](Address address) { return emulator.read_cpu(address); });
}
