#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 406: Haradius Zero's rewired TLROM and 29F040 flash board.
class Mapper406 final : public Mapper {
  public:
    Mapper406(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void clock_irq_counter() override;
    void clock_cpu() override;

    bool has_flash_storage() const override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class FlashCommand : Byte { Read, Unlock1, Unlock2, Program, Erase1, Erase2, Erase3 };
    enum class FlashOperation : Byte { None, Program, Sector, Chip };

    struct Registers {
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_latch = 0U;
        bool irq_reload = false;
        bool irq_enabled = false;
        bool irq_pending = false;
    };

    void write_mmc3(Address addr, Byte value);
    void write_flash(std::size_t address, Byte value);
    void begin_flash(FlashOperation operation,
                     std::size_t address,
                     Byte data,
                     std::uint32_t cycles);
    [[nodiscard]] std::size_t prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;

    std::vector<Byte> flash_;
    std::vector<Byte> chr_rom_;
    const Byte submapper_;
    const MirrorMode initial_mirror_;
    Registers registers_;
    FlashCommand flash_command_ = FlashCommand::Read;
    FlashOperation flash_operation_ = FlashOperation::None;
    bool flash_id_mode_ = false;
    Byte flash_toggle_ = 0U;
    Byte pending_data_ = 0U;
    std::uint32_t pending_address_ = 0U;
    std::uint32_t flash_cycles_ = 0U;
};

} // namespace mapperbus::core
