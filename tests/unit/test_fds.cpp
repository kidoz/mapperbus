#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "../support/fds_image.hpp"
#include "core/fds/fds.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

using mapperbus::tests::make_fds_image;

Byte next_byte(Fds& fds) {
    for (int cycle = 0; cycle < 700000; ++cycle) {
        fds.step(1);
        if ((fds.read(0x4030) & 0x80) != 0)
            return fds.read(0x4031);
    }
    FAIL("Timed out waiting for an FDS byte");
    return 0;
}

void next_block(Fds& fds) {
    fds.write(0x4025, 0x25);
    fds.write(0x4025, 0x65);
}

void seek_file_payload(Fds& fds) {
    fds.write(0x4023, 1);
    fds.write(0x4025, 0x65);
    for (const int size : {56, 2, 16}) {
        for (int index = 0; index < size + 2; ++index)
            (void)next_byte(fds);
        next_block(fds);
    }
    REQUIRE(next_byte(fds) == 4);
}

} // namespace

TEST_CASE("FDS loads a headerless single-side image", "[fds]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, /*with_header=*/false)));
    REQUIRE(fds.is_loaded());
    REQUIRE(fds.side_count() == 1);
    REQUIRE(fds.current_side() == 0);
}

TEST_CASE("FDS strips the fwNES header and counts sides", "[fds]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(2, /*with_header=*/true)));
    REQUIRE(fds.side_count() == 2);

    Fds empty;
    REQUIRE_FALSE(empty.load_disk(std::vector<Byte>{}));
}

TEST_CASE("FDS timer IRQ fires after the reload count", "[fds][irq]") {
    Fds fds;
    fds.write(0x4023, 0x01); // enable disk I/O registers
    fds.write(0x4020, 0x64); // reload low = 100
    fds.write(0x4021, 0x00); // reload high
    fds.write(0x4022, 0x02); // enable timer, one-shot

    fds.step(99);
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(1);
    REQUIRE(fds.irq_pending());

    // Reading $4030 reports and acknowledges the timer IRQ.
    const Byte status = fds.read(0x4030);
    REQUIRE((status & 0x01) != 0);
    REQUIRE_FALSE(fds.irq_pending());

    // One-shot: it does not re-fire.
    fds.step(200);
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS timer IRQ repeats when configured", "[fds][irq]") {
    Fds fds;
    fds.write(0x4023, 0x01);
    fds.write(0x4020, 0x0A); // reload = 10
    fds.write(0x4021, 0x00);
    fds.write(0x4022, 0x03); // enable + repeat

    fds.step(10);
    REQUIRE(fds.irq_pending());
    fds.read(0x4030); // ack
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(10);
    REQUIRE(fds.irq_pending()); // fired again
}

TEST_CASE("FDS streams disk bytes in order", "[fds][transfer]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);               // enable disk I/O
    fds.write(0x4025, 0x01 | 0x04 | 0x40); // motor on + read mode + start

    REQUIRE(next_byte(fds) == 1);
    REQUIRE(next_byte(fds) == '*');
    REQUIRE(next_byte(fds) == 'N');
}

TEST_CASE("FDS transfer can raise an IRQ per byte", "[fds][transfer][irq]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);
    fds.write(0x4025, 0x01 | 0x04 | 0x40 | 0x80); // + IRQ on byte transfer

    for (int cycle = 0; cycle < 700000 && !fds.irq_pending(); ++cycle)
        fds.step(1);
    REQUIRE(fds.irq_pending());
    fds.read(0x4031); // reading the data clears the transfer IRQ
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS drive status reflects the inserted disk", "[fds]") {
    Fds fds;
    REQUIRE((fds.read(0x4032) & 0x01) != 0); // no disk: bit0 set

    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    REQUIRE((fds.read(0x4032) & 0x01) == 0); // disk present: bit0 clear
    REQUIRE((fds.read(0x4032) & 0x04) != 0); // write protected: bit2 set

    fds.eject();
    REQUIRE((fds.read(0x4032) & 0x01) != 0);
    REQUIRE((fds.read(0x4032) & 0x04) != 0);
    REQUIRE(fds.current_side() == -1);
}

TEST_CASE("FDS state serializes drive + disk position", "[fds][state]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    fds.write(0x4023, 0x01);
    fds.write(0x4025, 0x01 | 0x04 | 0x40);
    (void)next_byte(fds);
    (void)next_byte(fds);

    StateWriter writer;
    fds.save_state(writer);

    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored.is_loaded());
    REQUIRE(restored.current_side() == 0);

    // Both continue identically: next delivered byte matches.
    fds.step(149);
    restored.step(149);
    REQUIRE(restored.read(0x4031) == fds.read(0x4031));
}

TEST_CASE("FDS write mode stores file bytes and marks the disk dirty", "[fds][transfer]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image()));
    fds.set_write_protected(false);
    seek_file_payload(fds);
    fds.write(0x4025, 0x61);
    fds.write(0x4024, 0xDE);
    (void)next_byte(fds);
    fds.write(0x4024, 0xAD);
    (void)next_byte(fds);
    fds.synchronize_disk();
    CHECK(fds.disk_image()[75] == 0xDE);
    CHECK(fds.disk_image()[76] == 0xAD);
    CHECK(fds.dirty());
    fds.insert_side(0);
    seek_file_payload(fds);
    CHECK(next_byte(fds) == 0xDE);
    CHECK(next_byte(fds) == 0xAD);
}

