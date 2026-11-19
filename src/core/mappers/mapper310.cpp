#include "core/mappers/mapper310.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgUnitBytes = 16384U;
constexpr std::size_t kMaxPrgBytes = 4U * 1024U * 1024U;
constexpr std::size_t kChrBankBytes = 8192U;
constexpr std::uint16_t kStateTag = 0x3100U;

} // namespace

Mapper310::Mapper310(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Mapper310::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 310 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgUnitBytes || prg_size > kMaxPrgBytes ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 310 requires power-of-two 16 KiB-4 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 32768U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 310 requires exactly 32 KiB volatile CHR RAM and no other cartridge RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 310 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper310::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const unsigned mode = address_latch_ & 3U;
    std::size_t bank = static_cast<std::size_t>(address_latch_ >> 2U) << 6U;
    bank |= data_latch_ & 0x3FU;

    if (mode == 0U) {
        bank = (bank & ~1UZ) | ((addr >> 14U) & 1U);
    } else if (mode == 1U && addr >= 0xC000U) {
        bank = (bank & ~7UZ) | 7UZ;
    }

    const std::size_t offset = mode == 2U ? bank * kPrgUnitBytes +
                                                ((data_latch_ & 0x80U) != 0U ? kChrBankBytes : 0U) +
                                                (addr & 0x1FFFU)
                                          : bank * kPrgUnitBytes + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper310::write_prg(Address addr, Byte value) {
    if ((addr & 0xC000U) == 0x8000U) {
        data_latch_ = value;
    } else if ((addr & 0xC000U) == 0xC000U) {
        address_latch_ = addr & 0x0FU;
        chr_bank_ = value & 3U;
    }
}

Byte Mapper310::read_chr(Address addr) {
    return chr_ram_[static_cast<std::size_t>(chr_bank_) * kChrBankBytes + (addr & 0x1FFFU)];
}

void Mapper310::write_chr(Address addr, Byte value) {
    const unsigned mode = address_latch_ & 3U;
    if (mode == 1U || mode == 2U) {
        chr_ram_[static_cast<std::size_t>(chr_bank_) * kChrBankBytes + (addr & 0x1FFFU)] = value;
    }
}

MirrorMode Mapper310::mirror_mode() const {
    return (data_latch_ & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper310::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper310::reset() {
    data_latch_ = 0U;
    address_latch_ = 0U;
    chr_bank_ = 0U;
}

std::uint16_t Mapper310::state_tag() const {
    return kStateTag;
}

void Mapper310::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(data_latch_);
    writer.write(address_latch_);
    writer.write(chr_bank_);
    writer.write_array(chr_ram_);
}

void Mapper310::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto data_latch = reader.read<Byte>();
    const auto address_latch = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();
    std::array<Byte, 32768> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        address_latch > 0x0FU || chr_bank > 3U) {
        reader.fail();
        return;
    }

    data_latch_ = data_latch;
    address_latch_ = address_latch;
    chr_bank_ = chr_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
