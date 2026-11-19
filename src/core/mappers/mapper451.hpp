#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 451: Haratyler HG/MP flash, banked CHR RAM and MP3 controller.
class Mapper451 final : public Mapper {
  public:
    Mapper451(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte data) override;
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
    enum class Mp3Phase : Byte { Idle, Command, Terminate, Status };

    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;
    [[nodiscard]] Byte mp3_status() const;
    void write_flash(std::size_t address, Address bus_address, Byte value);
    void begin_flash(FlashOperation operation,
                     std::size_t address,
                     Byte data,
                     std::uint32_t cycles);
    void clock_flash();
    void clock_mp3();
    void read_mp3();
    void execute_mp3_command();

    std::vector<Byte> flash_;
    std::array<Byte, 0x4000> chr_ram_{};
    const MirrorMode initial_mirror_;
    Byte bank_ = 0U;
    MirrorMode mirror_ = MirrorMode::Vertical;
    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;

    FlashCommand flash_command_ = FlashCommand::Read;
    FlashOperation flash_operation_ = FlashOperation::None;
    bool flash_id_mode_ = false;
    Byte flash_toggle_ = 0U;
    Byte pending_data_ = 0U;
    std::uint32_t pending_address_ = 0U;
    std::uint32_t flash_cycles_ = 0U;

    Mp3Phase mp3_phase_ = Mp3Phase::Idle;
    Byte mp3_shift_ = 0U;
    Byte mp3_bit_count_ = 0U;
    std::uint32_t mp3_cycles_ = 0U;
    Byte mp3_status_index_ = 0U;
    Byte mp3_volume_ = 0U;
    Byte mp3_equalizer_ = 0U;
    Byte mp3_folder_ = 0U;
    Byte mp3_song_ = 0U;
    bool mp3_playing_ = false;
};

} // namespace mapperbus::core
