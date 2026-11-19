#include "core/mappers/mapper300.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 8192U;
constexpr std::size_t kBankCount = 8U;
constexpr Byte kLatchMask = 0x1DU;
constexpr uint16_t kStateTag = 0x3000U;

} // namespace

// Original implementation from the immutable NESdev mapper 300 article, r7775.
Mapper300::Mapper300(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper300::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 300U || header.submapper != 0U) {
        return std::unexpected("Mapper 300 requires NES 2.0 mapper 300 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankSize ||
        prg_size > kBankCount * kPrgBankSize || !std::has_single_bit(chr_size) ||
        chr_size < kChrBankSize || chr_size > kBankCount * kChrBankSize) {
        return std::unexpected(
            "Mapper 300 requires power-of-two 16-128 KiB PRG and 8-64 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 300 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 300 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper300::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t bank = (address_latch_ >> 2U) & 7U;
    const std::size_t offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper300::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U) {
        address_latch_ = static_cast<Byte>(addr) & kLatchMask;
    }
}

Byte Mapper300::read_chr(Address addr) {
    const std::size_t bank = (address_latch_ >> 2U) & 7U;
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper300::write_chr(Address, Byte) {}

MirrorMode Mapper300::mirror_mode() const {
    return (address_latch_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper300::power_on() {
    reset();
}

void Mapper300::reset() {
    // Electrical reset is undocumented; bank zero and vertical CIRAM are deterministic policy.
    address_latch_ = 0U;
}

void Mapper300::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(address_latch_);
}

void Mapper300::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto address_latch = reader.read<Byte>();
    if (!reader.ok() || tag != kStateTag || (address_latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    address_latch_ = address_latch;
}

} // namespace mapperbus::core
