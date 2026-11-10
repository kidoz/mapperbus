#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// J.Y. Company ASIC and its documented outer-bank multicart PCBs.
class JyCompany final : public Mapper {
  public:
    JyCompany(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static bool supports_mapper(uint16_t id);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void reset() override;
    void clock_cpu() override;
    void observe_cpu_write(Address addr, Byte data) override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    bool irq_pending() const override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct OuterBank {
        unsigned base;
        unsigned mask;
    };
    OuterBank prg_outer_bank() const;
    OuterBank chr_outer_bank() const;
    bool outer_aliases_a11() const;
    std::size_t prg_address(Address addr) const;
    std::size_t chr_address(Address addr) const;
    bool chr_nametable(Address addr) const;
    void clock_irq(Byte source);
    void disable_irq();
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_;
    std::vector<Byte> prg_ram_;
    const bool chr_ram_;
    const bool persistent_;
    const bool inhibit_nametables_;
    const uint16_t mapper_id_;
    std::array<Byte, 4> prg_banks_{};
    std::array<uint16_t, 8> chr_banks_{};
    std::array<uint16_t, 4> nt_banks_{};
    std::array<Byte, 4> config_{};
    std::array<Byte, 2> latches_{};
    Byte jumpers_ = 0U;
    Byte multiplicand_ = 0U;
    Byte multiplier_ = 0U;
    uint16_t product_ = 0U;
    uint16_t next_product_ = 0U;
    Byte multiply_remaining_ = 0U;
    Byte accumulator_ = 0U;
    Byte test_ = 0U;
    Byte irq_mode_ = 0U;
    Byte irq_config_ = 0U;
    Byte prescaler_ = 0U;
    Byte counter_ = 0U;
    Byte xor_ = 0U;
    Byte enabled_ = 0U;
    Byte pending_ = 0U;
    Byte a12_ = 0U;
};
} // namespace mapperbus::core
