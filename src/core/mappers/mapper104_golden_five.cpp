#include "core/mappers/mapper104_golden_five.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x140000U;
constexpr std::size_t kOuterBankSize = 0x40000U;
constexpr std::size_t kInnerBankSize = 0x4000U;
constexpr std::uint16_t kStateTag = 0x6800U;

} // namespace

Mapper104GoldenFive::Mapper104GoldenFive(const INesHeader&,
                                         std::vector<Byte> prg_rom,
                                         std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper104GoldenFive::validate_geometry(const INesHeader& header,
                                                    std::size_t prg_size,
                                                    std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 104 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != 0U)
        return std::unexpected("Mapper 104 requires 1280 KiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0x2000U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 104 requires 8 KiB volatile CHR RAM and no other memory");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 104 requires vertical mirroring");
    return {};
}

Byte Mapper104GoldenFive::outer_bank() const {
    return outer_register_ & 0x07U;
}

Byte Mapper104GoldenFive::read_prg(Address addr) {
    if (addr < 0x8000U || outer_bank() > 4U)
        return 0U;
    const Byte inner_bank = addr < 0xC000U ? inner_register_ : 0x0FU;
    const std::size_t offset = static_cast<std::size_t>(outer_bank()) * kOuterBankSize +
                               static_cast<std::size_t>(inner_bank) * kInnerBankSize +
                               (addr & 0x3FFFU);
    return prg_rom_[offset];
}

Byte Mapper104GoldenFive::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && outer_bank() > 4U ? 0U : 0xFFU;
}

void Mapper104GoldenFive::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (addr < 0xC000U) {
        if ((outer_register_ & 0x08U) == 0U)
            outer_register_ = value & 0x0FU;
    } else {
        inner_register_ = value & 0x0FU;
    }
}

Byte Mapper104GoldenFive::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper104GoldenFive::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper104GoldenFive::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Mapper104GoldenFive::power_on() {
    outer_register_ = 0U;
    inner_register_ = 0U;
    chr_ram_.fill(0U);
}

void Mapper104GoldenFive::reset() {}

void Mapper104GoldenFive::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(outer_register_);
    writer.write(inner_register_);
    writer.write_array(chr_ram_);
}

void Mapper104GoldenFive::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto outer_register = reader.read<Byte>();
    const auto inner_register = reader.read<Byte>();
    std::array<Byte, 0x2000> chr_ram{};
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag || outer_register > 0x0FU || inner_register > 0x0FU) {
        reader.fail();
        return;
    }
    outer_register_ = outer_register;
    inner_register_ = inner_register;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
