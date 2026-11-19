#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Dance2000Speech;

/// NES 2.0 mapper 518: Subor/Dance2000 banking, CHR RAM and LPC speech.
class Mapper518 final : public Mapper {
  public:
    Mapper518(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    ~Mapper518() override;

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Result<void> configure_board_profile(std::string_view profile) override;

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool has_expansion_audio() const override;
    void set_region(Region region) override;
    void clock_audio() override;
    float audio_output() const override;

    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct FdcRegisters {
        Byte configuration = 0U;
        Byte output = 0U;
        Byte data = 0U;
    };

    [[nodiscard]] bool secondary_selected(Address addr) const;
    [[nodiscard]] std::size_t secondary_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> wram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    std::vector<Byte> secondary_ram_;
    MirrorMode initial_mirror_ = MirrorMode::Horizontal;
    Region region_ = Region::Dendy;
    bool sb97_ = false;
    Byte prg_bank_ = 0U;
    Byte mode_ = 0U;
    Byte ciram_a10_ = 0U;
    Byte ppu_a13_ = 0U;
    FdcRegisters fdc_;
    std::unique_ptr<Dance2000Speech> speech_;
};

} // namespace mapperbus::core
