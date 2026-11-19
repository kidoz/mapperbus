#include "core/mappers/mapper323.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::size_t kChrSize = 1024U * 1024U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 4096U;
constexpr std::uint16_t kStateTag = 0x3230U;

bool valid_shift_register(Byte shift, uint8_t count) {
    if (count > 4U || shift > 0x1FU)
        return false;
    const auto sentinel = static_cast<Byte>(0x10U >> count);
    return (shift & sentinel) != 0U && (shift & (sentinel - 1U)) == 0U;
}

} // namespace

FaridSlrom323::FaridSlrom323(const INesHeader&,
                             std::vector<Byte> prg_rom,
                             std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> FaridSlrom323::validate_geometry(const INesHeader& header,
                                              std::size_t prg_size,
                                              std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 323 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != kChrSize)
        return std::unexpected("Mapper 323 requires 1 MiB PRG-ROM and 1 MiB CHR-ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 323 has no cartridge RAM or NVRAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 323 requires ordinary H/V startup nametable metadata");
    return {};
}

Byte FaridSlrom323::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const auto selected = static_cast<unsigned>(prg_bank_ & 7U);
    unsigned bank = 0U;
    switch ((control_ >> 2U) & 3U) {
    case 0U:
    case 1U:
        bank = (selected & 6U) | (addr >= 0xC000U ? 1U : 0U);
        break;
    case 2U:
        bank = addr < 0xC000U ? 0U : selected;
        break;
    default:
        bank = addr < 0xC000U ? selected : 7U;
        break;
    }
    const auto outer = static_cast<std::size_t>((outer_bank_ >> 4U) & 7U) * kOuterBankSize;
    return prg_rom_[outer + bank * kPrgBankSize + (addr & 0x3FFFU)];
}

void FaridSlrom323::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && (prg_bank_ & 0x10U) == 0U && (outer_bank_ & 8U) == 0U)
            outer_bank_ = value & 0x78U;
        return;
    }
    if ((value & 0x80U) != 0U) {
        shift_register_ = 0x10U;
        write_count_ = 0U;
        control_ |= 0x0CU;
        return;
    }

    shift_register_ = static_cast<Byte>((shift_register_ >> 1U) | ((value & 1U) << 4U));
    ++write_count_;
    if (write_count_ == 5U) {
        write_register(addr, shift_register_);
        shift_register_ = 0x10U;
        write_count_ = 0U;
    }
}

void FaridSlrom323::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U)
        write_prg(addr, value);
}

void FaridSlrom323::write_register(Address addr, Byte value) {
    value &= 0x1FU;
    if (addr < 0xA000U)
        control_ = value;
    else if (addr < 0xC000U)
        chr_bank0_ = value;
    else if (addr < 0xE000U)
        chr_bank1_ = value;
    else
        prg_bank_ = value;
}

Byte FaridSlrom323::read_chr(Address addr) {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0U;
    if ((control_ & 0x10U) != 0U) {
        bank = upper ? chr_bank1_ : chr_bank0_;
    } else {
        bank = chr_bank0_ & 0x1EU;
        if (upper)
            ++bank;
    }
    const auto outer = static_cast<std::size_t>((outer_bank_ >> 4U) & 7U) * kOuterBankSize;
    return chr_rom_[outer + bank * kChrBankSize + (addr & 0x0FFFU)];
}

void FaridSlrom323::write_chr(Address, Byte) {}

MirrorMode FaridSlrom323::mirror_mode() const {
    switch (control_ & 3U) {
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

void FaridSlrom323::reset() {
    outer_bank_ = 0U;
    shift_register_ = 0x10U;
    write_count_ = 0U;
    control_ |= 0x0CU;
}

void FaridSlrom323::power_on() {
    control_ = 0x0CU;
    chr_bank0_ = 0U;
    chr_bank1_ = 0U;
    prg_bank_ = 0U;
    reset();
}

void FaridSlrom323::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(shift_register_);
    writer.write(write_count_);
    writer.write(control_);
    writer.write(chr_bank0_);
    writer.write(chr_bank1_);
    writer.write(prg_bank_);
    writer.write(outer_bank_);
}

void FaridSlrom323::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto shift = reader.read<Byte>();
    const auto count = reader.read<uint8_t>();
    const auto control = reader.read<Byte>();
    const auto chr0 = reader.read<Byte>();
    const auto chr1 = reader.read<Byte>();
    const auto prg = reader.read<Byte>();
    const auto outer = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_shift_register(shift, count) || control > 0x1FU ||
        chr0 > 0x1FU || chr1 > 0x1FU || prg > 0x1FU || (outer & ~0x78U) != 0U) {
        reader.fail();
        return;
    }

    shift_register_ = shift;
    write_count_ = count;
    control_ = control;
    chr_bank0_ = chr0;
    chr_bank1_ = chr1;
    prg_bank_ = prg;
    outer_bank_ = outer;
}

} // namespace mapperbus::core
