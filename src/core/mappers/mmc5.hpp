#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

struct Mmc5Pulse {
    uint8_t duty = 0;
    uint8_t sequence_pos = 0;
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t length_counter = 0;
    bool enabled = false;
    bool halt_length = false;
    bool constant_volume = false;
    uint8_t volume = 0;
    uint8_t envelope_period = 0;
    uint8_t envelope_divider = 0;
    uint8_t envelope_decay = 0;
    bool envelope_start = false;

    void clock_timer();
    void clock_envelope();
    void clock_length();
    uint8_t output() const;
};

/// Mapper 5: MMC5 (ExROM)
/// PRG: up to 1 MB, flexible banking
/// CHR: up to 1 MB
/// Audio: 2 pulse + PCM
class Mmc5 : public Mapper {
  public:
    Mmc5(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

    bool has_expansion_audio() const override {
        return true;
    }
    void clock_audio() override;
    float audio_output() const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x10000> exram_{}; // 64 KB extended RAM (simplified)
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;

    // PRG banking (simplified: mode 3 — four 8KB banks)
    uint8_t prg_mode_ = 3;
    std::array<uint8_t, 5> prg_banks_{};
    uint8_t num_prg_8k_ = 0;

    // CHR banking (simplified)
    std::array<uint16_t, 8> chr_banks_1k_{};

    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // Audio
    Mmc5Pulse pulse1_;
    Mmc5Pulse pulse2_;
    uint8_t pcm_output_ = 0;
    uint32_t audio_cycle_ = 0;

    static constexpr std::array<std::array<uint8_t, 8>, 4> kDuty = {{
        {0, 1, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 0, 0, 0},
        {1, 0, 0, 1, 1, 1, 1, 1},
    }};
};

} // namespace mapperbus::core
