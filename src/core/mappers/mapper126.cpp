#include "core/mappers/mapper126.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kWramSize = 8192U;
constexpr std::size_t kChipSize = 1024U * 1024U;
constexpr uint16_t kStateBase = 0x7E00U;

bool valid_chr_ram_size(std::size_t size) {
    return size >= 8192U && size <= 262144U && std::has_single_bit(size);
}

} // namespace

Tec9719::Tec9719(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : kWramSize),
      chr_ram_(chr_rom_.empty() ? (header.is_nes2 ? header.chr_ram_size : kWramSize) : 0U),
      submapper_(header.is_nes2 ? header.submapper : 0U), initial_mirroring_(header.mirror_mode),
      has_battery_(header.has_battery) {
    registers_.mirroring = static_cast<Byte>(initial_mirroring_);
}

Result<void> Tec9719::validate_geometry(const INesHeader& header,
                                        std::size_t prg_size,
                                        std::size_t chr_size) {
    if (header.mapper_number != 126U)
        return std::unexpected("TEC9719 requires mapper 126");
    const auto submapper = header.is_nes2 ? header.submapper : 0U;
    if (submapper > 2U || (header.is_nes2 && header.submapper > 2U))
        return std::unexpected("Mapper 126 supports submappers 0 through 2");
    if (submapper == 0U) {
        if (prg_size < 32768U || prg_size > 4194304U || !std::has_single_bit(prg_size))
            return std::unexpected("Mapper 126 submapper 0 requires power-of-two 32 KiB-4 MiB PRG");
    } else if (prg_size != 2U * kChipSize) {
        return std::unexpected("Mapper 126 submappers 1/2 require two packed 1 MiB PRG chips");
    }
    if (chr_size != 0U &&
        (chr_size < 8192U || chr_size > 2097152U || !std::has_single_bit(chr_size)))
        return std::unexpected("Mapper 126 requires power-of-two 8 KiB-2 MiB CHR ROM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 126 supports H/V startup CIRAM");
    if (header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 126 does not support CHR NVRAM");
    if (header.is_nes2) {
        if (header.prg_nvram_size != (header.has_battery ? kWramSize : 0U) ||
            header.prg_ram_size > kWramSize || (header.has_battery && header.prg_ram_size != 0U) ||
            (!header.has_battery && header.prg_ram_size != 0U && header.prg_ram_size != kWramSize))
            return std::unexpected("Mapper 126 supports zero or 8 KiB WRAM and battery NVRAM");
        if ((chr_size == 0U && !valid_chr_ram_size(header.chr_ram_size)) ||
            (chr_size != 0U && header.chr_ram_size != 0U))
            return std::unexpected("Mapper 126 CHR RAM must be power-of-two 8-256 KiB");
    }
    return {};
}

bool Tec9719::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && !prg_ram_.empty());
}

Byte Tec9719::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return prg_ram_.empty() ? 0U : prg_ram_[addr & 0x1FFFU];
    if (addr < 0x8000U)
        return 0U;

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    std::size_t offset = static_cast<std::size_t>(prg_bank(slot)) * 8192U + (addr & 0x1FFFU);
    if ((registers_.outer & 0x40U) != 0U)
        offset = (offset & ~0x20000U) | (static_cast<std::size_t>(registers_.outer & 1U) << 17U);
    offset &= ~0x3C0000U;
    offset |= static_cast<std::size_t>((registers_.outer >> 1U) & 3U) << 18U;
    offset |= static_cast<std::size_t>((registers_.outer >> 4U) & 1U) << 20U;
    offset |= static_cast<std::size_t>((~registers_.outer >> 5U) & 1U) << 21U;

    if (submapper_ == 1U)
        offset = (offset & 0x0FFFFFU) | ((offset & 0x200000U) >> 1U);
    else if (submapper_ == 2U)
        offset =
            (offset & 0x0FFFFFU) | (static_cast<std::size_t>((registers_.misc >> 2U) & 1U) << 20U);
    if ((registers_.misc & 1U) != 0U)
        offset = (offset & ~1U) | sl0_;
    return prg_rom_[offset % prg_rom_.size()];
}

