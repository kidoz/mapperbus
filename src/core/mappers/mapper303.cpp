#include "core/mappers/mapper303.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::uint16_t kStateTag = 0x3030;
} // namespace

Mapper303::Mapper303(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Mapper303::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 303 || h.submapper != 0 || prg != 128 * 1024 || chr != 0)
        return std::unexpected(
            "Mapper 303 requires NES 2.0 submapper 0, 128 KiB PRG ROM and no CHR ROM");
    if (h.has_battery || h.has_trainer || h.prg_ram_size != 8192 || h.chr_ram_size != 8192 ||
        h.prg_nvram_size != 0 || h.chr_nvram_size != 0 ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "Mapper 303 requires 8 KiB each of volatile PRG and CHR RAM and ordinary CIRAM");
    return {};
}

bool Mapper303::maps_prg(Address addr) const {
    return addr >= 0x6000;
}
Byte Mapper303::read_prg(Address addr) {
    if (addr < 0x6000)
        return 0;
    if (addr < 0x8000)
        return prg_ram_[addr & 0x1FFF];
    const std::size_t page = addr < 0xC000 ? bank_ : 2;
    return prg_rom_[page * 0x4000 + (addr & 0x3FFF)];
}
void Mapper303::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000)
        prg_ram_[addr & 0x1FFF] = value;
}
Byte Mapper303::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFF];
}
void Mapper303::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFF] = value;
}
MirrorMode Mapper303::mirror_mode() const {
    return horizontal_ ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
bool Mapper303::maps_expansion(Address addr) const {
    return addr == 0x4030;
}
Byte Mapper303::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 1 : 0;
}
Byte Mapper303::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0;
    return std::exchange(irq_, false);
}
void Mapper303::write_expansion(Address addr, Byte value) {
    // NESdev r7779. A6/A3/A2 are sampled independently of the data bus.
    if ((addr & 0xFF00) == 0x4A00) {
        latch_ = ((addr >> 4) & 4) | ((addr >> 2) & 3);
    } else if ((addr & 0xFF00) == 0x5100) {
        bank_ = latch_;
    } else if (addr == 0x4020) {
        counter_ = (counter_ & 0xFF00) | value;
        irq_ = false;
    } else if (addr == 0x4021) {
        counter_ = (counter_ & 0x00FF) | (static_cast<std::uint16_t>(value) << 8);
        counting_ = true;
        irq_ = false;
    } else if (addr == 0x4025) {
        horizontal_ = (value & 8) != 0;
    }
}
void Mapper303::clock_cpu() {
    if (counting_ && --counter_ == 0) {
        irq_ = true;
        counting_ = false;
    }
}
bool Mapper303::irq_pending() const {
    return irq_;
}
void Mapper303::reset() {
    bank_ = latch_ = 0;
    counter_ = 0;
    counting_ = irq_ = horizontal_ = false;
}
void Mapper303::power_on() {
    reset();
    prg_ram_.fill(0);
    chr_ram_.fill(0);
}
void Mapper303::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(bank_);
    writer.write(latch_);
    writer.write(counter_);
    writer.write(static_cast<Byte>(horizontal_ | (counting_ << 1) | (irq_ << 2)));
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}
void Mapper303::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto bank = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto counter = reader.read<std::uint16_t>();
    const auto flags = reader.read<Byte>();
    decltype(prg_ram_) prg{};
    decltype(chr_ram_) chr{};
    reader.read_array(prg);
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || bank > 7 || latch > 7 || flags > 5) {
        reader.fail();
        return;
    }
    bank_ = bank;
    latch_ = latch;
    counter_ = counter;
    horizontal_ = (flags & 1) != 0;
    counting_ = (flags & 2) != 0;
    irq_ = (flags & 4) != 0;
    prg_ram_ = prg;
    chr_ram_ = chr;
}
} // namespace mapperbus::core
