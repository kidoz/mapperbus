#include "core/mappers/ret_cufrom.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::uint16_t kStateTag = 0x2900U;
} // namespace

// Hardware behavior is from NESdev iNES Mapper 029, immutable revision 4093.
RetCufrom::RetCufrom(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> RetCufrom::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 29 supports submapper 0");
    if (prg_size != 131072U || chr_size != 0U)
        return std::unexpected("RET-CUFROM requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("RET-CUFROM revD has no nonvolatile memory");
    if (header.is_nes2 && (header.prg_ram_size != 8192U || header.chr_ram_size != 32768U))
        return std::unexpected("RET-CUFROM requires 8 KiB WRAM and 32 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("RET-CUFROM is hard-wired for vertical mirroring");
    return {};
}

Byte RetCufrom::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const auto bank = addr < 0xC000U ? (latch_ >> 2U) & 7U : 7U;
        return prg_rom_[static_cast<std::size_t>(bank) * 16384U + (addr & 0x3FFFU)];
    }
    return addr >= 0x6000U ? wram_[addr & 0x1FFFU] : 0U;
}

void RetCufrom::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = value;
    else if (addr >= 0x6000U)
        wram_[addr & 0x1FFFU] = value;
}

bool RetCufrom::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte RetCufrom::read_chr(Address addr) {
    return chr_ram_[static_cast<std::size_t>(latch_ & 3U) * 8192U + (addr & 0x1FFFU)];
}

void RetCufrom::write_chr(Address addr, Byte value) {
    chr_ram_[static_cast<std::size_t>(latch_ & 3U) * 8192U + (addr & 0x1FFFU)] = value;
}

MirrorMode RetCufrom::mirror_mode() const {
    return MirrorMode::Vertical;
}

void RetCufrom::power_on() {
    latch_ = 0U;
    wram_.fill(0U);
    chr_ram_.fill(0U);
}

void RetCufrom::reset() {
    // The 74HC377 latch has no console reset input; RAM is retained as well.
}

void RetCufrom::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(latch_);
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void RetCufrom::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 32768> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag) {
        reader.fail();
        return;
    }
    latch_ = latch;
    wram_ = wram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
