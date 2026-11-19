#include "core/mappers/mapper270.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
constexpr uint16_t kStateTag = 0x2700U;
constexpr std::size_t kMaxBaseOneBusBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaxOuterOneBusBytes = 64U * 1024U * 1024U;

} // namespace

Mapper270::Mapper270(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wram_(header.prg_ram_size),
      has_chr_ram_(header.chr_ram_size != 0U), submapper_(header.submapper) {
    reset();
}

Result<void> Mapper270::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!h.is_nes2 || h.mapper_number != 270U || h.submapper > 3U)
        return std::unexpected("Mapper 270 requires NES 2.0 submapper 0, 1, 2 or 3");
    const auto max_prg_size =
        h.submapper == 0U || h.submapper == 2U ? kMaxOuterOneBusBytes : kMaxBaseOneBusBytes;
    if (!std::has_single_bit(prg_size) || prg_size < 32768U || prg_size > max_prg_size ||
        chr_size != 0U)
        return std::unexpected(
            "Mapper 270 requires power-of-two shared PRG ROM up to its A25 envelope");
    const auto chr_ram = h.submapper == 2U ? 8192U : 0U;
    if (h.has_battery ||
        (h.prg_ram_size != 0U && !(h.console_type == 6U && h.prg_ram_size == 8192U)) ||
        h.prg_nvram_size != 0U ||
        (h.chr_ram_size != chr_ram &&
         !(h.console_type == 6U && h.submapper == 0U && h.chr_ram_size == 8192U)) ||
        h.chr_nvram_size != 0U)
        return std::unexpected("Mapper 270 memory population does not match its submapper");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 270 supports H/V CIRAM");
    return {};
}

Byte Mapper270::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U && !wram_.empty())
        return wram_[addr & 0x1FFFU];
    if (addr < 0x8000U)
        return 0U;
    auto slot = static_cast<unsigned>((addr - 0x8000U) / 8192U);
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U)
        slot ^= 2U;
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank =
        (inner & mask) | (registers_.middle & (255U ^ mask)) | ((registers_.outer >> 4U) << 8U);
    const auto decoded = static_cast<std::size_t>(bank) * 8192U + (addr & 8191U);
    const auto offset = (decoded & ~0x3000000UZ) | outer_address_bits();
    return prg_rom_[offset & (prg_rom_.size() - 1U)];
}

Byte Mapper270::read_chr(Address addr) {
    if (registers_.chr_ram_enabled)
        return chr_ram_[addr & 8191U];
    auto slot = static_cast<unsigned>((addr & 8191U) / 1024U);
    if ((registers_.control & 0x80U) != 0U)
        slot ^= 4U;
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U) |
                          ((registers_.outer & 15U) << 11U);
    const auto decoded = static_cast<std::size_t>(bank) * 1024U + (addr & 1023U);
    const auto offset = (decoded & ~0x3000000UZ) | outer_address_bits();
    return prg_rom_[offset & (prg_rom_.size() - 1U)];
}

void Mapper270::write_chr(Address addr, Byte value) {
    if (registers_.chr_ram_enabled)
        chr_ram_[addr & 8191U] = value;
}

MirrorMode Mapper270::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper270::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x7FFU)
               : std::nullopt;
}

bool Mapper270::maps_expansion(Address addr) const {
    return (addr >= 0x4100U && addr <= 0x410BU) || addr == 0x412BU || addr == 0x412CU ||
           (has_chr_ram_ && addr == 0x4242U);
}

Byte Mapper270::expansion_read_mask(Address addr) const {
    return submapper_ == 2U && addr == 0x412CU && (registers_.uio_direction & 0x10U) == 0U ? 0x10U
                                                                                           : 0U;
}

Byte Mapper270::read_expansion(Address addr) {
    return expansion_read_mask(addr) != 0U && jumper_inserted_ ? 0x10U : 0U;
}

void Mapper270::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4100U && addr <= 0x410BU) {
        write_system(addr & 15U, value);
    } else if (addr == 0x412BU) {
        registers_.uio_direction = value;
    } else if (addr == 0x412CU) {
        const auto writable = static_cast<Byte>(bank_bits() & registers_.uio_direction);
        registers_.uio_output =
            static_cast<Byte>((registers_.uio_output & ~bank_bits()) | (value & writable));
    } else if (has_chr_ram_ && addr == 0x4242U) {
        registers_.chr_ram_enabled = (value & 1U) != 0U;
    }
}

void Mapper270::write_system(unsigned reg, Byte value) {
    switch (reg) {
    case 0U:
        registers_.outer = value;
        break;
    case 1U:
        registers_.latch = value;
        break;
    case 2U:
        registers_.counter = 0U;
        break;
    case 3U:
        registers_.enabled = registers_.pending = false;
        break;
    case 4U:
        registers_.enabled = true;
        break;
    case 5U:
        registers_.control = value & 0xE7U;
        break;
    case 6U:
        registers_.mirror = value & 1U;
        break;
    case 7U:
    case 8U:
    case 9U:
        registers_.prg[reg - 7U] = value;
        break;
    case 10U:
        registers_.middle = value;
        break;
    case 11U:
        registers_.mode = value & 0xCFU;
        break;
    default:
        break;
    }
}

std::optional<Byte> Mapper270::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}

bool Mapper270::write_ppu_register(Address addr, Byte value) {
    if (addr < 0x2010U || addr > 0x201FU)
        return false;
    if (addr >= 0x2012U && addr <= 0x2017U)
        registers_.chr[addr - 0x2012U] = value;
    else if (addr == 0x2018U)
        registers_.intermediate = value & 0x70U;
    else if (addr == 0x201AU)
        registers_.chr_middle = value;
    return true;
}

