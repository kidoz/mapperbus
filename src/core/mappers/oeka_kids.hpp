#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 096: Oeka Kids 32 KiB PRG banking and PPU-addressed CHR RAM banking.
class OekaKids final : public Mapper {
  public:
    OekaKids(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::size_t chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x8000> chr_ram_{};
    Byte outer_bank_ = 0U;
    Byte inner_bank_ = 0U;
    Byte ppu_region_ = 0U;
};

} // namespace mapperbus::core
