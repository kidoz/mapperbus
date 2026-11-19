#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Discrete latch multicarts 204, 428, 459, 462 and 541.
class MulticartLatches final : public Mapper {
  public:
    MulticartLatches(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
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
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> chr_ram_;
    const uint16_t id_;
    Address address_ = 0;
    Byte outer_ = 0;
    Byte inner_ = 0;
    Byte chr_mask_ = 0;
    Byte jumpers_ = 0;
    Byte upper_jumpers_ = 0;
};

} // namespace mapperbus::core
