#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 235: address-latched Golden Game with configurable ROM sockets.
class GoldenGame final : public Mapper {
  public:
    GoldenGame(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    Byte prg_read_mask(Address addr) const override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    Result<void> configure_board_profile(std::string_view profile) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::optional<unsigned> chip_index() const;
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    Byte sockets_;
    Address latch_ = 0U;
};

} // namespace mapperbus::core
