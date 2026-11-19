#include "core/mappers/nes_event.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

NesEvent::NesEvent(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> NesEvent::validate_geometry(const INesHeader& h,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 105 supports submapper 0");
    if (prg_size != 262144U || chr_size != 0U)
        return std::unexpected("Mapper 105 requires 256 KiB PRG ROM and no CHR ROM");
    if (h.has_battery || h.chr_nvram_size != 0U ||
        (h.is_nes2 &&
         (h.prg_ram_size != 8192U || h.prg_nvram_size != 0U || h.chr_ram_size != 8192U)))
        return std::unexpected("Mapper 105 requires volatile 8 KiB WRAM and 8 KiB CHR RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 105 supports ordinary H/V CIRAM");
    return {};
}

Byte NesEvent::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return (prg_bank_ & 0x10U) == 0U ? wram_[addr & 0x1FFFU] : 0U;
    if (addr < 0x8000U)
        return 0U;
    const auto page = prg_page(addr);
    return prg_rom_[page * 16384U + (addr & 0x3FFFU)];
}

unsigned NesEvent::prg_page(Address addr) const {
    const bool upper_window = addr >= 0xC000U;
    if (initialized_ && (chr_bank0_ & 0x08U) == 0U) {
        const unsigned bank_pair = (chr_bank0_ >> 1U) & 3U;
        return bank_pair * 2U + static_cast<unsigned>(upper_window);
    }

    const unsigned chip_base = initialized_ && (chr_bank0_ & 0x08U) != 0U ? 8U : 0U;
    const unsigned bank = prg_bank_ & 7U;
    const unsigned mode = (control_ >> 2U) & 3U;
    unsigned page = 0U;
    switch (mode) {
    case 0U:
    case 1U:
        page = (bank & 6U) + static_cast<unsigned>(upper_window);
        break;
    case 2U:
        page = upper_window ? bank : 0U;
        break;
    default:
        page = upper_window ? 7U : bank;
        break;
    }
    return chip_base + page;
}

void NesEvent::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((prg_bank_ & 0x10U) == 0U)
            wram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;
    if ((value & 0x80U) != 0U) {
        shift_register_ = 0x10U;
        write_count_ = 0U;
        control_ |= 0x0CU;
        return;
    }

    shift_register_ = static_cast<Byte>((shift_register_ >> 1U) | ((value & 1U) << 4U));
    ++write_count_;
    if (write_count_ == 5U) {
        write_register(addr, shift_register_);
        shift_register_ = 0x10U;
        write_count_ = 0U;
    }
}

void NesEvent::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U)
        write_prg(addr, value);
}

void NesEvent::write_register(Address addr, Byte value) {
    if (addr < 0xA000U) {
        control_ = value & 0x1FU;
    } else if (addr < 0xC000U) {
        const bool was_held = (chr_bank0_ & 0x10U) != 0U;
        chr_bank0_ = value & 0x1FU;
        const bool held = (chr_bank0_ & 0x10U) != 0U;
        if (!was_held && held)
            initialized_ = true;
        if (held) {
            counter_ = 0U;
            irq_pending_ = false;
        }
    } else if (addr < 0xE000U) {
        chr_bank1_ = value & 0x1FU;
    } else {
        prg_bank_ = value & 0x1FU;
    }
}

bool NesEvent::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && (prg_bank_ & 0x10U) == 0U);
}

Byte NesEvent::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void NesEvent::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode NesEvent::mirror_mode() const {
    switch (control_ & 3U) {
    case 0U:
        return MirrorMode::SingleLower;
    case 1U:
        return MirrorMode::SingleUpper;
    case 2U:
        return MirrorMode::Vertical;
    default:
        return MirrorMode::Horizontal;
    }
}

Result<void> NesEvent::configure_board_jumpers(Byte value) {
    if (value > 15U)
        return std::unexpected("Mapper 105 DIP switches require a value 0..15");
    dips_ = value;
    return {};
}

uint32_t NesEvent::irq_threshold() const {
    return 0x20000000U | (static_cast<uint32_t>(dips_) << 25U);
}

bool NesEvent::irq_pending() const {
    return irq_pending_;
}

void NesEvent::clock_cpu() {
    if ((chr_bank0_ & 0x10U) != 0U)
        return;
    counter_ = (counter_ + 1U) & 0x3FFFFFFFU;
    if (counter_ == irq_threshold())
        irq_pending_ = true;
}

void NesEvent::reset() {
    shift_register_ = 0x10U;
    write_count_ = 0U;
    control_ = 0U;
    chr_bank0_ = 0U;
    chr_bank1_ = 0U;
    prg_bank_ = 0U;
    initialized_ = false;
    irq_pending_ = false;
    counter_ = 0U;
}

void NesEvent::power_on() {
    wram_.fill(0U);
    chr_ram_.fill(0U);
    reset();
}

std::span<const Byte> NesEvent::battery_ram() const {
    return {};
}

void NesEvent::set_battery_ram(std::span<const Byte>) {}

uint16_t NesEvent::state_tag() const {
    return static_cast<uint16_t>(0x6900U | dips_);
}

void NesEvent::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(shift_register_);
    writer.write(write_count_);
    writer.write(control_);
    writer.write(chr_bank0_);
    writer.write(chr_bank1_);
    writer.write(prg_bank_);
    writer.write(static_cast<Byte>(initialized_));
    writer.write(static_cast<Byte>(irq_pending_));
    writer.write(counter_);
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void NesEvent::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto shift = reader.read<Byte>();
    const auto count = reader.read<Byte>();
    const auto control = reader.read<Byte>();
    const auto chr0 = reader.read<Byte>();
    const auto chr1 = reader.read<Byte>();
    const auto prg = reader.read<Byte>();
    const auto initialized = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto counter = reader.read<uint32_t>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 8192> chr{};
    reader.read_array(wram);
    reader.read_array(chr);

    const bool held = (chr0 & 0x10U) != 0U;
    const bool valid = reader.ok() && tag == state_tag() && shift <= 0x1FU && count <= 4U &&
                       control <= 0x1FU && chr0 <= 0x1FU && chr1 <= 0x1FU && prg <= 0x1FU &&
                       initialized <= 1U && irq <= 1U && counter < 0x40000000U &&
                       (initialized != 0U || !held) && (!held || (counter == 0U && irq == 0U));
    if (!valid) {
        reader.fail();
        return;
    }

    shift_register_ = shift;
    write_count_ = count;
    control_ = control;
    chr_bank0_ = chr0;
    chr_bank1_ = chr1;
    prg_bank_ = prg;
    initialized_ = initialized != 0U;
    irq_pending_ = irq != 0U;
    counter_ = counter;
    wram_ = wram;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
