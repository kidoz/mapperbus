#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Konami VRC5/QTa adapter and external 40-pin cartridge.
class Mapper547 final : public Mapper {
  public:
    Mapper547(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    bool maps_prg(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    std::optional<std::array<Byte, 2>> background_pattern(Address tile,
                                                          Address pattern) const override;
    bool irq_pending() const override;
    void clock_cpu() override;
    void power_on() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    unsigned ram_offset(Address addr) const;
    unsigned nt_offset(Address addr) const;
    Byte translated(Byte output) const;
    std::vector<Byte> prg_;
    std::vector<Byte> kanji_;
    std::array<Byte, 16384> ram_{};
    std::array<Byte, 8192> chr_ram_{};
    std::array<Byte, 2048> qtram_{};
    std::array<Byte, 14> regs_{};
    Word counter_ = 0;
    bool pending_ = false;
};
} // namespace mapperbus::core
