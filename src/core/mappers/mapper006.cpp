#include "core/mappers/mapper006.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x0600U;
constexpr std::size_t kWramSize = 32768U;
constexpr std::size_t kChrRamSize = 32768U;
} // namespace

// Original implementation from NESdev mapper 006 r18921 and its Super Magic
// Card hardware register description. Mapper 008 selects submapper 4 only.
FfeMagicCard::FfeMagicCard(const INesHeader& header, std::vector<Byte> prg_ram, std::vector<Byte>)
    : prg_ram_(std::move(prg_ram)),
      initial_latch_mode_(static_cast<Byte>((header.mapper_number == 8U ? 4U
                                             : header.is_nes2           ? header.submapper
                                                                        : 1U)
                                            << 5U)),
      initial_mirroring_(header.mirror_mode == MirrorMode::Vertical ? 0U : 0x10U) {
    power_on();
}

Result<void> FfeMagicCard::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.mapper_number != 6U && header.mapper_number != 8U)
        return std::unexpected("FFE Magic Card supports mappers 006 and 008");
    if (header.mapper_number == 8U && header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 008 is mapper 006 submapper 4 and declares submapper 0");
    if (header.mapper_number == 6U && header.is_nes2 && header.submapper > 7U)
        return std::unexpected("Mapper 006 supports submappers 0 through 7");
    if ((prg_size != 131072U && prg_size != 262144U) || chr_size != 0U)
        return std::unexpected("Mapper 006 requires 128/256 KiB PRG data and no CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Magic Card RAM is not battery-backed");
    if (header.is_nes2 && (header.prg_ram_size != kWramSize || header.chr_ram_size != kChrRamSize))
        return std::unexpected("Mapper 006 requires 32 KiB volatile PRG and CHR RAM declarations");
    if (header.has_trainer)
        return std::unexpected(
            "Mapper 006 trainer loading requires cartridge trainer-byte routing");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 006 requires H/V startup mirroring");
    return {};
}

bool FfeMagicCard::pass_through() const {
    return (config_ & 0x80U) != 0U;
}
bool FfeMagicCard::play_mode() const {
    return (config_ & 0x40U) != 0U;
}
bool FfeMagicCard::prg_protected() const {
    return (mode_1m_address_ & 2U) != 0U;
}
bool FfeMagicCard::chr_protected() const {
    return (latch_mode_ & 0xE0U) >= 0x80U;
}
bool FfeMagicCard::extended_prg_mode() const {
    return (mode_2m_address_ & 1U) == 0U;
}
bool FfeMagicCard::chr_1k_mode() const {
    return (config_ & 1U) != 0U;
}
bool FfeMagicCard::mmc4_mode() const {
    return chr_1k_mode() && (config_ & 4U) == 0U;
}

std::optional<std::size_t> FfeMagicCard::prg_offset(Address addr) const {
    if (pass_through())
        return std::nullopt;
    if (!play_mode()) {
        if (addr >= 0x6000U && addr < 0x8000U)
            return static_cast<std::size_t>(prg_4m_banks_[3U]) * 8192U + (addr & 0x1FFFU);
        if (addr >= 0x8000U && addr < 0xC000U)
            return static_cast<std::size_t>(prg_4m_banks_[(addr >> 13U) & 1U]) * 8192U +
                   (addr & 0x1FFFU);
        return std::nullopt;
    }
    if (extended_prg_mode()) {
        const auto slot = static_cast<unsigned>((addr >> 13U) & 3U);
        const bool mode_2m = (mode_2m_address_ & 2U) != 0U;
        const auto bank = mode_2m ? prg_2m_banks_[slot] & 0x3FU : prg_4m_banks_[slot] & 0x3FU;
        return static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU);
    }

    const unsigned mode = (latch_mode_ >> 5U) & 7U;
    const bool upper = addr >= 0xC000U;
    switch (mode) {
    case 0U:
        return static_cast<std::size_t>(upper ? 7U : latch_ & 7U) * 16384U + (addr & 0x3FFFU);
    case 1U:
        return static_cast<std::size_t>(upper ? 7U : (latch_ >> 2U) & 15U) * 16384U +
               (addr & 0x3FFFU);
    case 2U:
        return static_cast<std::size_t>(upper ? 15U : latch_ & 15U) * 16384U + (addr & 0x3FFFU);
    case 3U:
        return static_cast<std::size_t>(upper ? latch_ & 15U : 15U) * 16384U + (addr & 0x3FFFU);
    case 4U:
        return static_cast<std::size_t>((latch_ >> 4U) & 3U) * 32768U + (addr & 0x7FFFU);
    default:
        return 3U * 32768U + (addr & 0x7FFFU);
    }
}

