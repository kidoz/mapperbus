#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 369: N49C-300 multicart with NROM, NTDEC 2722, and MMC3 games.
class Mapper369 final : public Mapper {
  public:
    Mapper369(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void clock_cpu() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class Mode : Byte { Nrom128, Nrom256, Ntdec, Mmc3Tlrom, Mmc3Tkrom };

    struct NtdecRegisters {
        Byte prg_bank = 0U;
        std::uint16_t irq_counter = 0U;
        bool irq_enabled = false;
    };

    struct Mmc3Registers {
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_latch = 0U;
        bool irq_reload = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool ram_enabled = true;
        bool ram_write_protected = false;
    };

    struct Registers {
        Byte outer = 0U;
        NtdecRegisters ntdec;
        Mmc3Registers mmc3;
    };

    [[nodiscard]] static std::optional<Mode> mode_for_outer(Byte value);
    [[nodiscard]] static bool is_outer_register(Address addr);
    [[nodiscard]] bool is_mmc3() const;
    [[nodiscard]] std::size_t prg_base() const;
    [[nodiscard]] std::size_t chr_base() const;
    [[nodiscard]] unsigned mmc3_prg_bank(Address addr) const;
    [[nodiscard]] unsigned mmc3_chr_bank(Address addr) const;
    [[nodiscard]] std::uint16_t memory_state_tag() const;
    void write_outer(Address addr, Byte value);
    void write_ntdec(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_nvram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Mode mode_ = Mode::Nrom128;
    Registers registers_;
};

} // namespace mapperbus::core
