#include "core/mappers/mapper553.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mapper553::Mapper553(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : mirror_(h.mirror_mode), prg_(std::move(prg)), chr_(std::move(chr)) {}
Result<void> Mapper553::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 553 || !h.is_nes2 || h.submapper || prg != 16384 || chr != 8192 ||
        h.prg_ram_size || h.prg_nvram_size || h.chr_ram_size || h.chr_nvram_size || h.has_battery ||
        h.has_trainer ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected("Sachen 3013 requires 16 KiB PRG ROM and 8 KiB CHR ROM");
    return {};
}
Byte Mapper553::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0;
    return addr < 0xC000U ? 0x3AU : prg_[addr & 0x3FFFU];
}
void Mapper553::write_prg(Address, Byte) {}
Byte Mapper553::read_chr(Address addr) {
    return chr_[addr & 0x1FFFU];
}
void Mapper553::write_chr(Address, Byte) {}
MirrorMode Mapper553::mirror_mode() const {
    return mirror_;
}
void Mapper553::reset() {}
void Mapper553::save_state(StateWriter& w) const {
    w.write<Word>(553);
    w.write(mirror_);
}
void Mapper553::load_state(StateReader& r) {
    if (r.read<Word>() != 553 || r.read<MirrorMode>() != mirror_)
        r.fail();
}
} // namespace mapperbus::core
