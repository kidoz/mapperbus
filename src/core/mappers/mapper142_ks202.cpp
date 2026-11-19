#include "core/mappers/mapper142_ks202.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 142 r5032 and its linked VRC3/pinout descriptions. Undocumented
// bank selectors 0, 5 and 7 retain the selection but do not alter PRG banks.
KaiserKs202::KaiserKs202(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> KaiserKs202::validate_geometry(const INesHeader& header,
                                            std::size_t prg_size,
                                            std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 142 supports submapper 0");
    if (prg_size < 32768U || prg_size > 131072U || prg_size % 8192U != 0U || chr_size != 0U)
        return std::unexpected(
            "Mapper 142 requires 32-128 KiB PRG ROM in 8 KiB units and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 142 requires only 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 142 requires fixed H/V mirroring");
    return {};
}

Byte KaiserKs202::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;

    std::size_t bank = prg_rom_.size() / 8192U - 1U;
    if (addr < 0x8000U)
        bank = prg_banks_[0U];
    else if (addr < 0xE000U)
        bank = prg_banks_[1U + (addr - 0x8000U) / 8192U];
    return prg_rom_[(bank * 8192U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void KaiserKs202::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;

    const auto group = static_cast<unsigned>(addr) >> 12U;
    if (group <= 11U) {
        const auto shift = (group - 8U) * 4U;
        irq_reload_ = static_cast<uint16_t>((irq_reload_ & ~(0xFU << shift)) |
                                            ((static_cast<unsigned>(value) & 15U) << shift));
    } else if (group == 12U) {
        irq_control_ = value & 7U;
        irq_pending_ = false;
        if ((irq_control_ & 2U) != 0U)
            irq_counter_ = irq_reload_;
    } else if (group == 13U) {
        irq_pending_ = false;
    } else if (group == 14U) {
        bank_select_ = value & 7U;
    } else {
        constexpr std::array<int, 8> kBankSlots = {-1, 1, 2, 3, 0, -1, -1, -1};
        const auto slot = kBankSlots[bank_select_];
        if (slot >= 0)
            prg_banks_[static_cast<std::size_t>(slot)] = value & 15U;
    }
}

bool KaiserKs202::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte KaiserKs202::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void KaiserKs202::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode KaiserKs202::mirror_mode() const {
    return mirror_;
}

void KaiserKs202::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void KaiserKs202::reset() {
    // RESET pin behavior and power-up latches are not established by the sources.
    prg_banks_.fill(0U);
    bank_select_ = 0U;
    irq_reload_ = 0U;
    irq_counter_ = 0U;
    irq_control_ = 0U;
    irq_pending_ = false;
}

void KaiserKs202::clock_cpu() {
    if ((irq_control_ & 2U) == 0U)
        return;

    if ((irq_control_ & 4U) != 0U) {
        const auto low = static_cast<unsigned>(irq_counter_ & 0xFFU);
        if (low == 0xFFU) {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | (irq_reload_ & 0xFFU));
            irq_pending_ = true;
        } else {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | (low + 1U));
        }
    } else if (irq_counter_ == 0xFFFFU) {
        irq_counter_ = irq_reload_;
        irq_pending_ = true;
    } else {
        ++irq_counter_;
    }
}

bool KaiserKs202::irq_pending() const {
    return irq_pending_;
}

void KaiserKs202::acknowledge_irq() {
    irq_pending_ = false;
}

uint16_t KaiserKs202::state_tag() const {
    return static_cast<uint16_t>(0x8E00U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void KaiserKs202::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(prg_banks_);
    writer.write(bank_select_);
    writer.write(irq_reload_);
    writer.write(irq_counter_);
    writer.write(irq_control_);
    writer.write(static_cast<Byte>(irq_pending_));
    writer.write_array(chr_ram_);
}

void KaiserKs202::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 4> banks{};
    reader.read_array(banks);
    const auto bank_select = reader.read<Byte>();
    const auto irq_reload = reader.read<uint16_t>();
    const auto irq_counter = reader.read<uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);

    if (!reader.ok() || tag != state_tag() ||
        !std::ranges::all_of(banks, [](Byte bank) { return bank <= 15U; }) || bank_select > 7U ||
        irq_control > 7U || irq_pending > 1U) {
        reader.fail();
        return;
    }

    prg_banks_ = banks;
    bank_select_ = bank_select;
    irq_reload_ = irq_reload;
    irq_counter_ = irq_counter;
    irq_control_ = irq_control;
    irq_pending_ = irq_pending != 0U;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
