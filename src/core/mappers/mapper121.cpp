#include "core/mappers/mapper121.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr Byte kInactivePrgBank = 0xFFU;
constexpr std::array<Byte, 4> kProtection0 = {0x83U, 0x83U, 0x42U, 0x00U};
constexpr std::array<Byte, 4> kProtection1 = {0x00U, 0x02U, 0x02U, 0x03U};

} // namespace

Mapper121::Mapper121(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      a9713_(prg_rom_.size() == 0x80000U),
      chr_a18_from_ppu_(!a9713_ && chr_rom_.size() == 0x80000U),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper121::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 121 supports submapper 0");
    const bool a9711 = prg_size == 0x40000U &&
                       ((chr_size >= 0x2000U && chr_size <= 0x40000U && chr_size % 0x400U == 0U) ||
                        chr_size == 0x80000U);
    const bool a9713 = prg_size == 0x80000U && chr_size == 0x80000U;
    if (!a9711 && !a9713)
        return std::unexpected("Mapper 121 requires A9711 256 KiB PRG with up to 256/512 KiB CHR, "
                               "or A9713 512 KiB PRG/CHR");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 121 supported boards contain PRG/CHR ROM without RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 121 supports ordinary H/V CIRAM");
    return {};
}

void Mapper121::reset() {
    mirror_mode_ = initial_mirror_;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    protection_latch_ = 0U;
    protection_index_ = 0U;
    protected_prg_banks_.fill(kInactivePrgBank);
    protection_read_index_ = 0U;
    outer_bank_ = false;
}

Byte Mapper121::reverse_bank(Byte value) {
    return static_cast<Byte>(((value & 0x01U) << 5U) | ((value & 0x02U) << 3U) |
                             ((value & 0x04U) << 1U) | ((value & 0x08U) >> 1U) |
                             ((value & 0x10U) >> 3U) | ((value & 0x20U) >> 5U));
}

Byte Mapper121::normal_prg_bank(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / 0x2000U);
    if (slot == 1U)
        return bank_registers_[7U] & 0x3FU;
    if (slot == 3U)
        return 31U;
    const bool fixed = (slot == 0U) == prg_mode_;
    return fixed ? 30U : static_cast<Byte>(bank_registers_[6U] & 0x3FU);
}

Byte Mapper121::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / 0x2000U);
    const auto protected_bank = slot == 0U ? kInactivePrgBank : protected_prg_banks_[slot - 1U];
    const auto inner = protected_bank == kInactivePrgBank ? normal_prg_bank(addr) : protected_bank;
    const auto bank =
        static_cast<unsigned>(inner & 0x1FU) | (static_cast<unsigned>(outer_bank_) << 5U);
    const auto offset = bank * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper121::apply_protection_index(Byte index) {
    const auto bank = reverse_bank(protection_latch_);
    switch (index) {
    case 0x26U:
        protected_prg_banks_[2U] = bank;
        break;
    case 0x28U:
        protected_prg_banks_[1U] = bank;
        break;
    case 0x2AU:
        protected_prg_banks_[0U] = bank;
        break;
    case 0x2CU:
        if (bank != 0U)
            protected_prg_banks_[2U] = bank;
        break;
    case 0x2FU:
        break;
    case 0x20U:
    case 0x29U:
    case 0x2BU:
    case 0x3CU:
    case 0x3FU:
        protected_prg_banks_[2U] = bank;
        break;
    default:
        protected_prg_banks_.fill(kInactivePrgBank);
        break;
    }
}

void Mapper121::write_mmc3(Address addr, Byte value) {
    if (addr < 0xA000U) {
        if ((addr & 1U) == 0U) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if ((addr & 1U) == 0U)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (addr < 0xE000U) {
        if ((addr & 1U) == 0U)
            irq_reload_ = value;
        else {
            irq_reload_flag_ = true;
            irq_counter_ = 0U;
        }
    } else if ((addr & 1U) == 0U) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

void Mapper121::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    const auto protection_port = addr & 0xE003U;
    if (protection_port == 0x8001U) {
        protection_latch_ = value;
        if (protection_index_ == 0x26U || protection_index_ == 0x28U || protection_index_ == 0x2AU)
            apply_protection_index(protection_index_);
        write_mmc3(addr, value);
        return;
    }
    if (protection_port == 0x8003U) {
        protection_index_ = value & 0x3FU;
        apply_protection_index(protection_index_);
        write_mmc3(0x8000U, value);
        return;
    }
    write_mmc3(addr, value);
}

Byte Mapper121::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    const auto bank =
        slot < 4U ? static_cast<unsigned>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U))
                  : static_cast<unsigned>(bank_registers_[slot - 2U]);
    const auto outer = a9713_ ? static_cast<unsigned>(outer_bank_) << 8U
                       : chr_a18_from_ppu_ && (addr & 0x1000U) != 0U ? 0x100U
                                                                     : 0U;
    const auto offset = (bank | outer) * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper121::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper121::mirror_mode() const {
    return mirror_mode_;
}

bool Mapper121::irq_pending() const {
    return irq_pending_;
}

void Mapper121::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper121::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

bool Mapper121::maps_expansion(Address addr) const {
    return (addr & 0xF000U) == 0x5000U;
}

Byte Mapper121::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0U;
    const auto& values = (addr & 0x0100U) != 0U ? kProtection1 : kProtection0;
    return values[protection_read_index_];
}

void Mapper121::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    if (a9713_ && (addr & 0xF180U) == 0x5180U) {
        outer_bank_ = (value & 0x80U) != 0U;
        return;
    }
    protection_read_index_ = value & 3U;
}

uint16_t Mapper121::state_tag() const {
    return static_cast<uint16_t>(0x7900U | (a9713_ ? 1U : 0U) |
                                 (initial_mirror_ == MirrorMode::Horizontal ? 2U : 0U) |
                                 ((chr_rom_.size() / 0x2000U) << 2U));
}

void Mapper121::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write(protection_latch_);
    writer.write(protection_index_);
    writer.write_array(protected_prg_banks_);
    writer.write<Byte>(outer_bank_ ? 1U : 0U);
    writer.write(protection_read_index_);
}

void Mapper121::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank_select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto protection_latch = reader.read<Byte>();
    const auto protection_index = reader.read<Byte>();
    std::array<Byte, 3> protected_banks{};
    reader.read_array(protected_banks);
    const auto outer = reader.read<Byte>();
    const auto protection_read_index = reader.read<Byte>();

    const bool valid_protected_banks =
        std::all_of(protected_banks.begin(), protected_banks.end(), [](Byte bank) {
            return bank <= 0x3FU || bank == kInactivePrgBank;
        });
    if (!reader.ok() || tag != state_tag() || bank_select > 7U || (mode & 0x3FU) != 0U ||
        banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        (mirror != static_cast<Byte>(MirrorMode::Horizontal) &&
         mirror != static_cast<Byte>(MirrorMode::Vertical)) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || protection_index > 0x3FU ||
        !valid_protected_banks || outer > 1U || (!a9713_ && outer != 0U) ||
        protection_read_index > 3U) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    protection_latch_ = protection_latch;
    protection_index_ = protection_index;
    protected_prg_banks_ = protected_banks;
    outer_bank_ = outer != 0U;
    protection_read_index_ = protection_read_index;
}

} // namespace mapperbus::core
