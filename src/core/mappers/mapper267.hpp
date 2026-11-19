#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 267: JY-119 MMC3 clone with a shared PRG/CHR outer bank.
class Mapper267 final : public Mapper {
  public:
    Mapper267(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] unsigned outer_bank() const;
    [[nodiscard]] unsigned prg_bank(Address addr) const;
    [[nodiscard]] unsigned chr_bank(Address addr) const;
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const MirrorMode initial_mirror_;
    MirrorMode mirror_mode_;
    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 8> bank_registers_{};
    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
