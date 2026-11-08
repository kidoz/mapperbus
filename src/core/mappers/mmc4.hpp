#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Nintendo FxROM (10): 16 KiB PRG banks and two PPU-controlled CHR latches.
class Mmc4 final : public Mapper {
  public:
    Mmc4(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    Byte prg_bank_ = 0;
    // B/C select left FD/FE; D/E select right FD/FE.
    std::array<Byte, 4> chr_banks_{};
    std::array<Byte, 2> latches_{};
    const MirrorMode initial_mirror_;
    MirrorMode mirror_;
};

} // namespace mapperbus::core
