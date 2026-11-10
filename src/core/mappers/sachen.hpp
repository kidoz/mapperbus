#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// Sachen latches, 8259 A-D, and the eight-register SA-015/SA-020A ASIC.
class Sachen final : public Mapper {
  public:
    Sachen(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static bool supports_mapper(uint16_t id);
    static Result<void> validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    bool is_8259() const;
    bool readable_registers() const;
    unsigned mirror_page(unsigned table) const;
    std::size_t chr_address(Address addr) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> chr_ram_{};
    const uint16_t id_;
    const MirrorMode fixed_mirror_;
    std::array<Byte, 8> registers_{};
    Byte select_ = 0U;
    Byte latch_ = 0U;
    Byte jumper_ = 0U;
};
} // namespace mapperbus::core
