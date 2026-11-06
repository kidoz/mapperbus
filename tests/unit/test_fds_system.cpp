#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "../support/fds_image.hpp"
#include "core/binary_file.hpp"
#include "core/emulator.hpp"
#include "core/fds/fds_mapper.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-fds-system-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directories(root);
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

Byte next_byte(Fds& fds) {
    for (int cycle = 0; cycle < 700000; ++cycle) {
        fds.step(1);
        if ((fds.read(0x4030) & 0x80) != 0)
            return fds.read(0x4031);
    }
    FAIL("Timed out waiting for disk transfer");
    return 0;
}

void write_payload_byte(Fds& drive, Byte value) {
    drive.insert_side(0);
    drive.write(0x4023, 1);
    drive.write(0x4025, 0x65);
    for (const int size : {56, 2, 16}) {
        for (int index = 0; index < size + 2; ++index)
            (void)next_byte(drive);
        drive.write(0x4025, 0x25);
        drive.write(0x4025, 0x65);
    }
    REQUIRE(next_byte(drive) == 4);
    drive.write(0x4025, 0x61);
    drive.write(0x4024, value);
    (void)next_byte(drive);
    drive.write(0x4025, 0x26); // stop motor before flushing
}
} // namespace

TEST_CASE("FDS BIOS boots through the bus and reads disk data into adapter RAM", "[fds][system]") {
    Emulator emulator;
    REQUIRE(
        emulator.load_fds(mapperbus::tests::make_fds_bios(), mapperbus::tests::make_fds_image(2)));
    REQUIRE(emulator.is_fds());
    REQUIRE(emulator.mapper_number() == 20);
    emulator.reset();
    CHECK(emulator.cpu().pc() == 0xE000);
    for (int frame = 0; frame < 30; ++frame)
        emulator.step_frame();
    CHECK(emulator.read_cpu(0x6000) == 1);
    const auto before = emulator.save_state();
    CHECK_FALSE(emulator.load_fds(std::vector<Byte>(8191), mapperbus::tests::make_fds_image()));
    CHECK(emulator.save_state() == before);
    CHECK_FALSE(emulator.load_fds(mapperbus::tests::make_fds_bios(), std::vector<Byte>(65500)));
    CHECK(emulator.save_state() == before);
}

