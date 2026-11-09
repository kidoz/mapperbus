#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Tengen RAMBO-1 (64) and board 800037 with CHR-driven CIRAM (158).
class Rambo1 final : public Mapper {
  public:
    Rambo1(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    void reset() override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void clock_cpu_falling_edge() override;
    bool irq_pending() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] Byte chr_bank(Address addr) const;
    void clock_scanline_counter();
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const bool chr_ciram_;
    std::array<Byte, 3> prg_banks_ = {31U, 31U, 31U};
    // R0-R5, R8, R9. There is no cartridge RAM.
    std::array<Byte, 8> chr_banks_{};
    Byte select_ = 0x20U;
    Byte mirror_ = 0U;
    Byte latch_ = 0xFFU;
    Byte counter_ = 0U;
    Byte cpu_mode_ = 1U;
    Byte reload_ = 0U;
    Byte enabled_ = 0U;
    Byte pending_ = 1U;
    Byte filter_ = 0U;
    Byte a12_ = 0U;
    Byte irq_delay_ = 0U;
    Byte restart_cycle_ = 0U;
    Byte load_delay_ = 0U;
    uint16_t cpu_remaining_ = 1018U;
};

} // namespace mapperbus::core
