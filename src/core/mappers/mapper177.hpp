#pragma once

#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class LpcSpeech;

/// Mapper 177: Henggedianzi 32 KiB PRG banking, banked WRAM and CHR RAM.
class Henggedianzi177 final : public Mapper {
  public:
    Henggedianzi177(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    ~Henggedianzi177() override;

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Result<void> configure_board_profile(std::string_view profile) override;

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
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

    void reset() override;
    void power_on() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    bool chr16_enabled() const;
    Address chr_address(Address addr) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_ram_;
    std::vector<Byte> wram_;
    MirrorMode mirror_;
    Byte submapper_ = 0U;
    Byte latch_ = 0U;
    Byte nametable_chr_bank_ = 0U;
    Region region_ = Region::NTSC;
    std::unique_ptr<LpcSpeech> speech_;
};

} // namespace mapperbus::core
