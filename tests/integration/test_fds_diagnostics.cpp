#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../support/fds_image.hpp"
#include "core/binary_file.hpp"
#include "core/cartridge/rom_crc32.hpp"
#include "core/emulator.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
struct DiagnosticMedia {
    std::vector<Byte> bios;
    std::vector<Byte> disk;
};

DiagnosticMedia load_diagnostic(Emulator& emulator,
                                const char* filename,
                                const char* digest,
                                const std::string& changes_path = {}) {
    const auto* directory = std::getenv("MAPPERBUS_FDS_DIAGNOSTICS");
    if (directory == nullptr || *directory == '\0') {
        SKIP("Fetch public diagnostics and set MAPPERBUS_FDS_DIAGNOSTICS to run this case");
    }
    const auto* bios_path = std::getenv("MAPPERBUS_FDS_TEST_BIOS");
    REQUIRE(bios_path != nullptr);
    auto bios = read_binary_file_capped(bios_path, 8192, "FDS BIOS");
    INFO((bios ? "BIOS read" : bios.error()));
    REQUIRE(bios);
    auto disk = read_binary_file_capped(
        (std::filesystem::path(directory) / filename).string(), 65516, "FDS diagnostic");
    INFO((disk ? filename : disk.error()));
    REQUIRE(disk);
    REQUIRE(sha256_hex(rom_sha256(*disk)) == digest);
    auto loaded = emulator.load_fds(*bios, *disk, changes_path);
    INFO((loaded ? "loaded" : loaded.error()));
    REQUIRE(loaded);
    emulator.reset();
    return {.bios = std::move(*bios), .disk = std::move(*disk)};
}

float frames(Emulator& emulator, int count) {
    float expansion_peak = 0;
    std::array<float, 4096> samples{};
    for (int frame = 0; frame < count; ++frame) {
        emulator.step_frame();
        REQUIRE_FALSE(emulator.cpu().jammed());
        const float expansion = emulator.fds().audio_output();
        REQUIRE(std::isfinite(expansion));
        expansion_peak = std::max(expansion_peak, std::abs(expansion));
        while (emulator.drain_audio(samples.data(), samples.size()) != 0) {
        }
    }
    return expansion_peak;
}

void press(Emulator& emulator, Button button) {
    emulator.controller().set_button_state(0, button, true);
    frames(emulator, 5);
    emulator.controller().set_button_state(0, button, false);
    frames(emulator, 5);
}

void artifact(const Emulator& emulator, const char* name) {
    const auto* output = std::getenv("MAPPERBUS_FDS_TEST_OUTPUT");
    if (output == nullptr || *output == '\0')
        return;
    std::filesystem::create_directories(output);
    const std::string header = "P6\n256 240\n255\n";
    std::vector<Byte> bytes(header.begin(), header.end());
    for (auto pixel : emulator.frame_buffer().pixels) {
        bytes.push_back(static_cast<Byte>(pixel >> 16U));
        bytes.push_back(static_cast<Byte>(pixel >> 8U));
        bytes.push_back(static_cast<Byte>(pixel));
    }
    REQUIRE(write_binary_file_atomic(
        (std::filesystem::path(output) / name).string(), bytes, "FDS diagnostic frame"));
}
struct DiskWriteFixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-fds-diagnostic-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    DiskWriteFixture() {
        std::filesystem::create_directories(root);
    }
    ~DiskWriteFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

constexpr auto kBiosDumperDigest =
    "84e6dab104af6ca174fe428cd4e568e55bff3ffb1adc026402095ebcb7347dcd";

DiagnosticMedia boot_bios_dumper(Emulator& emulator, const std::string& save = {}) {
    auto media = load_diagnostic(emulator, "bios-dumper.fds", kBiosDumperDigest, save);
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x14) == 1); // BGMode: selection, from upstream ram.asm
    REQUIRE(emulator.read_cpu(0x2E) == 0); // DumpMode: fast
    uint32_t calculated = 0;
    for (unsigned index = 0; index < 4; ++index)
        calculated |= static_cast<uint32_t>(emulator.read_cpu(static_cast<Address>(0x2A + index)))
                      << (index * 8U);
    REQUIRE(calculated == crc32(media.bios)); // program's independently executed CRC32
    return media;
}

