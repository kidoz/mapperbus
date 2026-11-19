#include "core/mappers/mapper349.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x20000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kOuterBankSize = 0x20000UZ;
constexpr std::uint16_t kLatchMask = 0x10FFU;
constexpr std::uint16_t kStateTag = 0x3490U;

} // namespace

// Board decoding follows NESdev mapper 349 oldid 7906. The article does not
// specify reset or power values, so a zero latch and cold CHR-RAM clearing are
// deterministic emulator policies. Smaller fitted ROMs wrap after bank decoding.
Mapper349::Mapper349(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper349::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 349U) {
        return std::unexpected("Mapper 349 requires mapper ID 349");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 349 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 349 requires power-of-two 128-1024 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0x2000U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 349 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 349 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper349::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const bool unrom = (latch_ & 0x1000U) != 0U;
    const bool fixed_a14 = (latch_ & 0x0040U) != 0U;
    const bool upper_window = (addr & 0x4000U) != 0U;
    const Byte inner = latch_ & 0x07U;
    Byte bank = inner;
    if (unrom) {
        bank = upper_window ? 0x07U : static_cast<Byte>(inner | (fixed_a14 ? 1U : 0U));
    } else if (!fixed_a14) {
        bank = static_cast<Byte>((inner & 0x06U) | (upper_window ? 1U : 0U));
    }

    const std::size_t offset = static_cast<std::size_t>((latch_ >> 3U) & 0x07U) * kOuterBankSize +
                               static_cast<std::size_t>(bank) * kPrgBankSize +
                               (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper349::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        latch_ = static_cast<std::uint16_t>((addr & 0x1000U) | value);
    }
}

Byte Mapper349::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

Byte Mapper349::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper349::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper349::mirror_mode() const {
    return (latch_ & 0x0080U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper349::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper349::reset() {
    latch_ = 0U;
}

void Mapper349::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper349::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<std::uint16_t>();
    std::array<Byte, 0x2000> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
