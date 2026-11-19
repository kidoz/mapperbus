#include "core/mappers/shanghai158b.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
INesHeader sugar_wiring(INesHeader h) {
    h.mapper_number = 215;
    return h;
}
} // namespace
Shanghai158B::Shanghai158B(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : Mmc3(sugar_wiring(h), std::move(prg), std::move(chr)) {
    reset();
}

Result<void> Shanghai158B::validate_geometry(const INesHeader& h,
                                             std::size_t prg,
                                             std::size_t chr) {
    const bool valid_chips = (prg == 0x20000 && (chr == 0x10000 || chr == 0x20000)) ||
                             (prg == 0x40000 && chr == 0x40000);
    if (!h.is_nes2 || h.mapper_number != 258 || h.submapper || !valid_chips || h.has_trainer ||
        h.has_battery || h.prg_ram_size || h.prg_nvram_size || h.chr_ram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Unsupported Shanghai Paradise 158B ROM or RAM configuration");
    return {};
}

bool Shanghai158B::maps_expansion(Address addr) const {
    return (addr & 0xF000) == 0x5000;
}
Byte Shanghai158B::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 15U : 0U;
}
Byte Shanghai158B::read_expansion(Address addr) {
    // Nonzero fuse-matrix outputs for the four populated rows; CPU D4-D7 float.
    if (!maps_expansion(addr))
        return 0;
    const unsigned col = addr & 7;
    switch (table_) {
    case 1:
        return col == 6 ? 3 : 0;
    case 2:
        return col == 4 ? 3 : col == 5 ? 4 : 0;
    case 3:
        return col == 3 ? 1 : col == 5 ? 4 : col == 6 ? 5 : 0;
    case 7:
        return col >= 3 && col <= 5 ? static_cast<Byte>(1U << (col - 3)) : col == 6 ? 15 : 0;
    default:
        return 0;
    }
}
void Shanghai158B::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    switch (addr & 7) {
    case 2:
        table_ = value & 7;
        break;
    case 0:
    case 1:
    case 7:
        Mmc3::write_expansion(addr, value);
        break;
    }
}
void Shanghai158B::reset() {
    Mmc3::reset();
    table_ = 7;
    write_expansion(0x5001, 0x3F);
    write_expansion(0x5007, 4);
}
void Shanghai158B::save_state(StateWriter& w) const {
    w.write<std::uint16_t>(0x2580);
    w.write(table_);
    Mmc3::save_state(w);
}
void Shanghai158B::load_state(StateReader& r) {
    const auto tag = r.read<std::uint16_t>();
    const auto table = r.read<Byte>();
    if (!r.ok() || tag != 0x2580 || table > 7) {
        r.fail();
        return;
    }
    StateWriter backup;
    Mmc3::save_state(backup);
    Mmc3::load_state(r);
    if (!r.ok()) {
        StateReader restore(backup.data());
        Mmc3::load_state(restore);
        return;
    }
    table_ = table;
}
} // namespace mapperbus::core