template <typename Predicate> int wait_for(Emulator& emulator, Predicate ready, int limit = 10000) {
    int elapsed = 0;
    while (elapsed < limit && !ready()) {
        frames(emulator, 1);
        ++elapsed;
    }
    INFO("PC " << emulator.cpu().pc() << " BGMode " << int(emulator.read_cpu(0x14)) << " file "
               << int(emulator.read_cpu(0x27)) << " frames " << elapsed);
    REQUIRE(ready());
    return elapsed;
}

// V7 FDS states end with the length-prefixed physical track and nine bytes of
// audio/watchdog state. Validate the boundary before inspecting/injecting faults.
struct PhysicalTrack {
    std::vector<Byte> state;
    std::size_t offset = 0;
    explicit PhysicalTrack(const Fds& drive) {
        StateWriter writer;
        drive.save_state(writer);
        state = writer.take();
        constexpr std::size_t kTrackBytes = 84000;
        REQUIRE(state.size() >= kTrackBytes + 9 + sizeof(uint64_t));
        offset = state.size() - kTrackBytes - 9;
        StateReader size(std::span<const Byte>(state).subspan(offset - sizeof(uint64_t)));
        REQUIRE(size.read<uint64_t>() == kTrackBytes);
        REQUIRE(state[offset + 3537] == 0x80);
    }
    std::span<const Byte> bytes() const {
        return std::span<const Byte>(state).subspan(offset, 84000);
    }
};

// Normal-polynomial, bit-serial CRC, independently of the core's reflected
// byte update. FDS streams transmit each data byte least-significant bit first.
uint16_t physical_crc(std::span<const Byte> bytes) {
    uint32_t remainder = 0;
    for (const Byte byte : bytes) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto feedback = ((remainder >> 15U) ^ (static_cast<uint32_t>(byte) >> bit)) & 1U;
            remainder = (remainder << 1U) & 0xFFFFU;
            if (feedback != 0)
                remainder ^= 0x1021U;
        }
    }
    uint32_t reflected = 0;
    for (unsigned bit = 0; bit < 16; ++bit)
        reflected |= ((remainder >> bit) & 1U) << (15U - bit);
    return static_cast<uint16_t>(reflected);
}

void verify_dump(Emulator& emulator, const DiagnosticMedia& media, bool slow) {
    constexpr std::string_view kSuccess = "OK!";
    REQUIRE(std::equal(kSuccess.begin(), kSuccess.end(), emulator.ppu().vram().begin() + 0x195));
    emulator.fds().synchronize_disk();
    const auto disk = emulator.fds().disk_image();
    REQUIRE(disk[56] == 2);
    REQUIRE(disk[57] == (slow ? 12 : 5));
    std::vector<std::span<const Byte>> blocks{disk.first(56), disk.subspan(56, 2)};
    std::vector<Byte> dumped;
    std::size_t offset = 58;
    std::size_t original_offset = 58;
    for (unsigned file = 0; file < disk[57]; ++file) {
        REQUIRE(offset + 17 <= disk.size());
        REQUIRE(disk[offset] == 3);
        REQUIRE(disk[offset + 1] == file);
        const auto size = static_cast<std::size_t>(disk[offset + 13]) |
                          (static_cast<std::size_t>(disk[offset + 14]) << 8U);
        REQUIRE(size + 17 <= disk.size() - offset);
        REQUIRE(disk[offset + 16] == 4);
        blocks.push_back(disk.subspan(offset, 16));
        blocks.push_back(disk.subspan(offset + 16, size + 1));
        if (file < 4) {
            REQUIRE(std::ranges::equal(
                disk.subspan(offset, size + 17),
                std::span<const Byte>(media.disk).subspan(original_offset, size + 17)));
            original_offset += size + 17;
        } else {
            REQUIRE(std::string(disk.begin() + static_cast<std::ptrdiff_t>(offset + 3),
                                disk.begin() + static_cast<std::ptrdiff_t>(offset + 11)) ==
                    "DISKSYS" + std::to_string(file - 4));
            REQUIRE(size == (slow ? 1024 : 8192));
            REQUIRE(disk[offset + 11] == 0);
            REQUIRE(disk[offset + 12] == 0xE0 + (slow ? (file - 4) * 4 : 0));
            dumped.insert(dumped.end(),
                          disk.begin() + static_cast<std::ptrdiff_t>(offset + 17),
                          disk.begin() + static_cast<std::ptrdiff_t>(offset + 17 + size));
        }
        offset += size + 17;
    }
    REQUIRE(dumped == media.bios);
    // Check the actual post-write track before any remount can regenerate CRCs.
    PhysicalTrack snapshot(emulator.fds());
    const auto track = snapshot.bytes();
    std::size_t position = 0;
    for (const auto block : blocks) {
        while (position < track.size() && track[position] == 0)
            ++position;
        REQUIRE(position + block.size() + 3 <= track.size());
        REQUIRE(track[position] == 0x80);
        REQUIRE(std::ranges::equal(block, track.subspan(position + 1, block.size())));
        const auto crc = physical_crc(track.subspan(position, block.size() + 1));
        REQUIRE(track[position + block.size() + 1] == static_cast<Byte>(crc));
        REQUIRE(track[position + block.size() + 2] == static_cast<Byte>(crc >> 8U));
        position += block.size() + 3;
    }
}

