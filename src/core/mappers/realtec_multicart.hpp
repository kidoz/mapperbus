#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 236: Realtec 8031/8155, 8099 and CHR-RAM 8106 address latches.
class RealtecMulticart final : public Mapper {
  public:
    RealtecMulticart(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom);
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
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    Byte lower_mask() const;
    Byte upper_mask() const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_;
    bool chr_ram_;
    Byte lower_ = 0U;
    Byte upper_ = 0U;
    Byte pads_ = 0U;
};

} // namespace mapperbus::core
