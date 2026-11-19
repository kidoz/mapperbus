#include "core/mappers/mapper342.hpp"

#include <algorithm>
#include <bit>
#include <tuple>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x4000UZ;
constexpr std::size_t kMaxPrgSize = 0x8000000UZ;
constexpr std::size_t kMinChrRamSize = 0x2000UZ;
constexpr std::size_t kMaxChrRamSize = 0x80000UZ;
constexpr std::size_t kWramSize = 0x8000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::uint16_t kStateTag = 0xC047U;

// An inner mapper drives only selected pins of each CPLD register.
template <class T> void field(T& reg, unsigned mask, unsigned bits) {
    reg = static_cast<T>((reg & ~mask) | (bits & mask));
}

// Serialize individual latch widths in the same order for save and restore,
// independently of compiler padding in RuntimeState.
auto state_latches(auto& s) {
    return std::tie(s.jy_xor,
                    s.rom_at_6000,
                    s.low_prg,
                    s.h3001_counter,
                    s.h3001_latch,
                    s.fme_counter,
                    s.h3001_enable,
                    s.h3001_pending,
                    s.fme_select,
                    s.fme_control,
                    s.fme_pending,
                    s.vrc_control,
                    s.vrc_latch,
                    s.vrc_counter,
                    s.vrc_pending,
                    s.vrc_phase,
                    s.vrc_divider,
                    s.cv_nt_reads,
                    s.cv_idle,
                    s.cv_line,
                    s.cv_irq_line,
                    s.cv_enable,
                    s.cv_pending,
                    s.serial,
                    s.bank_select,
                    s.chr_latches,
                    s.irq_latch,
                    s.irq_counter,
                    s.irq_enable,
                    s.irq_reload,
                    s.irq_ready,
                    s.irq_pending,
                    s.a12,
                    s.low_m2);
}

bool native_register(Address addr) {
    return (addr & 0xF000U) == 0x5000U;
}

} // namespace

// CPLD register and IRQ wiring: ClusterM/coolgirl, revision c39dba13.
Mapper342::Mapper342(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), flash_address_mask_(std::bit_ceil(prg_rom_.size()) - 1U),
      wram_(header.prg_ram_size + header.prg_nvram_size), battery_(header.has_battery),
      chr_ram_(header.chr_ram_size) {
    power_on();
}

