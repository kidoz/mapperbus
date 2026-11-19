#include "core/mappers/mapper043.hpp"

#include <array>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr Address kRegisterMask = 0x71FFU;
constexpr Address kPrgBankRegister = 0x4022U;
constexpr Address kTonyIrqRegister = 0x4122U;
constexpr Address kYsIrqRegister = 0x8122U;
constexpr std::size_t kSmallPrgOffset = 0x10000U;
constexpr std::size_t kFixedPrgOffset = 0x12000U;
constexpr std::array kPrgBanks = {4U, 3U, 4U, 4U, 4U, 7U, 5U, 6U};

bool is_register(Address addr, Address reg) {
    return (addr & kRegisterMask) == (reg & kRegisterMask);
}

bool is_irq_register(Address addr) {
    return is_register(addr, kTonyIrqRegister) || is_register(addr, kYsIrqRegister);
}

} // namespace

// Original implementation from NESdev mapper 043 revision 4258.
Mapper043::Mapper043(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(header.mirror_mode) {}

Result<void> Mapper043::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 043 supports submapper 0");
    if (prg_size != 0x14000U || chr_size != 0x2000U)
        return std::unexpected("Mapper 043 requires packed 80 KiB PRG and 8 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 043 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 043 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper043::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;

    std::size_t offset = 0U;
    if (addr < 0x8000U)
        offset = 2U * 0x2000U + (addr & 0x1FFFU);
    else if (addr < 0xA000U)
        offset = 0x2000U + (addr & 0x1FFFU);
    else if (addr < 0xC000U)
        offset = addr & 0x1FFFU;
    else if (addr < 0xE000U)
        offset = kPrgBanks[prg_bank_] * 0x2000U + (addr & 0x1FFFU);
    else
        offset = kFixedPrgOffset + (addr & 0x1FFFU);
    return prg_rom_[offset];
}

void Mapper043::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U)
        write_register(addr, value);
}

bool Mapper043::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

bool Mapper043::maps_expansion(Address addr) const {
    return addr >= 0x4020U && addr < 0x6000U &&
           (addr >= 0x5000U || is_register(addr, kPrgBankRegister) || is_irq_register(addr));
}

Byte Mapper043::expansion_read_mask(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U ? 0xFFU : 0U;
}

Byte Mapper043::read_expansion(Address addr) {
    return addr >= 0x5000U && addr < 0x6000U ? prg_rom_[kSmallPrgOffset + (addr & 0x07FFU)] : 0U;
}

void Mapper043::write_expansion(Address addr, Byte value) {
    write_register(addr, value);
}

Byte Mapper043::read_chr(Address addr) {
    return chr_rom_[addr & 0x1FFFU];
}

void Mapper043::write_chr(Address, Byte) {}

MirrorMode Mapper043::mirror_mode() const {
    return mirror_;
}

void Mapper043::reset() {
    prg_bank_ = 0U;
    irq_counter_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Mapper043::irq_pending() const {
    return irq_pending_;
}

void Mapper043::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper043::clock_cpu() {
    if (!irq_enabled_)
        return;
    if (irq_counter_ == 0x0FFFU) {
        irq_counter_ = 0U;
        irq_pending_ = true;
    } else {
        ++irq_counter_;
    }
}

void Mapper043::write_register(Address addr, Byte value) {
    if (is_register(addr, kPrgBankRegister)) {
        prg_bank_ = value & 7U;
        return;
    }
    if (!is_irq_register(addr))
        return;
    irq_enabled_ = (value & 1U) != 0U;
    if (!irq_enabled_) {
        irq_pending_ = false;
        irq_counter_ = 0U;
    }
}

uint16_t Mapper043::state_tag() const {
    return static_cast<uint16_t>(0x2B00U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper043::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write(irq_counter_);
    writer.write(static_cast<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U)));
}

void Mapper043::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto irq_counter = reader.read<uint16_t>();
    const auto irq_flags = reader.read<Byte>();
    const bool enabled = (irq_flags & 1U) != 0U;
    const bool pending = (irq_flags & 2U) != 0U;
    if (!reader.ok() || tag != state_tag() || prg_bank > 7U || irq_counter > 0x0FFFU ||
        (irq_flags & ~3U) != 0U || (pending && !enabled)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    irq_counter_ = irq_counter;
    irq_enabled_ = enabled;
    irq_pending_ = pending;
}

} // namespace mapperbus::core