Byte FfeMagicCard::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U && (play_mode() || prg_protected()) && !pass_through())
        return wram_[static_cast<std::size_t>((config_ >> 4U) & 3U) * 8192U + (addr & 0x1FFFU)];
    if (const auto offset = prg_offset(addr))
        return prg_ram_[*offset % prg_ram_.size()];
    return 0U;
}

void FfeMagicCard::write_prg(Address addr, Byte value) {
    if (pass_through())
        return;
    if (addr >= 0x8000U) {
        if (prg_protected())
            latch_ = value & 0x3FU;
        prg_2m_banks_[(addr >> 13U) & 3U] = value & 0x3FU;
    }
    if (addr >= 0x6000U && addr < 0x8000U && (play_mode() || prg_protected())) {
        wram_[static_cast<std::size_t>((config_ >> 4U) & 3U) * 8192U + (addr & 0x1FFFU)] = value;
        return;
    }
    if (!prg_protected())
        if (const auto offset = prg_offset(addr))
            prg_ram_[*offset % prg_ram_.size()] = value;
}

bool FfeMagicCard::maps_prg(Address addr) const {
    if (pass_through())
        return false;
    if (addr >= 0x6000U && addr < 0x8000U)
        return true;
    if (addr < 0x8000U)
        return false;
    return play_mode() || addr < 0xC000U;
}

std::size_t FfeMagicCard::chr_offset(Address addr) const {
    addr &= 0x1FFFU;
    if (chr_1k_mode()) {
        if (mmc4_mode()) {
            const auto half = static_cast<unsigned>(addr >> 12U);
            const auto pair = static_cast<unsigned>((addr >> 10U) & 2U);
            const auto reg = half * 4U + pair + mmc4_latches_[half];
            return static_cast<std::size_t>(chr_banks_[reg] & 0xFCU) * 1024U + (addr & 0x0FFFU);
        }
        const auto slot = static_cast<unsigned>(addr >> 10U);
        return static_cast<std::size_t>(chr_banks_[slot]) * 1024U + (addr & 0x03FFU);
    }

    unsigned bank = 0U;
    if (extended_prg_mode()) {
        bank = latch_ & 3U;
    } else {
        switch ((latch_mode_ >> 5U) & 7U) {
        case 1U:
        case 3U:
        case 4U:
        case 5U:
            bank = latch_ & 3U;
            break;
        case 6U:
            bank = latch_ & 1U;
            break;
        default:
            break;
        }
    }
    return static_cast<std::size_t>(bank) * 8192U + addr;
}

Byte FfeMagicCard::read_chr(Address addr) {
    return pass_through() ? 0U : chr_ram_[chr_offset(addr) % chr_ram_.size()];
}

void FfeMagicCard::write_chr(Address addr, Byte value) {
    if (!pass_through() && !chr_protected())
        chr_ram_[chr_offset(addr) % chr_ram_.size()] = value;
}

