#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Kaiser UNL-KS7057: eight 2 KiB PRG windows and volatile 8 KiB CHR RAM.
class KaiserKs7057 final : public Mapper {
  public:
    KaiserKs7057(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    std::array<Byte, 8> prg_banks_{};
    MirrorMode initial_mirror_;
    MirrorMode mirror_;
};

} // namespace mapperbus::core
