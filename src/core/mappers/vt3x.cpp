#include "core/mappers/vt3x.hpp"

#include <algorithm>
#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::array<unsigned, 8> kPrgMasks{0x3FU, 0x1FU, 0x0FU, 0x07U, 0x03U, 0x01U, 0x00U, 0xFFU};
// VT02 calls CHR mask codes 3/7 invalid; zero is this component's deterministic fallback.
constexpr std::array<unsigned, 8> kChrMasks{0xFFU, 0x7FU, 0x3FU, 0x00U, 0x1FU, 0x0FU, 0x07U, 0x00U};
constexpr std::uint16_t kStateTag = 0xD296U;
constexpr std::size_t kMaximumRomSize = 8UZ * 1024UZ * 1024UZ;

Byte unscramble_chr(Byte value) {
    Byte result = 0U;
    constexpr std::array<unsigned, 8> source{4U, 2U, 1U, 7U, 0U, 6U, 5U, 3U};
    for (unsigned destination = 0U; destination < 8U; ++destination) {
        result |= static_cast<Byte>(((value >> source[destination]) & 1U) << destination);
    }
    return result;
}

} // namespace

Vt3x::Vt3x(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_(std::move(prg)), chr_(std::move(chr)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size, 0U), nvram_size_(header.prg_nvram_size),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Vt3x::validate_geometry(const INesHeader& header, std::size_t prg, std::size_t chr) {
    if (!header.is_nes2 || header.mapper_number != 296U || header.submapper != 0U) {
        return std::unexpected("VT3x component requires NES 2.0 mapper 296 submapper 0");
    }
    if (!std::has_single_bit(prg) || prg < 32768U || prg > kMaximumRomSize ||
        (chr != 0U && (!std::has_single_bit(chr) || chr < 8192U || chr > kMaximumRomSize))) {
        return std::unexpected("Unsupported VT3x ROM geometry");
    }
    const auto cpu_ram = header.prg_ram_size + header.prg_nvram_size;
    if ((cpu_ram != 0U && cpu_ram != 8192U) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size == 8192U)) {
        return std::unexpected("VT3x supports no CPU RAM or one optional 8 KiB RAM/NVRAM chip");
    }
    if ((header.chr_ram_size != 0U && header.chr_ram_size != 8192U) ||
        header.chr_nvram_size != 0U) {
        return std::unexpected("VT3x supports only optional 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("VT3x component requires horizontal or vertical startup mirroring");
    }
    return {};
}

bool Vt3x::maps_prg(Address addr) const {
    return addr >= 0x8000U || (!prg_ram_.empty() && addr >= 0x6000U && addr < 0x8000U);
}

Byte Vt3x::read_prg(Address addr) {
    if (!prg_ram_.empty() && addr >= 0x6000U && addr < 0x8000U) {
        return prg_ram_[addr - 0x6000U];
    }
    return addr >= 0x8000U ? prg_[prg_address(addr)] : 0U;
}

void Vt3x::write_prg(Address addr, Byte value) {
    if (!prg_ram_.empty() && addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }
    switch (registers_.configuration & 3U) {
    case 0U:
        write_mmc3(addr, value);
        break;
    case 1U:
        write_mmc1(addr, value);
        break;
    case 2U:
        registers_.unrom_bank = value;
        break;
    case 3U:
        registers_.cnrom_bank = value;
        break;
    }
}

void Vt3x::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (consecutive && addr >= 0x8000U && (registers_.configuration & 3U) == 1U &&
        (value & 0x80U) == 0U) {
        return;
    }
    write_prg(addr, value);
}

unsigned Vt3x::prg_inner_bank(unsigned slot) const {
    switch (registers_.configuration & 3U) {
    case 1U: {
        const unsigned bank = registers_.mmc1_prg & 0x0FU;
        switch ((registers_.mmc1_control >> 2U) & 3U) {
        case 0U:
        case 1U:
            return (bank & 0x0EU) * 2U + slot;
        case 2U:
            return slot < 2U ? slot : bank * 2U + slot - 2U;
        default:
            return slot < 2U ? bank * 2U + slot : 0xFEU + slot - 2U;
        }
    }
    case 2U:
        return slot < 2U ? registers_.unrom_bank * 2U + slot : 0xFEU + slot - 2U;
    case 3U:
        return slot;
    default:
        break;
    }
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    if (slot < 2U) {
        return registers_.prg[slot];
    }
    if (slot == 2U && (registers_.prg_mode & 0x40U) != 0U) {
        return registers_.prg[2];
    }
    return slot == 2U ? 0xFEU : 0xFFU;
}