TEST_CASE("FDS write mode honors the write-protect tab", "[fds][transfer]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image()));
    seek_file_payload(fds);
    fds.write(0x4025, 0x61);
    fds.write(0x4024, 0xFF);
    (void)next_byte(fds);
    CHECK(fds.disk_image()[75] == 0);
    CHECK_FALSE(fds.dirty());
}

TEST_CASE("FDS generated block CRC verifies and gaps are skipped", "[fds][transfer]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image()));
    fds.write(0x4023, 1);
    fds.write(0x4025, 0x65);
    for (int index = 0; index < 58; ++index)
        (void)next_byte(fds);
    CHECK((fds.read(0x4030) & 0x10) == 0);
    next_block(fds);
    CHECK(next_byte(fds) == 2);
    CHECK(next_byte(fds) == 1);
}

TEST_CASE("FDS mirroring and disk I/O write gating follow $4023/$4025", "[fds]") {
    Fds fds;
    fds.write(0x4025, 0x08);
    CHECK(fds.mirror_mode() == MirrorMode::Vertical);
    fds.write(0x4023, 1);
    fds.write(0x4025, 0x08);
    CHECK(fds.mirror_mode() == MirrorMode::Horizontal);
    fds.write(0x4023, 0);
    CHECK(fds.mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("FDS load_state clamps inconsistent disk geometry", "[fds][state]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(1, false)));
    StateWriter writer;
    fds.save_state(writer);

    SECTION("side indices beyond the serialized data are rejected") {
        std::vector<Byte> blob = writer.data();
        // Layout: loaded_(1) num_sides_(4) current_side_(4) write_protected_(1),
        // then the length-prefixed disk data.
        blob[1] = 0x04; // num_sides_ = 4, but only one side of data follows
        blob[5] = 0x03; // current_side_ = 3: out of range
        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(restored.side_count() == 1);
        REQUIRE(restored.current_side() == -1); // ejected rather than OOB
    }

    SECTION("disk position past the side size is clamped") {
        std::vector<Byte> blob = writer.data();
        // disk_position_ (u32) follows the 10-byte disk header fields, the
        // 8-byte length prefix + 65500 disk bytes, and 13 drive/timer bytes.
        const std::size_t pos_off = 10 + 8 + Fds::kSideSize + 13;
        blob[pos_off + 0] = 0xFF;
        blob[pos_off + 1] = 0xFF;
        blob[pos_off + 2] = 0xFF;
        blob[pos_off + 3] = 0xFF;
        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE(reader.ok());
        // The transfer engine reports end-of-disk instead of indexing past
        // the disk buffer.
        restored.write(0x4023, 0x01);
        restored.write(0x4025, 0x01 | 0x04 | 0x40);
        restored.step(50001);
        REQUIRE((restored.read(0x4030) & 0x40) != 0);
    }

    SECTION("truncation after the disk image still clamps the head") {
        std::vector<Byte> blob = writer.data();
        blob[5] = 40; // current_side_ = 40, past the single serialized side
        // Keep the disk payload and only part of the following fields, so the
        // reader fails after current_side_ was already consumed.
        const std::size_t keep = 10 + 8 + Fds::kSideSize + 4;
        REQUIRE(blob.size() > keep);
        blob.resize(keep);

        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(restored.current_side() == -1);
        restored.write(0x4023, 0x01);
        restored.write(0x4025, 0x01 | 0x40); // motor on + start, write mode
        restored.step(5000);
    }
}

TEST_CASE("FDS state rejects invalid wavetable and modulation positions", "[fds][state]") {
    Fds baseline;
    StateWriter writer;
    baseline.save_state(writer);

    SECTION("wavetable position") {
        auto blob = writer.data();
        blob[118] = 0xFF;
        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        (void)restored.audio_output();
    }

    SECTION("negative transfer delay") {
        auto blob = writer.data();
        constexpr std::size_t transfer_delay_offset = 35;
        const std::int32_t invalid = -1;
        std::memcpy(blob.data() + transfer_delay_offset, &invalid, sizeof(invalid));
        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        restored.step(1);
    }
}

TEST_CASE("FDS rejects an image past the side cap", "[fds]") {
    std::vector<Byte> image(Fds::kMaxDiskImageBytes + 1, 0);
    Fds fds;
    REQUIRE_FALSE(fds.load_disk(image));
    REQUIRE_FALSE(fds.is_loaded());
}

// --- FDS audio ---

namespace {

// Load a square wavetable (32 low, 32 high) and start the wave unit.
void start_fds_square(Fds& fds, uint16_t frequency, Byte gain_reg) {
    fds.write(0x4023, 2);    // release the sound unit from reset
    fds.write(0x4080, 0xA0); // seed gain before enabling an automatic envelope
    fds.write(0x4089, 0x80); // open wavetable for writes
    for (Address i = 0; i < 64; ++i) {
        fds.write(0x4040 + i, (i < 32) ? 0x00 : 0x3F);
    }
    fds.write(0x4089, 0x00);                                // close, full master volume
    fds.write(0x4080, gain_reg);                            // volume gain / envelope
    fds.write(0x4082, static_cast<Byte>(frequency & 0xFF)); // freq low
    fds.write(0x4083, static_cast<Byte>(frequency >> 8));   // freq high, enable
}

float fds_peak(Fds& fds, int cycles) {
    float peak = 0.0f;
    for (int i = 0; i < cycles; ++i) {
        fds.clock_audio();
        peak = std::max(peak, fds.audio_output());
    }
    return peak;
}

} // namespace

TEST_CASE("FDS wave frequency matches hardware formula", "[fds][audio]") {
    // f = CPU * F / (65536 * 64): F = $400 -> 436.96 Hz.
    Fds fds;
    fds.reset();
    start_fds_square(fds, 0x0400, 0xA0); // envelope disabled, gain 32
    // Count rising transitions across one emulated second.
    int rising = 0;
    bool high = false;
    for (int i = 0; i < 1789773; ++i) {
        fds.clock_audio();
        const float s = fds.audio_output();
        if (!high && s > 0.05f) {
            high = true;
            ++rising;
        } else if (high && s < 0.02f) {
            high = false;
        }
    }
    REQUIRE(rising > 430);
    REQUIRE(rising < 444);
}

TEST_CASE("FDS volume envelope ramps the gain down", "[fds][audio]") {
    Fds fds;
    fds.reset();
    // Envelope enabled, decrease, speed 4: one step per
    // 8 * (4+1) * (0xE8+1) = 9320 cycles; 32 steps ~ 300k cycles.
    start_fds_square(fds, 0x0400, 0x04);
    const float early = fds_peak(fds, 20000);
    REQUIRE(early > 0.0f);
    fds_peak(fds, 400000); // let the ramp finish
    const float late = fds_peak(fds, 20000);
    REQUIRE(late == 0.0f);
}

TEST_CASE("FDS envelope halt freezes the ramp", "[fds][audio]") {
    Fds fds;
    fds.reset();
    start_fds_square(fds, 0x0400, 0x04);
    fds.write(0x4083, 0x44); // wave enabled (bit7 clear), envelopes halted (bit6)
    const float early = fds_peak(fds, 20000);
    fds_peak(fds, 400000);
    const float late = fds_peak(fds, 20000);
    REQUIRE(early > 0.0f);
    REQUIRE(late > 0.9f * early); // gain unchanged while halted
}

TEST_CASE("FDS rewrites erase old sync markers when gap lengths change", "[fds][transfer][state]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image()));
    fds.set_write_protected(false);
    fds.write(0x4023, 1);
    fds.write(0x4025, 0x65);
    for (int index = 0; index < 58; ++index)
        (void)next_byte(fds);
    // Rewrite block 2 with a gap three bytes longer than the original track.
    fds.write(0x4025, 0x21);
    fds.step(18667); // 125 byte periods at 149 1/3 CPU cycles each
    fds.write(0x4024, 0x80);
    fds.write(0x4025, 0x61);
    (void)next_byte(fds);
    fds.write(0x4024, 2);
    (void)next_byte(fds);
    StateWriter writer;
    fds.save_state(writer);
    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    for (auto* drive : {&fds, &restored}) {
        drive->write(0x4024, 0); // hide the existing file by changing declared count
        (void)next_byte(*drive);
        drive->write(0x4025, 0x71);
        (void)next_byte(*drive);
        (void)next_byte(*drive);
        drive->write(0x4025, 0x25);
        REQUIRE(drive->disk_image()[56] == 2);
        REQUIRE(drive->disk_image()[57] == 0);
        REQUIRE(drive->disk_image()[58] == 3); // hidden file preserved
        REQUIRE(drive->dirty());
        drive->insert_side(0);
        drive->write(0x4025, 0x65);
        for (int index = 0; index < 58; ++index)
            (void)next_byte(*drive);
        next_block(*drive);
        REQUIRE(next_byte(*drive) == 2);
        REQUIRE(next_byte(*drive) == 0);
    }
    StateWriter a, b;
    fds.save_state(a);
    restored.save_state(b);
    CHECK(a.data() == b.data());
}

TEST_CASE("FDS reload registers remain writable while disk I/O is reset", "[fds][irq]") {
    Fds fds;
    fds.write(0x4020, 3);
    fds.write(0x4021, 1); // $0103, written before enabling disk I/O
    fds.write(0x4022, 2); // enabling the timer is still gated
    fds.step(1000);
    REQUIRE_FALSE(fds.irq_pending());
    fds.write(0x4023, 1);
    fds.write(0x4022, 2);
    fds.step(258);
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(1);
    REQUIRE(fds.irq_pending());
}

TEST_CASE("FDS zero timer reload fires on the next CPU cycle", "[fds][irq]") {
    Fds fds;
    fds.write(0x4023, 1);
    fds.write(0x4022, 3);
    fds.step(1);
    REQUIRE(fds.irq_pending());
    REQUIRE((fds.read(0x4030) & 1) != 0);
    fds.step(1);
    REQUIRE(fds.irq_pending());
    fds.write(0x4022, 0);
    REQUIRE_FALSE(fds.irq_pending());
    fds.step(65536);
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS reinsertion clears a stale transfer IRQ but preserves a timer IRQ", "[fds][irq]") {
    Fds fds;
    REQUIRE(fds.load_disk(make_fds_image(2)));
    fds.write(0x4023, 1);
    fds.write(0x4025, 0xE5);
    for (int cycle = 0; cycle < 700000 && !fds.irq_pending(); ++cycle)
        fds.step(1);
    REQUIRE(fds.irq_pending());
    fds.insert_side(1);
    REQUIRE_FALSE(fds.irq_pending());
    REQUIRE((fds.read(0x4030) & 0x80) == 0);
    fds.write(0x4020, 1);
    fds.write(0x4022, 2);
    fds.step(1);
    REQUIRE(fds.irq_pending());
    fds.insert_side(0);
    REQUIRE(fds.irq_pending());
    REQUIRE((fds.read(0x4030) & 1) != 0);
    REQUIRE_FALSE(fds.irq_pending());
}

TEST_CASE("FDS state preserves head position on tracks with many small files", "[fds][state]") {
    auto disk = make_fds_image();
    std::fill(disk.begin() + 58, disk.end(), 0);
    disk[57] = 1;
    for (int file = 0; file < 400; ++file) {
        const auto offset = 58 + static_cast<std::size_t>(file) * 17;
        disk[offset] = 3;
        disk[offset + 16] = 4;
    }
    Fds source;
    REQUIRE(source.load_disk(disk));
    source.write(0x4023, 1);
    source.write(0x4025, 0x65);
    source.step(14000000); // past byte 84000 on a valid track longer than the default
    StateWriter writer;
    source.save_state(writer);
    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    StateWriter copy;
    restored.save_state(copy);
    REQUIRE(copy.data() == writer.data());
    REQUIRE(next_byte(source) == next_byte(restored));
}

TEST_CASE("FDS sound reset silences output while preserving wavetable RAM", "[fds][audio]") {
    Fds fds;
    fds.reset();
    fds.write(0x4023, 2);
    start_fds_square(fds, 0x0400, 0xA0);
    REQUIRE(fds_peak(fds, 10000) > 0.05f);
    fds.write(0x4023, 0);
    REQUIRE((fds.read(0x4090) & 0x3F) == 0);
    REQUIRE(fds.audio_output() == 0.0f);
    fds.write(0x4080, 0xA0); // volume is held in reset
    REQUIRE(fds_peak(fds, 10000) == 0.0f);
    fds.write(0x4089, 0x80); // wave RAM and its write enable are still accessible
    REQUIRE((fds.read(0x4060) & 0x3F) == 0x3F);
    fds.write(0x4040, 12);
    REQUIRE((fds.read(0x4040) & 0x3F) == 12);
    fds.write(0x4023, 2);
    fds.write(0x4089, 0);
    fds.write(0x4080, 0xA0);
    REQUIRE(fds_peak(fds, 10000) > 0.05f);
}

TEST_CASE("FDS CRC status detects a corrupt physical block", "[fds][transfer][crc]") {
    Fds source;
    REQUIRE(source.load_disk(make_fds_image()));
    StateWriter writer;
    source.save_state(writer);
    for (bool corrupt : {false, true}) {
        auto state = writer.data();
        // Stream payload follows its size prefix, before the four v6 audio and five v7 watchdog
        // bytes.
        constexpr std::size_t kTrackSize = 84000;
        constexpr std::size_t kFirstCrc = 3537 + 1 + 56;
        if (corrupt)
            state[state.size() - 9 - kTrackSize + kFirstCrc] ^= 1U;
        Fds drive;
        StateReader reader(state);
        drive.load_state(reader);
        REQUIRE(reader.ok());
        drive.write(0x4023, 1);
        drive.write(0x4025, 0x65);
        for (int index = 0; index < 57; ++index)
            (void)next_byte(drive);
        drive.write(0x4025, 0x75); // BIOS selects CRC checking after the first CRC byte
        (void)next_byte(drive);
        CHECK(((drive.read(0x4030) & 0x10) != 0) == corrupt);
    }
}

TEST_CASE("FDS boot bypass may declare an intentionally absent final file", "[fds][format]") {
    auto image = make_fds_image();
    image[57] = 2; // one complete file, followed by a deliberately absent file
    Fds drive;
    REQUIRE(drive.load_disk(image));
    // BIOS/game code decides what a declared-but-absent file means. The loader
    // still rejects a partial physical block rather than manufacturing data.
    auto truncated = image;
    truncated[58 + 13] = 0xFF;
    truncated[58 + 14] = 0xFF;
    REQUIRE_FALSE(drive.load_disk(truncated));
    REQUIRE(drive.is_loaded());
}

TEST_CASE("FDS modulation counter readback supports the public audio viewer", "[fds][audio]") {
    Fds drive;
    drive.reset();
    drive.write(0x4023, 2);
    drive.write(0x4087, 0x80); // halt modulation so the programmed counter stays fixed
    for (Byte value : {0, 1, 63, 64, 127, 255}) {
        drive.write(0x4085, value);
        CHECK((drive.read(0x4097) & 0x7F) == (value & 0x7F));
        drive.clock_audio();
        CHECK((drive.read(0x4097) & 0x7F) == (value & 0x7F));
    }
    drive.write(0x4023, 0);
    CHECK((drive.read(0x4097) & 0x7F) == 0);
}

TEST_CASE("FDS wave RAM reads follow the playback pointer when writes are disabled",
          "[fds][audio]") {
    Fds drive;
    drive.reset();
    drive.write(0x4023, 2);
    drive.write(0x4089, 0x80);
    drive.write(0x4040, 12);
    drive.write(0x4041, 23);
    CHECK((drive.read(0x4040) & 0x3F) == 12);
    CHECK((drive.read(0x4041) & 0x3F) == 23);
    drive.write(0x4089, 0);
    CHECK((drive.read(0x407F) & 0x3F) == 12);
    drive.write(0x4082, 0);
    drive.write(0x4083, 4); // one wave-table position every 64 CPU clocks
    for (int cycle = 0; cycle < 64; ++cycle)
        drive.clock_audio();
    CHECK((drive.read(0x4040) & 0x3F) == 23);
    CHECK((drive.read(0x407F) & 0x3F) == 23);
}

TEST_CASE("FDS wavetable writes hold the sample while phase keeps running", "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0x0400, 0xA0);
    for (int cycle = 0; cycle < 32 * 64; ++cycle)
        drive.clock_audio();
    const float held = drive.audio_output();
    REQUIRE(held > 0.1f);
    drive.write(0x4089, 0x80);
    drive.write(0x4060, 0); // overwriting the current slot must not change the held sample
    CHECK(drive.audio_output() == held);
    for (int cycle = 0; cycle < 33 * 64; ++cycle) {
        drive.clock_audio();
        REQUIRE(drive.audio_output() == held);
    }
    drive.write(0x4089, 0);
    CHECK((drive.read(0x4040) & 0x3FU) == 0); // phase advanced from 32 through wrap to 1
    CHECK(drive.audio_output() == 0.0f);
}

TEST_CASE("FDS volume changes latch at wave position zero and direct mute is immediate",
          "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0x0400, 0xA0);
    for (int cycle = 0; cycle < 32 * 64; ++cycle)
        drive.clock_audio();
    const float full = drive.audio_output();
    REQUIRE(full > 0.1f);
    drive.write(0x4080, 0x90);                 // gain 16, pending until position zero
    CHECK((drive.read(0x4090) & 0x3FU) == 16); // register changes immediately
    CHECK(drive.audio_output() == full);
    for (int cycle = 0; cycle < 64 * 64; ++cycle)
        drive.clock_audio();
    CHECK(drive.audio_output() == full * 0.5f);
    drive.write(0x4080, 0x80);
    CHECK(drive.audio_output() == 0.0f);
}

TEST_CASE("FDS wave and modulation debug reads expose their 16-cycle accumulators",
          "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0x0400, 0xA0);
    drive.write(0x4086, 0x20);
    drive.write(0x4087, 0);
    for (int cycle = 0; cycle < 15; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4091) == 0);
    CHECK(drive.read(0x4093) == 0);
    drive.clock_audio();
    CHECK(drive.read(0x4091) == 16); // (0x400 * 64) >> 12
    CHECK(drive.read(0x4093) == 1);  // 0x20 >> 5
    drive.write(0x4083, 0x84);
    CHECK(drive.read(0x4091) == 0);
    drive.write(0x4087, 0x80);
    CHECK(drive.read(0x4093) == 0);
}

TEST_CASE("FDS modulation retains pitch fractions below one legacy frequency unit",
          "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 1, 0xA0);
    drive.write(0x4084, 0xBE); // gain 62
    drive.write(0x4085, 50);   // rounded multiplier wraps to 1: wave pitch = 1
    drive.write(0x4087, 0x80);
    for (int cycle = 0; cycle < 65536; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4091) == 1);      // 4096 ticks retain all low bits
    CHECK((drive.read(0x4094)) == 0xC1); // settled (50 * 62) bits 4..11
}

