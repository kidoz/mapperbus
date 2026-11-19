#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 257 submapper 2: Dongda/Hongda 512 KiB educational-cartridge banking.
/// Tape input requires a host tape source and is intentionally left floating.
class Mapper257 final : public Mapper {
  public:
    Mapper257(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

    [[nodiscard]] bool tape_output() const {
        return tape_output_;
    }

  private:
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] Address chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> prg_ram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte mode_ = 0U;
    bool tape_output_ = false;
    bool latched_a0_ = false;
    bool latched_a9_ = false;
    bool previous_a13_ = false;
};

} // namespace mapperbus::core
