#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 286: Benshieng BMC-BS-5 address-latched multicart.
class BenshiengBs5 final : public Mapper {
  public:
    BenshiengBs5(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    Result<void> configure_board_jumpers(Byte value) override;
    Result<void> configure_board_profile(std::string_view profile) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 4> prg_banks_{};
    std::array<Byte, 4> chr_banks_{};
    MirrorMode mirror_ = MirrorMode::Horizontal;
    Byte pads_ = 0U;
};

} // namespace mapperbus::core