MirrorMode FfeMagicCard::mirror_mode() const {
    if ((mode_1m_address_ & 1U) == 0U)
        return (latch_mode_ & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return (latch_mode_ & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Byte> FfeMagicCard::read_nametable(Address addr) const {
    if (pass_through() || (config_ & 2U) != 0U)
        return std::nullopt;
    const auto slot = static_cast<unsigned>((addr >> 10U) & 3U);
    return chr_ram_[static_cast<std::size_t>(chr_banks_[8U + slot]) * 1024U + (addr & 0x03FFU)];
}

bool FfeMagicCard::write_nametable(Address addr, Byte value) {
    if (pass_through() || (config_ & 2U) != 0U)
        return false;
    if (!chr_protected()) {
        const auto slot = static_cast<unsigned>((addr >> 10U) & 3U);
        chr_ram_[static_cast<std::size_t>(chr_banks_[8U + slot]) * 1024U + (addr & 0x03FFU)] =
            value;
    }
    return true;
}

void FfeMagicCard::observe_cpu_write(Address addr, Byte value) {
    if (pass_through())
        return;
    if (addr >= 0x2000U && addr <= 0x3FFFU)
        scratch_ram_[0x800U + (addr & 7U)] = value;
    else if (addr >= 0x4000U && addr <= 0x4015U)
        scratch_ram_[addr - 0x4000U] = value;
    else if (addr >= 0x4500U && addr <= 0x451FU)
        scratch_ram_[0x500U + (addr & 0x1FU)] = value;
}

bool FfeMagicCard::observes_ppu_a12() const {
    return true;
}
void FfeMagicCard::observe_ppu_a12(bool high) {
    if (high && !ppu_a12_ && (config_ & 8U) != 0U)
        clock_irq();
    ppu_a12_ = high;
}
bool FfeMagicCard::observes_ppu_reads() const {
    return true;
}
void FfeMagicCard::observe_ppu_read(Address addr) {
    if (!mmc4_mode())
        return;
    addr &= 0x1FFFU;
    const auto half = static_cast<unsigned>(addr >> 12U);
    if ((addr & 0x0FF8U) == 0x0FD8U)
        mmc4_latches_[half] = 0U;
    else if ((addr & 0x0FF8U) == 0x0FE8U)
        mmc4_latches_[half] = 1U;
}

void FfeMagicCard::clock_irq() {
    if (!irq_enabled_)
        return;
    ++irq_counter_;
    if (irq_counter_ == 0U)
        irq_pending_ = true;
}
void FfeMagicCard::clock_cpu() {
    if (!pass_through() && (config_ & 8U) == 0U)
        clock_irq();
}
bool FfeMagicCard::irq_pending() const {
    return irq_pending_;
}
void FfeMagicCard::acknowledge_irq() {
    irq_pending_ = false;
}

bool FfeMagicCard::maps_expansion(Address addr) const {
    if (pass_through())
        return false;
    return addr == 0x4500U || addr == 0x4501U || (addr >= 0x5000U && addr <= 0x5FFFU);
}
Byte FfeMagicCard::read_expansion(Address addr) {
    if (addr >= 0x5000U && addr <= 0x5FFFU)
        return scratch_ram_[addr & 0x0FFFU];
    if (addr == 0x4500U)
        return static_cast<Byte>(0x40U | (latch_mode_ & 0xF0U) | (mode_1m_address_ & 2U) |
                                 (mode_1m_address_ & 1U));
    if (addr == 0x4501U)
        return static_cast<Byte>((latch_ << 2U) | mode_2m_address_);
    return 0U;
}

void FfeMagicCard::write_expansion(Address addr, Byte value) {
    if (pass_through())
        return;
    if (addr >= 0x5000U && addr <= 0x5FFFU) {
        scratch_ram_[addr & 0x0FFFU] = value;
    } else if (addr >= 0x42FCU && addr <= 0x42FFU) {
        latch_mode_ = value & 0xF0U;
        mode_1m_address_ = static_cast<Byte>(addr & 3U);
    } else if (addr >= 0x43FCU && addr <= 0x43FFU) {
        latch_ = static_cast<Byte>((latch_ & 0xFCU) | (value & 3U));
        mode_2m_address_ = static_cast<Byte>(addr & 3U);
    } else if (addr == 0x4500U) {
        config_ = value;
    } else if (addr == 0x4501U) {
        irq_pending_ = false;
        irq_enabled_ = false;
    } else if (addr == 0x4502U) {
        irq_pending_ = false;
        irq_counter_ = (irq_counter_ & 0xFF00U) | value;
    } else if (addr == 0x4503U) {
        irq_pending_ = false;
        irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0x00FFU) | (value << 8U));
        irq_enabled_ = true;
    } else if (addr >= 0x4504U && addr <= 0x4507U) {
        const auto slot = static_cast<unsigned>(addr - 0x4504U);
        prg_4m_banks_[slot] = value & 0x3FU;
        if (!extended_prg_mode() || (mode_2m_address_ & 2U) != 0U)
            prg_2m_banks_[slot] = value & 0x3FU;
    } else if (addr >= 0x4510U && addr <= 0x451BU) {
        chr_banks_[addr - 0x4510U] = value;
    }
}

void FfeMagicCard::initialize_registers() {
    prg_2m_banks_.fill(0U);
    prg_4m_banks_.fill(0U);
    chr_banks_.fill(0U);
    mmc4_latches_.fill(0U);
    latch_ = 0U;
    latch_mode_ = initial_latch_mode_ | initial_mirroring_;
    mode_1m_address_ = 3U;
    mode_2m_address_ = 3U;
    config_ = 0x42U;
    irq_counter_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
    ppu_a12_ = false;
    scratch_ram_[0x500U] = config_;
    scratch_ram_[0x504U] = 0U;
    scratch_ram_[0x505U] = 0U;
    scratch_ram_[0x506U] = 0U;
    scratch_ram_[0x507U] = 0U;
    std::fill_n(scratch_ram_.begin() + 0x510U, 12U, 0U);
}
void FfeMagicCard::reset() {
    initialize_registers();
}
void FfeMagicCard::power_on() {
    // The input vector is the already-loaded game image in volatile PRG DRAM.
    chr_ram_.fill(0U);
    wram_.fill(0U);
    scratch_ram_.fill(0U);
    initialize_registers();
}

void FfeMagicCard::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(initial_latch_mode_);
    writer.write(initial_mirroring_);
    writer.write(static_cast<uint32_t>(prg_ram_.size()));
    writer.write(latch_mode_);
    writer.write(latch_);
    writer.write(mode_1m_address_);
    writer.write(mode_2m_address_);
    writer.write(config_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>(irq_enabled_));
    writer.write(static_cast<Byte>(irq_pending_));
    writer.write(static_cast<Byte>(ppu_a12_));
    writer.write_array(prg_2m_banks_);
    writer.write_array(prg_4m_banks_);
    writer.write_array(chr_banks_);
    writer.write_array(mmc4_latches_);
    writer.write_bytes(prg_ram_);
    writer.write_bytes(wram_);
    writer.write_bytes(chr_ram_);
    writer.write_bytes(scratch_ram_);
}