unsigned Tec9719::prg_bank(unsigned slot) const {
    constexpr std::array<Byte, 16> kModes{
        {0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 12U, 13U, 14U, 15U, 12U, 13U, 14U, 15U}};
    const auto r6 = static_cast<unsigned>(registers_.banks[6]);
    const auto r7 = static_cast<unsigned>(registers_.banks[7]);
    switch (kModes[registers_.mode & 15U]) {
    case 0U:
        if ((registers_.bank_mode & 0x40U) != 0U)
            return slot == 0U ? 30U : slot == 1U ? r7 & 31U : slot == 2U ? r6 & 31U : 31U;
        return slot == 0U ? r6 & 31U : slot == 1U ? r7 & 31U : slot == 2U ? 30U : 31U;
    case 1U:
    case 2U:
        return ((r6 >> 1U) & 15U) * 2U + (slot & 1U);
    case 3U:
        return ((r6 >> 2U) & 7U) * 4U + slot;
    case 12U: {
        if (slot >= 2U)
            return slot + 28U;
        const auto bank = slot == 0U ? r6 : r7;
        return ((bank & 12U) << 1U) | ((bank & 2U) != 0U ? 3U : 0U) | (bank & 1U);
    }
    case 13U:
        return slot < 2U ? ((r6 & 15U) * 2U + (slot & 1U)) : slot + 28U;
    case 14U: {
        if (slot >= 2U)
            return slot + 28U;
        const auto bank = slot == 0U ? r6 : r7;
        return ((bank & 4U) << 2U) | ((bank & 2U) << 2U) | ((bank & 2U) << 1U) | ((bank & 1U) * 3U);
    }
    default:
        return (r6 & 7U) * 4U + slot;
    }
}

void Tec9719::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_.empty() && (registers_.ram_flags & 0x40U) == 0U)
            prg_ram_[addr & 0x1FFFU] = value;
        if ((registers_.ram_flags & 0x80U) != 0U)
            write_outer_register(addr & 3U, value);
        return;
    }
    if (addr < 0x8000U)
        return;

    auto effective = static_cast<Address>(addr | ((registers_.mode & 8U) != 0U ? 1U : 0U));
    if ((registers_.mode & 0x0DU) == 0x0DU)
        effective = 0x8001U;
    write_mmc3(effective, value);
}

void Tec9719::write_outer_register(unsigned index, Byte value) {
    if (index == 2U) {
        auto next = registers_.chr_bank;
        next = static_cast<Byte>((next & 0xFEU) | (value & 1U));
        if ((registers_.chr_bank & 0x10U) == 0U)
            next = static_cast<Byte>((next & 0xFDU) | (value & 2U));
        if ((registers_.chr_bank & 0x20U) == 0U)
            next = static_cast<Byte>((next & 0xFBU) | (value & 4U));
        if ((registers_.chr_bank & 0x40U) == 0U)
            next = static_cast<Byte>((next & 0xF7U) | (value & 8U));
        if ((registers_.chr_bank & 0x80U) == 0U)
            next = static_cast<Byte>((next & 0x8FU) | (value & 0x70U));
        next = static_cast<Byte>((next & 0x7FU) | (value & 0x80U));
        registers_.chr_bank = next;
        return;
    }
    if ((registers_.mode & 0x80U) != 0U)
        return;
    if (index == 0U)
        registers_.outer = value;
    else if (index == 1U)
        registers_.misc = value & (submapper_ == 2U ? 7U : 3U);
    else
        registers_.mode = value & 0xBFU;
}

void Tec9719::write_mmc3(Address addr, Byte value) {
    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        if ((registers_.misc & 2U) != 0U && (value & 2U) != 0U)
            registers_.mirroring = static_cast<Byte>((value & 1U) != 0U ? MirrorMode::SingleUpper
                                                                        : MirrorMode::SingleLower);
        else
            registers_.mirroring = static_cast<Byte>((value & 1U) != 0U ? MirrorMode::Horizontal
                                                                        : MirrorMode::Vertical);
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= 1U;
        break;
    case 0xE000U:
        registers_.irq_flags &= 1U;
        break;
    case 0xE001U:
        registers_.irq_flags |= 2U;
        break;
    default:
        break;
    }
}

