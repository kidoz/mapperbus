#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco163.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

[[nodiscard]] std::uint64_t hash_frame(const FrameBuffer& fb) {
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a
    for (std::uint32_t pixel : fb.pixels) {
        h = (h ^ pixel) * 1099511628211ULL;
    }
    return h;
}

} // namespace

TEST_CASE("StateWriter/StateReader round-trips primitives, arrays and byte runs", "[state]") {
    StateWriter writer;
    writer.write<std::uint8_t>(0xAB);
    writer.write<std::uint16_t>(0x1234);
    writer.write<std::uint32_t>(0xDEADBEEF);
    writer.write<std::uint64_t>(0x0102030405060708ULL);
    writer.write<bool>(true);
    const std::array<std::uint8_t, 4> arr = {1, 2, 3, 4};
    writer.write_array(arr);
    const std::vector<Byte> run = {9, 8, 7};
    writer.write_bytes(run);

    StateReader reader(writer.data());
    REQUIRE(reader.read<std::uint8_t>() == 0xAB);
    REQUIRE(reader.read<std::uint16_t>() == 0x1234);
    REQUIRE(reader.read<std::uint32_t>() == 0xDEADBEEF);
    REQUIRE(reader.read<std::uint64_t>() == 0x0102030405060708ULL);
    REQUIRE(reader.read<bool>() == true);
    std::array<std::uint8_t, 4> arr_out{};
    reader.read_array(arr_out);
    REQUIRE(arr_out == arr);
    std::vector<Byte> run_out;
    reader.read_bytes(run_out);
    REQUIRE(run_out == run);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0);
}

TEST_CASE("StateReader flags truncated reads", "[state]") {
    StateWriter writer;
    writer.write<std::uint16_t>(0x1234);

    StateReader reader(writer.data());
    REQUIRE(reader.read<std::uint16_t>() == 0x1234);
    REQUIRE(reader.ok());
    (void)reader.read<std::uint32_t>(); // past the end
    REQUIRE_FALSE(reader.ok());
}

TEST_CASE("StateReader rejects a byte run with a corrupt length prefix", "[state]") {
    StateWriter writer;
    const std::vector<Byte> run = {1, 2, 3};
    writer.write_bytes(run);
    std::vector<Byte> blob = writer.data();

    // Overwrite the u64 length with UINT64_MAX so that pos_ + size wraps to a
    // small value — an overflow-vulnerable bounds check would accept it and
    // read far out of bounds.
    const std::uint64_t huge = 0xFFFF'FFFF'FFFF'FFFFULL;
    std::memcpy(blob.data(), &huge, sizeof(huge));

    StateReader reader(blob);
    std::vector<Byte> out;
    reader.read_bytes(out);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(out.empty());
}

TEST_CASE("StateReader rejects a byte run above the caller limit", "[state]") {
    StateWriter writer;
    const std::vector<Byte> run(8, 1);
    writer.write_bytes(run);

    StateReader reader(writer.data());
    std::vector<Byte> out;
    reader.read_bytes(out, 4);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(out.empty());
}

TEST_CASE("Save-state is deterministic across a save/load cycle", "[state][integration]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate");
    REQUIRE(std::filesystem::exists(rom));

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();

    // Warm up, then snapshot.
    for (int i = 0; i < 30; ++i) {
        emu.step_frame();
    }
    const std::vector<Byte> snapshot = emu.save_state();
    REQUIRE_FALSE(snapshot.empty());

    // Run further and record where the machine ends up.
    for (int i = 0; i < 20; ++i) {
        emu.step_frame();
    }
    const std::uint64_t reference = hash_frame(emu.frame_buffer());

    // Restore the snapshot and replay the same number of frames.
    REQUIRE(emu.load_state(snapshot));
    for (int i = 0; i < 20; ++i) {
        emu.step_frame();
    }
    REQUIRE(hash_frame(emu.frame_buffer()) == reference);
}

TEST_CASE("Loading a snapshot into a fresh emulator reproduces frames", "[state][integration]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate2");

    Emulator source;
    REQUIRE(source.load_cartridge(rom.string()).has_value());
    source.reset();
    for (int i = 0; i < 45; ++i) {
        source.step_frame();
    }
    const std::vector<Byte> snapshot = source.save_state();

    // Advance the source so any shared-static state cannot mask a bad restore.
    for (int i = 0; i < 10; ++i) {
        source.step_frame();
    }
    const std::uint64_t reference = hash_frame(source.frame_buffer());

    Emulator restored;
    REQUIRE(restored.load_cartridge(rom.string()).has_value());
    restored.reset();
    REQUIRE(restored.load_state(snapshot));
    for (int i = 0; i < 10; ++i) {
        restored.step_frame();
    }
    REQUIRE(hash_frame(restored.frame_buffer()) == reference);
}