Result<void> Mapper342::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 342U) {
        return std::unexpected("Mapper 342 requires mapper ID 342");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 342 requires NES 2.0 submapper 0");
    }
    if (prg_size % kMinPrgSize != 0U || prg_size < kMinPrgSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 342 requires 16 KiB-aligned PRG flash up to 128 MiB");
    }
    if (chr_size != 0U || !std::has_single_bit(header.chr_ram_size) ||
        header.chr_ram_size < kMinChrRamSize || header.chr_ram_size > kMaxChrRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 342 requires 8-512 KiB volatile CHR RAM and no CHR ROM/NVRAM");
    }
    if (header.has_trainer || (header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        header.has_battery != (header.prg_nvram_size == kWramSize)) {
        return std::unexpected("Mapper 342 supports only optional 32 KiB RAM or battery FRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical &&
        header.mirror_mode != MirrorMode::FourScreen) {
        return std::unexpected("Mapper 342 requires H/V or four-screen cartridge metadata");
    }
    return {};
}

unsigned Mapper342::core_code() const {
    return (state_.registers[6] & 31U) | ((state_.registers[7] & 0x40U) >> 1U);
}
void Mapper342::set_prg_mode(unsigned mode) {
    field(state_.registers[3], 0xE0U, mode << 5U);
}
void Mapper342::set_chr_mode(unsigned mode) {
    field(state_.registers[4], 0xE0U, mode << 5U);
}
void Mapper342::set_mirror(unsigned mode) {
    field(state_.registers[7], 0x18U, mode << 3U);
}

bool Mapper342::wram_mapped(Address addr) const {
    return addr >= 0x6000U && addr < 0x8000U && !wram_.empty() && !state_.rom_at_6000 &&
           (state_.registers[7] & 0x01U) != 0U;
}

std::uint32_t Mapper342::mapped_prg_offset(Address addr) const {
    const auto mode = static_cast<unsigned>((state_.registers[3] >> 5U) & 7U);
    const auto slot = static_cast<unsigned>((addr & 0x7FFFU) >> 13U);
    unsigned bank = 0U;
    switch (mode) {
    case 0U:
    case 1U:
    case 2U:
    case 3U: {
        const bool first_half = slot < 2U;
        const unsigned index = ((mode & 1U) == 0U) == first_half ? 0U : 2U;
        bank = (state_.prg_banks[index] & 0xFEU) | (slot & 1U);
        break;
    }
    case 4U:
    case 5U: {
        constexpr std::array<std::array<unsigned, 4>, 2> slots{
            {{0U, 1U, 2U, 3U}, {2U, 1U, 0U, 3U}}};
        bank = state_.prg_banks[slots[mode - 4U][slot]];
        break;
    }
    case 6U:
    case 7U: {
        const unsigned index = mode == 6U ? 1U : 0U;
        bank = (state_.prg_banks[index] & 0xFCU) | slot;
        break;
    }
    }

    if (addr < 0x8000U)
        bank = state_.low_prg;
    std::uint32_t offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    const std::uint32_t mask = static_cast<std::uint32_t>(state_.registers[2] & 0x7FU) << 14U;
    const std::uint32_t base = (static_cast<std::uint32_t>(state_.registers[0]) << 22U) |
                               (static_cast<std::uint32_t>(state_.registers[1]) << 14U);
    offset = (offset & ~mask) | base;
    return offset & flash_address_mask_;
}

unsigned Mapper342::chr_bank(unsigned slot) const {
    const auto mode = state_.registers[4] >> 5U;
    unsigned bank = 0U;
    const auto paired = [&](unsigned index) {
        return (state_.chr_banks[index] & 0x1FEU) | (slot & 1U);
    };
    switch (mode) {
    case 0U:
    case 1U: // mapper 163 is absent from the default firmware
        bank = (state_.chr_banks[0] & 0x1F8U) | slot;
        break;
    case 2U:
        if (slot < 2U) {
            bank = paired(0U);
        } else if (slot < 4U) {
            bank = paired(2U);
        } else {
            bank = state_.chr_banks[slot];
        }
        break;
    case 3U:
        if (slot < 4U) {
            bank = state_.chr_banks[slot + 4U];
        } else if (slot < 6U) {
            bank = paired(0U);
        } else {
            bank = paired(2U);
        }
        break;
    case 4U:
    case 5U: {
        const unsigned half = slot >> 2U;
        const unsigned latch = mode == 5U ? ((state_.chr_latches >> half) & 1U) : 0U;
        bank = (state_.chr_banks[half * 4U + latch] & 0x1FCU) | (slot & 3U);
        break;
    }
    case 6U:
        bank = paired(slot / 2U * 2U);
        break;
    case 7U:
        bank = state_.chr_banks[slot];
        break;
    }
    if (core_code() == 24U && (state_.registers[6] & 0x40U))
        bank >>= 1U;
    return bank;
}

std::uint32_t Mapper342::mapped_chr_offset(Address addr) const {
    std::uint32_t offset =
        chr_bank((addr >> 10U) & 7U) * kChrBankSize + (addr & (kChrBankSize - 1U));
    const std::uint32_t mask = (static_cast<std::uint32_t>(state_.registers[2] & 0x80U) << 11U) |
                               (static_cast<std::uint32_t>(state_.registers[4] & 0x1FU) << 13U);
    return (offset & ~mask) % chr_ram_.size();
}

Byte Mapper342::read_prg(Address addr) {
    if (addr < 0x8000U && (addr < 0x6000U || !state_.rom_at_6000)) {
        return wram_mapped(addr) ? wram_[(state_.registers[5] & 3U) * 0x2000U + (addr & 0x1FFFU)]
                                 : 0U;
    }
    const auto offset = mapped_prg_offset(addr);
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0xFFU;
}

void Mapper342::write_prg(Address addr, Byte value) {
    if (wram_mapped(addr)) {
        wram_[(state_.registers[5] & 3U) * 0x2000U + (addr & 0x1FFFU)] = value;
    }
    write_inner(addr, value);
}

void Mapper342::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    // The CPLD samples the first write of an RMW sequence. External SRAM still sees both.
    if (consecutive) {
        if (wram_mapped(addr))
            wram_[(state_.registers[5] & 3U) * 0x2000U + (addr & 0x1FFFU)] = value;
    } else {
        write_prg(addr, value);
    }
}

