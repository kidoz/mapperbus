#include "core/mappers/mapper042.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x2A00U;
constexpr uint16_t kIrqCounterMask = 0x7FFFU;
constexpr uint16_t kIrqCounterActive = 0x6000U;
} // namespace

// NESdev mapper 42 r4239 and The Mad Dumper's 42.txt (2000-05-29).
Mapper042::Mapper042(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> Mapper042::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 42 supports submapper 0");
    if (prg_size != 131072U || (chr_size != 0U && chr_size != 131072U))
        return std::unexpected("Mapper 42 requires 128 KiB PRG and 128 KiB CHR ROM or CHR RAM");
    if (header.has_battery || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 42 has no persistent cartridge memory");
    if (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                           header.chr_ram_size != (chr_size == 0U ? 8192U : 0U)))
        return std::unexpected("Mapper 42 requires no CPU RAM and exactly the fitted CHR memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 42 supports ordinary H/V nametable wiring");
    return {};
}

Byte Mapper042::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return prg_rom_[static_cast<std::size_t>(prg_bank_) * 8192U + (addr & 0x1FFFU)];
    return prg_rom_[0x18000U + (addr & 0x7FFFU)];
}

void Mapper042::write_prg(Address addr, Byte value) {
    // 42.txt disables ROM output during writes, so none of these registers conflicts.
    switch (addr & 0xE003U) {
    case 0x8000U:
        if (!chr_rom_.empty())
            chr_bank_ = value & 15U;
        break;
    case 0xE000U:
        prg_bank_ = value & 15U;
        break;
    case 0xE001U:
        mirror_ = (value & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xE002U:
        irq_enabled_ = (value & 2U) != 0U;
        if (!irq_enabled_)
            irq_counter_ = 0U;
        break;
    default:
        break;
    }
}

bool Mapper042::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper042::read_chr(Address addr) {
    if (chr_rom_.empty())
        return chr_ram_[addr & 0x1FFFU];
    return chr_rom_[static_cast<std::size_t>(chr_bank_) * 8192U + (addr & 0x1FFFU)];
}

void Mapper042::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper042::mirror_mode() const {
    return mirror_;
}

void Mapper042::reset() {
    // Deterministic reset policy; the hardware references do not specify reset wiring.
    prg_bank_ = 0U;
    chr_bank_ = 0U;
    mirror_ = MirrorMode::Vertical;
    irq_counter_ = 0U;
    irq_enabled_ = false;
}

void Mapper042::power_on() {
    reset();
    chr_ram_.fill(0U);
}

bool Mapper042::irq_pending() const {
    return irq_enabled_ && (irq_counter_ & kIrqCounterActive) == kIrqCounterActive;
}

void Mapper042::clock_cpu() {
    if (irq_enabled_)
        irq_counter_ = (irq_counter_ + 1U) & kIrqCounterMask;
}

uint16_t Mapper042::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (chr_rom_.empty() ? 1U : 0U));
}

void Mapper042::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write(chr_bank_);
    writer.write(static_cast<Byte>(mirror_ == MirrorMode::Horizontal));
    writer.write(static_cast<Byte>(irq_enabled_));
    writer.write(irq_counter_);
    if (chr_rom_.empty())
        writer.write_array(chr_ram_);
}

void Mapper042::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    const auto irq_enabled = reader.read<Byte>();
    const auto irq_counter = reader.read<uint16_t>();
    std::array<Byte, 8192> chr_ram{};
    if (chr_rom_.empty())
        reader.read_array(chr_ram);
    if (!reader.ok() || tag != state_tag() || prg_bank > 15U || chr_bank > 15U || mirror > 1U ||
        irq_enabled > 1U || irq_counter > kIrqCounterMask ||
        (irq_enabled == 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_bank_ = chr_bank;
    mirror_ = mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    irq_enabled_ = irq_enabled != 0U;
    irq_counter_ = irq_counter;
    if (chr_rom_.empty())
        chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
