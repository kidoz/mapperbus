#include "core/mappers/mapper554.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mapper554::Mapper554(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_(std::move(prg)), chr_(std::move(chr)) {}
Result<void> Mapper554::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 554 || !h.is_nes2 || h.submapper || prg != 131072 || chr != 131072 ||
        h.prg_ram_size || h.prg_nvram_size || h.chr_ram_size || h.chr_nvram_size || h.has_battery ||
        h.has_trainer || h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected(
            "KS-7010 requires 128 KiB each of PRG and CHR ROM and vertical mirroring");
    return {};
}
bool Mapper554::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte Mapper554::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0;
    constexpr std::array<unsigned, 4> fixed{10, 11, 6, 7};
    const unsigned bank = addr < 0x8000U ? bank_ : fixed[(addr - 0x8000U) >> 13U];
    return prg_[bank * 8192U + (addr & 0x1FFFU)];
}
void Mapper554::observe_cpu_read(Address addr, Byte) {
    // Decode only the read strobes documented on the KS-7010.
    if ((addr >= 0xCAB6U && addr <= 0xCAD6U) || addr == 0xEBE2U || addr == 0xEE32U ||
        addr == 0xFFFCU)
        bank_ = static_cast<Byte>((addr >> 2U) & 15U);
}
void Mapper554::write_prg(Address, Byte) {}
Byte Mapper554::read_chr(Address addr) {
    return chr_[bank_ * 8192U + (addr & 0x1FFFU)];
}
void Mapper554::write_chr(Address, Byte) {}
MirrorMode Mapper554::mirror_mode() const {
    return MirrorMode::Vertical;
}
void Mapper554::reset() {
    bank_ = 0;
}
void Mapper554::save_state(StateWriter& w) const {
    w.write<Word>(554);
    w.write(bank_);
}
void Mapper554::load_state(StateReader& r) {
    const auto tag = r.read<Word>();
    const auto bank = r.read<Byte>();
    if (!r.ok() || tag != 554 || bank > 15) {
        r.fail();
        return;
    }
    bank_ = bank;
}
} // namespace mapperbus::core
