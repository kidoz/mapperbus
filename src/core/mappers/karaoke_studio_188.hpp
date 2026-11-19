#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class KaraokeStudio188 final : public Mapper {
  public:
    KaraokeStudio188(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    void set_microphone_inputs(bool level, bool button_a_pressed, bool button_b_pressed);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::span<const Byte> selected_lower_rom() const;

    std::vector<Byte> internal_rom_;
    std::vector<Byte> external_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    Byte control_ = 0U;
    bool microphone_level_ = false;
    bool button_a_pressed_ = false;
    bool button_b_pressed_ = false;
};

} // namespace mapperbus::core
