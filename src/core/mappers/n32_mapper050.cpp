#include "core/mappers/n32_mapper050.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::uint16_t kIrqThreshold = 0x1000U;
constexpr Address kRegisterMask = 0x4120U;
constexpr Address kPrgRegister = 0x4020U;
constexpr Address kIrqRegister = 0x4120U;

bool is_register_address(Address addr) {
    return addr >= 0x4020U && addr <= 0x5FFFU;
}
} // namespace

N32Mapper050::N32Mapper050(const INesHeader& header,
                           std::vector<Byte> prg_rom,
                           std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(header.mirror_mode) {}

Result<void> N32Mapper050::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 050 supports submapper 0");
    if (prg_size != 131072U || chr_size != 8192U)
        return std::unexpected("Mapper 050 requires 128 KiB PRG and 8 KiB CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 050 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 050 requires fixed H/V mirroring");
    return {};
}

Byte N32Mapper050::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;

    unsigned bank = 0U;
    if (addr < 0x8000U)
        bank = 0x0FU;
    else if (addr < 0xA000U)
        bank = 8U;
    else if (addr < 0xC000U)
        bank = 9U;
    else if (addr < 0xE000U)
        bank = prg_bank_;
    else
        bank = 0x0BU;
    return prg_rom_[bank * kPrgBankSize + (addr & 0x1FFFU)];
}

void N32Mapper050::write_prg(Address, Byte) {}

bool N32Mapper050::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte N32Mapper050::read_chr(Address addr) {
    return chr_rom_[addr & 0x1FFFU];
}

void N32Mapper050::write_chr(Address, Byte) {}

MirrorMode N32Mapper050::mirror_mode() const {
    return mirror_;
}

bool N32Mapper050::maps_expansion(Address addr) const {
    return is_register_address(addr);
}

Byte N32Mapper050::expansion_read_mask(Address addr) const {
    return is_register_address(addr) ? 0U : 0xFFU;
}

Byte N32Mapper050::read_expansion(Address) {
    return 0U;
}

void N32Mapper050::write_expansion(Address addr, Byte value) {
    if (!is_register_address(addr))
        return;

    switch (addr & kRegisterMask) {
    case kPrgRegister:
        prg_bank_ =
            static_cast<Byte>((value & 0x08U) | ((value & 0x06U) >> 1U) | ((value & 0x01U) << 2U));
        break;
    case kIrqRegister:
        if ((value & 1U) != 0U) {
            irq_enabled_ = 1U;
        } else {
            irq_enabled_ = 0U;
            irq_counter_ = 0U;
        }
        break;
    default:
        break;
    }
}

void N32Mapper050::reset() {
    prg_bank_ = 0U;
    irq_counter_ = 0U;
    irq_enabled_ = 0U;
}

void N32Mapper050::clock_cpu() {
    if (irq_enabled_ != 0U && irq_counter_ < kIrqThreshold)
        ++irq_counter_;
}

bool N32Mapper050::irq_pending() const {
    return irq_counter_ == kIrqThreshold;
}

std::uint16_t N32Mapper050::state_tag() const {
    return static_cast<std::uint16_t>(0x3200U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void N32Mapper050::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write(irq_counter_);
    writer.write(irq_enabled_);
}

void N32Mapper050::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_enabled = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || prg_bank > 0x0FU || irq_counter > kIrqThreshold ||
        irq_enabled > 1U || (irq_enabled == 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    irq_counter_ = irq_counter;
    irq_enabled_ = irq_enabled;
}

} // namespace mapperbus::core
