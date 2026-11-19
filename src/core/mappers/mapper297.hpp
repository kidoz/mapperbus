#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 297: TXC 01-22110-000 MMC1/Mapper 70 multicart banking component.
/// The board's fixed vertical CIRAM wiring is independent of MMC1 mirroring bits.
class Mapper297 final : public Mapper {
  public:
    Mapper297(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] bool mmc1_mode() const;
    [[nodiscard]] std::size_t mapper70_prg_address(Address addr) const;
    [[nodiscard]] std::size_t mmc1_prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;
    void write_mmc1(Address addr, Byte value);
    void commit_mmc1_register(Address addr, Byte value);
    void reset_serial_port();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;

    Byte mode_ = 0U;
    Byte mapper70_latch_ = 0U;
    Byte serial_value_ = 0U;
    std::uint8_t serial_count_ = 0U;
    Byte control_ = 0x0CU;
    Byte chr_bank0_ = 0U;
    Byte chr_bank1_ = 0U;
    Byte prg_bank_ = 0U;
};

} // namespace mapperbus::core
