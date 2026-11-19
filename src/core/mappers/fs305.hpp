#pragma once

#include <array>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/lpc_speech.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Waixing FS305/Nanjing NJ0430 and the Jncota CHR-ROM wiring (mapper 551).
class Fs305 final : public Mapper {
  public:
    Fs305(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
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
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;
    void set_region(Region region) override;
    Result<void> configure_board_jumpers(Byte value) override;
    std::span<Byte> trainer_ram() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::size_t ram_address(Address addr) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_;
    std::vector<Byte> wram_;
    uint16_t mapper_id_;
    Byte submapper_;
    MirrorMode fixed_mirror_;
    bool battery_;
    std::optional<LpcSpeech> speech_;
    std::array<Byte, 4> regs_{};
    Byte jumpers_ = 0;
    bool read_jumpers_ = false;
};
} // namespace mapperbus::core
