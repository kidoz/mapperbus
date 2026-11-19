#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 327: BMC-10-24-C-A1 MMC3 6-in-1 multicart.
class Mapper327 final : public Mapper {
  public:
    Mapper327(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte outer_locked = 0U;
        Byte bank_select = 0U;
        Byte bank_mode = 0U;
        std::array<Byte, 8> banks{};
        Byte mirroring = 0U;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        Byte irq_flags = 0U;
        Byte ram_flags = 0x80U;
    };

    [[nodiscard]] unsigned prg_inner_mask() const;
    [[nodiscard]] unsigned chr_inner_mask() const;
    [[nodiscard]] unsigned prg_bank(unsigned slot) const;
    [[nodiscard]] unsigned chr_bank(Address addr) const;
    [[nodiscard]] bool ram_enabled() const;
    [[nodiscard]] bool ram_write_protected() const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 8192> chr_ram_{};
    Registers registers_;
    MirrorMode initial_mirroring_ = MirrorMode::Vertical;
};

} // namespace mapperbus::core
