#include "core/mappers/mapper166.hpp"

#include <array>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0xA600U;
constexpr std::array<Byte, 4> kRegisterMasks = {0x11U, 0x1CU, 0x1FU, 0x1FU};
} // namespace

// Original decoding from NESdev mapper 167 r22345, including its mapper-166 errata.
Mapper166::Mapper166(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), incorrect_bank_order_(header.mapper_number == 166U) {
    power_on();
}

Result<void> Mapper166::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 166U && header.mapper_number != 167U)
        return std::unexpected("Mapper166 supports mapper IDs 166 and 167");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mappers 166/167 support submapper 0");
    if (prg_size != 1048576U || chr_size != 0U)
        return std::unexpected("Mappers 166/167 require 1 MiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 8192U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mappers 166/167 require 8 KiB volatile PRG and CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mappers 166/167 support H/V startup mirroring");
    return {};
}

Byte Mapper166::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U ? prg_ram_[addr & 0x1FFFU] : 0U;

    const Byte selected = static_cast<Byte>((((registers_[0] ^ registers_[1]) & 0x10U) << 1U) |
                                            ((registers_[2] ^ registers_[3]) & 0x1FU));
    const Byte mode = static_cast<Byte>((registers_[1] >> 2U) & 3U);
    const bool upper = addr >= 0xC000U;
    Byte bank = selected;
    switch (mode) {
    case 0U:
        if (upper)
            bank = incorrect_bank_order_ ? 0x07U : 0x20U;
        break;
    case 1U:
        if (!upper)
            bank = 0x1FU;
        break;
    default:
        if (!upper && !incorrect_bank_order_)
            bank = selected ^ 1U;
        break;
    }
    const auto offset = static_cast<std::size_t>(bank) * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper166::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        const auto index = static_cast<std::size_t>((addr >> 13U) & 3U);
        registers_[index] = value & kRegisterMasks[index];
    } else if (addr >= 0x6000U) {
        prg_ram_[addr & 0x1FFFU] = value;
    }
}

bool Mapper166::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper166::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper166::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper166::mirror_mode() const {
    return (registers_[0] & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Mapper166::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper166::reset() {
    // The article does not specify reset inputs; soft reset retains both RAMs.
    registers_.fill(0U);
}

uint16_t Mapper166::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (incorrect_bank_order_ ? 1U : 0U));
}

void Mapper166::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(registers_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper166::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 4> registers{};
    std::array<Byte, 8192> prg_ram{}, chr_ram{};
    reader.read_array(registers);
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);
    bool valid = reader.ok() && tag == state_tag();
    for (std::size_t index = 0U; index < registers.size(); ++index)
        valid = valid && (registers[index] & ~kRegisterMasks[index]) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }
    registers_ = registers;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
