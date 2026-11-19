#include "core/mappers/mapper417.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4170U;
constexpr Byte kIrqEnabled = 1U;
constexpr Byte kIrqPending = 2U;

} // namespace

// Original implementation from NESdev mapper 417 oldid 19973. Electrical reset
// is unspecified: cleared latches are a deterministic compatibility policy.
// The board owns no RAM; CIRAM belongs to the console and is mapped by bank.
Mapper417::Mapper417(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper417::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 417U) {
        return std::unexpected("Mapper 417 requires mapper ID 417");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 417 requires NES 2.0 submapper 0 or 1");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 417 requires 32 KiB-2 MiB PRG and 8-256 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 417 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 417 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 417 does not provide trainer RAM");
    }
    return {};
}

bool Mapper417::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper417::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper417::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0xE000U) {
        return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U))];
    }
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const auto offset =
        static_cast<std::size_t>(prg_banks_[slot]) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper417::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0x8070U) {
    case 0x8030U:
        irq_counter_ = 0U;
        irq_enabled_ = true;
        return;
    case 0x8040U:
        irq_enabled_ = false;
        irq_pending_ = false;
        return;
    default:
        break;
    }

    const Address decoded = addr & 0x8073U;
    if (decoded >= 0x8000U && decoded <= 0x8002U) {
        prg_banks_[decoded & 3U] = value;
    } else if (decoded >= 0x8010U && decoded <= 0x8013U) {
        const auto slot = static_cast<unsigned>(decoded & 3U);
        chr_banks_[slot] = value;
        if (submapper_ == 1U) {
            nametable_banks_[slot] = (value & 0x80U) != 0U ? 1U : 0U;
        }
    } else if (decoded >= 0x8020U && decoded <= 0x8023U) {
        chr_banks_[4U + (decoded & 3U)] = value;
    } else if (submapper_ == 0U && decoded >= 0x8050U && decoded <= 0x8053U) {
        nametable_banks_[decoded & 3U] = value & 1U;
    }
}

Byte Mapper417::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const auto offset =
        static_cast<std::size_t>(chr_banks_[slot]) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper417::write_chr(Address, Byte) {}

MirrorMode Mapper417::mirror_mode() const {
    return initial_mirror_;
}

std::optional<Address> Mapper417::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U) {
        return std::nullopt;
    }
    const auto slot = static_cast<unsigned>((addr >> 10U) & 3U);
    return static_cast<Address>((static_cast<Address>(nametable_banks_[slot]) << 10U) |
                                (addr & 0x03FFU));
}

void Mapper417::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    nametable_banks_.fill(0U);
    irq_counter_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
}

std::uint16_t Mapper417::irq_mask() const {
    return submapper_ == 0U ? 0x03FFU : 0x0FFFU;
}

void Mapper417::clock_cpu() {
    irq_counter_ = static_cast<std::uint16_t>((irq_counter_ + 1U) & irq_mask());
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

bool Mapper417::irq_pending() const {
    return irq_pending_;
}

void Mapper417::acknowledge_irq() {
    irq_pending_ = false;
}

std::uint16_t Mapper417::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_ |
                                      (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U));
}

void Mapper417::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write_array(nametable_banks_);
    writer.write(irq_counter_);
    writer.write<Byte>((irq_enabled_ ? kIrqEnabled : 0U) | (irq_pending_ ? kIrqPending : 0U));
}

void Mapper417::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    std::array<Byte, 3> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    std::array<Byte, 4> nametable_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    reader.read_array(nametable_banks);
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_flags = reader.read<Byte>();

    const bool valid_nametables =
        std::ranges::all_of(nametable_banks, [](Byte bank) { return bank <= 1U; });
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || ram_size != 0U || !valid_nametables ||
        irq_counter > irq_mask() || irq_flags > (kIrqEnabled | kIrqPending) ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    nametable_banks_ = nametable_banks;
    irq_counter_ = irq_counter;
    irq_enabled_ = (irq_flags & kIrqEnabled) != 0U;
    irq_pending_ = (irq_flags & kIrqPending) != 0U;
}

} // namespace mapperbus::core
