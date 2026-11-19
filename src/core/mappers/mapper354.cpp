#include "core/mappers/mapper354.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kMaxPrgSizeSubmapper0 = 0x200000UZ;
constexpr std::size_t kMaxPrgSizeSubmapper1 = 0x400000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3540U;

} // namespace

// NESdev mapper 354 oldid 20572 defines all register and reset bits. Decode the
// full address space before wrapping fitted ROMs. Cold CHR-RAM clearing is a
// deterministic policy; soft reset retains RAM while clearing both latches.
Mapper354::Mapper354(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), submapper_(header.submapper) {
    power_on();
}

Result<void> Mapper354::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 354U) {
        return std::unexpected("Mapper 354 requires mapper ID 354");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 354 requires NES 2.0 submapper 0 or 1");
    }
    const auto max_prg_size =
        header.submapper == 0U ? kMaxPrgSizeSubmapper0 : kMaxPrgSizeSubmapper1;
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankSize || prg_size > max_prg_size) {
        return std::unexpected("Mapper 354 requires power-of-two 16 KiB-2/4 MiB PRG ROM");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 354 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 354 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 354 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper354::read_prg(Address addr) {
    const auto mode = static_cast<unsigned>(address_latch_ & 7U);
    if (addr < 0x8000U && (addr < 0x6000U || mode != 5U)) {
        return 0U;
    }

    auto bank = static_cast<std::size_t>(data_latch_ & 0x3FU);
    std::size_t inner_offset = 0U;
    if (addr < 0x8000U) {
        inner_offset =
            bank * kPrgBankSize + ((data_latch_ & 0x80U) != 0U ? 0x2000UZ : 0UZ) + (addr & 0x1FFFU);
    } else {
        switch (mode) {
        case 0U:
        case 4U:
            bank = (bank & 0x3EU) | ((addr >> 14U) & 1U);
            inner_offset = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 1U:
            if (addr >= 0xC000U) {
                bank |= 7U;
            }
            inner_offset = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 2U:
        case 6U:
            inner_offset = bank * kPrgBankSize + ((data_latch_ & 0x80U) != 0U ? 0x2000UZ : 0UZ) +
                           (addr & 0x1FFFU);
            break;
        case 3U:
        case 7U:
            inner_offset = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 5U:
            bank = (bank & 0x38U) | 0x06U | ((addr >> 14U) & 1U);
            inner_offset = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        default:
            break;
        }
    }

    const std::size_t outer_offset =
        (submapper_ == 1U && (address_latch_ & 0x1000U) != 0U ? 0x200000UZ : 0UZ) +
        ((address_latch_ & 0x0010U) != 0U ? 0x100000UZ : 0UZ);
    return prg_rom_[(outer_offset + inner_offset) % prg_rom_.size()];
}

void Mapper354::write_prg(Address addr, Byte value) {
    if (addr >= 0xF000U || (submapper_ == 1U && addr >= 0xE000U)) {
        address_latch_ = addr;
        data_latch_ = value;
    }
}

bool Mapper354::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && (address_latch_ & 7U) == 5U);
}

Byte Mapper354::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper354::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper354::write_chr(Address addr, Byte value) {
    if ((address_latch_ & 0x0008U) == 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper354::mirror_mode() const {
    return (data_latch_ & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper354::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper354::reset() {
    address_latch_ = 0U;
    data_latch_ = 0U;
}

std::uint16_t Mapper354::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_);
}

void Mapper354::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(address_latch_);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper354::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<Address>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_address = (address_latch == 0U && data_latch == 0U) ||
                               address_latch >= 0xF000U ||
                               (submapper_ == 1U && address_latch >= 0xE000U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || !valid_address) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    data_latch_ = data_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
