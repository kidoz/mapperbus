#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 168: RacerMate's banked PRG ROM, CHR RAM and cycle-counter IRQ.
class Racermate168 final : public Mapper {
  public:
    Racermate168(const INesHeader& header,
                 std::vector<Byte> prg_rom,
                 const std::vector<Byte>& chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    bool irq_pending() const override;
    void clock_cpu() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::size_t chr_offset(Address addr) const;
    bool persistent_offset(std::size_t offset) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 65536> chr_ram_{};
    bool all_battery_ = false;
    Byte bank_select_ = 0U;
    uint16_t irq_counter_ = 0U;
    bool irq_disabled_ = false;
    bool ram_protected_ = true;
};

} // namespace mapperbus::core
