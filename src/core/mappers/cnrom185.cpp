#include "core/mappers/cnrom185.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Cnrom185::Cnrom185(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      submapper_(header.is_nes2 ? header.submapper : 0U), mirror_mode_(header.mirror_mode) {}

Result<void> Cnrom185::validate_geometry(const INesHeader& h,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U && (h.submapper < 4U || h.submapper > 7U))
        return std::unexpected("Mapper 185 supports submappers 0 and 4 through 7");
    if ((prg_size != 0x4000U && prg_size != 0x8000U) || chr_size != 0x2000U)
        return std::unexpected("Mapper 185 requires 16/32 KiB PRG ROM and 8 KiB CHR ROM");
    if (h.has_battery || h.prg_nvram_size != 0U || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && h.prg_ram_size != 0U))
        return std::unexpected("Mapper 185 has no cartridge RAM or battery storage");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 185 supports fixed H/V CIRAM");
    return {};
}

Byte Cnrom185::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    return prg_rom_[(addr - 0x8000U) % prg_rom_.size()];
}

void Cnrom185::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = static_cast<Byte>(value & read_prg(addr) & 0xC3U);
}

void Cnrom185::observe_cpu_read(Address addr, Byte /*data*/) {
    if (submapper_ == 0U && ppudata_reads_ < 2U && (addr & 0xE007U) == 0x2007U)
        ++ppudata_reads_;
}

bool Cnrom185::chr_enabled() const {
    return submapper_ == 0U ? ppudata_reads_ >= 2U
                            : (latch_ & 3U) == static_cast<Byte>(submapper_ - 4U);
}

Byte Cnrom185::read_chr(Address addr) {
    if (!chr_enabled()) {
        // The multiplexed bus normally retains its low address byte. Submapper
        // zero also models the documented D0 pull-up needed by its known-game
        // compatibility heuristic; other boards retain their unspecified decay.
        return static_cast<Byte>((addr & 0xFFU) | (submapper_ == 0U ? 1U : 0U));
    }
    return chr_rom_[addr & 0x1FFFU];
}

void Cnrom185::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Cnrom185::mirror_mode() const {
    return mirror_mode_;
}

void Cnrom185::reset() {
    latch_ = 0U;
    ppudata_reads_ = 0U;
}

uint16_t Cnrom185::state_tag() const {
    return static_cast<uint16_t>(0xB900U | submapper_);
}

void Cnrom185::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write(ppudata_reads_);
}

void Cnrom185::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Byte>();
    const auto ppudata_reads = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || (latch & 0x3CU) != 0U || ppudata_reads > 2U ||
        (submapper_ != 0U && ppudata_reads != 0U)) {
        reader.fail();
        return;
    }
    latch_ = latch;
    ppudata_reads_ = ppudata_reads;
}

} // namespace mapperbus::core
