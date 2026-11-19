#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 456: Realtec K6C3001A MMC3 multicart with a $4100 outer bank.
class Mapper456 final : public Mapper {
  public:
    Mapper456(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer_bank = 0U;
        Byte bank_select = 0U;
        bool prg_mode = false;
        bool chr_inversion = false;
        std::array<Byte, 8> bank_registers{};
        MirrorMode mirror_mode = MirrorMode::Vertical;
        bool prg_ram_enabled = true;
        bool prg_ram_write_protected = false;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        bool irq_reload_pending = false;
        bool irq_enabled = false;
        bool irq_pending = false;
    };

    [[nodiscard]] unsigned inner_prg_bank(Address addr) const;
    [[nodiscard]] unsigned inner_chr_bank(Address addr) const;
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t initial_mirror_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_{};
};

} // namespace mapperbus::core
