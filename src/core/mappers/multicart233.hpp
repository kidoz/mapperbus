#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 233 with separate profiles for conflicting published board models.
class Multicart233 final : public Mapper {
  public:
    Multicart233(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    Result<void> configure_board_profile(std::string_view profile) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class Profile : Byte { Reset42, Disch233, EverynesVector };
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    Profile profile_ = Profile::Reset42;
    Byte control_ = 0U;
    Byte auxiliary_ = 0U;
};

} // namespace mapperbus::core