void Mapper342::write_mmc1(Address addr, Byte value) {
    auto& prg = state_.prg_banks;
    if (value & 0x80U) {
        state_.serial = 0x10U;
        set_prg_mode(0U);
        field(prg[2], 0x1FU, 0x1EU);
        return;
    }
    const bool complete = (state_.serial & 1U) != 0U;
    state_.serial = static_cast<Byte>((state_.serial >> 1U) | ((value & 1U) << 4U));
    if (!complete)
        return;
    const Byte data = state_.serial;
    state_.serial = 0x10U;
    switch ((addr >> 13U) & 3U) {
    case 0: {
        const unsigned mode = (data >> 2U) & 3U;
        set_prg_mode(mode < 2U ? 7U : mode == 2U ? 1U : 0U);
        if (mode >= 2U)
            field(prg[2], 0x1EU, mode == 2U ? 0U : 0x1EU);
        set_chr_mode(data & 16U ? 4U : 0U);
        set_mirror((data & 3U) ^ 2U);
        break;
    }
    case 1:
        field(state_.chr_banks[0], 0x7CU, data << 2U);
        field(prg[0], 0x20U, data << 1U);
        field(prg[2], 0x20U, data << 1U);
        if (state_.registers[6] & 0x20U)
            field(state_.registers[5], 3U, (data & 8U) ? 2U : 3U);
        break;
    case 2:
        field(state_.chr_banks[4], 0x7CU, data << 2U);
        break;
    case 3:
        field(prg[0], 0x1EU, data << 1U);
        field(state_.registers[7], 1U, (data & 16U) ? 0U : 1U);
        break;
    }
}

void Mapper342::write_mmc3(Address addr, Byte value) {
    const Byte flags = state_.registers[6] >> 5U;
    // The 189 profile takes the PRG bank from $4120-$7FFF.
    const bool txc = (flags & 2U) != 0U;
    const bool namco = (flags & 4U) != 0U;
    switch (addr & 0xE001U) {
    case 0x8000:
        state_.bank_select = value & 7U;
        if (!txc && !namco)
            set_prg_mode(4U | ((value >> 6U) & 1U));
        if (!namco)
            set_chr_mode(2U | (value >> 7U));
        break;
    case 0x8001: {
        constexpr std::array<unsigned, 6> chr_slots{0, 2, 4, 5, 6, 7};
        if (state_.bank_select < 6U)
            field(state_.chr_banks[chr_slots[state_.bank_select]], 0xFFU, value);
        else if (!txc)
            state_.prg_banks[state_.bank_select - 6U] = value;
        break;
    }
    case 0xA000:
        if (!namco)
            set_mirror(value & 1U);
        break;
    case 0xC000:
        state_.irq_latch = value;
        break;
    case 0xC001:
        state_.irq_reload = 1U;
        break;
    case 0xE000:
        state_.irq_enable = state_.irq_ready = state_.irq_pending = 0U;
        break;
    case 0xE001:
        if (!namco)
            state_.irq_enable = 1U;
        break;
    }
}

void Mapper342::write_taito(Address addr, Byte value) {
    const bool tc0190_irq = (state_.registers[6] & 0x20U) != 0U;
    switch (addr & 0xE003U) {
    case 0x8000:
        field(state_.prg_banks[0], 63U, value);
        if (!tc0190_irq)
            set_mirror((value >> 6U) & 1U);
        break;
    case 0x8001:
        field(state_.prg_banks[1], 63U, value);
        break;
    case 0x8002:
    case 0x8003:
        field(state_.chr_banks[(addr & 1U) * 2U], 0xFEU, value << 1U);
        break;
    case 0xA000:
    case 0xA001:
    case 0xA002:
    case 0xA003:
        field(state_.chr_banks[4U + (addr & 3U)], 0xFFU, value);
        break;
    case 0xC000:
        if (tc0190_irq)
            state_.irq_latch = static_cast<Byte>(~value);
        break;
    case 0xC001:
        if (tc0190_irq)
            state_.irq_reload = 1U;
        break;
    case 0xC002:
        if (tc0190_irq)
            state_.irq_enable = 1U;
        break;
    case 0xC003:
        if (tc0190_irq)
            state_.irq_enable = state_.irq_ready = state_.irq_pending = 0U;
        break;
    case 0xE000:
        if (tc0190_irq)
            set_mirror((value >> 6U) & 1U);
        break;
    }
}

