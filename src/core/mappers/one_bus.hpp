#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Unencrypted, normal-register OneBus banking component (2bpp, no EVA).
/// Not registered as mapper 256 until console capabilities are integrated.
class OneBus final : public Mapper {
  public:
    OneBus(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address, Byte) override {}
    MirrorMode mirror_mode() const override;
    std::optional<Address> ciram_address(Address addr) const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address) const override {
        return 0U;
    }
    void write_expansion(Address addr, Byte value) override;
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override;
    bool write_ppu_register(Address addr, Byte value) override;
    void reset() override;
    bool irq_pending() const override;
    bool observes_ppu_a12() const override {
        return true;
    }
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
    };
    void clock_counter();
    void write_system(unsigned reg, Byte value);
    Registers registers_;
    std::vector<Byte> prg_;
    // Empty means CHR addresses the same immutable PRG storage, never a copy.
    std::vector<Byte> chr_;
};

} // namespace mapperbus::core