TEST_CASE("FDS modulation increment readback decodes all eight entries", "[fds][audio]") {
    constexpr std::array<Byte, 8> kIncrements = {0, 1, 2, 4, 12, 12, 14, 15};
    for (Byte entry = 0; entry < 8; ++entry) {
        Fds drive;
        drive.reset();
        drive.write(0x4023, 2);
        drive.write(0x4087, 0x80);
        for (int index = 0; index < 32; ++index)
            drive.write(0x4088, entry);
        CHECK((drive.read(0x4095) & 0x0FU) == kIncrements[entry]);
    }
}

TEST_CASE("FDS DAC status applies 32-cycle PWM without pulsing the audio mixer", "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0, 0x90);
    drive.write(0x4089, 0x80);
    drive.write(0x4040, 63);
    drive.write(0x4089, 0);
    int on = 0;
    for (int cycle = 0; cycle < 32; ++cycle) {
        drive.clock_audio();
        const auto value = drive.read(0x4096);
        REQUIRE((value & 0xC0U) == 0x40);
        if ((value & 0x3FU) != 0) {
            CHECK((value & 0x3FU) == 63);
            ++on;
        }
        CHECK(drive.audio_output() == 0.075f);
    }
    CHECK(on == 16);
}

TEST_CASE("FDS envelope halt restarts both timers and wave halt accelerates them", "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0x400, 0xA0);
    drive.write(0x408A, 1); // period 16 CPU clocks at speed zero
    drive.write(0x4080, 0); // decrease volume
    drive.write(0x4084, 0x90);
    drive.write(0x4084, 0); // decrease mod gain
    for (int cycle = 0; cycle < 12; ++cycle)
        drive.clock_audio();
    drive.write(0x4083, 0x44); // freeze and restart both timers
    for (int cycle = 0; cycle < 32; ++cycle)
        drive.clock_audio();
    drive.write(0x4083, 4);
    for (int cycle = 0; cycle < 15; ++cycle)
        drive.clock_audio();
    CHECK((drive.read(0x4090) & 0x3FU) == 32);
    CHECK((drive.read(0x4092) & 0x3FU) == 16);
    drive.clock_audio();
    CHECK((drive.read(0x4090) & 0x3FU) == 31);
    CHECK((drive.read(0x4092) & 0x3FU) == 15);
    drive.write(0x4083, 0x84); // fourfold envelope rate while wave halted
    for (int cycle = 0; cycle < 4; ++cycle)
        drive.clock_audio();
    CHECK((drive.read(0x4090) & 0x3FU) == 30);
    CHECK((drive.read(0x4092) & 0x3FU) == 14);
}

