#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 359: SB-5013 / GCL8050 / 841242C multicart.
class Mapper359 final : public Mapper {
  public:
    Mapper359(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    bool observes_ppu_a12() const override {
        return true;
    }
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        std::array<Byte, 4> prg_banks{};
        Byte prg_base = 0U;
        Byte bank_size = 0U;
        Byte mirror = 0U;
        Byte chr_base = 0U;
        std::array<Byte, 8> chr_banks{};
        std::uint16_t m2_counter = 0U;
        Byte irq_latch = 0U;
        Byte pa12_counter = 0U;
        Byte irq_mode = 0U;
        Byte irq_flags = 0U;
        Byte a12_pins = 0U;
    };

    [[nodiscard]] unsigned prg_inner_mask() const;
    [[nodiscard]] bool irq_enabled() const;
    void set_irq_enabled(bool enabled);
    void clock_pa12_irq();
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    const bool two_kib_chr_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
