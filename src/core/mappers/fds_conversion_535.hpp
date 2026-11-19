#pragma once

#include <array>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

// 128 KiB FDS conversions with unbanked CHR RAM and PRG ROM at $6000.
class FdsConversion535 final : public Mapper {
  public:
    FdsConversion535(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override {
        return addr >= 0x6000U;
    }
    Byte prg_read_mask(Address addr) const override {
        return maps_prg(addr) ? 0xFFU : 0U;
    }
    Byte read_chr(Address addr) override {
        return chr_ram_[addr & 0x1FFFU];
    }
    void write_chr(Address addr, Byte value) override {
        chr_ram_[addr & 0x1FFFU] = value;
    }
    MirrorMode mirror_mode() const override {
        return mirror_;
    }
    void clock_cpu() override;
    bool irq_pending() const override {
        return irq_pending_ != 0U;
    }
    void acknowledge_irq() override {
        irq_pending_ = 0U;
    }
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::optional<std::size_t> ram_offset(Address addr) const;
    const std::uint16_t id_;
    const MirrorMode initial_mirror_;
    std::vector<Byte> prg_;
    std::vector<Byte> ram_;
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode mirror_;
    Byte bank_ = 0;
    Byte irq_enabled_ = 0;
    Byte irq_pending_ = 0;
    std::uint16_t counter_ = 0;
};
} // namespace mapperbus::core