void check_bios_dump(bool slow) {
    DiskWriteFixture fixture;
    const auto path = (fixture.root / "progress.fds-save").string();
    Emulator emulator;
    const auto media = boot_bios_dumper(emulator, path);
    const auto identity = emulator.content_hash();
    if (slow) {
        press(emulator, Button::Select);
        REQUIRE(emulator.read_cpu(0x2E) == 1);
    }
    press(emulator, Button::Start);
    wait_for(emulator, [&] {
        return emulator.fds().disk_active() && (emulator.read_cpu(0xFA) & 0x54U) == 0x40;
    }); // BIOS control mirror: data write
    frames(emulator, 1);
    wait_for(emulator, [&] {
        return emulator.fds().disk_active() && (emulator.read_cpu(0xFA) & 0x54U) == 0x40;
    });
    const auto state = emulator.save_state();
    Emulator replay;
    REQUIRE(replay.load_fds(media.bios, media.disk));
    REQUIRE(replay.load_state(state));
    const int elapsed = wait_for(emulator, [&] { return emulator.read_cpu(0x14) == 5; });
    frames(emulator, 2);
    frames(replay, elapsed + 2);
    REQUIRE(replay.save_state() == emulator.save_state());
    verify_dump(emulator, media, slow);
    artifact(emulator, slow ? "bios-dumper-slow.ppm" : "bios-dumper-fast.ppm");
    REQUIRE(emulator.flush_disk_changes());
    Emulator fresh;
    REQUIRE(fresh.load_fds(media.bios, media.disk, path));
    REQUIRE(fresh.content_hash() == identity);
    REQUIRE(std::ranges::equal(fresh.fds().disk_image(), emulator.fds().disk_image()));
}

void check_bios_dump_retry(bool crc_fault, std::size_t crc_byte = 0) {
    Emulator emulator;
    const auto media = boot_bios_dumper(emulator);
    if (crc_fault) {
        PhysicalTrack snapshot(emulator.fds());
        snapshot.state[snapshot.offset + 3537 + 1 + 56 + crc_byte] ^= 1U;
        StateReader reader(snapshot.state);
        emulator.fds().load_state(reader);
        REQUIRE(reader.ok());
    } else {
        emulator.fds().set_write_protected(true);
    }
    press(emulator, Button::Start);
    const std::string expected = crc_fault ? "Err. 27" : "Err. 03";
    wait_for(
        emulator,
        [&] {
            const auto& tiles = emulator.ppu().vram();
            return std::equal(expected.begin(), expected.end(), tiles.begin() + 0x195);
        },
        2400);
    REQUIRE(std::ranges::equal(emulator.fds().disk_image(), media.disk));
    REQUIRE_FALSE(emulator.fds().dirty());
    artifact(emulator,
             crc_fault ? (crc_byte == 0 ? "bios-dumper-crc-low-error.ppm"
                                        : "bios-dumper-crc-high-error.ppm")
                       : "bios-dumper-protected.ppm");
    emulator.fds().set_write_protected(false);
    emulator.fds().eject();
    frames(emulator, 4);
    emulator.fds().insert_side(0);
    wait_for(emulator, [&] { return emulator.read_cpu(0x14) == 5; });
    frames(emulator, 2);
    verify_dump(emulator, media, false);
}
} // namespace

TEST_CASE("Public FDS BIOS dumper writes one verified 8 KiB file and replays mid-write state",
          "[integration][fds][diagnostic][write]") {
    check_bios_dump(false);
}

