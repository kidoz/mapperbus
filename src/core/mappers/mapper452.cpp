#include "core/mappers/mapper452.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x100000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4520U;
constexpr Address kLatchMask = 0x1FE0U;
constexpr Byte kDataMask = 0x7FU;
constexpr Byte kNrom256Mode = 0x04U;
constexpr Byte kNrom128Mode = 0x02U;

} // namespace

// Original implementation from NESdev mapper 452 oldid 19632. Reset behavior is
// unspecified; zeroing both latches is a deterministic compatibility policy.
Mapper452::Mapper452(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper452::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 452U) {
        return std::unexpected("Mapper 452 requires mapper ID 452");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 452 requires NES 2.0 submapper 0");
    }
    if ((prg_size != kMinPrgSize && prg_size != kMaxPrgSize) || chr_size != 0U) {
        return std::unexpected("Mapper 452 requires exactly 1 or 2 MiB PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != kPrgRamSize || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 452 requires exactly 8 KiB volatile PRG RAM and no NVRAM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 452 requires exactly 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 452 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 452 does not provide trainer RAM");
    }
    return {};
}

bool Mapper452::maps_prg_ram(Address addr) const {
    if (addr < 0x8000U) {
        return false;
    }
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned ram_slot = (data_ >> 5U) & 3U;
    const bool nrom128 = (data_ & kNrom256Mode) == 0U && (data_ & kNrom128Mode) != 0U;
    return slot == ram_slot || (nrom128 && slot == (ram_slot ^ 2U));
}

std::size_t Mapper452::prg_offset(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned bank16 = (latch_ >> 6U) & 0x7FU;
    if ((data_ & kNrom256Mode) == 0U) {
        if ((data_ & kNrom128Mode) != 0U) {
            const unsigned bank8 = (latch_ >> 5U) & 0xFFU;
            return bank8 * kPrgBankSize + (addr & (kPrgBankSize - 1U));
        }
        const unsigned selected = slot < 2U ? bank16 : 0U;
        return selected * 2UZ * kPrgBankSize + (addr & (2UZ * kPrgBankSize - 1U));
    }

    unsigned bank8 = (bank16 << 1U) | slot;
    if (slot == 3U) {
        const bool upper_bank = (data_ & 0x08U) != 0U;
        if (upper_bank) {
            bank8 |= 0x04U;
            if ((data_ & 0x10U) != 0U) {
                bank8 |= 0x08U;
            }
        }
    }
    return bank8 * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper452::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    if (maps_prg_ram(addr)) {
        return prg_ram_[addr & (kPrgRamSize - 1U)];
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper452::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if (maps_prg_ram(addr)) {
        prg_ram_[addr & (kPrgRamSize - 1U)] = value;
    }
    if (addr < 0xE000U) {
        latch_ = addr & kLatchMask;
        data_ = value & kDataMask;
    }
}

bool Mapper452::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper452::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper452::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper452::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper452::mirror_mode() const {
    return (data_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper452::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper452::reset() {
    latch_ = 0U;
    data_ = 0U;
}

void Mapper452::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(latch_);
    writer.write(data_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper452::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Address>();
    const auto data = reader.read<Byte>();
    std::array<Byte, kPrgRamSize> prg_ram{};
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == kChrRamSize && (latch & ~kLatchMask) == 0U &&
                       (data & ~kDataMask) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    latch_ = latch;
    data_ = data;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