TEST_CASE("FDS audio state restores a held sample, pending gain, and tick phase",
          "[fds][audio][state]") {
    Fds source;
    source.reset();
    start_fds_square(source, 0x0400, 0xA0);
    for (int cycle = 0; cycle < 32 * 64 + 7; ++cycle)
        source.clock_audio();
    source.write(0x4080, 0x90); // pending gain, current output remains full volume
    source.write(0x4089, 0x80);
    source.write(0x4060, 0);
    StateWriter writer;
    source.save_state(writer);
    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored.audio_output() > 0.1f);
    for (int cycle = 0; cycle < 10000; ++cycle) {
        if (cycle == 123) {
            source.write(0x4089, 0);
            restored.write(0x4089, 0);
        }
        source.clock_audio();
        restored.clock_audio();
        REQUIRE(restored.audio_output() == source.audio_output());
        for (Address address = 0x4090; address <= 0x4097; ++address)
            REQUIRE(restored.read(address) == source.read(address));
    }
    StateWriter copy, expected;
    restored.save_state(copy);
    source.save_state(expected);
    CHECK(copy.data() == expected.data());
}

TEST_CASE("FDS rejects invalid audio latch and phase state", "[fds][audio][state]") {
    Fds source;
    source.reset();
    StateWriter writer;
    source.save_state(writer);
    constexpr std::array<Byte, 4> kInvalid = {32, 64, 33, 2};
    for (std::size_t index = 0; index < kInvalid.size(); ++index) {
        auto blob = writer.data();
        blob[blob.size() - 9 + index] = kInvalid[index];
        Fds restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        restored.clock_audio();
        CHECK(restored.audio_output() == 0.0f);
    }
}

