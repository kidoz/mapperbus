#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/pec_computer.hpp"

namespace mapperbus::core {

/// Dongda Sonic REC-9388 and Golden Key computer cartridges.
class Education440442 final : public PecComputer {
  public:
    Education440442(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override {
        return addr >= 0x6000;
    }
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override {
        return (addr & 0xF000) == 0x5000;
    }
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool observes_ppu_address() const override {
        return id_ == 442;
    }
    void observe_ppu_address(Address addr) override;
    bool observes_ppu_reads() const override {
        return id_ == 440;
    }
    void observe_ppu_read(Address addr) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte mode = 0, auxiliary = 0, index = 0, status = 0, pins = 0, pa13 = 0;
    };
    unsigned prg_bank() const;
    std::vector<Byte> prg_;
    std::array<Byte, 8192> chr_{}, ram_{};
    uint16_t id_;
    MirrorMode mirror_;
    Registers regs_;
};
} // namespace mapperbus::core
