#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 357: Bit Corp 4602 four-game DIP-selected multicart.
class Mapper357 final : public Mapper {
  public:
    Mapper357(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void write_expansion(Address addr, Byte value) override;

    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte smb_bank = 0U;
        Byte smb_fixed_banks = 0U;
        Byte unrom_bank = 0U;
        std::uint16_t irq_counter = 0U;
        Byte irq_flags = 0U;
    };

    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    Byte dip_switches_ = 0U;
    Registers registers_;
};

} // namespace mapperbus::core
