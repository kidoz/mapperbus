#include "core/mappers/mapper128.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr Address kAddressMask = 0x23E3U;
constexpr Address kLock = 0x2000U;
constexpr Address kMirroredInner = 0x0080U;
constexpr Address kCpuA14 = 0x0002U;

} // namespace

// Original decoding from NESdev r21955; iNES mapper 128 redirects to mapper 265.
Mapper128::Mapper128(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mapper_(header.mapper_number) {}

Result<void> Mapper128::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 128U && header.mapper_number != 265U)
        return std::unexpected("Mapper128 supports mapper IDs 128 and 265");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mappers 128/265 support submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x80000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mappers 128/265 require 16 KiB-512 KiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mappers 128/265 require 8 KiB volatile CHR RAM and no CPU RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mappers 128/265 support ordinary H/V CIRAM");
    return {};
}

Byte Mapper128::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const bool upper = addr >= 0xC000U;
    unsigned inner = data_latch_ & 7U;
    if ((address_latch_ & kMirroredInner) == 0U && upper)
        inner = 7U;
    if ((address_latch_ & kCpuA14) != 0U)
        inner = (inner & 6U) | static_cast<unsigned>(upper);
    const unsigned outer = ((address_latch_ >> 6U) & 0x0CU) | ((address_latch_ >> 5U) & 0x03U);
    const auto offset = (outer * 8U + inner) * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper128::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    data_latch_ = value & 7U;
    if ((address_latch_ & kLock) == 0U)
        address_latch_ = addr & kAddressMask;
}

Byte Mapper128::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper128::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper128::mirror_mode() const {
    return (address_latch_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper128::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper128::reset() {
    address_latch_ = 0U;
    data_latch_ = 0U;
}

uint16_t Mapper128::state_tag() const {
    return static_cast<uint16_t>(0x8000U | mapper_);
}

void Mapper128::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(address_latch_);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper128::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto address_latch = reader.read<Address>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != state_tag() || (address_latch & ~kAddressMask) != 0U ||
        data_latch > 7U) {
        reader.fail();
        return;
    }
    address_latch_ = address_latch;
    data_latch_ = data_latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