TEST_CASE("FDS RAM adapter maps writable PRG/CHR and immutable BIOS", "[fds][system]") {
    Fds drive;
    const auto bios = mapperbus::tests::make_fds_bios();
    FdsMapper mapper(bios, drive);
    for (Address address : {0x6000, 0x8000, 0xDFFF}) {
        mapper.write_prg(address, 42);
        CHECK(mapper.read_prg(address) == 42);
    }
    mapper.write_prg(0xE000, 0);
    CHECK(mapper.read_prg(0xE000) == bios[0]);
    mapper.write_chr(0x1FFF, 23);
    CHECK(mapper.read_chr(0x1FFF) == 23);
    drive.write(0x4023, 1);
    drive.write(0x4025, 8);
    CHECK(mapper.mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("FDS disk changes persist separately and reject a wrong or corrupt save",
          "[fds][system][save]") {
    Fixture fixture;
    const auto save_path = fixture.root / "disk.fds-save";
    const auto bios = mapperbus::tests::make_fds_bios();
    const auto disk = mapperbus::tests::make_fds_image(2);
    Emulator source;
    REQUIRE(source.load_fds(bios, disk, save_path.string()));
    const auto identity = source.content_hash();
    write_payload_byte(source.fds(), 42);
    CHECK(source.fds().dirty());
    REQUIRE(source.flush_disk_changes());
    CHECK_FALSE(source.fds().dirty());
    CHECK(source.content_hash() == identity);
    Emulator restored;
    REQUIRE(restored.load_fds(bios, disk, save_path.string()));
    CHECK(restored.fds().disk_image()[75] == 42);
    const auto exported = fixture.root / "export.fds";
    REQUIRE(restored.export_disk(exported.string()));
    auto bytes = read_binary_file_capped(exported.string(), Fds::kMaxDiskImageBytes, "export");
    REQUIRE(bytes);
    CHECK((*bytes)[75] == 42);
    CHECK(disk[75] == 0);
    const auto state = restored.save_state();
    auto other = disk;
    other[100] ^= 1;
    CHECK_FALSE(restored.load_fds(bios, other, save_path.string()));
    CHECK(restored.save_state() == state);
    {
        std::fstream file(save_path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('x');
    }
    CHECK_FALSE(restored.load_fds(bios, disk, save_path.string()));
    CHECK(restored.save_state() == state);
}

TEST_CASE("FDS snapshots restore disk side and writes with stable source identity",
          "[fds][system][state]") {
    const auto bios = mapperbus::tests::make_fds_bios();
    const auto disk = mapperbus::tests::make_fds_image(2);
    Emulator emulator;
    REQUIRE(emulator.load_fds(bios, disk));
    emulator.reset();
    for (int frame = 0; frame < 30; ++frame)
        emulator.step_frame();
    const auto state = emulator.save_state();
    write_payload_byte(emulator.fds(), 42);
    emulator.fds().insert_side(1);
    REQUIRE(emulator.load_state(state));
    CHECK(emulator.fds().current_side() == 0);
    CHECK(emulator.fds().disk_image()[75] == 0);
    auto wrong_bios = bios;
    wrong_bios[100] ^= 1;
    Emulator other;
    REQUIRE(other.load_fds(wrong_bios, disk));
    CHECK_FALSE(other.load_state(state));
}

TEST_CASE("FDS switching refuses to discard disk changes when persistence fails",
          "[fds][system][save]") {
    Fixture fixture;
    Emulator emulator;
    REQUIRE(emulator.load_fds(mapperbus::tests::make_fds_bios(),
                              mapperbus::tests::make_fds_image(),
                              (fixture.root / "missing" / "disk.fds-save").string()));
    write_payload_byte(emulator.fds(), 42);
    const auto before = emulator.save_state();
    CHECK_FALSE(emulator.flush_disk_changes());
    CHECK_FALSE(
        emulator.load_fds(mapperbus::tests::make_fds_bios(), mapperbus::tests::make_fds_image(2)));
    CHECK(emulator.save_state() == before);
    std::filesystem::create_directory(fixture.root / "missing");
    REQUIRE(emulator.flush_disk_changes());
}

TEST_CASE("FDS CPU program observes both clean and corrupt block CRCs", "[fds][system][crc]") {
    auto bios = mapperbus::tests::make_fds_bios();
    // Original synthetic firmware: consume block 1 and its first CRC byte,
    // select CRC checking, consume the second byte, and store status at $6000.
    const Byte program[] = {
        0x78, 0xA9, 0x01, 0x8D, 0x23, 0x40, 0xA9, 0x65, 0x8D, 0x25, 0x40, 0xA2,
        0x39, 0xAD, 0x30, 0x40, 0x10, 0xFB, 0xAD, 0x31, 0x40, 0xCA, 0xD0, 0xF5,
        0xA9, 0x75, 0x8D, 0x25, 0x40, 0xAD, 0x30, 0x40, 0x10, 0xFB, 0xAD, 0x31,
        0x40, 0xAD, 0x30, 0x40, 0x29, 0x10, 0x8D, 0x00, 0x60, 0x4C, 0x2D, 0xE0,
    };
    std::ranges::copy(program, bios.begin());
    for (bool corrupt : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_fds(bios, mapperbus::tests::make_fds_image()));
        if (corrupt) {
            StateWriter writer;
            emulator.fds().save_state(writer);
            auto state = writer.take();
            constexpr std::size_t kTrackSize = 84000;
            constexpr std::size_t kFirstCrc = 3537 + 1 + 56;
            state[state.size() - kTrackSize + kFirstCrc] ^= 1;
            StateReader reader(state);
            emulator.fds().load_state(reader);
            REQUIRE(reader.ok());
        }
        emulator.reset();
        for (int frame = 0; frame < 30; ++frame)
            emulator.step_frame();
        REQUIRE(emulator.cpu().pc() == 0xE02D);
        REQUIRE(emulator.read_cpu(0x6000) == (corrupt ? 0x10 : 0));
    }
}
