#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/vrc7.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header_for(uint16_t mapper, uint8_t prg16k, uint8_t chr8k) {
    return INesHeader{
        .prg_rom_banks = prg16k,
        .chr_rom_banks = chr8k,
        .mapper_number = mapper,
        .submapper = 0,
        .mirror_mode = MirrorMode::Vertical,
        .region = Region::NTSC,
        .has_battery = false,
        .has_trainer = false,
        .is_nes2 = false,
    };
}

// PRG ROM where the first byte of each 8 KB bank equals the bank index.
std::vector<Byte> banked_prg(int banks_8k) {
    std::vector<Byte> prg(static_cast<std::size_t>(banks_8k) * 0x2000, 0);
    for (int b = 0; b < banks_8k; ++b) {
        prg[static_cast<std::size_t>(b) * 0x2000] = static_cast<Byte>(b);
    }
    return prg;
}

// CHR ROM where the first byte of each 1 KB bank equals the bank index.
std::vector<Byte> banked_chr(int banks_1k) {
    std::vector<Byte> chr(static_cast<std::size_t>(banks_1k) * 0x400, 0);
    for (int b = 0; b < banks_1k; ++b) {
        chr[static_cast<std::size_t>(b) * 0x400] = static_cast<Byte>(b);
    }
    return chr;
}

} // namespace

TEST_CASE("MMC5 PRG mode 3 selects four independent 8 KB banks", "[mmc5]") {
    Mmc5 m(header_for(5, 4, 1), banked_prg(8), banked_chr(8));
    m.write_expansion(0x5100, 0x03);     // PRG mode 3
    m.write_expansion(0x5114, 0x80 | 2); // $8000 = ROM bank 2
    m.write_expansion(0x5115, 0x80 | 3); // $A000 = ROM bank 3
    m.write_expansion(0x5116, 0x80 | 4); // $C000 = ROM bank 4
    m.write_expansion(0x5117, 5);        // $E000 = ROM bank 5 (always ROM)

    REQUIRE(m.read_prg(0x8000) == 2);
    REQUIRE(m.read_prg(0xA000) == 3);
    REQUIRE(m.read_prg(0xC000) == 4);
    REQUIRE(m.read_prg(0xE000) == 5);
}

TEST_CASE("MMC5 banks PRG-RAM at $6000 via $5113", "[mmc5]") {
    Mmc5 m(header_for(5, 4, 1), banked_prg(8), banked_chr(8));
    m.write_expansion(0x5113, 0); // RAM bank 0
    m.write_prg(0x6000, 0xAB);
    m.write_expansion(0x5113, 1); // RAM bank 1
    m.write_prg(0x6000, 0xCD);

    REQUIRE(m.read_prg(0x6000) == 0xCD); // bank 1
    m.write_expansion(0x5113, 0);
    REQUIRE(m.read_prg(0x6000) == 0xAB); // bank 0 preserved
}

TEST_CASE("MMC5 maps PRG-RAM into the $8000 window when bit7 is clear", "[mmc5]") {
    Mmc5 m(header_for(5, 4, 1), banked_prg(8), banked_chr(8));
    m.write_expansion(0x5100, 0x03);
    m.write_expansion(0x5114, 0x00); // $8000 = PRG-RAM (bit7 clear)
    m.write_prg(0x8000, 0x55);
    REQUIRE(m.read_prg(0x8000) == 0x55);

    m.write_expansion(0x5114, 0x80 | 1); // remap to ROM bank 1
    REQUIRE(m.read_prg(0x8000) == 1);
    m.write_prg(0x8000, 0x99); // write to ROM window is ignored
    REQUIRE(m.read_prg(0x8000) == 1);
}

TEST_CASE("MMC5 CHR mode 3 selects eight 1 KB banks", "[mmc5]") {
    Mmc5 m(header_for(5, 4, 4), banked_prg(8), banked_chr(32));
    m.write_prg(0x5101, 0x03); // 1 KB CHR mode
    m.write_expansion(0x5120, 2);
    m.write_expansion(0x5127, 7);
    REQUIRE(m.read_chr(0x0000) == 2);
    REQUIRE(m.read_chr(0x1C00) == 7);
}

TEST_CASE("MMC5 scanline IRQ fires at the target scanline", "[mmc5][irq]") {
    Mmc5 m(header_for(5, 4, 1), banked_prg(8), banked_chr(8));
    m.write_expansion(0x5203, 16);   // target scanline
    m.write_expansion(0x5204, 0x80); // enable IRQ
    m.on_ppu_frame_start();

    for (int i = 0; i < 15; ++i) {
        m.clock_irq_counter();
        REQUIRE_FALSE(m.irq_pending());
    }
    m.clock_irq_counter(); // scanline 16
    REQUIRE(m.irq_pending());

    const Byte status = m.read_expansion(0x5204);
    REQUIRE((status & 0x80) != 0);  // pending reported
    REQUIRE((status & 0x40) != 0);  // in-frame
    REQUIRE_FALSE(m.irq_pending()); // read acknowledged it
}

TEST_CASE("MMC5 scanline IRQ stays silent when disabled", "[mmc5][irq]") {
    Mmc5 m(header_for(5, 4, 1), banked_prg(8), banked_chr(8));
    m.write_expansion(0x5203, 4); // target, but no enable write
    m.on_ppu_frame_start();
    for (int i = 0; i < 20; ++i) {
        m.clock_irq_counter();
    }
    REQUIRE_FALSE(m.irq_pending());
}

