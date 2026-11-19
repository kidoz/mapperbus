#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Hongda/Subor C/E BASIC board with per-tile CHR ROM selection.
class Mapper560 final : public Mapper {
  public:
    Mapper560(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
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
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    std::optional<std::array<Byte, 2>> background_pattern(Address tile,
                                                          Address pattern) const override;

  private:
    Byte pattern_byte(Address addr, Byte shadow) const;
    const Byte submapper_;
    std::vector<Byte> prg_;
    std::vector<Byte> chr_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 1024> shadow_{};
    Word tile_ = 0;
    bool previous_nt_ = false;
    bool mode_ = true;
};
} // namespace mapperbus::core
