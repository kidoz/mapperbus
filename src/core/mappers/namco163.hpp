#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 19: Namco 163 (N163)
/// PRG: 256 KB, 8 KB switchable
/// CHR: 256 KB, 1 KB switchable
/// Audio: up to 8 wavetable channels (shared 128-byte internal RAM)
class Namco163 : public Mapper {
  public:
    Namco163(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    bool maps_expansion(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    bool irq_pending() const override {
        return irq_pending_;
    }

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
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;
    uint8_t num_prg_8k_ = 0;

    std::array<uint8_t, 4> prg_banks_{};
    std::array<uint8_t, 8> chr_banks_{};
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // IRQ
    uint16_t irq_counter_ = 0;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;

    // N163 internal sound RAM (128 bytes)
    std::array<Byte, 128> sound_ram_{};
    uint8_t sound_addr_ = 0;
    bool auto_increment_ = false;
    uint8_t num_channels_ = 1; // 1-8 channels
    uint16_t audio_timer_ = 0;
    uint8_t current_channel_ = 0;

    // Per-channel state
    struct N163Channel {
        uint32_t phase = 0;      // 24-bit phase accumulator
        uint32_t frequency = 0;  // 18-bit frequency
        uint8_t wave_addr = 0;   // Wave address in sound RAM
        uint8_t wave_length = 0; // Wave length (256 - L*4)
        uint8_t volume = 0;      // 4-bit volume
        uint8_t output = 0;      // Current output sample
    };
    std::array<N163Channel, 8> channels_{};
};

} // namespace mapperbus::core