void Mapper342::write_jy(Address addr, Byte value) {
    // Default bitstream uses the MMC3 counter for the Aladdin J.Y. board.
    switch (addr & 0xF000U) {
    case 0x8000:
        field(state_.prg_banks[addr & 3U], 63U, value);
        break;
    case 0x9000:
        field(state_.chr_banks[addr & 7U], 255U, value);
        break;
    case 0xD000:
        if ((addr & 3U) == 1U)
            set_mirror(value & 3U);
        break;
    case 0xC000:
        switch (addr & 7U) {
        case 0:
            state_.irq_enable = value & 1U;
            break;
        case 2:
            state_.irq_enable = 0;
            break;
        case 3:
            state_.irq_enable = 1;
            break;
        case 5:
            state_.irq_latch = value ^ state_.jy_xor;
            state_.irq_reload = 1;
            break;
        case 6:
            state_.jy_xor = value;
            break;
        }
        if (!state_.irq_enable)
            state_.irq_pending = state_.irq_ready = 0;
        break;
    }
}

void Mapper342::write_h3001(Address addr, Byte value) {
    switch (addr & 0xF007U) {
    case 0x8000:
        field(state_.prg_banks[0], 63U, value);
        break;
    case 0xA000:
        field(state_.prg_banks[1], 63U, value);
        break;
    case 0xC000:
        field(state_.prg_banks[2], 63U, value);
        break;
    case 0x9001:
        set_mirror(value >> 7U);
        break;
    case 0x9003:
        state_.h3001_enable = value >> 7U;
        state_.h3001_pending = 0;
        break;
    case 0x9004:
        state_.h3001_counter = state_.h3001_latch;
        state_.h3001_pending = 0;
        break;
    case 0x9005:
        field(state_.h3001_latch, 0xFF00U, value << 8U);
        break;
    case 0x9006:
        field(state_.h3001_latch, 0xFFU, value);
        break;
    default:
        if ((addr & 0xF000U) == 0xB000U)
            field(state_.chr_banks[addr & 7U], 255U, value);
        break;
    }
}

void Mapper342::write_castlevania(Address addr, Byte value) {
    // This is the subset in the COOLGIRL CPLD for Castlevania III.
    switch (addr) {
    case 0x5105: {
        field(state_.registers[7], 0x20U, value == 0xFFU ? 0x20U : 0U);
        constexpr std::array<unsigned, 4> modes{2, 0, 1, 3};
        if (value != 0xFFU)
            set_mirror(modes[((value >> 3U) & 2U) | ((value >> 2U) & 1U)]);
        break;
    }
    case 0x5115:
        field(state_.prg_banks[0], 31U, value & 0xFEU);
        field(state_.prg_banks[1], 31U, value | 1U);
        break;
    case 0x5116:
    case 0x5117:
        field(state_.prg_banks[addr & 3U], 31U, value);
        break;
    case 0x5203:
        state_.cv_irq_line = value;
        state_.cv_pending = 0;
        break;
    case 0x5204:
        state_.cv_enable = value >> 7U;
        state_.cv_pending = 0;
        break;
    default:
        if ((addr & 0xFFF4U) == 0x5120U) {
            const unsigned slot = ((addr & 8U) >> 1U) | (addr & 3U);
            field(state_.chr_banks[slot], 255U, value);
        }
        break;
    }
}

