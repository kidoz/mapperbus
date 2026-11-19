#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 355: Hwang Shinwei 3D-BLOCK with PIC16C54 protection.
class Mapper355 final : public Mapper {
  public:
    using PicRom = std::array<std::uint16_t, 512>;

    Mapper355(const INesHeader& header,
              std::vector<Byte> prg_rom,
              std::vector<Byte> chr_rom,
              PicRom pic_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    static Result<PicRom> decode_pic_rom(std::span<const Byte> data);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    void observe_cpu_read(Address addr, Byte data) override;
    void observe_cpu_write(Address addr, Byte data) override;
    void clock_cpu() override;
    bool irq_pending() const override;

    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct PicState {
        std::array<Byte, 32> ram{};
        std::array<std::uint16_t, 2> stack{};
        std::uint16_t pc = 0U;
        std::uint16_t timer_divider = 0U;
        Address address_pins = 0U;
        Byte w = 0U;
        Byte stack_depth = 0U;
        Byte trisa = 0x0FU;
        Byte trisb = 0xFFU;
        Byte option = 0xFFU;
        Byte clock_divider = 0U;
        bool sleeping = false;
    };

    [[nodiscard]] Byte read_register(Byte addr) const;
    void write_register(Byte addr, Byte value);
    void write_result(Byte addr, Byte destination, Byte value);
    void set_pc(std::uint16_t value);
    void skip_next();
    void push_stack(std::uint16_t value);
    [[nodiscard]] std::uint16_t pop_stack();
    void set_zero(Byte value);
    void set_add_flags(unsigned left, unsigned right, Byte result);
    void set_sub_flags(unsigned left, unsigned right, Byte result);
    void clock_timer();
    void execute_instruction();
    [[nodiscard]] std::uint32_t pic_identity() const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    PicRom pic_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode mirror_ = MirrorMode::Vertical;
    PicState pic_;
};

} // namespace mapperbus::core
