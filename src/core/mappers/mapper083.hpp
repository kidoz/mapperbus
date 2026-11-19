#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Cony/Yoko ASIC shared by mapper 83 submappers 0-3 and mapper 264.
class ConyYoko final : public Mapper {
  public:
    ConyYoko(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void clock_cpu() override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class Profile : Byte {
        Standard,
        LargeChr,
        NvramMulticart,
        SmallMulticart,
        Yoko,
    };

    static Profile profile_for(const INesHeader& header);
    Address asic_address(Address addr) const;
    bool has_nvram() const;
    bool has_large_chr() const;
    bool has_128k_inner_prg() const;
    std::size_t prg_offset(Address addr) const;
    void clock_irq();
    uint16_t state_tag() const;

    const Profile profile_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> nvram_;
    Byte jumpers_ = 0U;
    Byte prg_base_ = 0U;
    Byte mode_ = 0U;
    std::array<Byte, 4> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    uint16_t irq_counter_ = 0U;
    Byte irq_source_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool ppu_a12_ = false;
    std::array<Byte, 4> scratch_ram_{};
};

} // namespace mapperbus::core
