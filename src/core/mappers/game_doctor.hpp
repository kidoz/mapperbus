#pragma once
#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"
namespace mapperbus::core {
/// Cartridge gameplay mode of the Bung Super and Venus Turbo Game Doctors.
class GameDoctor final : public Mapper {
  public:
    static Result<std::unique_ptr<Mapper>> create(const INesHeader&,
                                                  std::vector<Byte>,
                                                  std::vector<Byte>,
                                                  std::span<const Byte>);
    Result<void> configure_board_profile(std::string_view) override;
    Result<void> configure_game_doctor_bios(std::span<const Byte>) override;
    Byte read_prg(Address addr) override;
    bool maps_prg(Address addr) const override {
        return addr >= 0x6000;
    }
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte value) override;
    bool observes_ppu_address() const override {
        return turbo_;
    }
    void observe_ppu_address(Address addr) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool irq_pending() const override {
        return s_.pending != 0;
    }
    void acknowledge_irq() override {
        s_.pending = 0;
    }
    void clock_cpu() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    GameDoctor(const INesHeader&, std::vector<Byte>, std::vector<Byte>, std::span<const Byte>);
    unsigned prg_bank(unsigned slot) const;
    unsigned chr_offset(Address addr) const;
    bool wide_prg() const;
    const bool turbo_;
    const Byte initial_mode_;
    const std::vector<Byte> prg_image_, chr_image_, trainer_;
    std::array<Byte, 2048> bios_{};
    bool has_bios_ = false;
    std::vector<Byte> prg_, chr_;
    std::array<Byte, 8192> wram_{};
    struct Registers {
        Byte mode = 0, outer = 1, turbo_mode = 0, latch = 0, chr8 = 0, chr_lock = 0, ppu_slot = 0;
        Byte boot = 0, pending = 0, timer_enable = 0, disk_enable = 0;
        Word counter = 0, target = 0, disk_phase = 0;
        std::array<Byte, 4> pages{28, 29, 30, 31};
        std::array<Byte, 8> tiles{0, 1, 2, 3, 4, 5, 6, 7};
    } s_;
};
} // namespace mapperbus::core
