#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 497: Subor LOGO board with banked 32 KiB CHR RAM.
class Mapper497 final : public Mapper {
  public:
    Mapper497(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte mode = 0U;
        Byte nametable_bits = 0U;
        bool a13_high = false;
    };

    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x8000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