TEST_CASE("FDS modulation test control steps twice per table entry and halt clears ghost bit",
          "[fds][audio]") {
    Fds drive;
    drive.reset();
    start_fds_square(drive, 0x0400, 0xA0);
    drive.write(0x4087, 0x80);
    for (int index = 0; index < 32; ++index)
        drive.write(0x4088, index == 0 ? 1 : 2);
    drive.write(0x4087, 0x40); // force a carry on every 16-cycle unit tick
    for (int cycle = 0; cycle < 16; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4097) == 1);
    CHECK((drive.read(0x4095) & 0x0FU) == 1); // second half of entry zero
    drive.write(0x4087, 0xC0);                // clear ghost bit, retain address zero
    drive.write(0x4087, 0x40);
    for (int cycle = 0; cycle < 16; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4097) == 2);
    CHECK((drive.read(0x4095) & 0x0FU) == 1);
    for (int cycle = 0; cycle < 16; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4097) == 3);
    CHECK((drive.read(0x4095) & 0x0FU) == 2);
    drive.write(0x4083, 0x84); // wave halt stops mod accumulator too
    for (int cycle = 0; cycle < 64; ++cycle)
        drive.clock_audio();
    CHECK(drive.read(0x4097) == 3);
}

TEST_CASE("FDS imports legacy audio phase into the wider accumulators", "[fds][audio][state]") {
    Fds source;
    source.reset();
    start_fds_square(source, 0x0400, 0xA0);
    source.write(0x4086, 0x20);
    source.write(0x4087, 0);
    for (int cycle = 0; cycle < 80; ++cycle)
        source.clock_audio();
    StateWriter writer;
    source.save_state(writer);
    auto legacy = writer.data();
    legacy.resize(legacy.size() - 9); // v5 had no audio latches or watchdog state
    // Frozen v3-v5 offsets for an empty drive: 47 drive bytes + 64 wave bytes,
    // write flag + frequency precede the wave fraction; mod fraction is at 158.
    constexpr std::size_t kWaveFraction = 114;
    constexpr std::size_t kModFraction = 158;
    std::uint32_t wave = 0, mod = 0;
    std::memcpy(&wave, legacy.data() + kWaveFraction, sizeof(wave));
    std::memcpy(&mod, legacy.data() + kModFraction, sizeof(mod));
    wave >>= 2U;
    mod <<= 4U;
    std::memcpy(legacy.data() + kWaveFraction, &wave, sizeof(wave));
    std::memcpy(legacy.data() + kModFraction, &mod, sizeof(mod));
    Fds restored;
    StateReader reader(legacy);
    restored.load_state(reader, true, false, false);
    REQUIRE(reader.ok());
    CHECK(reader.remaining() == 0);
    CHECK(restored.read(0x4091) == source.read(0x4091));
    CHECK(restored.read(0x4093) == source.read(0x4093));
    for (int cycle = 0; cycle < 1000; ++cycle) {
        source.clock_audio();
        restored.clock_audio();
        REQUIRE(restored.read(0x4091) == source.read(0x4091));
        REQUIRE(restored.read(0x4093) == source.read(0x4093));
    }
}

