#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Mapper162Fs304 final : public Mapper {
  public:
    Mapper162Fs304(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::size_t prg_bank() const;
    Address chr_address(Address addr) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode mirror_;
    Byte bank_low_ = 0U;
    Byte bank_alternative_ = 0U;
    Byte bank_high_ = 0U;
    Byte mode_ = 0U;
    bool latched_a9_ = false;
    bool previous_a13_ = false;
};

} // namespace mapperbus::core