std::size_t Vt3x::prg_address(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const unsigned mask = kPrgMasks[registers_.prg_mode & 7U];
    const unsigned inner = prg_inner_bank(slot);
    unsigned bank =
        (inner & mask) | (registers_.prg_middle & ~mask & 0xFFU) | ((registers_.outer >> 4U) << 8U);
    if ((registers_.outer_extension & 0x01U) != 0U) {
        bank |= 1U << 12U;
    }
    if ((registers_.outer_extension & 0x04U) != 0U) {
        bank |= 1U << 13U;
    }
    if ((registers_.common_extension & 1U) != 0U) {
        bank |= 1U << 14U;
    }
    return (static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU)) & (prg_.size() - 1U);
}

Byte Vt3x::read_chr(Address addr) {
    if ((registers_.configuration & 4U) != 0U) {
        return chr_ram_[addr & 0x1FFFU];
    }
    const auto value = chr_.empty() ? prg_[chr_address(addr)] : chr_[chr_address(addr)];
    return registers_.chr_encrypted != 0U ? unscramble_chr(value) : value;
}

void Vt3x::write_chr(Address addr, Byte value) {
    if ((registers_.configuration & 4U) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

unsigned Vt3x::chr_inner_bank(unsigned slot) const {
    if ((registers_.configuration & 3U) == 1U) {
        const bool upper = slot >= 4U;
        unsigned bank = 0U;
        if ((registers_.mmc1_control & 0x10U) != 0U) {
            bank = (upper ? registers_.mmc1_chr1 : registers_.mmc1_chr0) * 4U;
        } else {
            bank = (registers_.mmc1_chr0 & 0x1EU) * 4U + (upper ? 4U : 0U);
        }
        return bank + (slot & 3U);
    }
    if ((registers_.configuration & 3U) == 3U) {
        return registers_.cnrom_bank * 8U + slot;
    }
    if ((registers_.control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        const unsigned pair = slot >> 1U;
        return (registers_.chr[pair] & 0xFEU) | (slot & 1U);
    }
    return registers_.chr[slot - 2U];
}

std::size_t Vt3x::chr_address(Address addr) const {
    const auto slot = static_cast<unsigned>((addr & 0x1FFFU) >> 10U);
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned inner = chr_inner_bank(slot);
    unsigned bank = (inner & mask) | (registers_.chr_middle & 0xF8U & ~mask) |
                    ((registers_.chr_intermediate >> 4U) << 8U) |
                    ((registers_.outer & 0x0FU) << 11U);
    if ((registers_.outer_extension & 0x02U) != 0U) {
        bank |= 1U << 15U;
    }
    if ((registers_.outer_extension & 0x08U) != 0U) {
        bank |= 1U << 16U;
    }
    if ((registers_.common_extension & 1U) != 0U) {
        bank |= 1U << 17U;
    }
    const auto& storage = chr_.empty() ? prg_ : chr_;
    return (static_cast<std::size_t>(bank) * 1024U + (addr & 0x03FFU)) & (storage.size() - 1U);
}

MirrorMode Vt3x::mirror_mode() const {
    if ((registers_.configuration & 3U) == 1U) {
        switch (registers_.mmc1_control & 3U) {
        case 0U:
            return MirrorMode::SingleLower;
        case 1U:
            return MirrorMode::SingleUpper;
        case 2U:
            return MirrorMode::Vertical;
        default:
            return MirrorMode::Horizontal;
        }
    }
    return registers_.native_mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Vt3x::ciram_address(Address addr) const {
    if (addr < 0x2000U && (registers_.configuration & 4U) == 0U &&
        (registers_.control & 0x20U) != 0U) {
        return addr & 0x07FFU;
    }
    return std::nullopt;
}

bool Vt3x::maps_expansion(Address addr) const {
    return (addr >= 0x4100U && addr <= 0x410BU) || addr == 0x411DU || addr == 0x411EU ||
           addr == 0x412CU || addr == 0x412EU;
}

Byte Vt3x::expansion_read_mask(Address) const {
    return 0U;
}

void Vt3x::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4100U && addr <= 0x410BU) {
        write_native(addr - 0x4100U, value);
        return;
    }
    switch (addr) {
    case 0x411DU:
        registers_.configuration = value & 7U;
        break;
    case 0x411EU:
        if ((value & 0xC0U) == 0U || (value & 0xC0U) == 0xC0U) {
            registers_.chr_encrypted = (value & 0xC0U) == 0xC0U ? 1U : 0U;
        }
        break;
    case 0x412CU:
        registers_.outer_extension = value & 0x0FU;
        break;
    case 0x412EU:
        registers_.common_extension = value & 1U;
        break;
    default:
        break;
    }
}

void Vt3x::write_native(unsigned index, Byte value) {
    switch (index) {
    case 0U:
        registers_.outer = value;
        break;
    case 1U:
        registers_.irq_latch = value;
        break;
    case 2U:
        registers_.irq_counter = 0U;
        break;
    case 3U:
        registers_.irq_enabled = 0U;
        registers_.irq_pending = 0U;
        break;
    case 4U:
        registers_.irq_enabled = 1U;
        break;
    case 5U:
        registers_.control = value & 0xE7U;
        break;
    case 6U:
        registers_.native_mirror = value & 1U;
        break;
    case 7U:
    case 8U:
    case 9U:
        registers_.prg[index - 7U] = value;
        break;
    case 10U:
        registers_.prg_middle = value;
        break;
    case 11U:
        registers_.prg_mode = value & 0xCFU;
        break;
    default:
        break;
    }
}

void Vt3x::write_mmc3(Address addr, Byte value) {
    if ((registers_.prg_mode & 8U) != 0U) {
        return;
    }
    switch (addr & 0xE001U) {
    case 0x8000U:
        write_native(5U, static_cast<Byte>((value & 0xDFU) | (registers_.control & 0x20U)));
        break;
    case 0x8001U: {
        const unsigned index = registers_.control & 7U;
        if (index < 6U) {
            registers_.chr[index] = value;
        } else {
            registers_.prg[index - 6U] = value;
        }
        break;
    }
    case 0xA000U:
        write_native(6U, value);
        break;
    case 0xC000U:
        write_native(1U, value);
        break;
    case 0xC001U:
        write_native(2U, value);
        break;
    case 0xE000U:
        write_native(3U, value);
        break;
    case 0xE001U:
        write_native(4U, value);
        break;
    default:
        break;
    }
}

void Vt3x::write_mmc1(Address addr, Byte value) {
    if ((value & 0x80U) != 0U) {
        registers_.mmc1_shift = 0U;
        registers_.mmc1_writes = 0U;
        registers_.mmc1_control |= 0x0CU;
        return;
    }
    registers_.mmc1_shift |= static_cast<Byte>((value & 1U) << registers_.mmc1_writes);
    ++registers_.mmc1_writes;
    if (registers_.mmc1_writes == 5U) {
        commit_mmc1(addr, registers_.mmc1_shift);
        registers_.mmc1_shift = 0U;
        registers_.mmc1_writes = 0U;
    }
}

void Vt3x::commit_mmc1(Address addr, Byte value) {
    switch ((addr >> 13U) & 3U) {
    case 0U:
        registers_.mmc1_control = value & 0x1FU;
        break;
    case 1U:
        registers_.mmc1_chr0 = value & 0x1FU;
        break;
    case 2U:
        registers_.mmc1_chr1 = value & 0x1FU;
        break;
    default:
        registers_.mmc1_prg = value & 0x1FU;
        break;
    }
}

std::optional<Byte> Vt3x::read_ppu_register(Address addr, Byte open_bus) {
    if (addr >= 0x2010U && addr <= 0x201FU) {
        return open_bus;
    }
    return std::nullopt;
}

bool Vt3x::write_ppu_register(Address addr, Byte value) {
    if (addr < 0x2010U || addr > 0x201FU) {
        return false;
    }
    if (addr >= 0x2012U && addr <= 0x2017U) {
        registers_.chr[addr - 0x2012U] = value;
    } else if (addr == 0x2018U) {
        registers_.chr_intermediate = value & 0x70U;
    } else if (addr == 0x201AU) {
        registers_.chr_middle = value;
    }
    return true;
}

void Vt3x::reset() {
    registers_ = Registers{};
    registers_.native_mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
}

void Vt3x::power_on() {
    reset();
    chr_ram_.fill(0U);
    if (nvram_size_ == 0U) {
        std::ranges::fill(prg_ram_, 0U);
    }
}

bool Vt3x::irq_pending() const {
    return registers_.irq_pending != 0U;
}

bool Vt3x::observes_ppu_a12() const {
    return true;
}

void Vt3x::clock_irq() {
    registers_.irq_counter =
        registers_.irq_counter == 0U ? registers_.irq_latch : registers_.irq_counter - 1U;
    if (registers_.irq_counter == 0U && registers_.irq_enabled != 0U) {
        registers_.irq_pending = 1U;
    }
}

void Vt3x::clock_cpu() {
    if (registers_.a12_high == 0U && registers_.a12_low_cycles < 3U) {
        ++registers_.a12_low_cycles;
    }
}

void Vt3x::observe_ppu_a12(bool high) {
    if (high && registers_.a12_high == 0U && registers_.a12_low_cycles >= 3U &&
        (registers_.prg_mode & 0x80U) == 0U) {
        clock_irq();
    }
    registers_.a12_high = high ? 1U : 0U;
    if (high) {
        registers_.a12_low_cycles = 0U;
    }
}

void Vt3x::on_ppu_hblank() {
    if ((registers_.prg_mode & 0x80U) != 0U) {
        clock_irq();
    }
}

std::span<const Byte> Vt3x::battery_ram() const {
    return nvram_size_ == 0U ? std::span<const Byte>{} : std::span<const Byte>{prg_ram_};
}

void Vt3x::set_battery_ram(std::span<const Byte> data) {
    if (nvram_size_ != 0U && data.size() == nvram_size_) {
        std::ranges::copy(data, prg_ram_.begin());
    }
}

void Vt3x::save_state(StateWriter& writer) const {
    static_assert(sizeof(Registers) == 34U);
    writer.write(kStateTag);
    writer.write<std::uint32_t>(prg_.size());
    writer.write<std::uint32_t>(chr_.size());
    writer.write<std::uint32_t>(prg_ram_.size());
    writer.write<std::uint32_t>(nvram_size_);
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(registers_);
    writer.write_bytes(prg_ram_);
    writer.write_array(chr_ram_);
}

void Vt3x::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto nvram_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    Registers next;
    reader.read(next);
    std::vector<Byte> next_prg_ram;
    reader.read_bytes(next_prg_ram, 8192U);
    std::array<Byte, 8192> next_chr_ram{};
    reader.read_array(next_chr_ram);

    const bool valid =
        reader.ok() && tag == kStateTag && prg_size == prg_.size() && chr_size == chr_.size() &&
        ram_size == prg_ram_.size() && nvram_size == nvram_size_ &&
        initial_mirror == static_cast<Byte>(initial_mirror_) &&
        next_prg_ram.size() == prg_ram_.size() && (next.control & ~0xE7U) == 0U &&
        next.native_mirror <= 1U && (next.prg_mode & ~0xCFU) == 0U &&
        (next.chr_intermediate & ~0x70U) == 0U && next.configuration <= 7U &&
        next.outer_extension <= 0x0FU && next.common_extension <= 1U && next.chr_encrypted <= 1U &&
        next.irq_enabled <= 1U && next.irq_pending <= 1U &&
        (next.irq_pending == 0U || next.irq_enabled != 0U) && next.a12_high <= 1U &&
        next.a12_low_cycles <= 3U && (next.a12_high == 0U || next.a12_low_cycles == 0U) &&
        next.mmc1_shift <= 0x1FU && next.mmc1_writes <= 4U && next.mmc1_control <= 0x1FU &&
        next.mmc1_chr0 <= 0x1FU && next.mmc1_chr1 <= 0x1FU && next.mmc1_prg <= 0x1FU;
    if (!valid) {
        reader.fail();
        return;
    }
    registers_ = next;
    prg_ram_ = std::move(next_prg_ram);
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