TEST_CASE("FDS envelope master zero uses the hardware measured plus-one period", "[fds][audio]") {
    // Loopy's 2019 hardware report corrects the divider to
    // 8 * (speed + 1) * (master + 1), including the master-zero boundary.
    // Test both independent timers, direct-register restart, and wave halt.
    for (const Byte master : {0, 1, 255}) {
        for (const Byte speed : {0, 63}) {
            for (const bool halted : {false, true}) {
                Fds drive;
                drive.reset();
                drive.write(0x4023, 2);
                drive.write(0x4083, 0x40); // hold both timers while seeding
                drive.write(0x408A, master);
                drive.write(0x4080, 0xA0);
                drive.write(0x4084, 0x90);
                drive.write(0x4080, speed);
                drive.write(0x4084, speed);
                drive.write(0x4083, halted ? 0x80 : 0);
                const auto period = 8U * (static_cast<unsigned>(speed) + 1U) *
                                    (static_cast<unsigned>(master) + 1U) / (halted ? 4U : 1U);
                INFO("master " << static_cast<int>(master) << " speed " << static_cast<int>(speed)
                               << " halted " << halted);
                for (unsigned cycle = 1; cycle < period; ++cycle)
                    drive.clock_audio();
                CHECK((drive.read(0x4090) & 0x3FU) == 32);
                CHECK((drive.read(0x4092) & 0x3FU) == 16);
                drive.clock_audio();
                CHECK((drive.read(0x4090) & 0x3FU) == 31);
                CHECK((drive.read(0x4092) & 0x3FU) == 15);
                // Restart only the volume divider halfway through a period.
                const auto lead = period / 2;
                for (unsigned cycle = 0; cycle < lead; ++cycle)
                    drive.clock_audio();
                drive.write(0x4080, speed);
                for (unsigned cycle = 0; cycle < period - lead; ++cycle)
                    drive.clock_audio();
                CHECK((drive.read(0x4090) & 0x3FU) == 31);
                CHECK((drive.read(0x4092) & 0x3FU) == 14);
                for (unsigned cycle = 0; cycle < lead; ++cycle)
                    drive.clock_audio();
                CHECK((drive.read(0x4090) & 0x3FU) == 30);
                CHECK((drive.read(0x4092) & 0x3FU) == 14);
            }
        }
    }
}

