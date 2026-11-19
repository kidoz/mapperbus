#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 384: L1A16 VRC4e multicart with 2 KiB WRAM.
class Mapper384 final : public Mapper {
  public:
    Mapper384(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    void power_on() override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    void write_vrc4(Address addr, Byte value);
    void irq_control(Byte value);
    void irq_tick();
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x0800> wram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;

    Byte outer_bank_ = 0U;
    std::array<Byte, 2> prg_banks_{};
    std::array<std::uint16_t, 8> chr_banks_{};
    MirrorMode mirror_mode_ = MirrorMode::Vertical;
    Byte mode_ = 0U;
    Byte irq_latch_ = 0U;
    Byte irq_counter_ = 0U;
    std::uint16_t irq_prescaler_ = 341U;
    Byte irq_control_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
