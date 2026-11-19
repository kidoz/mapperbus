#include "core/mappers/mapper422.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kWramSize = 0x2000UZ;
constexpr std::size_t kMinPrgSize = 0x8000UZ;
constexpr std::size_t kMinChrSize = 0x2000UZ;
constexpr std::size_t kMaxPrgSize = 0x400000UZ;
constexpr std::size_t kMaxChrSize = 0x200000UZ;
constexpr std::size_t kMaxChrRamSize = 0x40000UZ;
constexpr std::size_t kChipSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::uint16_t kStateBase = 0x4220U;

bool valid_chr_ram_size(std::size_t size) {
    return size >= kMinChrSize && size <= kMaxChrRamSize && std::has_single_bit(size);
}

} // namespace

// Original implementation from NESdev mapper 422/534 oldid 22468. The SL0
// input is a board jumper. Reset uses the family's deterministic WRAM-enabled
// policy; only the four outer registers have documented zero reset values.
Mapper422::Mapper422(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size),
      chr_ram_(chr_rom_.empty() ? header.chr_ram_size : 0U), submapper_(header.submapper),
      initial_mirroring_(header.mirror_mode), has_battery_(header.has_battery) {
    power_on();
}

Result<void> Mapper422::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 422U) {
        return std::unexpected("Mapper 422 requires mapper ID 422");
    }
    if (!header.is_nes2 || header.submapper > 2U) {
        return std::unexpected("Mapper 422 requires NES 2.0 submapper 0, 1 or 2");
    }
    if (header.submapper == 0U) {
        if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || !std::has_single_bit(prg_size)) {
            return std::unexpected(
                "Mapper 422 submapper 0 requires power-of-two 32 KiB-4 MiB PRG ROM");
        }
    } else if (prg_size != 2U * kChipSize) {
        return std::unexpected("Mapper 422 submappers 1/2 require two packed 1 MiB PRG chips");
    }
    if (chr_size != 0U &&
        (chr_size < kMinChrSize || chr_size > kMaxChrSize || !std::has_single_bit(chr_size))) {
        return std::unexpected("Mapper 422 requires power-of-two 8 KiB-2 MiB CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 422 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 422 provides neither trainer storage nor CHR NVRAM");
    }
    if (header.prg_nvram_size != (header.has_battery ? kWramSize : 0U) ||
        (header.has_battery && header.prg_ram_size != 0U) ||
        (!header.has_battery && header.prg_ram_size != 0U && header.prg_ram_size != kWramSize)) {
        return std::unexpected("Mapper 422 supports zero or 8 KiB WRAM and battery NVRAM");
    }
    if ((chr_size == 0U && !valid_chr_ram_size(header.chr_ram_size)) ||
        (chr_size != 0U && header.chr_ram_size != 0U)) {
        return std::unexpected("Mapper 422 CHR RAM must be power-of-two 8-256 KiB");
    }
    return {};
}

bool Mapper422::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && !prg_ram_.empty());
}

Byte Mapper422::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper422::prg_bank(unsigned slot) const {
    const auto r6 = static_cast<unsigned>(registers_.banks[6U]);
    const auto r7 = static_cast<unsigned>(registers_.banks[7U]);
    switch (registers_.mode & 0x0FU) {
    case 0U:
    case 4U:
        if ((registers_.bank_mode & 0x40U) != 0U) {
            return slot == 0U ? 30U : slot == 1U ? r7 & 31U : slot == 2U ? r6 & 31U : 31U;
        }
        return slot == 0U ? r6 & 31U : slot == 1U ? r7 & 31U : slot == 2U ? 30U : 31U;
    case 1U:
    case 2U:
    case 5U:
    case 6U:
        return ((r6 >> 1U) & 15U) * 2U + (slot & 1U);
    case 3U:
    case 7U:
        return ((r6 >> 2U) & 7U) * 4U + slot;
    case 8U:
    case 12U: {
        if (slot >= 2U) {
            return slot + 28U;
        }
        const auto bank = slot == 0U ? r6 : r7;
        return ((bank & 12U) << 1U) | ((bank & 2U) != 0U ? 3U : 0U) | (bank & 1U);
    }
    case 9U:
    case 13U:
        return slot < 2U ? (r6 & 15U) * 2U + (slot & 1U) : slot + 28U;
    case 10U:
    case 14U: {
        if (slot >= 2U) {
            return slot + 28U;
        }
        const auto bank = slot == 0U ? r6 : r7;
        return ((bank & 4U) << 2U) | ((bank & 2U) << 2U) | ((bank & 1U) << 2U) |
               ((bank & 2U) << 1U) | (bank & 1U);
    }
    default:
        return (r6 & 7U) * 4U + slot;
    }
}

