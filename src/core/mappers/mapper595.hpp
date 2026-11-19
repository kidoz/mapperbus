#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"
#include "core/mappers/sst39sf040.hpp"

namespace mapperbus::core {

/// NES-4MROM-512 flash cartridge with a continuously clocked serial PRG latch.
class Mapper595 final : public Mapper {
  public:
    Mapper595(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    bool maps_prg(Address addr) const override;
    bool unmirrored_nametables() const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    void clock_cpu() override;
    bool has_flash_storage() const override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::uint32_t flash_address(Address addr) const;
    Sst39sf040 flash_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 16384> video_ram_{};
    Byte bank_ = 0;
};
} // namespace mapperbus::core
