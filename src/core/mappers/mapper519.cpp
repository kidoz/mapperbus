#include "core/mappers/mapper519.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrOuterBankSize = 0x008000UZ;
constexpr std::size_t kChrInnerBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x5900U;
constexpr std::uint16_t kAddressLatchMask = 0x01FFU;
constexpr std::uint16_t kLockMask = 0x0100U;
constexpr std::uint16_t kNrom128Mask = 0x0080U;
constexpr std::uint16_t kDipModeMask = 0x0040U;
constexpr std::uint16_t kPrgBankMask = 0x003FU;
constexpr Byte kMirrorMask = 0x80U;
constexpr Byte kChrInnerMask = 0x03U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 519 oldid 8101.
// Reset is unspecified: restore bank zero, NROM-256, unlocked latches and the
// header CIRAM arrangement while retaining the physical DIP-switch setting.
Mapper519::Mapper519(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper519::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 519U) {
        return std::unexpected("Mapper 519 requires mapper ID 519");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 519 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 519 requires power-of-two 32 KiB-1 MiB PRG and 8 KiB-1 MiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 519 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 519 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 519 does not provide trainer RAM");
    }
    return {};
}

Result<void> Mapper519::configure_board_jumpers(Byte value) {
    if (value > 15U) {
        return std::unexpected("Mapper 519 DIP switch requires a value 0..15");
    }
    dip_switch_ = value;
    return {};
}

Byte Mapper519::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto read_address =
        (address_latch_ & kDipModeMask) != 0U
            ? static_cast<Address>((addr & 0xFFF0U) | static_cast<Address>(dip_switch_))
            : addr;
    const auto bank = address_latch_ & kPrgBankMask;
    const std::size_t offset =
        (address_latch_ & kNrom128Mask) != 0U
            ? static_cast<std::size_t>(bank) * kPrgBankSize + (read_address & 0x3FFFU)
            : static_cast<std::size_t>(bank & 0x3EU) * kPrgBankSize + (read_address & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper519::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((address_latch_ & kLockMask) == 0U) {
        address_latch_ = addr & kAddressLatchMask;
        data_latch_ = value;
        return;
    }
    data_latch_ = static_cast<Byte>((data_latch_ & ~kChrInnerMask) | (value & kChrInnerMask));
}

bool Mapper519::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper519::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper519::read_chr(Address addr) {
    if (addr >= kChrInnerBankSize || chr_rom_.empty()) {
        return 0U;
    }
    const auto outer = static_cast<std::size_t>((data_latch_ >> 2U) & 0x1FU);
    const auto inner = static_cast<std::size_t>(data_latch_ & kChrInnerMask);
    const auto offset = outer * kChrOuterBankSize + inner * kChrInnerBankSize + addr;
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper519::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper519::mirror_mode() const {
    return (data_latch_ & kMirrorMask) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper519::reset() {
    address_latch_ = 0U;
    data_latch_ = initial_mirror_ == MirrorMode::Horizontal ? kMirrorMask : 0U;
}

std::uint16_t Mapper519::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (static_cast<std::uint16_t>(dip_switch_) << 1U));
}

void Mapper519::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(address_latch_);
    writer.write(data_latch_);
}

void Mapper519::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<std::uint16_t>();
    const auto data_latch = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (address_latch & ~kAddressLatchMask) != 0U) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    data_latch_ = data_latch;
}

} // namespace mapperbus::core