TEST_CASE("Public FDS BIOS dumper writes eight verified 1 KiB files and replays mid-write state",
          "[integration][fds][diagnostic][write]") {
    check_bios_dump(true);
}

TEST_CASE("Public FDS BIOS dumper reports write protection and retries after reinsertion",
          "[integration][fds][diagnostic][write]") {
    check_bios_dump_retry(false);
}

TEST_CASE("Public FDS BIOS dumper reports error 27 for either CRC byte and retries",
          "[integration][fds][diagnostic][write]") {
    for (std::size_t crc_byte = 0; crc_byte < 2; ++crc_byte) {
        CAPTURE(crc_byte);
        check_bios_dump_retry(true, crc_byte);
    }
}

TEST_CASE("FDS physical CRCs match fixed vectors from the pinned FDSKey reference",
          "[fds][diagnostic][crc]") {
    // FDSKey f07ab828, fdsemu.c fds_crc(): seed $8000, delayed input,
    // two appended zero bytes, excluding the sync byte. These constants were
    // generated with that algorithm, not MapperBus's CRC routine.
    constexpr std::array<Byte, 10> kDigits{0x80, '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    REQUIRE(physical_crc(kDigits) == 0x837F);
    Fds drive;
    REQUIRE(drive.load_disk(mapperbus::tests::make_fds_image()));
    PhysicalTrack snapshot(drive);
    const auto track = snapshot.bytes();
    constexpr std::array<std::size_t, 4> kSizes{56, 2, 16, 257};
    constexpr std::array<uint16_t, 4> kCrcs{0x85C3, 0x2ED5, 0x5D15, 0x020D};
    std::size_t position = 3537;
    for (std::size_t index = 0; index < kSizes.size(); ++index) {
        CAPTURE(index);
        const auto size = kSizes[index];
        REQUIRE(track[position] == 0x80);
        REQUIRE(physical_crc(track.subspan(position, size + 1)) == kCrcs[index]);
        REQUIRE(track[position + size + 1] == static_cast<Byte>(kCrcs[index]));
        REQUIRE(track[position + size + 2] == static_cast<Byte>(kCrcs[index] >> 8U));
        REQUIRE(physical_crc(track.subspan(position, size + 3)) == 0);
        position += size + 3 + 122;
    }
}

TEST_CASE("Public Sour FDS IRQ v7 reports success for all 21 cases",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "fdsirqtests-v7.fds",
                    "38c3ed71d73c71bd09c54e687173bcc2ceea9aa78c78a2813f6c56106814e86b");
    frames(emulator, 600);
    // The ROM increments $0303 per case. Its own comparison writes 'X' on
    // failure and 'O' on success into $0404 + case, independently of rendering.
    REQUIRE(emulator.read_cpu(0x0303) == 21);
    for (int test = 1; test <= 21; ++test) {
        INFO("Sour IRQ case " << test);
        REQUIRE(emulator.read_cpu(static_cast<Address>(0x0404 + test)) == 'O');
    }
    REQUIRE(emulator.cpu().pc() == 0x6093); // completed, displaying results
    artifact(emulator, "sour-irq-v7.ppm");
}

TEST_CASE("Public FDS mirroring diagnostic passes all three checks",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "mirroring-test.fds",
                    "501bda77be6190bfb57d07a707880e74029e3e0f910441f6f7ba88431e70c2e3");
    frames(emulator, 600);
    // Symbols from the upstream zero-page allocation: Mode and failure counts.
    REQUIRE(emulator.read_cpu(0x15) == 1);
    REQUIRE(emulator.read_cpu(0x16) == 0); // NametableFails
    REQUIRE(emulator.read_cpu(0x17) == 0); // ReadFails
    REQUIRE(emulator.read_cpu(0x18) == 0); // IOToggleFails
    artifact(emulator, "mirroring.ppm");
}

TEST_CASE("Public FDS I/O viewer observes disk removal with I/O writes disabled",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "4023-test.fds",
                    "958dfad1656aaf1c3c82b162b68acada253ff59ed01726ff79430222fa49df3c");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x14) == 0x83); // upstream FDS_IO_ENABLE_MIRROR
    press(emulator, Button::A);
    REQUIRE(emulator.read_cpu(0x14) == 0x82);
    emulator.fds().eject();
    frames(emulator, 4);
    // Upstream DumpRegisters writes $4032 to nametable row 13, column 10.
    constexpr auto kDriveStatusTile = 13 * 32 + 10;
    REQUIRE((emulator.ppu().vram()[kDriveStatusTile] & 5U) == 5);
    emulator.fds().insert_side(0);
    frames(emulator, 4);
    REQUIRE((emulator.ppu().vram()[kDriveStatusTile] & 1U) == 0);
    press(emulator, Button::A);
    REQUIRE(emulator.read_cpu(0x14) == 0x83);
    artifact(emulator, "io-reset.ppm");
}