std::size_t Mapper422::prg_offset(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    unsigned bank = prg_bank(slot) & 31U;
    if ((registers_.outer & 0x40U) != 0U) {
        bank = (bank & ~0x10U) | ((registers_.outer & 1U) << 4U);
    }
    bank |= ((registers_.outer >> 1U) & 3U) << 5U;
    bank |= (registers_.outer & 0x10U) << 3U;
    bank |= ((~registers_.outer & 0x20U) << 3U);
    if (submapper_ == 1U) {
        bank = (bank & 0x7FU) | ((bank & 0x100U) >> 1U);
    } else if (submapper_ == 2U) {
        bank = (bank & 0x7FU) | ((registers_.misc & 4U) << 5U);
    }
    auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    if ((registers_.misc & 1U) != 0U) {
        offset = (offset & ~1UZ) | sl0_;
    }
    return offset;
}

Byte Mapper422::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_ram_.empty() ? 0U : prg_ram_[addr & (kWramSize - 1U)];
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) & (prg_rom_.size() - 1U)];
}

void Mapper422::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_.empty() && (registers_.ram_flags & 0x40U) == 0U) {
            prg_ram_[addr & (kWramSize - 1U)] = value;
        }
        if ((registers_.ram_flags & 0x80U) != 0U) {
            write_outer_register(addr & 3U, value);
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    auto effective = static_cast<Address>(addr | ((registers_.mode & 8U) != 0U ? 1U : 0U));
    if ((registers_.mode & 0x0DU) == 0x0DU) {
        effective = 0x8001U;
    }
    write_mmc3(effective, value);
}

void Mapper422::write_outer_register(unsigned index, Byte value) {
    if (index == 2U) {
        auto next = static_cast<Byte>((registers_.chr_bank & 0xFEU) | (value & 1U));
        if ((registers_.chr_bank & 0x10U) == 0U) {
            next = static_cast<Byte>((next & 0xFDU) | (value & 2U));
        }
        if ((registers_.chr_bank & 0x20U) == 0U) {
            next = static_cast<Byte>((next & 0xFBU) | (value & 4U));
        }
        if ((registers_.chr_bank & 0x40U) == 0U) {
            next = static_cast<Byte>((next & 0xF7U) | (value & 8U));
        }
        if ((registers_.chr_bank & 0x80U) == 0U) {
            next = static_cast<Byte>((next & 0x8FU) | (value & 0x70U));
        }
        registers_.chr_bank = static_cast<Byte>((next & 0x7FU) | (value & 0x80U));
        return;
    }
    if ((registers_.mode & 0x80U) != 0U) {
        return;
    }
    if (index == 0U) {
        registers_.outer = value;
    } else if (index == 1U) {
        registers_.misc = value & (submapper_ == 2U ? 7U : 3U);
    } else {
        registers_.mode = value & 0xBFU;
    }
}

