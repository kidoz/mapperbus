#include "core/mappers/mapper394.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x3940U;
constexpr Byte kModeMask = 0x1FU;
constexpr Byte kOuterMask = 0xDEU;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

bool maps_jy_expansion(Address addr) {
    return addr == 0x5000U || addr == 0x5400U || addr == 0x5C00U || (addr & 0xF800U) == 0x5800U;
}

} // namespace

// Original implementation from NESdev mapper 394 oldid 22930. Only the two
// literal low ports decode because their complete address masks are unknown.
// Restoring the documented power-on latches on soft reset is a deterministic
// compatibility policy; no RAM or expansion-audio population is documented.
Mapper394::Mapper394(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper394::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 394U) {
        return std::unexpected("Mapper 394 requires mapper ID 394");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 394 requires NES 2.0 submapper 0 or 1");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxRomSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxRomSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 394 requires 32-512 KiB PRG and 8-512 KiB CHR ROM in decoded bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 394 requires ROM-only memory metadata");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 394 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper394::mapper_mode() const {
    return (state_.mode >> 3U) & 3U;
}

bool Mapper394::jy_mode() const {
    return mapper_mode() >= 2U;
}

std::size_t Mapper394::prg_outer_base() const {
    return (static_cast<std::size_t>(state_.mode & 3U) << 18U);
}

std::size_t Mapper394::chr_outer_base() const {
    const unsigned a18 = submapper_ == 0U ? state_.mode & 1U : (state_.mode >> 2U) & 1U;
    return (static_cast<std::size_t>(state_.mode & 2U) << 18U) |
           (static_cast<std::size_t>(a18) << 18U);
}

std::size_t Mapper394::mmc3_prg_offset(Address addr) const {
    const auto& mmc3 = state_.mmc3;
    const bool wide_inner = (state_.outer & 0x10U) != 0U;
    const unsigned inner_banks = wide_inner ? 32U : 16U;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (mmc3.bank_select & 0x40U) != 0U;
    const unsigned bank = slot == 1U                ? mmc3.banks[7] % inner_banks
                          : slot == 3U              ? inner_banks - 1U
                          : (slot == 0U) == swapped ? inner_banks - 2U
                                                    : mmc3.banks[6] % inner_banks;
    std::size_t offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    if (!wide_inner) {
        offset |= static_cast<std::size_t>(state_.outer & 8U) << 14U;
    }
    return prg_outer_base() | offset;
}

std::size_t Mapper394::mmc3_chr_offset(Address addr) const {
    const auto& mmc3 = state_.mmc3;
    unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mmc3.bank_select & 0x80U) != 0U ? 4U : 0U);
    const unsigned bank =
        slot < 4U ? (mmc3.banks[slot / 2U] & 0xFEU) | (slot & 1U) : mmc3.banks[slot - 2U];
    std::size_t offset = 0U;
    if ((state_.outer & 0x80U) != 0U) {
        offset = bank * kChrBankSize;
    } else {
        offset = ((bank & 0x7FU) * kChrBankSize) |
                 (static_cast<std::size_t>(state_.outer & 0x40U) << 11U);
    }
    return chr_outer_base() | offset | (addr & 0x03FFU);
}

std::size_t Mapper394::jy_prg_offset(Address addr) const {
    const auto& jy = state_.jy;
    const unsigned mode = jy.config[0] & 3U;
    const unsigned shift = mode < 2U ? 2U - mode : 0U;
    const unsigned unit = kPrgBankSize << shift;
    unsigned bank = 0U;
    unsigned offset = addr & (unit - 1U);
    if (addr < 0x8000U) {
        bank = (static_cast<unsigned>(jy.prg_banks[3]) << shift) | ((1U << shift) - 1U);
        offset = addr & 0x1FFFU;
    } else {
        const unsigned slot = (addr - 0x8000U) / unit;
        const unsigned reg = mode == 0U ? 3U : (mode == 1U ? slot * 2U + 1U : slot);
        const bool fixed = reg == 3U && (jy.config[0] & 4U) == 0U;
        bank = fixed ? 0x7FU : jy.prg_banks[reg];
    }
    if (mode == 3U) {
        unsigned reversed = 0U;
        for (unsigned bit = 0U; bit < 7U; ++bit) {
            reversed |= ((bank >> bit) & 1U) << (6U - bit);
        }
        bank = reversed;
    }
    const unsigned bank_unit = addr < 0x8000U ? kPrgBankSize : unit;
    return prg_outer_base() | ((bank * bank_unit + offset) & 0x1FFFFU);
}

