#include "core/mappers/transformer_keyboard.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
Result<void> TransformerKeyboard::validate_geometry(const INesHeader& h,
                                                    std::size_t prg,
                                                    std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 329 || h.submapper || h.has_trainer || h.has_battery ||
        prg != 32768 || chr != 8192 || h.prg_ram_size != 8192 || h.prg_nvram_size ||
        h.chr_ram_size || h.chr_nvram_size || h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected(
            "Keyboard Transformer requires 32/8 KiB ROM, vertical CIRAM, and 8 KiB volatile WRAM");
    return {};
}
Byte TransformerKeyboard::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0;
    irq_ = false;
    switch (addr & 3) {
    case 0:
        return scancode_ & 15;
    case 1:
        return scancode_ >> 4;
    default:
        return 0;
    }
}
void TransformerKeyboard::save_state(StateWriter& w) const {
    w.write<Word>(329);
    w.write(scancode_);
    w.write<Byte>(irq_);
    Nrom::save_state(w);
}
void TransformerKeyboard::load_state(StateReader& r) {
    const auto tag = r.read<Word>();
    const auto code = r.read<Byte>();
    const auto irq = r.read<Byte>();
    if (!r.ok() || tag != 329 || irq > 1) {
        r.fail();
        return;
    }
    StateWriter backup;
    Nrom::save_state(backup);
    Nrom::load_state(r);
    if (!r.ok()) {
        StateReader restore(backup.data());
        Nrom::load_state(restore);
        return;
    }
    scancode_ = code;
    irq_ = irq;
}
} // namespace mapperbus::core