TEST_CASE("load_state rejects malformed and mismatched blobs", "[state]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate3");

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();

    SECTION("garbage is rejected") {
        const std::vector<Byte> garbage(64, 0x55);
        REQUIRE_FALSE(emu.load_state(garbage));
    }
    SECTION("empty blob is rejected") {
        REQUIRE_FALSE(emu.load_state({}));
    }
    SECTION("truncated valid blob is rejected") {
        const std::vector<Byte> before = emu.save_state();
        std::vector<Byte> blob = before;
        blob.resize(blob.size() / 2);
        REQUIRE_FALSE(emu.load_state(blob));
        // The rejected apply must roll back. A following frame also proves the
        // restored PPU and mapper indexes are still in range.
        REQUIRE(emu.save_state() == before);
        emu.step_frame();
    }
    SECTION("corrupted magic is rejected") {
        std::vector<Byte> blob = emu.save_state();
        blob[0] = 0x00;
        REQUIRE_FALSE(emu.load_state(blob));
    }
}

TEST_CASE("Save-state survives a file round-trip", "[state]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate4");
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path() / "mapperbus-savestate.mbst";

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();
    for (int i = 0; i < 25; ++i) {
        emu.step_frame();
    }
    REQUIRE(emu.save_state_to_file(state_path.string()).has_value());

    // The framebuffer is not part of the blob (it re-renders on the next
    // frame), so compare after replaying an equal number of frames on both.
    for (int i = 0; i < 5; ++i) {
        emu.step_frame();
    }
    const std::uint64_t reference = hash_frame(emu.frame_buffer());

    Emulator restored;
    REQUIRE(restored.load_cartridge(rom.string()).has_value());
    restored.reset();
    REQUIRE(restored.load_state_from_file(state_path.string()).has_value());
    for (int i = 0; i < 5; ++i) {
        restored.step_frame();
    }
    REQUIRE(hash_frame(restored.frame_buffer()) == reference);

    std::filesystem::remove(state_path);
}

TEST_CASE("Oversized ROM and save-state files are rejected before allocation", "[state]") {
    register_builtin_mappers();
    const auto directory = std::filesystem::temp_directory_path();
    const auto rom_path = directory / "mapperbus-oversize.nes";
    {
        std::ofstream file(rom_path, std::ios::binary | std::ios::trunc);
        file << "NES\x1A";
    }
    std::filesystem::resize_file(rom_path, kMaxRomBytes + 1);

    Emulator emu;
    const auto loaded = emu.load_cartridge(rom_path.string());
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("exceeds maximum size") != std::string::npos);
    REQUIRE_FALSE(emu.has_cartridge());
    std::filesystem::remove(rom_path);

    const auto playable = mapperbus::tests::write_visible_nrom_test_rom("savestate-cap");
    REQUIRE(emu.load_cartridge(playable.string()).has_value());
    emu.reset();
    const std::vector<Byte> before = emu.save_state();

    const auto state_path = directory / "mapperbus-oversize.state";
    {
        std::ofstream file(state_path, std::ios::binary | std::ios::trunc);
        file << "MBST";
    }
    std::filesystem::resize_file(state_path, Emulator::kMaxStateBytes + 1);
    const auto state = emu.load_state_from_file(state_path.string());
    REQUIRE_FALSE(state);
    REQUIRE(state.error().find("exceeds maximum size") != std::string::npos);
    REQUIRE(emu.save_state() == before);
    std::filesystem::remove(state_path);
    std::filesystem::remove(playable);
}

