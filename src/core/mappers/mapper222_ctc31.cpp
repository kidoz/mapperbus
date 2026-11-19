#include "core/mappers/mapper222_ctc31.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Ctc31::Ctc31(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    reset();
}

Result<void> Ctc31::validate_geometry(const INesHeader& header,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (header.mapper_number != 222U)
        return std::unexpected("CTC-31 requires mapper 222");
    if (header.is_nes2 && header.submapper > 1U)
        return std::unexpected("CTC-31 supports CTC-31 submapper 1 and legacy submapper 0");
    if (prg_size != 131072U || chr_size != 131072U)
        return std::unexpected("CTC-31 requires 128 KiB PRG and 128 KiB CHR ROM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("CTC-31 requires H/V startup mirroring");
    if (header.has_trainer || header.has_battery || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("CTC-31 has no RAM, NVRAM, or battery");
    return {};
}

void Ctc31::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    microwire_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_divider_ = 0U;
    irq_clock_mode_ = false;
    irq_pending_ = false;
}

Byte Ctc31::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x6000U && addr < 0x7000U ? microwire_ : 0U;

    const auto slot = (addr - 0x8000U) >> 13U;
    const std::size_t count = prg_rom_.size() / 0x2000U;
    const std::size_t bank = slot == 0U   ? prg_banks_[0U]
                             : slot == 1U ? prg_banks_[1U]
                             : slot == 2U ? count - 2U
                                          : count - 1U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

bool Ctc31::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x7000U);
}

Byte Ctc31::prg_read_mask(Address addr) const {
    return addr >= 0x6000U && addr < 0x7000U ? 1U : 0xFFU;
}

void Ctc31::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        microwire_ = value & 1U;
        return;
    }
    if (addr < 0x8000U)
        return;

    const auto group = static_cast<unsigned>(addr) >> 12U;
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 31U;
        return;
    }
    if (group == 9U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        return;
    }
    if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + ((addr & 2U) >> 1U)];
        if ((addr & 1U) == 0U)
            bank = (bank & 0xF0U) | (value & 15U);
        else
            bank = static_cast<Byte>((bank & 15U) | (static_cast<unsigned>(value) << 4U));
        return;
    }
    if (group != 15U)
        return;

    if ((addr & 0xF001U) == 0xF001U) {
        irq_pending_ = false;
        if (!irq_clock_mode_) {
            irq_latch_ = value;
            irq_counter_ = irq_latch_;
            set_irq_from_counter();
        }
    } else if ((addr & 0xF003U) == 0xF000U) {
        irq_clock_mode_ = false;
    } else if ((addr & 0xF003U) == 0xF002U) {
        irq_clock_mode_ = true;
        irq_divider_ = 0U;
    }
}

Byte Ctc31::read_chr(Address addr) {
    const auto address = static_cast<unsigned>(addr);
    const auto offset =
        static_cast<std::size_t>(chr_banks_[(address >> 10U) & 7U]) * 0x400U + (address & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Ctc31::write_chr(Address, Byte) {}

MirrorMode Ctc31::mirror_mode() const {
    return mirror_;
}

void Ctc31::set_irq_from_counter() {
    if ((irq_counter_ & 0xF0U) == 0xF0U) {
        irq_pending_ = true;
        irq_divider_ = 0U;
    }
}

void Ctc31::increment_irq_counter() {
    unsigned low = irq_counter_ & 15U;
    unsigned high = irq_counter_ >> 4U;
    if (low == 15U) {
        low = 0U;
    } else {
        ++low;
        // The lower 74'161 carry clocks the upper chip on the 14-to-15 edge.
        if (low == 15U)
            high = (high + 1U) & 15U;
    }
    irq_counter_ = static_cast<Byte>((high << 4U) | low);
    set_irq_from_counter();
}

void Ctc31::clock_cpu() {
    if (irq_pending_) {
        irq_divider_ = 0U;
        return;
    }
    irq_divider_ = (irq_divider_ + 1U) & 0x7FU;
    if (irq_clock_mode_ && irq_divider_ == 64U)
        increment_irq_counter();
}

bool Ctc31::irq_pending() const {
    return irq_pending_;
}

uint16_t Ctc31::state_tag() const {
    return static_cast<uint16_t>(0xDE00U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Ctc31::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(microwire_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_divider_);
    writer.write(static_cast<Byte>(irq_clock_mode_));
    writer.write(static_cast<Byte>(irq_pending_));
}

void Ctc31::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 2> prg{};
    std::array<Byte, 8> chr{};
    reader.read_array(prg);
    reader.read_array(chr);
    const auto mirror = reader.read<MirrorMode>();
    const auto microwire = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto divider = reader.read<uint16_t>();
    const auto clock_mode = reader.read<Byte>();
    const auto pending = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() ||
        std::ranges::any_of(prg, [](Byte bank) { return bank > 31U; }) ||
        (mirror != MirrorMode::Horizontal && mirror != MirrorMode::Vertical) || microwire > 1U ||
        divider > 127U || clock_mode > 1U || pending > 1U || (pending != 0U && divider != 0U)) {
        reader.fail();
        return;
    }
    prg_banks_ = prg;
    chr_banks_ = chr;
    mirror_ = mirror;
    microwire_ = microwire;
    irq_latch_ = latch;
    irq_counter_ = counter;
    irq_divider_ = divider;
    irq_clock_mode_ = clock_mode != 0U;
    irq_pending_ = pending != 0U;
}

} // namespace mapperbus::core
