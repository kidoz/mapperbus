#include "core/mappers/racermate168.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Racermate168::Racermate168(const INesHeader& header,
                           std::vector<Byte> prg_rom,
                           const std::vector<Byte>&)
    : prg_rom_(std::move(prg_rom)),
      all_battery_(header.is_nes2 && header.chr_nvram_size == 65536U) {
    power_on();
}

Result<void> Racermate168::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 168 supports submapper 0");
    if (prg_size != 65536U || chr_size != 0U)
        return std::unexpected("Mapper 168 requires 64 KiB PRG ROM and no CHR ROM");
    if (!header.has_battery)
        return std::unexpected("Mapper 168 requires battery-backed CHR RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 168 requires hardwired vertical mirroring");
    if (header.is_nes2) {
        const bool factory = header.chr_ram_size == 32768U && header.chr_nvram_size == 32768U;
        const bool modified = header.chr_ram_size == 0U && header.chr_nvram_size == 65536U;
        if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || (!factory && !modified))
            return std::unexpected(
                "Mapper 168 requires 32+32 KiB or 0+64 KiB CHR RAM/NVRAM and no CPU RAM");
    }
    return {};
}

Byte Racermate168::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const unsigned bank = addr < 0xC000U ? (bank_select_ >> 6U) : 3U;
    return prg_rom_[bank * 16384U + (addr & 0x3FFFU)];
}

void Racermate168::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (addr < 0xC000U) {
        bank_select_ = value & 0xCFU;
        return;
    }
    const bool disabled = (value & 4U) != 0U;
    if (irq_disabled_ && !disabled)
        ram_protected_ = false;
    irq_disabled_ = disabled;
    if (irq_disabled_)
        irq_counter_ = 0U;
}

std::size_t Racermate168::chr_offset(Address addr) const {
    const unsigned logical_bank = (addr & 0x1000U) == 0U ? 0U : (bank_select_ & 15U);
    const unsigned physical_bank = (0U - logical_bank) & 15U;
    return physical_bank * 4096U + (addr & 0x0FFFU);
}

bool Racermate168::persistent_offset(std::size_t offset) const {
    return all_battery_ || offset >= 32768U;
}

Byte Racermate168::read_chr(Address addr) {
    const auto offset = chr_offset(addr);
    if (ram_protected_ && persistent_offset(offset))
        // Match the core's established undriven external-PPU-bus convention.
        return static_cast<Byte>(addr);
    return chr_ram_[offset];
}

void Racermate168::write_chr(Address addr, Byte value) {
    const auto offset = chr_offset(addr);
    if (!ram_protected_ || !persistent_offset(offset))
        chr_ram_[offset] = value;
}

MirrorMode Racermate168::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Racermate168::reset() {}

void Racermate168::power_on() {
    bank_select_ = 0U;
    irq_counter_ = 0U;
    irq_disabled_ = false;
    ram_protected_ = true;
    if (!all_battery_)
        std::ranges::fill_n(chr_ram_.begin(), 32768U, 0U);
}

bool Racermate168::irq_pending() const {
    return (irq_counter_ & 0x0400U) != 0U;
}

void Racermate168::clock_cpu() {
    if (!irq_disabled_)
        irq_counter_ = static_cast<uint16_t>((irq_counter_ + 1U) & 0x0FFFU);
}

std::span<const Byte> Racermate168::battery_ram() const {
    return all_battery_ ? std::span<const Byte>{chr_ram_}
                        : std::span<const Byte>{chr_ram_}.subspan(32768U);
}

void Racermate168::set_battery_ram(std::span<const Byte> data) {
    if (data.size() != battery_ram().size())
        return;
    std::ranges::copy(data, all_battery_ ? chr_ram_.begin() : chr_ram_.begin() + 32768U);
}

Result<void> Racermate168::configure_board_jumpers(Byte value) {
    if (value > 1U)
        return std::unexpected("Mapper 168 jumpers are 0=J2+J3 and 1=J1+J4");
    all_battery_ = value != 0U;
    return {};
}

uint16_t Racermate168::state_tag() const {
    return static_cast<uint16_t>(0xA800U | (all_battery_ ? 1U : 0U));
}

void Racermate168::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_select_);
    const Byte flags = static_cast<Byte>((ram_protected_ ? 1U : 0U) | (irq_disabled_ ? 2U : 0U));
    writer.write(flags);
    writer.write(irq_counter_);
    writer.write_array(chr_ram_);
}

void Racermate168::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank = reader.read<Byte>();
    const auto flags = reader.read<Byte>();
    const auto counter = reader.read<uint16_t>();
    std::array<Byte, 65536> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (bank & 0x30U) != 0U || flags > 3U ||
        counter > 0x0FFFU || ((flags & 2U) != 0U && counter != 0U)) {
        reader.fail();
        return;
    }
    bank_select_ = bank;
    ram_protected_ = (flags & 1U) != 0U;
    irq_disabled_ = (flags & 2U) != 0U;
    irq_counter_ = counter;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