void Mapper342::write_vrc(Address addr, Byte value) {
    const unsigned flags = state_.registers[6] >> 5U;
    // Each board profile joins its VRC4a/c, VRC2a, VRC4e/f or VRC4b/d address pins.
    constexpr std::array<unsigned, 4> high_mask{0x84U, 1U, 0x2AU, 5U};
    constexpr std::array<unsigned, 4> low_mask{0x42U, 2U, 0x15U, 0xAU};
    const unsigned profile = (flags & 1U) | ((flags & 4U) >> 1U);
    const unsigned port =
        ((addr & high_mask[profile]) != 0U ? 2U : 0U) | ((addr & low_mask[profile]) != 0U);
    const unsigned group = addr >> 12U;
    if (group == 8U || group == 10U) {
        field(state_.prg_banks[(group - 8U) / 2U], 31U, value);
    } else if (group == 9U) {
        if (port >= 2U)
            field(state_.registers[3], 0x20U, value << 4U);
        else if (value != 0xFFU)
            set_mirror(value & 3U);
    } else if (group <= 14U) {
        const unsigned slot = (group - 11U) * 2U + (port >> 1U);
        const unsigned shift = (port & 1U) * 4U;
        field(state_.chr_banks[slot], 15U << shift, value << shift);
    } else {
        switch (port) {
        case 0:
            field(state_.vrc_latch, 15U, value);
            break;
        case 1:
            field(state_.vrc_latch, 240U, value << 4U);
            break;
        case 2:
            state_.vrc_control = value & 7U;
            state_.vrc_pending = 0;
            if (value & 2U) {
                state_.vrc_counter = state_.vrc_latch;
                state_.vrc_phase = 0;
                state_.vrc_divider = 0;
            }
            break;
        case 3:
            state_.vrc_pending = 0;
            field(state_.vrc_control, 2U, state_.vrc_control << 1U);
            break;
        }
    }
}

void Mapper342::write_fme7(Address addr, Byte value) {
    if (addr < 0xA000U) {
        state_.fme_select = value & 15U;
    } else if (addr < 0xC000U) {
        const unsigned command = state_.fme_select;
        if (command < 8U)
            field(state_.chr_banks[command], 255U, value);
        else if (command == 8U) {
            field(state_.registers[7], 1U, value >> 7U);
            state_.rom_at_6000 = (value & 0x40U) == 0U;
            state_.low_prg = value & 63U;
        } else if (command <= 11U)
            field(state_.prg_banks[command - 9U], 63U, value);
        else if (command == 12U)
            set_mirror(value & 3U);
        else if (command == 13U) {
            state_.fme_control = value & 0x81U;
            state_.fme_pending = 0;
        } else if (command == 14U)
            field(state_.fme_counter, 255U, value);
        else
            field(state_.fme_counter, 0xFF00U, value << 8U);
    }
}