void Mapper270::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U && !wram_.empty())
        wram_[addr & 0x1FFFU] = value;
    if (addr < 0x8000U || (registers_.mode & 8U) != 0U)
        return;
    switch (addr & 0xE001U) {
    case 0x8000U:
        write_system(5U, static_cast<Byte>((value & 0xDFU) | (registers_.control & 0x20U)));
        break;
    case 0x8001U: {
        const unsigned index = registers_.control & 7U;
        if (index < 6U)
            registers_.chr[index < 2U ? index + 4U : index - 2U] = value;
        else
            registers_.prg[index - 6U] = value;
        break;
    }
    case 0xA000U:
        write_system(6U, value);
        break;
    case 0xC000U:
        write_system(1U, value);
        break;
    case 0xC001U:
        write_system(2U, value);
        break;
    case 0xE000U:
        write_system(3U, value);
        break;
    case 0xE001U:
        write_system(4U, value);
        break;
    default:
        break;
    }
}

Result<void> Mapper270::configure_board_jumpers(Byte value) {
    if (submapper_ != 2U || value > 1U)
        return std::unexpected("Only mapper 270 submapper 2 exposes a jumper-cartridge input");
    jumper_inserted_ = value != 0U;
    return {};
}

void Mapper270::power_on() {
    std::ranges::fill(wram_, 0U);
    reset();
    chr_ram_.fill(0U);
}

void Mapper270::reset() {
    registers_ = Registers{};
}

bool Mapper270::irq_pending() const {
    return registers_.pending;
}

void Mapper270::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled)
        registers_.pending = true;
}

void Mapper270::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U)
        ++registers_.low_cycles;
}

void Mapper270::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U)
        clock_counter();
    registers_.a12 = high;
    if (high)
        registers_.low_cycles = 0U;
}

void Mapper270::on_ppu_hblank() {
    if ((registers_.mode & 0x80U) != 0U)
        clock_counter();
}

Byte Mapper270::bank_bits() const {
    switch (submapper_) {
    case 0U:
        return 0x07U;
    case 1U:
        return 0x02U;
    case 2U:
        return 0x03U;
    default:
        return 0x04U;
    }
}

std::size_t Mapper270::outer_address_bits() const {
    const bool a24 =
        (registers_.uio_output & 0x02U) != 0U ||
        ((submapper_ == 0U || submapper_ == 3U) && (registers_.uio_output & 0x04U) != 0U);
    const bool a25 =
        (submapper_ == 0U || submapper_ == 2U) && (registers_.uio_output & 0x01U) != 0U;
    return (a24 ? 0x1000000U : 0U) | (a25 ? 0x2000000U : 0U);
}

uint16_t Mapper270::state_tag() const {
    return static_cast<uint16_t>(kStateTag | submapper_ | (!wram_.empty() ? 0x40U : 0U) |
                                 (has_chr_ram_ && submapper_ == 0U ? 0x80U : 0U) |
                                 (jumper_inserted_ ? 0x10U : 0U));
}

void Mapper270::save_state(StateWriter& w) const {
    w.write(state_tag());
    w.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    w.write<uint32_t>(0U);
    w.write(registers_.outer);
    w.write(registers_.control);
    w.write(registers_.mirror);
    w.write_array(registers_.prg);
    w.write(registers_.middle);
    w.write(registers_.mode);
    w.write_array(registers_.chr);
    w.write(registers_.intermediate);
    w.write(registers_.chr_middle);
    w.write(registers_.latch);
    w.write(registers_.counter);
    w.write<Byte>((registers_.enabled ? 1U : 0U) | (registers_.pending ? 2U : 0U));
    w.write<Byte>((registers_.a12 ? 1U : 0U) | (registers_.low_cycles << 1U));
    w.write(registers_.uio_direction);
    w.write(registers_.uio_output);
    w.write<Byte>(registers_.chr_ram_enabled ? 1U : 0U);
    if (!wram_.empty())
        w.write_bytes(wram_);
    if (has_chr_ram_)
        w.write_array(chr_ram_);
}

void Mapper270::load_state(StateReader& r) {
    const auto tag = r.read<uint16_t>();
    const auto prg = r.read<uint32_t>(), chr = r.read<uint32_t>();
    Registers next;
    r.read(next.outer);
    r.read(next.control);
    r.read(next.mirror);
    r.read_array(next.prg);
    r.read(next.middle);
    r.read(next.mode);
    r.read_array(next.chr);
    r.read(next.intermediate);
    r.read(next.chr_middle);
    r.read(next.latch);
    r.read(next.counter);
    const auto irq = r.read<Byte>(), pins = r.read<Byte>();
    r.read(next.uio_direction);
    r.read(next.uio_output);
    const auto chr_select = r.read<Byte>();
    std::array<Byte, 0x2000> chr_ram{};
    std::vector<Byte> wram;
    if (!wram_.empty())
        r.read_bytes(wram, wram_.size());
    if (has_chr_ram_)
        r.read_array(chr_ram);
    if (!r.ok() || tag != state_tag() || prg != prg_rom_.size() || chr != 0U ||
        (next.control & ~0xE7U) != 0U || next.mirror > 1U || (next.mode & ~0xCFU) != 0U ||
        (next.intermediate & ~0x70U) != 0U || irq > 3U || irq == 2U || pins > 6U ||
        ((pins & 1U) != 0U && pins != 1U) || (next.uio_output & ~bank_bits()) != 0U ||
        chr_select > 1U || (!has_chr_ram_ && chr_select != 0U) || wram.size() != wram_.size()) {
        r.fail();
        return;
    }
    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    next.chr_ram_enabled = chr_select != 0U;
    registers_ = next;
    wram_.swap(wram);
    if (has_chr_ram_)
        chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
