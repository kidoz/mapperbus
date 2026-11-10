#pragma once

#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"
#include "core/mappers/sst39sf040.hpp"

namespace mapperbus::core {
/// UNROM 512 and GTROM. Mapper 111's CHR-ROM variant uses Mmc1 instead.
class FlashBoards final : public Mapper {
  public:
    FlashBoards(const INesHeader& header, std::vector<Byte> prg, const std::vector<Byte>& chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_cpu() override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte data) override;
    bool unmirrored_nametables() const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    bool has_flash_storage() const override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    std::optional<Byte> led_outputs() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    static bool gtrom_alias(Address addr);
    uint32_t prg_address(Address addr) const;
    std::size_t pattern_address(Address addr) const;
    std::vector<Byte> rom_;
    std::optional<Sst39sf040> flash_;
    std::vector<Byte> chr_ram_;
    const bool gtrom_;
    const Byte submapper_;
    const bool conflicts_;
    const bool single_screen_;
    const bool four_screen_;
    const MirrorMode fixed_mirror_;
    Byte latch_ = 0U;
    Byte leds_ = 0xFFU;
};
} // namespace mapperbus::core
