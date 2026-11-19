#include "core/mappers/mapper416.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x010000UZ;
constexpr std::size_t kMaxPrgSize = 0x020000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x008000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr Byte kDataLatchMask = 0xEEU;
constexpr Byte kNormalMode = 0x40U;
constexpr Byte kHorizontalMirror = 0x08U;
constexpr std::uint16_t kIrqOverflow = 0x1000U;
constexpr std::uint16_t kStateTag = 0x4160U;

bool is_expansion_address(Address addr) {
    return addr >= 0x4020U && addr <= 0x5FFFU;
}

} // namespace

// Original implementation from NESdev mapper 416 oldid 8044. The low-register
// masks are unspecified, so the related N-32 conversion's $4120 alias decode is
// retained. Cleared registers are a deterministic reset policy; no RAM exists.
Mapper416::Mapper416(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper416::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 416U) {
        return std::unexpected("Mapper 416 requires mapper ID 416");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 416 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 416 requires 64-128 KiB PRG and 8-32 KiB CHR ROM in 8 KiB units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 416 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 416 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 416 does not provide trainer RAM");
    }
    return {};
}

bool Mapper416::smb2j_mode() const {
    return (data_latch_ & kNormalMode) == 0U;
}

unsigned Mapper416::normal_prg_bank(Address addr) const {
    unsigned bank = 0x10U | ((data_latch_ & 0x80U) >> 4U) | ((data_latch_ & 0x20U) >> 4U);
    switch ((data_latch_ >> 1U) & 3U) {
    case 0U:
        break;
    case 1U:
        bank |= (addr >> 13U) & 1U;
        break;
    default:
        bank = (bank & ~3U) | ((addr >> 13U) & 3U);
        break;
    }
    return bank;
}

unsigned Mapper416::smb2j_prg_bank(Address addr) const {
    if (addr < 0xA000U) {
        return 0U;
    }
    if (addr < 0xC000U) {
        return 1U;
    }
    if (addr >= 0xE000U) {
        return 3U;
    }
    return ((prg_bank_ & 4U) << 1U) | (prg_bank_ & 3U);
}

Byte Mapper416::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned bank = addr < 0x8000U ? 7U
                          : smb2j_mode() ? smb2j_prg_bank(addr)
                                         : normal_prg_bank(addr);
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper416::write_prg(Address addr, Byte value) {
    if ((addr & 0x8000U) != 0U) {
        data_latch_ = value & kDataLatchMask;
    }
}

bool Mapper416::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper416::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper416::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>((data_latch_ >> 1U) & 3U) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper416::write_chr(Address, Byte) {}

MirrorMode Mapper416::mirror_mode() const {
    return (data_latch_ & kHorizontalMirror) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Mapper416::maps_expansion(Address addr) const {
    return is_expansion_address(addr);
}

Byte Mapper416::expansion_read_mask(Address addr) const {
    return is_expansion_address(addr) ? 0U : 0xFFU;
}

Byte Mapper416::read_expansion(Address) {
    return 0U;
}

void Mapper416::write_expansion(Address addr, Byte value) {
    if (!smb2j_mode() || !is_expansion_address(addr)) {
        return;
    }
    switch (addr & 0x4120U) {
    case 0x4020U:
        prg_bank_ = value & 7U;
        break;
    case 0x4120U:
        if ((value & 1U) != 0U) {
            irq_enabled_ = true;
        } else {
            irq_enabled_ = false;
            irq_counter_ = 0U;
        }
        break;
    default:
        break;
    }
}

void Mapper416::reset() {
    data_latch_ = 0U;
    prg_bank_ = 0U;
    irq_counter_ = 0U;
    irq_enabled_ = false;
}

void Mapper416::clock_cpu() {
    if (smb2j_mode() && irq_enabled_ && irq_counter_ < kIrqOverflow) {
        ++irq_counter_;
    }
}

bool Mapper416::irq_pending() const {
    return smb2j_mode() && irq_enabled_ && irq_counter_ == kIrqOverflow;
}

void Mapper416::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(data_latch_);
    writer.write(prg_bank_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>(irq_enabled_ ? 1U : 0U));
}

void Mapper416::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto data_latch = reader.read<Byte>();
    const auto prg_bank = reader.read<Byte>();
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_enabled = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || ram_size != 0U || (data_latch & ~kDataLatchMask) != 0U ||
        prg_bank > 7U || irq_counter > kIrqOverflow || irq_enabled > 1U ||
        (irq_enabled == 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }

    data_latch_ = data_latch;
    prg_bank_ = prg_bank;
    irq_counter_ = irq_counter;
    irq_enabled_ = irq_enabled != 0U;
}

} // namespace mapperbus::core
