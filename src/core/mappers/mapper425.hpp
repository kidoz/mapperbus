#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 425: normal OneBus banking with a 128 MiB outer-bank extension.
/// This is the documented 2bpp cartridge component, not a complete VT369 console.
class Mapper425 final : public Mapper {
  public:
    Mapper425(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Address> ciram_address(Address addr) const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override;
    bool write_ppu_register(Address addr, Byte value) override;

    void reset() override;
    bool irq_pending() const override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void on_ppu_hblank() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte control = 0U;
        Byte mirror = 0U;
        std::array<Byte, 3> prg{0U, 1U, 0xFEU};
        Byte middle = 0U;
        Byte mode = 0U;
        std::array<Byte, 6> chr{4U, 5U, 6U, 7U, 0U, 2U};
        Byte intermediate = 0U;
        Byte chr_middle = 0U;
        Byte latch = 0U;
        Byte counter = 0U;
        bool enabled = false;
        bool pending = false;
        bool a12 = false;
        Byte low_cycles = 0U;
        Byte extension = 0U;
    };

    void write_system(unsigned reg, Byte value);
    void clock_counter();
    [[nodiscard]] std::size_t prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Registers registers_;
};

} // namespace mapperbus::core
