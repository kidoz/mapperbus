#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 371: Spanish PEC-586 main cartridge and 1bpp CHR-RAM mode.
class Mapper371 final : public Mapper {
  public:
    Mapper371(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

    void set_tape_input(bool active);
    [[nodiscard]] bool tape_output() const;

  private:
    enum class PrgSource : Byte { FirstChip = 0U, SecondChip = 5U, ExpansionSlot = 7U };

    struct Registers {
        Byte main = 0U;
        Byte auxiliary = 3U;
        Byte nametable_pins = 0U;
    };

    [[nodiscard]] PrgSource prg_source() const;
    [[nodiscard]] Address chr_address(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    Registers registers_;
    bool tape_input_ = false;
};

} // namespace mapperbus::core