TEST_CASE("Public FDS audio viewer plays, resets, and reads the modulation counter",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "audio-registers.fds",
                    "80ccda5443653426f27e6b4b3125c9834e97931c50b9fcdccc00b8367ef9fe28");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x14) == 0x83);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0);
    // Upstream DoNothing copies $4090-$4097 into eight raw hex tiles at
    // nametable row 12, column 12 using the BIOS PrepareVRAMString routine.
    // Inspect the program's CPU-driven reads, not just direct host reads.
    constexpr auto kRegisterTiles = 12 * 32 + 12;
    const auto& tiles = emulator.ppu().vram();
    REQUIRE(tiles[kRegisterTiles + 2] == 0x7F);         // mod gain 63 + open-bus bit 6
    REQUIRE(tiles[kRegisterTiles + 3] == 0);            // halted mod accumulator
    REQUIRE(tiles[kRegisterTiles + 4] == 0);            // zero counter * gain
    REQUIRE((tiles[kRegisterTiles + 5] & 0x0FU) == 12); // reset entry's increment code
    REQUIRE(tiles[kRegisterTiles + 6] == 0x40);         // muted PWM output
    press(emulator, Button::Up);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0x7F);
    REQUIRE(tiles[kRegisterTiles + 4] == 0xFC); // settled (-1 * 63) >> 4
    press(emulator, Button::Up);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0x7E);
    REQUIRE(tiles[kRegisterTiles + 4] == 0xF8); // settled (-2 * 63) >> 4
    press(emulator, Button::A);
    REQUIRE(frames(emulator, 20) > 0.001f);
    bool saw_wave_phase = false, saw_mod_phase = false;
    for (int frame = 0; frame < 32; ++frame) {
        frames(emulator, 1);
        saw_wave_phase |= tiles[kRegisterTiles + 1] != 0;
        saw_mod_phase |= tiles[kRegisterTiles + 3] != 0;
    }
    REQUIRE(saw_wave_phase);
    REQUIRE(saw_mod_phase);
    const auto playing = emulator.save_state();
    frames(emulator, 8);
    const auto replay = emulator.save_state();
    REQUIRE(emulator.load_state(playing));
    frames(emulator, 8);
    REQUIRE(emulator.save_state() == replay);
    artifact(emulator, "audio-playing.ppm");
    press(emulator, Button::B);
    REQUIRE(emulator.read_cpu(0x14) == 0x81);
    REQUIRE((emulator.fds().read(0x4090) & 0x3FU) == 0);
    REQUIRE(frames(emulator, 10) == 0.0f);
    artifact(emulator, "audio-reset.ppm");
    press(emulator, Button::B);
    press(emulator, Button::A);
    REQUIRE(frames(emulator, 20) > 0.001f);
}

TEST_CASE("Public FDS watchdog diagnostic captures two NOP-slide IRQ addresses",
          "[integration][fds][diagnostic][watchdog]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "4030d1-addr.fds",
                    "ed9f62f8c85c3d94909359a55ff94a53427473378a2eff556f4b465f268fe0c5");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x10) == 2); // upstream IRQ_Count
    // Despite the upstream array names, the stack extraction stores the
    // high bytes at $11/$12 and low bytes at $13/$14.
    const auto first = static_cast<Address>((static_cast<uint32_t>(emulator.read_cpu(0x11)) << 8U) |
                                            emulator.read_cpu(0x13));
    const auto second = static_cast<Address>(
        (static_cast<uint32_t>(emulator.read_cpu(0x12)) << 8U) | emulator.read_cpu(0x14));
    INFO("IRQ addresses " << first << " / " << second);
    artifact(emulator, "watchdog-addresses.ppm");
    // Upstream's published Twin Famicom capture shows $67FF / $6E25.
    CHECK(first == 0x67FF);
    CHECK(second == 0x6E25);
}