unsigned Tec9719::mmc3_chr_bank(Address addr) const {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((registers_.bank_mode & 0x80U) != 0U)
        slot ^= 4U;
    if (slot < 4U)
        return (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    return registers_.banks[slot - 2U];
}

std::size_t Tec9719::chr_offset(Address addr) const {
    const auto inner = mmc3_chr_bank(addr);
    const auto low_bank =
        (registers_.mode & 0x10U) != 0U
            ? static_cast<unsigned>((registers_.chr_bank & 15U) << 3U) | ((addr >> 10U) & 7U)
            : inner;
    std::size_t offset = static_cast<std::size_t>(low_bank & 0x7FU) * 1024U + (addr & 0x3FFU);
    if ((registers_.outer & 0x80U) != 0U)
        offset |= static_cast<std::size_t>((registers_.outer >> 3U) & 1U) << 17U;
    else
        offset |= static_cast<std::size_t>(inner & 0x80U) << 10U;
    offset |= static_cast<std::size_t>(registers_.outer & 0x80U) << 11U;
    offset |= static_cast<std::size_t>(registers_.outer & 0x40U) << 13U;
    offset |= static_cast<std::size_t>(registers_.outer & 0x08U) << 17U;
    return offset;
}

Byte Tec9719::read_chr(Address addr) {
    const auto& memory = chr_rom_.empty() ? chr_ram_ : chr_rom_;
    return memory[chr_offset(addr) % memory.size()];
}

void Tec9719::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr) % chr_ram_.size()] = value;
}

MirrorMode Tec9719::mirror_mode() const {
    if ((registers_.mode & 0x20U) != 0U)
        return (registers_.banks[6] & 0x10U) != 0U ? MirrorMode::SingleUpper
                                                   : MirrorMode::SingleLower;
    return static_cast<MirrorMode>(registers_.mirroring);
}

Result<void> Tec9719::configure_board_jumpers(Byte value) {
    if (value > 1U)
        return std::unexpected("Mapper 126 SL0 accepts values 0 and 1");
    sl0_ = value;
    return {};
}

std::span<const Byte> Tec9719::battery_ram() const {
    return has_battery_ ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void Tec9719::set_battery_ram(std::span<const Byte> data) {
    if (has_battery_)
        std::ranges::copy(data.first(std::min(data.size(), prg_ram_.size())), prg_ram_.begin());
}

void Tec9719::power_on() {
    reset();
    if (!has_battery_)
        std::ranges::fill(prg_ram_, 0U);
    std::ranges::fill(chr_ram_, 0U);
}

void Tec9719::reset() {
    registers_ = Registers{};
    registers_.mirroring = static_cast<Byte>(initial_mirroring_);
}

bool Tec9719::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Tec9719::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Tec9719::clock_irq_counter() {
    const bool forced = (registers_.irq_flags & 1U) != 0U;
    if (registers_.irq_counter == 0U || forced) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U)
        registers_.irq_flags |= 4U;
}

uint16_t Tec9719::state_tag() const {
    return static_cast<uint16_t>(kStateBase | submapper_ |
                                 (initial_mirroring_ == MirrorMode::Vertical ? 4U : 0U) |
                                 (sl0_ != 0U ? 8U : 0U) | (has_battery_ ? 0x10U : 0U));
}

void Tec9719::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    writer.write<uint32_t>(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.misc);
    writer.write(registers_.chr_bank);
    writer.write(registers_.mode);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirroring);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write_bytes(prg_ram_);
    writer.write_bytes(chr_ram_);
}

void Tec9719::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.misc);
    reader.read(next.chr_bank);
    reader.read(next.mode);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    reader.read(next.mirroring);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    std::vector<Byte> prg_ram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(prg_ram, prg_ram_.size());
    reader.read_bytes(chr_ram, chr_ram_.size());

    const auto misc_mask = submapper_ == 2U ? 7U : 3U;
    const bool valid_mirror = next.mirroring <= 4U && next.mirroring != 2U;
    const bool valid_irq =
        next.irq_flags <= 7U && ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.misc & ~misc_mask) != 0U ||
        (next.mode & 0x40U) != 0U || next.bank_select > 7U || (next.bank_mode & 0x3FU) != 0U ||
        next.banks[6] > 0x3FU || next.banks[7] > 0x3FU || !valid_mirror || !valid_irq ||
        (next.ram_flags & 0x3FU) != 0U || prg_ram.size() != prg_ram_.size() ||
        chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }
    registers_ = next;
    prg_ram_.swap(prg_ram);
    chr_ram_.swap(chr_ram);
}

} // namespace mapperbus::core