void Mapper342::write_inner(Address addr, Byte value) {
    const unsigned code = core_code();
    const unsigned flags = state_.registers[6] >> 5U;
    auto& a = state_.prg_banks[0];
    auto& chr = state_.chr_banks[0];
    if (addr < 0x8000U) {
        if (code == 15U)
            write_castlevania(addr, value);
        if (code == 12U && addr >= 0x6000U)
            field(chr, 0x18U, ((value & 1U) << 4U) | ((value & 2U) << 2U));
        if (code == 20U && (flags & 2U) && addr >= 0x4120U)
            field(a, 0x3CU, (value | (value >> 4U)) << 2U);
        return;
    }
    switch (code) {
    case 1: // UxROM/Codemasters
        if ((flags & 1U) && (addr & 0xF000U) == 0x9000U)
            set_mirror(2U | ((value >> 4U) & 1U));
        else
            field(a, 0x3EU, value << 1U);
        break;
    case 2:
        field(chr, 0xF8U, value << 3U);
        break; // CNROM
    case 3:    // Irem 78
        field(a, 0x0EU, value << 1U);
        field(chr, 0x78U, (value >> 4U) << 3U);
        set_mirror(((value >> 3U) & 1U) ^ 1U);
        break;
    case 5: // Sunsoft-2
        field(a, 0x0EU, (value >> 4U) << 1U);
        field(state_.registers[7], 2U, value << 1U);
        break;
    case 8: // AxROM/BNROM
        field(a, 0x3CU, value << 2U);
        if (!(flags & 1U))
            set_mirror(2U | ((value >> 4U) & 1U));
        break;
    case 10: // Color Dreams
        field(a, 0x0CU, value << 2U);
        field(chr, 0x78U, (value >> 4U) << 3U);
        break;
    case 11: // GxROM
        field(a, 0x0CU, (value >> 4U) << 2U);
        field(chr, 0x18U, value << 3U);
        break;
    case 13:
        write_jy(addr, value);
        break;
    case 14:
        write_h3001(addr, value);
        break;
    case 16:
        write_mmc1(addr, value);
        break;
    case 17: { // MMC2/MMC4
        const unsigned page = addr >> 12U;
        if (page == 10U) {
            if (flags & 1U)
                field(a, 0x1EU, value << 1U);
            else
                field(a, 0x0FU, value);
        } else if (page >= 11U && page <= 14U) {
            constexpr std::array<unsigned, 4> slots{0, 1, 4, 5};
            field(state_.chr_banks[slots[page - 11U]], 0x7CU, value << 2U);
        } else if (page == 15U)
            set_mirror(value & 1U);
        break;
    }
    case 18: // Bandai 70/152
        field(chr, 0x78U, value << 3U);
        field(a, 0x0EU, (value >> 4U) << 1U);
        if (flags & 1U)
            set_mirror(2U | (value >> 7U));
        else
            field(a, 0x10U, value >> 3U);
        break;
    case 20:
        write_mmc3(addr, value);
        break;
    case 22:
        write_taito(addr, value);
        break;
    case 23: // FDS conversion, default CPLD has the IRQ option disabled
        switch (addr & 0x4003U) {
        case 0:
            field(chr, 0xF8U, value << 3U);
            break;
        case 0x4000:
            field(state_.low_prg, 15U, value);
            break;
        case 0x4001:
            set_mirror((value >> 3U) & 1U);
            break;
        }
        break;
    case 24:
        write_vrc(addr, value);
        break;
    case 25:
        write_fme7(addr, value);
        break;
    case 26: // Irem G-101
        switch (addr & 0xF000U) {
        case 0x8000:
            field(a, 63U, value);
            break;
        case 0x9000:
            field(state_.registers[3], 0x20U, value << 4U);
            set_mirror(value & 1U);
            break;
        case 0xA000:
            field(state_.prg_banks[1], 63U, value);
            break;
        case 0xB000:
            field(state_.chr_banks[addr & 7U], 0xFFU, value);
            break;
        }
        break;
    }
}

void Mapper342::observe_ppu_a12(bool high) {
    if (high && !state_.a12 && state_.low_m2 == 3U) {
        if (!state_.irq_counter || state_.irq_reload)
            state_.irq_counter = state_.irq_latch;
        else
            --state_.irq_counter;
        state_.irq_reload = 0;
    }
    state_.a12 = static_cast<Byte>(high);
    if (high)
        state_.low_m2 = 0U;
}

void Mapper342::clock_cpu() {
    if (observes_ppu_a12() && !state_.a12 && state_.low_m2 < 3U)
        ++state_.low_m2;
}
void Mapper342::clock_cpu_falling_edge() {
    if (state_.h3001_enable && state_.h3001_counter && --state_.h3001_counter == 0U)
        state_.h3001_pending = 1;
    if (state_.fme_control & 0x80U) {
        if (state_.fme_counter-- == 0U && (state_.fme_control & 1U))
            state_.fme_pending = 1;
    }
    if (state_.vrc_control & 2U) {
        const unsigned period = state_.vrc_phase == 2U ? 113U : 114U;
        if (++state_.vrc_divider == period) {
            state_.vrc_divider = 0;
            state_.vrc_phase = (state_.vrc_phase + 1U) % 3U;
            if (++state_.vrc_counter == 0U) {
                state_.vrc_counter = state_.vrc_latch;
                state_.vrc_pending = 1;
            }
        }
    }
    if (core_code() == 15U && state_.cv_idle < 16U)
        ++state_.cv_idle;
    if (state_.irq_enable) {
        if (state_.irq_counter)
            state_.irq_ready = 1U;
        else if (state_.irq_ready)
            state_.irq_pending = 1U;
    }
}

