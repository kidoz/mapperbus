#pragma once

#include <array>
#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class LpcSpeech;

/// Mapper 241: standard BxROM/WRAM, with an explicit optional LPC speech profile.
class BxromWram final : public Mapper {
  public:
    BxromWram(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    ~BxromWram() override;
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Result<void> configure_board_profile(std::string_view profile) override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte data) override;
    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;
    void set_region(Region region) override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    std::span<Byte> trainer_ram() override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_;
    std::array<Byte, 8192> wram_{};
    MirrorMode mirror_;
    bool chr_ram_;
    bool battery_;
    Byte bank_ = 0U;
    Region region_ = Region::NTSC;
    std::unique_ptr<LpcSpeech> speech_;
};

} // namespace mapperbus::core
