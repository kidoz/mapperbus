#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
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
        std::vector<Byte> blob = emu.save_state();
        blob.resize(blob.size() / 2);
        REQUIRE_FALSE(emu.load_state(blob));
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
