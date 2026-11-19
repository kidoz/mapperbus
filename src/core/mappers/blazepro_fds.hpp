#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// BlazePro FDS multicart FPGA with read-only disk-side data inside PRG ROM.
class BlazeProFds final : public Mapper {
  public:
    BlazeProFds(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override {
        return maps_prg(addr) ? 0xFF : 0;
    }
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override {
        return chr_[addr & 8191U];
    }
    void write_chr(Address addr, Byte value) override {
        chr_[addr & 8191U] = value;
    }
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    enum class Change : Byte { Ready, Eject, Insert };
    struct Registers {
        Byte mode = 0, bank = 0xFF, side = 0, control = 0;
        Word pointer = 0, phase = 0, timer = 0, reload = 0;
        Byte timer_control = 0;
        bool data_irq = false, timer_irq = false, inserted = true;
        Change change = Change::Ready;
        uint32_t poll_interval = 0, eject_delay = 0;
    };
    std::vector<Byte> prg_;
    std::array<Byte, 32768> work_{};
    std::array<Byte, 8192> chr_{};
    Registers regs_;
    bool disk_mode() const {
        return (regs_.mode >> 4) == 4;
    }
    Byte drive_status();
};
} // namespace mapperbus::core