void Mapper422::write_mmc3(Address addr, Byte value) {
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
        if ((registers_.misc & 2U) != 0U && (value & 2U) != 0U) {
            registers_.mirroring = static_cast<Byte>((value & 1U) != 0U ? MirrorMode::SingleUpper
                                                                        : MirrorMode::SingleLower);
        } else {
            registers_.mirroring = static_cast<Byte>((value & 1U) != 0U ? MirrorMode::Horizontal
                                                                        : MirrorMode::Vertical);
        }
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

unsigned Mapper422::mmc3_chr_bank(Address addr) const {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((registers_.bank_mode & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return (registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.banks[slot - 2U];
}

std::size_t Mapper422::chr_offset(Address addr) const {
    const auto inner = mmc3_chr_bank(addr);
    const auto low =
        (registers_.mode & 0x10U) != 0U
            ? static_cast<unsigned>((registers_.chr_bank & 15U) << 3U) | ((addr >> 10U) & 7U)
            : inner;
    unsigned bank = low & 0x7FU;
    bank |= (registers_.outer & 0x80U) != 0U ? ((registers_.outer & 0x10U) << 3U) : (inner & 0x80U);
    bank |= (registers_.outer & 0x40U) << 2U;
    bank |= (registers_.outer & 0x80U) << 2U;
    bank |= (registers_.outer & 0x08U) << 7U;
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper422::read_chr(Address addr) {
    const auto& memory = chr_rom_.empty() ? chr_ram_ : chr_rom_;
    return memory[chr_offset(addr) & (memory.size() - 1U)];
}

void Mapper422::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty()) {
        chr_ram_[chr_offset(addr) & (chr_ram_.size() - 1U)] = value;
    }
}

MirrorMode Mapper422::mirror_mode() const {
    if ((registers_.mode & 0x20U) != 0U) {
        return (registers_.banks[6U] & 0x10U) != 0U ? MirrorMode::SingleUpper
                                                    : MirrorMode::SingleLower;
    }
    return static_cast<MirrorMode>(registers_.mirroring);
}

Result<void> Mapper422::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 422 SL0 accepts values 0 and 1");
    }
    sl0_ = value;
    return {};
}

std::span<const Byte> Mapper422::battery_ram() const {
    return has_battery_ ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void Mapper422::set_battery_ram(std::span<const Byte> data) {
    if (has_battery_) {
        std::ranges::copy(data.first(std::min(data.size(), prg_ram_.size())), prg_ram_.begin());
    }
}

void Mapper422::power_on() {
    reset();
    if (!has_battery_) {
        std::ranges::fill(prg_ram_, 0U);
    }
    std::ranges::fill(chr_ram_, 0U);
}

void Mapper422::reset() {
    registers_ = Registers{};
    registers_.mirroring = static_cast<Byte>(initial_mirroring_);
}

bool Mapper422::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper422::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper422::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & 1U) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U) {
        registers_.irq_flags |= 4U;
    }
}

std::uint16_t Mapper422::state_tag() const {
    return static_cast<std::uint16_t>(kStateBase | submapper_ |
                                      (initial_mirroring_ == MirrorMode::Vertical ? 4U : 0U) |
                                      (sl0_ != 0U ? 8U : 0U) | (has_battery_ ? 0x10U : 0U));
}

void Mapper422::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
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

void Mapper422::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
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
    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && (next.misc & ~misc_mask) == 0U &&
                       (next.mode & 0x40U) == 0U && next.bank_select <= 7U &&
                       (next.bank_mode & 0x3FU) == 0U && next.banks[6U] <= 0x3FU &&
                       next.banks[7U] <= 0x3FU && valid_mirror && valid_irq &&
                       (next.ram_flags & 0x3FU) == 0U && prg_ram.size() == prg_ram_.size() &&
                       chr_ram.size() == chr_ram_.size();
    if (!valid) {
        reader.fail();
        return;
    }
    registers_ = next;
    prg_ram_.swap(prg_ram);
    chr_ram_.swap(chr_ram);
}

} // namespace mapperbus::core
