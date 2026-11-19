#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Mapper165 final : public Mapper {
  public:
    Mapper165(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    Byte chr_bank(Address addr) const;
    std::size_t prg_ram_size() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::array<Byte, 0x1000> chr_ram_{};
    std::size_t nvram_size_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};
    std::array<Byte, 2> chr_latches_{};

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_pending_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;

    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;
};

} // namespace mapperbus::core
