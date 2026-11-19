#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Normal 2bpp VT3x banking component. Legacy writes are conflict-free and reset
/// defaults are deterministic compatibility policies. Console opcode encryption,
/// PCM and RGB palette facilities are absent; mapper 296 remains unregistered.
class Vt3x final : public Mapper {
  public:
    Vt3x(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> ciram_address(Address addr) const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override;
    bool write_ppu_register(Address addr, Byte value) override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void on_ppu_hblank() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte control = 0U;
        Byte native_mirror = 0U;
        std::array<Byte, 3> prg{0U, 1U, 0xFEU};
        Byte prg_middle = 0U;
        Byte prg_mode = 0U;
        std::array<Byte, 6> chr{0U, 2U, 4U, 5U, 6U, 7U};
        Byte chr_intermediate = 0U;
        Byte chr_middle = 0U;
        Byte configuration = 0U;
        Byte outer_extension = 0U;
        Byte common_extension = 0U;
        Byte chr_encrypted = 0U;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        Byte irq_enabled = 0U;
        Byte irq_pending = 0U;
        Byte a12_high = 0U;
        Byte a12_low_cycles = 0U;
        Byte mmc1_shift = 0U;
        Byte mmc1_writes = 0U;
        Byte mmc1_control = 0x0CU;
        Byte mmc1_chr0 = 0U;
        Byte mmc1_chr1 = 0U;
        Byte mmc1_prg = 0U;
        Byte unrom_bank = 0U;
        Byte cnrom_bank = 0U;
    };

    void write_native(unsigned index, Byte value);
    void write_mmc3(Address addr, Byte value);
    void write_mmc1(Address addr, Byte value);
    void commit_mmc1(Address addr, Byte value);
    unsigned prg_inner_bank(unsigned slot) const;
    unsigned chr_inner_bank(unsigned slot) const;
    std::size_t prg_address(Address addr) const;
    std::size_t chr_address(Address addr) const;
    void clock_irq();

    std::vector<Byte> prg_;
    std::vector<Byte> chr_;
    std::array<Byte, 8192> chr_ram_{};
    std::vector<Byte> prg_ram_;
    std::size_t nvram_size_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