TEST_CASE("MMC5 save-state rejects a PRG mode outside 0..3", "[state][mmc5]") {
    INesHeader header{};
    header.mapper_number = 5;
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    Mmc5 baseline(header, prg, chr);
    StateWriter baseline_writer;
    baseline.save_state(baseline_writer);

    Mmc5 changed(header, prg, chr);
    changed.write_expansion(0x5100, 0x00);
    StateWriter changed_writer;
    changed.save_state(changed_writer);

    REQUIRE(baseline_writer.data().size() == changed_writer.data().size());
    std::vector<Byte> blob = baseline_writer.data();
    int diffs = 0;
    for (std::size_t index = 0; index < blob.size(); ++index) {
        if (baseline_writer.data()[index] != changed_writer.data()[index]) {
            blob[index] = 0xFF;
            ++diffs;
        }
    }
    REQUIRE(diffs >= 1);

    Mmc5 victim(header, prg, chr);
    StateReader reader(blob);
    victim.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    (void)victim.read_prg(0x8000);
    (void)victim.read_prg(0xE000);
}

TEST_CASE("Namco 163 save-state rejects a zero channel count", "[state][n163]") {
    INesHeader header{};
    header.mapper_number = 19;
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    Namco163 baseline(header, prg, chr);
    StateWriter baseline_writer;
    baseline.save_state(baseline_writer);

    Namco163 changed(header, prg, chr);
    changed.write_prg(0xF800, 0x7F);
    changed.write_expansion(0x4800, 0x70); // channel 8 enabled: ((0x70 >> 4) & 7) + 1
    StateWriter changed_writer;
    changed.save_state(changed_writer);

    REQUIRE(baseline_writer.data().size() == changed_writer.data().size());
    std::vector<Byte> blob = baseline_writer.data();
    int diffs = 0;
    for (std::size_t index = 0; index < blob.size(); ++index) {
        if (baseline_writer.data()[index] != changed_writer.data()[index]) {
            blob[index] = 0; // includes num_channels_, which hardware never stores as 0
            ++diffs;
        }
    }
    REQUIRE(diffs >= 1);

    Namco163 victim(header, prg, chr);
    StateReader reader(blob);
    victim.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    for (int step = 0; step < 64; ++step) {
        victim.clock_audio();
    }
    (void)victim.audio_output();
}

TEST_CASE("PPU save-state rejects a sprite count above the hardware limit", "[state][ppu]") {
    Ppu ppu;
    StateWriter writer;
    ppu.save_state(writer);
    std::vector<Byte> blob = writer.take();
    REQUIRE(blob.size() >= sizeof(int));

    int accepted = 8;
    std::memcpy(blob.data() + blob.size() - sizeof(accepted), &accepted, sizeof(accepted));
    {
        Ppu restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE(reader.ok());
        restored.step(30);
    }

    int rejected = 64;
    std::memcpy(blob.data() + blob.size() - sizeof(rejected), &rejected, sizeof(rejected));
    Ppu restored;
    StateReader reader(blob);
    restored.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    restored.step(30);
}

TEST_CASE("Mapper save-states reject unsafe restored indices", "[state][mappers]") {
    INesHeader header{};
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    SECTION("MMC3 bank select") {
        Mmc3 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        blob[0x2000 + 0x2000] = 0xFF;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        mapper.write_prg(0x8001, 1);
    }

    SECTION("Namco 163 sound address") {
        Namco163 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        constexpr std::size_t sound_addr_offset = 0x2000 + 0x2000 + 4 + 8 + 1 + 2 + 1 + 1 + 128;
        blob[sound_addr_offset] = 0xFF;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        (void)mapper.read_expansion(0x4800);
    }

    SECTION("MMC5 pulse sequencer") {
        Mmc5 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        constexpr std::size_t pulse_offset = 0x10000 + 0x2000 + 1 + 5 + 1 + 16 + 1;
        blob[pulse_offset + 1] = 0xFF;
        blob[pulse_offset + 6] = 1;
        blob[pulse_offset + 7] = 1;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        (void)mapper.audio_output();
    }
}

TEST_CASE("Invalid APU state is rejected and rolled back", "[state][apu]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("apu-state");
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();
    emu.step_frame();

    const std::vector<Byte> before = emu.save_state();
    StateWriter cpu_writer;
    emu.cpu().save_state(cpu_writer);
    StateWriter ppu_writer;
    emu.ppu().save_state(ppu_writer);
    constexpr std::size_t header_size = 4 + sizeof(std::uint32_t) + sizeof(std::uint16_t) + 1;
    const std::size_t apu_offset =
        header_size + cpu_writer.data().size() + ppu_writer.data().size();

    std::vector<Byte> blob = before;
    blob[apu_offset] = 0xFF;
    REQUIRE_FALSE(emu.load_state(blob));
    REQUIRE(emu.save_state() == before);
    emu.step_frame();
    std::filesystem::remove(rom);
}
