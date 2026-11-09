#include "core/mappers/jaleco_discrete.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 72 r24541, mapper 92 r4687, mapper 101 r21360.
JalecoDiscrete::JalecoDiscrete(const INesHeader& header,
                               std::vector<Byte> prg,
                               std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), mapper_(header.mapper_number),
      mirror_(header.mirror_mode),
      prg_bank_mask_(mapper_ == 101U ? 0U : (mapper_ == 72U ? 7U : 15U)) {}

Byte JalecoDiscrete::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    if (mapper_ == 101U)
        return prg_rom_[(addr & 0x7FFFU) % prg_rom_.size()];
    const std::size_t bank = mapper_ == 72U
                                 ? (addr < 0xC000U ? prg_bank_ : prg_rom_.size() / 0x4000U - 1U)
                                 : (addr < 0xC000U ? 0U : prg_bank_);
    return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
}

void JalecoDiscrete::write_prg(Address addr, Byte value) {
    if (mapper_ == 101U) {
        if (addr >= 0x6000U && addr < 0x8000U)
            chr_bank_ = value; // Ordinary bit order, including documented oversize bits.
        return;
    }
    if (addr < 0x8000U)
        return;
    // Sample the old PRG mapping exactly once: ROM drives both the bank data
    // and the P/C levels clocking the two independent rising-edge latches.
    const auto bus = static_cast<unsigned>(value & read_prg(addr));
    const auto rising = bus & ~static_cast<unsigned>(command_);
    if ((rising & 0x80U) != 0U)
        prg_bank_ = static_cast<Byte>(bus & prg_bank_mask_);
    if ((rising & 0x40U) != 0U)
        chr_bank_ = static_cast<Byte>(bus & 15U);
    command_ = static_cast<Byte>(bus & 0xC0U);
    // Bits 4/5 control the optional external speech device, which is absent
    // from this model. They do not alter banking or fixed CIRAM wiring.
}

Byte JalecoDiscrete::read_chr(Address addr) {
    const auto bank = static_cast<std::size_t>(chr_bank_);
    return chr_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % chr_rom_.size()];
}

void JalecoDiscrete::write_chr(Address, Byte) {}

MirrorMode JalecoDiscrete::mirror_mode() const {
    return mirror_;
}

void JalecoDiscrete::reset() {
    // Deterministic emulator defaults; physical latch power-up/reset behavior
    // is not certified by the source descriptions.
    prg_bank_ = 0U;
    chr_bank_ = 0U;
    command_ = 0U;
}

void JalecoDiscrete::save_state(StateWriter& writer) const {
    writer.write(prg_bank_);
    writer.write(chr_bank_);
    writer.write(command_);
}

void JalecoDiscrete::load_state(StateReader& reader) {
    reader.read(prg_bank_);
    reader.read(chr_bank_);
    reader.read(command_);
    if (!reader.ok() || prg_bank_ > prg_bank_mask_ || (mapper_ != 101U && chr_bank_ > 15U) ||
        (command_ & 0x3FU) != 0U || (mapper_ == 101U && command_ != 0U)) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