namespace {

// Drives a VRC7 channel: selects an instrument/volume, frequency and key-on.
void key_on_vrc7(Vrc7& m, uint8_t ch, uint8_t instrument, uint8_t volume) {
    m.write_prg(0x9010, static_cast<Byte>(0x30 + ch));
    m.write_prg(0x9030, static_cast<Byte>((instrument << 4) | (volume & 0x0F)));
    m.write_prg(0x9010, static_cast<Byte>(0x10 + ch));
    m.write_prg(0x9030, 0x80); // F-number low
    m.write_prg(0x9010, static_cast<Byte>(0x20 + ch));
    m.write_prg(0x9030, 0x10 | (4 << 1) | 0x01); // key-on, octave 4, F-num hi
}

float run_vrc7_peak(Vrc7& m, int audio_steps) {
    float peak = 0.0f;
    for (int i = 0; i < audio_steps; ++i) {
        m.clock_audio();
        const float s = m.audio_output();
        peak = std::max(peak, std::fabs(s));
    }
    return peak;
}

// FNV-1a hash over the quantized output waveform — distinguishes two signals
// of equal peak amplitude but different shape (e.g. phase modulation).
std::uint64_t run_vrc7_signature(Vrc7& m, int audio_steps) {
    std::uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < audio_steps; ++i) {
        m.clock_audio();
        const auto q = static_cast<std::int32_t>(m.audio_output() * 1000000.0f);
        h = (h ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(q))) * 1099511628211ULL;
    }
    return h;
}

} // namespace

TEST_CASE("VRC7 produces audio for a keyed-on preset instrument", "[vrc7][audio]") {
    Vrc7 m(header_for(85, 32, 0), banked_prg(64), {});
    key_on_vrc7(m, 0, /*instrument=*/1, /*volume=*/0);
    REQUIRE(run_vrc7_peak(m, 4000) > 0.0f);
}

TEST_CASE("VRC7 key-off and max attenuation are silent", "[vrc7][audio]") {
    Vrc7 m(header_for(85, 32, 0), banked_prg(64), {});
    // Volume 15 = -45 dB: effectively inaudible but not a hard zero (the
    // old implementation special-cased it to full silence).
    key_on_vrc7(m, 0, 1, 15);
    REQUIRE(run_vrc7_peak(m, 2000) < 1.0e-4f);

    // Key-off: after the release envelope runs out the channel is idle and
    // the cached output is exactly zero.
    m.write_prg(0x9010, 0x20);
    m.write_prg(0x9030, (4 << 1) | 0x01); // key-on bit cleared
    // Release at RR=7 runs ~64 dB/s: ~0.75 s to the 48 dB cutoff. The FM
    // core ticks once per 36 CPU cycles, so give it ~1 s of CPU cycles.
    run_vrc7_peak(m, 1800000);
    REQUIRE(run_vrc7_peak(m, 2000) == 0.0f);
}

TEST_CASE("VRC7 instruments have distinct timbres", "[vrc7][audio]") {
    // Two different presets advance their operators at different multiples and
    // carry different modulator depths, so their waveforms must differ.
    Vrc7 a(header_for(85, 32, 0), banked_prg(64), {});
    key_on_vrc7(a, 0, /*instrument=*/1, /*volume=*/0);
    const std::uint64_t sig_a = run_vrc7_signature(a, 4000);

    Vrc7 b(header_for(85, 32, 0), banked_prg(64), {});
    key_on_vrc7(b, 0, /*instrument=*/6, /*volume=*/0);
    const std::uint64_t sig_b = run_vrc7_signature(b, 4000);

    REQUIRE(sig_a != sig_b);
}

TEST_CASE("VRC7 custom instrument changes the timbre", "[vrc7][audio]") {
    // Patch 0 selects the user-defined custom instrument; writing a non-trivial
    // custom patch must change the produced waveform.
    Vrc7 flat(header_for(85, 32, 0), banked_prg(64), {});
    key_on_vrc7(flat, 0, /*instrument=*/0, /*volume=*/0);
    const std::uint64_t flat_sig = run_vrc7_signature(flat, 4000);

    Vrc7 shaped(header_for(85, 32, 0), banked_prg(64), {});
    // AR nibbles must be nonzero: attack rate 0 never opens the envelope
    // (true OPLL behavior), which would leave both patches silent.
    const uint8_t patch[8] = {0x21, 0x21, 0x10, 0x07, 0xF0, 0xF0, 0x00, 0x00};
    for (uint8_t i = 0; i < 8; ++i) {
        shaped.write_prg(0x9010, i);
        shaped.write_prg(0x9030, patch[i]);
    }
    key_on_vrc7(shaped, 0, /*instrument=*/0, /*volume=*/0);
    const std::uint64_t shaped_sig = run_vrc7_signature(shaped, 4000);

    REQUIRE(shaped_sig != flat_sig);
}

TEST_CASE("VRC7 PRG banking maps the selected ROM bank", "[vrc7]") {
    Vrc7 m(header_for(85, 32, 0), banked_prg(64), {});
    m.write_prg(0x8000, 10); // $8000 bank = 10
    m.write_prg(0x8010, 20); // $A000 bank = 20
    m.write_prg(0x9000, 30); // $C000 bank = 30
    REQUIRE(m.read_prg(0x8000) == 10);
    REQUIRE(m.read_prg(0xA000) == 20);
    REQUIRE(m.read_prg(0xC000) == 30);
    REQUIRE(m.read_prg(0xE000) == 63); // fixed last bank
}
