#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// COOLGIRL multicart CPLD, native loader registers and selectable game mappers.
class Mapper342 final : public Mapper {
  public:
    Mapper342(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool observes_ppu_a12() const override {
        return core_code() == 13U || core_code() == 20U || core_code() == 22U || state_.irq_enable;
    }
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void clock_cpu_falling_edge() override;
    bool irq_pending() const override {
        return state_.irq_pending || state_.h3001_pending || state_.fme_pending ||
               state_.vrc_pending || state_.cv_pending;
    }
    bool observes_ppu_reads() const override {
        return (state_.registers[4] >> 5U) == 5U || core_code() == 15U;
    }
    void observe_ppu_read(Address addr) override;
    std::optional<Address> nametable_address(Address addr) const override;
    bool unmirrored_nametables() const override {
        return true;
    }
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void power_on() override;
    void reset() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    struct RuntimeState {
        std::array<Byte, 8> registers{};
        std::array<Byte, 4> prg_banks{};
        std::array<std::uint16_t, 8> chr_banks{};
        Byte jy_xor = 0;
        Byte rom_at_6000 = 0, low_prg = 0;
        std::uint16_t h3001_counter = 0, h3001_latch = 0, fme_counter = 0;
        Byte h3001_enable = 0, h3001_pending = 0;
        Byte fme_select = 0, fme_control = 0, fme_pending = 0;
        Byte vrc_control = 0, vrc_latch = 0, vrc_counter = 0, vrc_pending = 0;
        Byte vrc_phase = 0, vrc_divider = 0;
        Byte cv_nt_reads = 0, cv_idle = 16, cv_line = 0, cv_irq_line = 0;
        Byte cv_enable = 0, cv_pending = 0;
        Byte serial = 0x10U;
        Byte bank_select = 0;
        Byte chr_latches = 0;
        Byte irq_latch = 0, irq_counter = 0, irq_enable = 0, irq_reload = 0;
        Byte irq_ready = 0, irq_pending = 0, a12 = 0, low_m2 = 0;
    };

    unsigned core_code() const;
    void write_mmc1(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);
    void write_jy(Address addr, Byte value);
    void write_h3001(Address addr, Byte value);
    void write_vrc(Address addr, Byte value);
    void write_fme7(Address addr, Byte value);
    void write_castlevania(Address addr, Byte value);
    void write_taito(Address addr, Byte value);
    void write_inner(Address addr, Byte value);
    void set_mirror(unsigned mode);
    void set_prg_mode(unsigned mode);
    void set_chr_mode(unsigned mode);
    unsigned chr_bank(unsigned slot) const;
    bool wram_mapped(Address addr) const;
    std::uint32_t mapped_prg_offset(Address addr) const;
    std::uint32_t mapped_chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::uint32_t flash_address_mask_;
    std::vector<Byte> wram_;
    bool battery_;
    std::vector<Byte> chr_ram_;
    RuntimeState state_;
};

} // namespace mapperbus::core