namespace {
void watchdog_clocks(Fds& drive, Address address, unsigned count) {
    for (unsigned cycle = 0; cycle < count; ++cycle)
        drive.clock_cpu_cycle(address);
}

std::pair<std::uint16_t, Byte> watchdog_counters(const Fds& drive) {
    StateWriter writer;
    drive.save_state(writer);
    const auto& bytes = writer.data();
    std::uint16_t timer = 0;
    std::memcpy(&timer, bytes.data() + bytes.size() - 5, sizeof(timer));
    return {timer, bytes[bytes.size() - 3]};
}
} // namespace

TEST_CASE("FDS watchdog expires after 4096 CPU cycles and repeats without refresh",
          "[fds][watchdog]") {
    Fds drive;
    drive.write(0x4023, 0x83);
    watchdog_clocks(drive, 0x6000, 4095);
    REQUIRE_FALSE(drive.irq_pending());
    drive.clock_cpu_cycle(0xDFFF);
    REQUIRE(drive.irq_pending());
    REQUIRE((drive.read(0x4030) & 3U) == 2);
    REQUIRE_FALSE(drive.irq_pending());
    watchdog_clocks(drive, 0x8000, 4095);
    REQUIRE_FALSE(drive.irq_pending());
    drive.clock_cpu_cycle(0x8000);
    REQUIRE((drive.read(0x4030) & 2U) != 0);
}

TEST_CASE("FDS watchdog refresh reload wins over decrement on the 128th refresh",
          "[fds][watchdog]") {
    for (const Address address : {0x0000, 0x2000, 0x5FFF, 0xE000, 0xFFFF}) {
        Fds drive;
        drive.write(0x4023, 0x83);
        watchdog_clocks(drive, address, 127);
        CHECK(watchdog_counters(drive) == std::pair<std::uint16_t, Byte>{3968, 127});
        drive.clock_cpu_cycle(address);
        CHECK(watchdog_counters(drive) == std::pair<std::uint16_t, Byte>{3245, 0});
        watchdog_clocks(drive, 0x6000, 3245);
        REQUIRE_FALSE(drive.irq_pending());
        drive.clock_cpu_cycle(0x6000);
        REQUIRE(drive.irq_pending());
        // Servicing the IRQ in ROM refreshes rows but must not itself ack it.
        watchdog_clocks(drive, 0xE149, 128);
        REQUIRE(drive.irq_pending());
        REQUIRE((drive.read(0x4030) & 2U) != 0);
        REQUIRE_FALSE(drive.irq_pending());
    }
}

