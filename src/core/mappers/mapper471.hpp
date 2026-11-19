#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 471: Impact Soft IM1 with unfiltered falling-A12 IRQs.
class Mapper471 final : public Mapper {
  public:
    Mapper471(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    bool observes_ppu_a12() const override {
        return true;
    }
    void observe_ppu_a12(bool high) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte bank = 0U;
        bool a12_high = false;
        bool irq_pending = false;
    };

    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_{};
};

} // namespace mapperbus::core
