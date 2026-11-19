#include "core/mappers/nesticle_mmc3.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
NesticleMmc3::NesticleMmc3(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : Mmc3(h, std::move(prg), std::move(chr)) {
    power_on();
}

Result<void> NesticleMmc3::validate_geometry(const INesHeader& h,
                                             std::size_t prg,
                                             std::size_t chr) {
    if (h.submapper || h.console_type || !h.has_trainer || h.has_battery || h.misc_rom_count ||
        prg != 131072 || chr != 262144 || h.prg_ram_size != 8192 || h.prg_nvram_size ||
        h.chr_ram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected("NESticle Akira requires a trainer, 128/256 KiB ROM and 8 KiB WRAM");
    return {};
}

Byte NesticleMmc3::read_prg(Address addr) {
    if (addr < 0x8000)
        return Mmc3::read_prg(addr);
    return physical_prg(prg_[(addr >> 13) & 3] * 8192U + (addr & 0x1FFF));
}

Byte NesticleMmc3::read_chr(Address addr) {
    return physical_chr(chr_[(addr >> 10) & 7] * 1024U + (addr & 0x3FF));
}

void NesticleMmc3::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000) != 0x8000) {
        // AX5202P WRAM chip select is independent of A001.D7; D6 still protects writes.
        Mmc3::write_prg(addr, (addr & 0xE001) == 0xA001 ? value | 0x80 : value);
        return;
    }
    if (!(addr & 1U)) {
        index_ = value;
        return;
    }
    if (index_ == 6 || index_ == 7 || index_ == 0x46 || index_ == 0x47) {
        const unsigned slot = index_ == 0x46 ? 2U : index_ & 1U;
        prg_[slot] = value;
        return;
    }
    const unsigned reg = index_ & 0x7F;
    const unsigned flip = (index_ & 0x80) ? 4U : 0U;
    if (reg < 2) {
        const unsigned slot = reg * 2U ^ flip;
        chr_[slot] = value & 0xFE;
        chr_[slot + 1] = value | 1;
    } else if (reg < 6) {
        chr_[(reg + 2U) ^ flip] = value;
    }
}

void NesticleMmc3::reset() {
    prg_ = {0, 1, 0xFE, 0xFF};
    chr_ = {0, 1, 2, 3, 4, 5, 6, 7};
    acknowledge_irq();
}

void NesticleMmc3::power_on() {
    power_on_ax5202p();
    index_ = 0;
    reset();
}

void NesticleMmc3::save_state(StateWriter& writer) const {
    writer.write<Word>(100);
    writer.write(index_);
    writer.write_array(prg_);
    writer.write_array(chr_);
    Mmc3::save_state(writer);
}

void NesticleMmc3::load_state(StateReader& reader) {
    const auto tag = reader.read<Word>();
    const auto index = reader.read<Byte>();
    decltype(prg_) prg{};
    decltype(chr_) chr{};
    reader.read_array(prg);
    reader.read_array(chr);
    if (!reader.ok() || tag != 100 || prg[3] != 0xFF) {
        reader.fail();
        return;
    }
    Mmc3::load_state(reader);
    if (reader.ok()) {
        index_ = index;
        prg_ = prg;
        chr_ = chr;
    }
}
} // namespace mapperbus::core