void Mapper342::observe_ppu_read(Address addr) {
    if (core_code() == 15U) {
        const bool new_frame = state_.cv_idle == 16U;
        state_.cv_idle = 0;
        if (new_frame)
            state_.cv_line = 0U;
        else if ((addr & 0x3000U) != 0x2000U)
            state_.cv_nt_reads = 0;
        else if (state_.cv_nt_reads < 3U)
            ++state_.cv_nt_reads;
        else if (++state_.cv_line == static_cast<Byte>(state_.cv_irq_line + 1U) && state_.cv_enable)
            state_.cv_pending = 1;
    }
    switch (addr & 0x3FF8U) {
    case 0x0FD8:
        field(state_.chr_latches, 1U, 0U);
        break;
    case 0x0FE8:
        field(state_.chr_latches, 1U, 1U);
        break;
    case 0x1FD8:
        field(state_.chr_latches, 2U, 0U);
        break;
    case 0x1FE8:
        field(state_.chr_latches, 2U, 2U);
        break;
    }
}

std::optional<Address> Mapper342::nametable_address(Address addr) const {
    if (core_code() == 20U && (state_.registers[6] & 0x20U))
        return static_cast<Address>(((chr_bank((addr >> 10U) & 7U) & 0x80U) << 3U) |
                                    (addr & 0x3FFU));
    if (addr >= 0x3000U && (state_.registers[7] & 0x20U)) {
        const unsigned mode = (state_.registers[7] >> 3U) & 3U;
        const unsigned page = mode >= 2U ? mode & 1U : (addr >> (10U + mode)) & 1U;
        return static_cast<Address>((page << 10U) | (addr & 0x3FFU));
    }
    return std::nullopt;
}

std::optional<Byte> Mapper342::read_nametable(Address addr) const {
    if (!(state_.registers[7] & 0x20U) || addr >= 0x3000U)
        return std::nullopt;
    // Four-screen VRAM occupies the last 4 KiB of the first 256 KiB CHR bank.
    const std::size_t base = std::min(chr_ram_.size(), 0x40000UZ) - 0x1000U;
    return chr_ram_[base + (addr & 0xFFFU)];
}

bool Mapper342::write_nametable(Address addr, Byte value) {
    if (!(state_.registers[7] & 0x20U) || addr >= 0x3000U)
        return false;
    const std::size_t base = std::min(chr_ram_.size(), 0x40000UZ) - 0x1000U;
    chr_ram_[base + (addr & 0xFFFU)] = value;
    return true;
}

bool Mapper342::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && state_.rom_at_6000) || wram_mapped(addr);
}

Byte Mapper342::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper342::read_chr(Address addr) {
    return chr_ram_[mapped_chr_offset(addr)];
}

void Mapper342::write_chr(Address addr, Byte value) {
    if ((state_.registers[7] & 0x02U) == 0U) {
        return;
    }
    chr_ram_[mapped_chr_offset(addr)] = value;
}

MirrorMode Mapper342::mirror_mode() const {
    const Byte control = state_.registers[7];
    if ((control & 0x20U) != 0U) {
        return MirrorMode::FourScreen;
    }
    switch ((control >> 3U) & 3U) {
    case 0U:
        return MirrorMode::Vertical;
    case 1U:
        return MirrorMode::Horizontal;
    case 2U:
        return MirrorMode::SingleLower;
    default:
        return MirrorMode::SingleUpper;
    }
}

bool Mapper342::maps_expansion(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U;
}

Byte Mapper342::expansion_read_mask(Address addr) const {
    // Native loader probes the new-Dendy flag. MapperBus models a console with CIRAM.
    return (native_register(addr) && core_code() == 0U) || (addr == 0x5204U && core_code() == 15U)
               ? 0xFFU
               : 0U;
}

Byte Mapper342::read_expansion(Address addr) {
    if (addr == 0x5204U && core_code() == 15U) {
        const Byte status =
            static_cast<Byte>((state_.cv_pending << 7U) | (state_.cv_idle < 16U ? 0x40U : 0U));
        state_.cv_pending = 0;
        return status;
    }
    return 0U;
}

