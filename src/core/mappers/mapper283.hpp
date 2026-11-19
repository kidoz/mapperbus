#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 283: BMC-GS-2004/GS-2013 32 KiB PRG multicart banking.
class BlockFamily283 final : public Mapper {
  public:
    BlockFamily283(const INesHeader& header,
                   std::vector<Byte> prg_rom,
                   const std::vector<Byte>& chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode mirror_;
    std::size_t main_rom_size_;
    bool has_auxiliary_rom_;
    Byte prg_bank_ = 0U;
};

} // namespace mapperbus::core