std::size_t Mapper394::jy_chr_offset(Address addr) const {
    const auto& jy = state_.jy;
    const unsigned mode = (jy.config[0] >> 3U) & 3U;
    const unsigned unit = 0x2000U >> mode;
    const unsigned slot = (addr & 0x1FFFU) / unit;
    unsigned reg = slot << (3U - mode);
    if (mode == 1U && (jy.config[3] & 0x80U) != 0U) {
        reg += 2U * jy.latches[slot];
    }
    const auto offset = static_cast<std::size_t>(jy.chr_banks[reg]) * unit + (addr & (unit - 1U));
    return chr_outer_base() | (offset & 0x1FFFFU);
}

std::size_t Mapper394::jy_nametable_chr_offset(Address addr) const {
    const auto bank = state_.jy.nt_banks[(static_cast<unsigned>(addr) >> 10U) & 3U];
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    return chr_outer_base() | (offset & 0x1FFFFU);
}

Byte Mapper394::read_prg(Address addr) {
    if (addr < 0x8000U && !(jy_mode() && addr >= 0x6000U && (state_.jy.config[0] & 0x80U) != 0U)) {
        return 0U;
    }
    std::size_t offset = 0U;
    switch (mapper_mode()) {
    case 0U:
        if (addr < 0x8000U) {
            return 0U;
        }
        offset = prg_outer_base() | (static_cast<std::size_t>(state_.outer & 8U) << 14U) |
                 (static_cast<std::size_t>((state_.outer >> 1U) & 3U) << 15U) | (addr & 0x7FFFU);
        break;
    case 1U:
        if (addr < 0x8000U) {
            return 0U;
        }
        offset = mmc3_prg_offset(addr);
        break;
    default:
        offset = jy_prg_offset(addr);
        break;
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper394::write_prg(Address addr, Byte value) {
    if (mapper_mode() == 1U) {
        write_mmc3(addr, value);
    } else if (jy_mode()) {
        write_jy(addr, value);
    }
}

bool Mapper394::maps_prg(Address addr) const {
    return addr >= 0x8000U || (jy_mode() && addr >= 0x6000U && (state_.jy.config[0] & 0x80U) != 0U);
}

Byte Mapper394::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper394::read_chr(Address addr) {
    std::size_t offset = 0U;
    switch (mapper_mode()) {
    case 0U:
        offset = chr_outer_base() | (static_cast<std::size_t>(state_.outer & 0x40U) << 11U) |
                 (addr & 0x1FFFU);
        break;
    case 1U:
        offset = mmc3_chr_offset(addr);
        break;
    default:
        offset = jy_chr_offset(addr);
        break;
    }
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper394::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper394::mirror_mode() const {
    if (!jy_mode()) {
        return state_.mmc3.mirror;
    }
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[state_.jy.config[1] & 3U];
}

bool Mapper394::jy_chr_nametable(Address addr) const {
    const auto& jy = state_.jy;
    if ((jy.config[0] & 0x20U) == 0U) {
        return false;
    }
    return (jy.config[0] & 0x40U) != 0U ||
           ((jy.nt_banks[(static_cast<unsigned>(addr) >> 10U) & 3U] ^ jy.config[2]) & 0x80U) != 0U;
}

std::optional<Address> Mapper394::nametable_address(Address addr) const {
    const auto& jy = state_.jy;
    if (!jy_mode() || ((jy.config[0] & 0x20U) == 0U && (jy.config[1] & 8U) == 0U)) {
        return std::nullopt;
    }
    return static_cast<Address>(
        ((jy.nt_banks[(static_cast<unsigned>(addr) >> 10U) & 3U] & 1U) << 10U) | (addr & 0x3FFU));
}

std::optional<Byte> Mapper394::read_nametable(Address addr) const {
    if (!jy_mode() || !jy_chr_nametable(addr)) {
        return std::nullopt;
    }
    return chr_rom_[jy_nametable_chr_offset(addr) % chr_rom_.size()];
}

bool Mapper394::write_nametable(Address addr, Byte /*value*/) {
    return jy_mode() && jy_chr_nametable(addr);
}

bool Mapper394::maps_expansion(Address addr) const {
    return addr == 0x5009U || addr == 0x500BU || (jy_mode() && maps_jy_expansion(addr));
}

Byte Mapper394::expansion_read_mask(Address addr) const {
    if (addr == 0x5009U || addr == 0x500BU) {
        return 0U;
    }
    if (jy_mode() && maps_jy_expansion(addr)) {
        return (addr & 0xF800U) == 0x5800U && addr != 0x5C00U ? 0xFFU : 0xC0U;
    }
    return 0U;
}

Byte Mapper394::read_expansion(Address addr) {
    if (!jy_mode() || !maps_jy_expansion(addr)) {
        return 0U;
    }
    if (addr == 0x5000U || addr == 0x5400U || addr == 0x5C00U) {
        return static_cast<Byte>(state_.jy.jumpers << 6U);
    }
    switch (addr & 3U) {
    case 0U:
        return static_cast<Byte>(state_.jy.product);
    case 1U:
        return static_cast<Byte>(state_.jy.product >> 8U);
    case 2U:
        return state_.jy.accumulator;
    default:
        return state_.jy.test;
    }
}

void Mapper394::write_expansion(Address addr, Byte value) {
    if (addr == 0x5009U) {
        state_.mode = value & kModeMask;
        return;
    }
    if (addr == 0x500BU) {
        state_.outer = value & kOuterMask;
        return;
    }
    if (!jy_mode() || (addr & 0xF800U) != 0x5800U) {
        return;
    }
    auto& jy = state_.jy;
    switch (addr & 3U) {
    case 0U:
        jy.multiplicand = value;
        break;
    case 1U:
        jy.multiplier = value;
        jy.next_product = static_cast<std::uint16_t>(jy.multiplicand * jy.multiplier);
        jy.multiply_remaining = 8U;
        break;
    case 2U:
        jy.accumulator = static_cast<Byte>(jy.accumulator + value);
        break;
    case 3U:
        jy.accumulator = 0U;
        jy.test = value;
        break;
    default:
        break;
    }
}

Result<void> Mapper394::configure_board_jumpers(Byte value) {
    if (value > 3U) {
        return std::unexpected("Mapper 394 J.Y. Company jumpers must be 0..3");
    }
    state_.jy.jumpers = value;
    return {};
}

void Mapper394::write_mmc3(Address addr, Byte value) {
    auto& mmc3 = state_.mmc3;
    switch (addr & 0xE001U) {
    case 0x8000U:
        mmc3.bank_select = value & 0xC7U;
        break;
    case 0x8001U:
        mmc3.banks[mmc3.bank_select & 7U] = (mmc3.bank_select & 7U) >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        mmc3.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        mmc3.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        mmc3.irq_reload = value;
        break;
    case 0xC001U:
        mmc3.irq_counter = 0U;
        mmc3.irq_flags |= kIrqReload;
        break;
    case 0xE000U:
        mmc3.irq_flags &= kIrqReload;
        break;
    case 0xE001U:
        mmc3.irq_flags |= kIrqEnabled;
        break;
    default:
        break;
    }
}

void Mapper394::disable_jy_irq() {
    auto& jy = state_.jy;
    jy.irq_enabled = 0U;
    jy.irq_pending = 0U;
    jy.prescaler = 0U;
}

void Mapper394::write_jy(Address addr, Byte value) {
    auto& jy = state_.jy;
    if ((addr & 0xF000U) == 0xC000U) {
        switch (addr & 7U) {
        case 0U:
            if ((value & 1U) == 0U) {
                disable_jy_irq();
            } else {
                jy.irq_enabled = 1U;
            }
            break;
        case 1U:
            jy.irq_mode = value;
            break;
        case 2U:
            disable_jy_irq();
            break;
        case 3U:
            jy.irq_enabled = 1U;
            break;
        case 4U:
            jy.prescaler = value ^ jy.irq_xor;
            break;
        case 5U:
            jy.counter = value ^ jy.irq_xor;
            break;
        case 6U:
            jy.irq_xor = value;
            break;
        case 7U:
            jy.irq_config = value;
            break;
        default:
            break;
        }
        return;
    }
    if ((addr & 0x800U) != 0U) {
        return;
    }
    const unsigned reg = addr & 7U;
    switch (addr & 0xF000U) {
    case 0x8000U:
        jy.prg_banks[reg & 3U] = value & 0x7FU;
        break;
    case 0x9000U:
        jy.chr_banks[reg] = (jy.chr_banks[reg] & 0xFF00U) | value;
        break;
    case 0xA000U:
        jy.chr_banks[reg] = static_cast<std::uint16_t>((jy.chr_banks[reg] & 0xFFU) |
                                                       (static_cast<unsigned>(value) << 8U));
        break;
    case 0xB000U: {
        auto& bank = jy.nt_banks[reg & 3U];
        bank =
            reg < 4U
                ? static_cast<std::uint16_t>((bank & 0xFF00U) | value)
                : static_cast<std::uint16_t>((bank & 0xFFU) | (static_cast<unsigned>(value) << 8U));
        break;
    }
    case 0xD000U:
        jy.config[reg & 3U] = value;
        break;
    default:
        break;
    }
}

void Mapper394::power_on() {
    reset();
}

void Mapper394::reset() {
    const auto jumpers = state_.jy.jumpers;
    state_ = State{};
    state_.mmc3.mirror = initial_mirror_;
    state_.jy.jumpers = jumpers;
}

bool Mapper394::irq_pending() const {
    return mapper_mode() == 1U ? (state_.mmc3.irq_flags & kIrqPending) != 0U
                               : jy_mode() && state_.jy.irq_pending != 0U;
}

void Mapper394::acknowledge_irq() {
    if (mapper_mode() == 1U) {
        state_.mmc3.irq_flags &= ~kIrqPending;
    }
}

void Mapper394::clock_irq_counter() {
    if (mapper_mode() != 1U) {
        return;
    }
    auto& mmc3 = state_.mmc3;
    if (mmc3.irq_counter == 0U || (mmc3.irq_flags & kIrqReload) != 0U) {
        mmc3.irq_counter = mmc3.irq_reload;
        mmc3.irq_flags &= ~kIrqReload;
    } else {
        --mmc3.irq_counter;
    }
    if (mmc3.irq_counter == 0U && (mmc3.irq_flags & kIrqEnabled) != 0U) {
        mmc3.irq_flags |= kIrqPending;
    }
}

void Mapper394::clock_jy_irq(Byte source) {
    auto& jy = state_.jy;
    const unsigned direction = jy.irq_mode >> 6U;
    if (jy.irq_enabled == 0U || (jy.irq_mode & 3U) != source ||
        (direction != 1U && direction != 2U)) {
        return;
    }
    const Byte mask = (jy.irq_mode & 4U) != 0U ? 7U : 0xFFU;
    jy.prescaler = static_cast<Byte>(jy.prescaler + (direction == 1U ? 1U : 255U));
    if ((jy.prescaler & mask) != (direction == 1U ? 0U : mask)) {
        return;
    }
    jy.counter = static_cast<Byte>(jy.counter + (direction == 1U ? 1U : 255U));
    if (jy.counter == (direction == 1U ? 0U : 0xFFU)) {
        jy.irq_pending = 1U;
    }
}

void Mapper394::clock_cpu() {
    if (!jy_mode()) {
        return;
    }
    auto& jy = state_.jy;
    if (jy.multiply_remaining != 0U) {
        --jy.multiply_remaining;
        if (jy.multiply_remaining == 0U) {
            jy.product = jy.next_product;
        }
    }
    clock_jy_irq(0U);
}

void Mapper394::observe_cpu_write(Address /*addr*/, Byte /*data*/) {
    if (jy_mode()) {
        clock_jy_irq(3U);
    }
}

bool Mapper394::observes_ppu_a12() const {
    return jy_mode();
}

void Mapper394::clock_jy_a12(bool high) {
    if (high && state_.jy.a12 == 0U) {
        clock_jy_irq(1U);
    }
    state_.jy.a12 = high ? 1U : 0U;
}

void Mapper394::observe_ppu_a12(bool high) {
    if (jy_mode()) {
        clock_jy_a12(high);
    }
}

bool Mapper394::observes_ppu_reads() const {
    return jy_mode();
}

void Mapper394::observe_ppu_read(Address addr) {
    if (!jy_mode()) {
        return;
    }
    clock_jy_a12((addr & 0x1000U) != 0U);
    clock_jy_irq(2U);
    auto& jy = state_.jy;
    if ((jy.config[3] & 0x80U) != 0U && addr < 0x2000U) {
        if ((addr & 0xFF8U) == 0xFD8U) {
            jy.latches[addr >> 12U] = 0U;
        } else if ((addr & 0xFF8U) == 0xFE8U) {
            jy.latches[addr >> 12U] = 1U;
        }
    }
}

void Mapper394::save_state(StateWriter& writer) const {
    writer.write(static_cast<std::uint16_t>(kStateTag | submapper_));
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(state_.mode);
    writer.write(state_.outer);

    const auto& mmc3 = state_.mmc3;
    writer.write(mmc3.bank_select);
    writer.write_array(mmc3.banks);
    writer.write(static_cast<Byte>(mmc3.mirror));
    writer.write(mmc3.irq_counter);
    writer.write(mmc3.irq_reload);
    writer.write(mmc3.irq_flags);
    writer.write(mmc3.ram_flags);

    const auto& jy = state_.jy;
    writer.write_array(jy.prg_banks);
    writer.write_array(jy.chr_banks);
    writer.write_array(jy.nt_banks);
    writer.write_array(jy.config);
    writer.write_array(jy.latches);
    writer.write(jy.jumpers);
    writer.write(jy.multiplicand);
    writer.write(jy.multiplier);
    writer.write(jy.product);
    writer.write(jy.next_product);
    writer.write(jy.multiply_remaining);
    writer.write(jy.accumulator);
    writer.write(jy.test);
    writer.write(jy.irq_mode);
    writer.write(jy.irq_config);
    writer.write(jy.prescaler);
    writer.write(jy.counter);
    writer.write(jy.irq_xor);
    writer.write(jy.irq_enabled);
    writer.write(jy.irq_pending);
    writer.write(jy.a12);
}

void Mapper394::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    State next;
    reader.read(next.mode);
    reader.read(next.outer);
    reader.read(next.mmc3.bank_select);
    reader.read_array(next.mmc3.banks);
    const auto mirror = reader.read<Byte>();
    next.mmc3.mirror = static_cast<MirrorMode>(mirror);
    reader.read(next.mmc3.irq_counter);
    reader.read(next.mmc3.irq_reload);
    reader.read(next.mmc3.irq_flags);
    reader.read(next.mmc3.ram_flags);
    reader.read_array(next.jy.prg_banks);
    reader.read_array(next.jy.chr_banks);
    reader.read_array(next.jy.nt_banks);
    reader.read_array(next.jy.config);
    reader.read_array(next.jy.latches);
    reader.read(next.jy.jumpers);
    reader.read(next.jy.multiplicand);
    reader.read(next.jy.multiplier);
    reader.read(next.jy.product);
    reader.read(next.jy.next_product);
    reader.read(next.jy.multiply_remaining);
    reader.read(next.jy.accumulator);
    reader.read(next.jy.test);
    reader.read(next.jy.irq_mode);
    reader.read(next.jy.irq_config);
    reader.read(next.jy.prescaler);
    reader.read(next.jy.counter);
    reader.read(next.jy.irq_xor);
    reader.read(next.jy.irq_enabled);
    reader.read(next.jy.irq_pending);
    reader.read(next.jy.a12);

    const auto& mmc3 = next.mmc3;
    const auto& jy = next.jy;
    const bool valid_mmc3 =
        (mmc3.bank_select & 0x38U) == 0U && mmc3.banks[6] <= 0x3FU && mmc3.banks[7] <= 0x3FU &&
        mirror <= 1U && mmc3.irq_flags <= 7U &&
        ((mmc3.irq_flags & kIrqPending) == 0U || (mmc3.irq_flags & kIrqEnabled) != 0U) &&
        (mmc3.ram_flags & 0x3FU) == 0U;
    const bool valid_jy =
        std::ranges::all_of(jy.prg_banks, [](Byte bank) { return bank <= 0x7FU; }) &&
        std::ranges::all_of(jy.latches, [](Byte latch) { return latch <= 1U; }) &&
        jy.jumpers <= 3U && jy.multiply_remaining <= 8U && jy.irq_enabled <= 1U &&
        jy.irq_pending <= 1U && jy.a12 <= 1U;
    if (!reader.ok() || tag != (kStateTag | submapper_) || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || initial_mirror != static_cast<Byte>(initial_mirror_) ||
        (next.mode & ~kModeMask) != 0U || (next.outer & ~kOuterMask) != 0U || !valid_mmc3 ||
        !valid_jy) {
        reader.fail();
        return;
    }
    state_ = next;
}

} // namespace mapperbus::core
