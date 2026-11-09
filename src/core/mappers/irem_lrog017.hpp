#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Napoleon Senki: banked 2 KiB CHR ROM, 8 KiB shared VRAM and console CIRAM.
class IremLrog017 final : public Mapper {
  public:
    IremLrog017(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_nametable(Address addr) const override;
    std::optional<Address> nametable_address(Address addr) const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> vram_{};
    Byte prg_bank_ = 0U;
    Byte chr_bank_ = 0U;
};

} // namespace mapperbus::core