void Mapper342::write_expansion(Address addr, Byte value) {
    // Both inner and native decoders see the old mapper selection on this strobe.
    write_inner(addr, value);
    if (!native_register(addr) || (state_.registers[7] & 0x80U) != 0U)
        return;
    const unsigned index = addr & 7U;
    if (index == 3U) {
        field(state_.chr_banks[0], 0xF8U, value << 3U);
    } else if (index == 5U) {
        field(state_.chr_banks[0], 0x100U, value << 1U);
        field(state_.prg_banks[0], 0x3EU, value >> 1U);
    } else if (index == 7U) {
        if (core_code() == 17U)
            state_.prg_banks[1] = 0xFDU;
        if (core_code() == 23U)
            state_.rom_at_6000 = 1;
        if (core_code() == 14U)
            state_.prg_banks[1] = 1;
    }
    state_.registers[index] = value;
}

void Mapper342::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0U);
    if (!battery_)
        std::ranges::fill(wram_, 0U);
}

void Mapper342::reset() {
    state_ = RuntimeState{};
    state_.registers[2] = 0x78U; // menu reset vector is in the first 128 KiB
    state_.prg_banks = {0U, 1U, 0xFEU, 0xFFU};
    state_.chr_banks = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
}

std::span<const Byte> Mapper342::battery_ram() const {
    return battery_ ? std::span<const Byte>(wram_) : std::span<const Byte>{};
}

void Mapper342::set_battery_ram(std::span<const Byte> data) {
    if (battery_ && data.size() == wram_.size()) {
        std::ranges::copy(data, wram_.begin());
    }
}

void Mapper342::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(wram_.size()));
    writer.write(static_cast<std::uint32_t>(chr_ram_.size()));
    writer.write_array(state_.registers);
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write_bytes(wram_);
    writer.write_bytes(chr_ram_);
    writer.write(static_cast<Byte>(battery_));
    std::apply([&](const auto&... pins) { (writer.write(pins), ...); }, state_latches(state_));
}

void Mapper342::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto wram_size = reader.read<std::uint32_t>();
    const auto chr_ram_size = reader.read<std::uint32_t>();
    RuntimeState next;
    reader.read_array(next.registers);
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    std::vector<Byte> wram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(wram, kWramSize);
    reader.read_bytes(chr_ram, kMaxChrRamSize);

    if (!reader.ok() || prg_size != prg_rom_.size() || wram_size != wram_.size() ||
        chr_ram_size != chr_ram_.size() || wram.size() != wram_.size() ||
        chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }
    if (tag == 0x3420U) {
        // Earlier component-only snapshots stored the CHR mode in D2-D0
        // and the mirror/four-screen bits one position lower than the CPLD.
        const Byte chr = next.registers[4];
        next.registers[4] = static_cast<Byte>(((chr & 7U) << 5U) | (chr & 31U));
        const Byte mirror = next.registers[7];
        next.registers[7] = static_cast<Byte>((mirror & 0xC3U) | ((mirror & 0x1CU) << 1U));
    } else if (tag == kStateTag) {
        if (reader.read<Byte>() != static_cast<Byte>(battery_))
            reader.fail();
        std::apply([&](auto&... pins) { (reader.read(pins), ...); }, state_latches(next));
    } else {
        reader.fail();
    }
    if (!reader.ok() || next.rom_at_6000 > 1U || next.low_prg > 63U || next.h3001_enable > 1U ||
        next.h3001_pending > 1U || next.fme_select > 15U || (next.fme_control & 0x7EU) ||
        next.fme_pending > 1U || next.vrc_control > 7U || next.vrc_pending > 1U ||
        next.vrc_phase > 2U || next.vrc_divider >= 114U || next.cv_nt_reads > 3U ||
        next.cv_idle > 16U || next.cv_enable > 1U || next.cv_pending > 1U || next.serial == 0U ||
        next.serial > 31U || next.bank_select > 7U || next.chr_latches > 3U || next.low_m2 > 3U ||
        next.a12 > 1U || (next.a12 && next.low_m2) || next.irq_enable > 1U ||
        next.irq_reload > 1U || next.irq_ready > 1U || next.irq_pending > 1U ||
        (!next.irq_enable && (next.irq_ready || next.irq_pending)) ||
        std::ranges::any_of(next.chr_banks, [](auto bank) { return bank > 0x1FFU; })) {
        reader.fail();
        return;
    }
    state_ = next;
    wram_ = std::move(wram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
