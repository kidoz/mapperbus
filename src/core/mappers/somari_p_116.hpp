#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class SomariP116 final : public Mapper {
  public:
    SomariP116(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    [[nodiscard]] static Result<void> validate_geometry(const INesHeader& header,
                                                        std::size_t prg_size,
                                                        std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

  private:
    struct Vrc2State {
        std::array<Byte, 2> prg_banks{};
        std::array<Byte, 8> chr_banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte microwire = 0U;
    };
    struct Mmc3State {
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        bool irq_reload_flag = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool ram_enabled = false;
        bool ram_write_protected = false;
    };
    struct Mmc1State {
        Byte shift = 0x10U;
        Byte write_count = 0U;
        Byte control = 0x0CU;
        Byte chr_bank0 = 0U;
        Byte chr_bank1 = 0U;
        Byte prg_bank = 0U;
    };

    [[nodiscard]] bool supervisor_address(Address addr) const;
    [[nodiscard]] bool vrc2_active() const;
    [[nodiscard]] bool mmc3_active() const;
    [[nodiscard]] bool mmc1_active() const;
    void write_supervisor(Byte value);
    void reset_mmc1_serial();
    void write_vrc2(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);
    void write_mmc1(Address addr, Byte value, bool consecutive);
    void commit_mmc1(Address addr, Byte value);
    [[nodiscard]] std::size_t vrc2_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t mmc3_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t mmc1_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t vrc2_chr_offset(Address addr) const;
    [[nodiscard]] std::size_t mmc3_chr_offset(Address addr) const;
    [[nodiscard]] std::size_t mmc1_chr_offset(Address addr) const;

    const Byte submapper_;
    const MirrorMode initial_mirror_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte supervisor_ = 1U;
    Vrc2State vrc2_{};
    Mmc3State mmc3_{};
    Mmc1State mmc1_{};
};

} // namespace mapperbus::core