TEST_CASE("FDS watchdog distinguishes interrupt masking from counter reset", "[fds][watchdog]") {
    Fds drive;
    drive.write(0x4023, 3); // D7 masks IRQ, counters still run
    watchdog_clocks(drive, 0x6000, 4096);
    REQUIRE_FALSE(drive.irq_pending());
    watchdog_clocks(drive, 0x6000, 100);
    const auto masked = watchdog_counters(drive);
    drive.write(0x4023, 0x83);
    REQUIRE(watchdog_counters(drive) == masked);
    watchdog_clocks(drive, 0x6000, 3996);
    REQUIRE(drive.irq_pending());
    drive.write(0x4023, 3); // D7 clear acknowledges, but preserves both counters
    REQUIRE_FALSE(drive.irq_pending());
    REQUIRE((drive.read(0x4030) & 2U) == 0);
    drive.write(0x4023, 0x82); // D0 resets both counters
    watchdog_clocks(drive, 0xE000, 512);
    CHECK(watchdog_counters(drive) == std::pair<std::uint16_t, Byte>{4095, 0});
    drive.write(0x4023, 0x83);
    watchdog_clocks(drive, 0x6000, 4095);
    REQUIRE_FALSE(drive.irq_pending());
    drive.clock_cpu_cycle(0x6000);
    REQUIRE(drive.irq_pending());
}

TEST_CASE("FDS refresh on the expiry cycle prevents a watchdog IRQ", "[fds][watchdog]") {
    Fds drive;
    drive.write(0x4023, 0x83);
    watchdog_clocks(drive, 0xE000, 127);
    watchdog_clocks(drive, 0x6000, 3968); // timer zero, one refresh short of reload
    REQUIRE_FALSE(drive.irq_pending());
    drive.clock_cpu_cycle(0xE000);
    CHECK(watchdog_counters(drive) == std::pair<std::uint16_t, Byte>{3245, 0});
    REQUIRE_FALSE(drive.irq_pending());
}

TEST_CASE("FDS watchdog timer and transfer IRQs acknowledge independently", "[fds][watchdog]") {
    Fds drive;
    REQUIRE(drive.load_disk(make_fds_image()));
    drive.write(0x4023, 0x83);
    drive.write(0x4025, 0xE5);
    (void)next_byte(drive); // consume first byte, then request another
    for (int cycle = 0; cycle < 1000 && !drive.irq_pending(); ++cycle)
        drive.step(1);
    REQUIRE(drive.irq_pending());
    watchdog_clocks(drive, 0x6000, 4096);
    drive.write(0x4020, 1);
    drive.write(0x4022, 2);
    drive.step(1);
    CHECK((drive.read(0x4030) & 0x83U) == 0x83); // all three sources
    REQUIRE(drive.irq_pending());                // transfer remains pending after status ack
    (void)drive.read(0x4031);
    REQUIRE_FALSE(drive.irq_pending());
    watchdog_clocks(drive, 0x6000, 4096);
    drive.write(0x4022, 0); // disabling timer must retain watchdog
    drive.write(0x4024, 0); // clearing transfer must retain watchdog
    (void)drive.read(0x4032);
    REQUIRE(drive.irq_pending());
    CHECK((drive.read(0x4030) & 3U) == 2);
}

TEST_CASE("FDS watchdog state resumes counters and pending IRQs", "[fds][watchdog][state]") {
    Fds source;
    source.write(0x4023, 0x83);
    watchdog_clocks(source, 0xE000, 20);
    watchdog_clocks(source, 0x6000, 4000);
    StateWriter writer;
    source.save_state(writer);
    Fds restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    for (unsigned cycle = 0; cycle < 4300; ++cycle) {
        source.clock_cpu_cycle(0x6000);
        restored.clock_cpu_cycle(0x6000);
        REQUIRE(source.irq_pending() == restored.irq_pending());
    }
    REQUIRE(restored.irq_pending());
    StateWriter pending;
    restored.save_state(pending);
    Fds latched;
    StateReader pending_reader(pending.data());
    latched.load_state(pending_reader);
    REQUIRE(pending_reader.ok());
    REQUIRE(latched.irq_pending());
    REQUIRE((latched.read(0x4030) & 2U) != 0);
    // Old snapshots contain no D7 value; start with watchdog IRQ disabled.
    auto legacy = writer.data();
    legacy.resize(legacy.size() - 5);
    Fds imported;
    StateReader old_reader(legacy);
    imported.load_state(old_reader, true, true, false);
    REQUIRE(old_reader.ok());
    watchdog_clocks(imported, 0x6000, 8192);
    REQUIRE_FALSE(imported.irq_pending());
}

TEST_CASE("FDS rejects malformed watchdog counters and flags", "[fds][watchdog][state]") {
    Fds source;
    StateWriter writer;
    source.save_state(writer);
    for (const auto [offset, value] :
         std::array<std::pair<unsigned, Byte>, 4>{{{1, 0x10}, {2, 128}, {3, 2}, {4, 2}}}) {
        auto invalid = writer.data();
        invalid[invalid.size() - 5 + offset] = value;
        Fds restored;
        StateReader reader(invalid);
        restored.load_state(reader);
        REQUIRE_FALSE(reader.ok());
    }
}