void FfeMagicCard::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto initial_mode = reader.read<Byte>();
    const auto initial_mirror = reader.read<Byte>();
    const auto prg_size = reader.read<uint32_t>();
    const auto latch_mode = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto mode_1m = reader.read<Byte>();
    const auto mode_2m = reader.read<Byte>();
    const auto config = reader.read<Byte>();
    const auto counter = reader.read<uint16_t>();
    const auto enabled = reader.read<Byte>();
    const auto pending = reader.read<Byte>();
    const auto a12 = reader.read<Byte>();
    std::array<Byte, 4> banks_2m{}, banks_4m{};
    std::array<Byte, 12> chr_banks{};
    std::array<Byte, 2> latches{};
    reader.read_array(banks_2m);
    reader.read_array(banks_4m);
    reader.read_array(chr_banks);
    reader.read_array(latches);
    std::vector<Byte> prg, wram, chr, scratch;
    reader.read_bytes(prg, prg_ram_.size());
    reader.read_bytes(wram, wram_.size());
    reader.read_bytes(chr, chr_ram_.size());
    reader.read_bytes(scratch, scratch_ram_.size());
    const auto valid_banks = [](const auto& banks) {
        return std::ranges::all_of(banks, [](Byte bank) { return bank <= 0x3FU; });
    };
    if (!reader.ok() || tag != kStateTag || initial_mode != initial_latch_mode_ ||
        initial_mirror != initial_mirroring_ || prg_size != prg_ram_.size() ||
        (latch_mode & 0x0FU) != 0U || latch > 0x3FU || mode_1m > 3U || mode_2m > 3U ||
        enabled > 1U || pending > 1U || a12 > 1U || !valid_banks(banks_2m) ||
        !valid_banks(banks_4m) || latches[0U] > 1U || latches[1U] > 1U ||
        prg.size() != prg_ram_.size() || wram.size() != wram_.size() ||
        chr.size() != chr_ram_.size() || scratch.size() != scratch_ram_.size()) {
        reader.fail();
        return;
    }
    latch_mode_ = latch_mode;
    latch_ = latch;
    mode_1m_address_ = mode_1m;
    mode_2m_address_ = mode_2m;
    config_ = config;
    irq_counter_ = counter;
    irq_enabled_ = enabled == 1U;
    irq_pending_ = pending == 1U;
    ppu_a12_ = a12 == 1U;
    prg_2m_banks_ = banks_2m;
    prg_4m_banks_ = banks_4m;
    chr_banks_ = chr_banks;
    mmc4_latches_ = latches;
    prg_ram_ = std::move(prg);
    wram_.fill(0U);
    std::ranges::copy(wram, wram_.begin());
    chr_ram_.fill(0U);
    std::ranges::copy(chr, chr_ram_.begin());
    scratch_ram_.fill(0U);
    std::ranges::copy(scratch, scratch_ram_.begin());
}

} // namespace mapperbus::core
