#include "core/mappers/one_bus.hpp"

#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
// r12696 calls 3/7 invalid; zero-mask fallback follows NRS b685625's
// compatibility behavior. Do not interpret it as verified silicon wiring.
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
constexpr uint16_t kStateTag = 0xB100U;
} // namespace

OneBus::OneBus(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_(std::move(prg)), chr_(std::move(chr)) {}

Result<void> OneBus::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 256U || h.submapper != 0U)
        return std::unexpected("OneBus component requires NES 2.0 mapper 256 submapper 0");
    if (!std::has_single_bit(prg) || prg < 32768U || prg > 8388608U ||
        (chr != 0U && (!std::has_single_bit(chr) || chr < 8192U || chr > 4194304U)))
        return std::unexpected("Unsupported OneBus component ROM geometry");
    if (h.has_battery || h.prg_ram_size != 0U || h.prg_nvram_size != 0U || h.chr_ram_size != 0U ||
        h.chr_nvram_size != 0U)
        return std::unexpected("OneBus component has no external RAM or battery");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("OneBus component supports H/V CIRAM");
    return {};
}

Byte OneBus::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    auto slot = static_cast<unsigned>((addr - 0x8000U) / 8192U);
    // The detailed r23930 table swaps PQ0/PQ2 only; PQ1/FF stay fixed.
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U)
        slot ^= 2U;
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank =
        (inner & mask) | (registers_.middle & (255U ^ mask)) | ((registers_.outer >> 4U) << 8U);
    return prg_[(static_cast<std::size_t>(bank) * 8192U + (addr & 8191U)) & (prg_.size() - 1U)];
}

Byte OneBus::read_chr(Address addr) {
    auto slot = static_cast<unsigned>((addr & 8191U) / 1024U);
    if ((registers_.control & 0x80U) != 0U)
        slot ^= 4U;
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U) |
                          ((registers_.outer & 15U) << 11U);
    const auto& data = chr_.empty() ? prg_ : chr_;
    return data[(static_cast<std::size_t>(bank) * 1024U + (addr & 1023U)) & (data.size() - 1U)];
}

MirrorMode OneBus::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
std::optional<Address> OneBus::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x7FFU)
               : std::nullopt;
}
bool OneBus::maps_expansion(Address addr) const {
    return addr >= 0x4100U && addr <= 0x410BU;
}
void OneBus::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr))
        write_system(addr & 15U, value);
}
void OneBus::write_system(unsigned reg, Byte value) {
    switch (reg) {
    case 0U:
        registers_.outer = value;
        break;
    case 1U:
        registers_.latch = value;
        break;
    case 2U:
        registers_.counter = 0U;
        break; // Reload on next selected clock.
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
std::optional<Byte> OneBus::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}
bool OneBus::write_ppu_register(Address addr, Byte value) {
    if (addr < 0x2010U || addr > 0x201FU)
        return false;
    if (addr >= 0x2012U && addr <= 0x2017U)
        registers_.chr[addr - 0x2012U] = value;
    else if (addr == 0x2018U)
        registers_.intermediate = value & 0x70U;
    else if (addr == 0x201AU)
        registers_.chr_middle = value;
    // Remaining native graphics controls are reserved for console integration.
    return true;
}
void OneBus::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U || (registers_.mode & 8U) != 0U)
        return;
    switch (addr & 0xE001U) {
    case 0x8000U:
        // Bit 5 is not forwarded. Preserve native CIRAM selection as policy;
        // NRS clears it instead, a distinction requiring hardware verification.
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
        break; // A001 has no native WRAM register to receive the write.
    }
}
void OneBus::reset() {
    registers_ = Registers{};
}
bool OneBus::irq_pending() const {
    return registers_.pending;
}
void OneBus::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled)
        registers_.pending = true;
}
void OneBus::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U)
        ++registers_.low_cycles;
}
void OneBus::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U)
        clock_counter();
    registers_.a12 = high;
    if (high)
        registers_.low_cycles = 0U;
}
void OneBus::on_ppu_hblank() {
    // One pulse per scanline, independent of rendering. NRS b685625 gates IRQ
    // visibility on rendering; this timer policy still needs hardware checks.
    if ((registers_.mode & 0x80U) != 0U)
        clock_counter();
}
void OneBus::save_state(StateWriter& w) const {
    w.write(kStateTag);
    w.write<uint32_t>(static_cast<uint32_t>(prg_.size()));
    w.write<uint32_t>(static_cast<uint32_t>(chr_.size()));
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
}
void OneBus::load_state(StateReader& r) {
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
    if (!r.ok() || tag != kStateTag || prg != prg_.size() || chr != chr_.size() ||
        (next.control & ~0xE7U) != 0U || next.mirror > 1U || (next.mode & ~0xCFU) != 0U ||
        (next.intermediate & ~0x70U) != 0U || irq > 3U || irq == 2U || pins > 6U ||
        ((pins & 1U) != 0U && pins != 1U)) {
        r.fail();
        return;
    }
    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    registers_ = next;
}

} // namespace mapperbus::core
