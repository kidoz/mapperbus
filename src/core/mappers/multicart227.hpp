#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Address-latched mapper 227, including the battery-backed RPG population.
class Multicart227 final : public Mapper {
  public:
    Multicart227(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    Result<void> configure_board_jumpers(Byte value) override;
    Result<void> configure_board_profile(std::string_view profile) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    std::vector<Byte> wram_;
    Byte submapper_;
    Byte pads_ = 0U;
    Address latch_ = 0U;
};

} // namespace mapperbus::core
