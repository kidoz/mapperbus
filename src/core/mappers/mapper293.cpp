#include "core/mappers/mapper293.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 16U * 1024U;
constexpr std::size_t kDocumentedPrgSize = 512U * 1024U;
constexpr std::size_t kMaximumPrgSize = 1024U * 1024U;
constexpr std::uint16_t kStateTag = 0x1250U;

} // namespace

// Original implementation from NESdev mapper 293 r7751 and the NewStar PCB report.
Mapper293::Mapper293(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Mapper293::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 293U)
        return std::unexpected("Mapper293 requires mapper ID 293");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 293 supports submapper 0");
    if ((prg_size != kDocumentedPrgSize && prg_size != kMaximumPrgSize) || chr_size != 0U)
        return std::unexpected("Mapper 293 requires 512 KiB or 1 MiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 293 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 293 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper293::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const unsigned outer = ((outer_bank_ >> 4U) & 3U) | ((outer_bank_ & 1U) << 2U);
    const unsigned inner = inner_bank_ & 7U;
    const unsigned mode = (((inner_bank_ >> 3U) & 1U) << 1U) | ((outer_bank_ >> 6U) & 1U);
    const unsigned first = outer * 8U;
    unsigned low = first + inner;
    unsigned high = first + 7U;

    switch (mode) {
    case 1U:
        low &= ~1U;
        break;
    case 2U:
        high = low;
        break;
    case 3U:
        low &= ~1U;
        high = low + 1U;
        break;
    default:
        break;
    }

    const unsigned bank = addr < 0xC000U ? low : high;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper293::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (addr < 0xC000U)
        outer_bank_ = value;
    if ((addr & 0xA000U) == 0x8000U)
        inner_bank_ = value;
}

Byte Mapper293::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper293::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper293::mirror_mode() const {
    return (outer_bank_ & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper293::power_on() {
    outer_bank_ = 0U;
    inner_bank_ = 0U;
    chr_ram_.fill(0U); // Deterministic cold-RAM policy; soft reset retains all state.
}

void Mapper293::reset() {}

void Mapper293::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write<std::uint32_t>(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(outer_bank_);
    writer.write(inner_bank_);
    writer.write_array(chr_ram_);
}

void Mapper293::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto outer = reader.read<Byte>();
    const auto inner = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size()) {
        reader.fail();
        return;
    }
    outer_bank_ = outer;
    inner_bank_ = inner;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
