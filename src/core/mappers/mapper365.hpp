#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 365: Asder PC-95 PRG/CHR banking and documented expansion ports.
class Mapper365 final : public Mapper {
  public:
    Mapper365(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        std::array<Byte, 4> prg_banks{0U, 0U, 0U, 1U};
        std::array<Byte, 8> chr_banks{};
        std::array<Byte, 4> unknown_prg{};
        std::array<Byte, 2> printer_output{};
        Byte keyboard_row = 0U;
        Byte unknown_4905 = 0U;
        Byte unknown_4111 = 0U;
        std::array<Byte, 16> unknown_491x{};
    };

    std::vector<Byte> prg_rom_;
    std::vector<Byte> prg_ram_;
    std::vector<Byte> chr_ram_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
