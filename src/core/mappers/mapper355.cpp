#include "core/mappers/mapper355.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x8000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kPicRomBytes = 1024UZ;
constexpr std::uint16_t kStateTag = 0x3550U;
constexpr Byte kStatusMask = 0x3FU;
constexpr Byte kCarry = 0x01U;
constexpr Byte kDigitCarry = 0x02U;
constexpr Byte kZero = 0x04U;
constexpr Byte kPowerDown = 0x08U;
constexpr Byte kTimeOut = 0x10U;

} // namespace

// NESdev mapper 355 oldid 7933 defines fixed NROM-256 memory and the PIC pinout.
// RAM contents and output latches are deterministic at cold power; soft reset
// retains them while resetting the PIC's control state and releasing /IRQ.
Mapper355::Mapper355(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte>,
                     PicRom pic_rom)
    : prg_rom_(std::move(prg_rom)), pic_rom_(std::move(pic_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper355::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 355U) {
        return std::unexpected("Mapper 355 requires mapper ID 355");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 355 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize) {
        return std::unexpected("Mapper 355 requires exactly 32 KiB PRG ROM");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 355 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 355 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 355 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Result<Mapper355::PicRom> Mapper355::decode_pic_rom(std::span<const Byte> data) {
    if (data.size() != kPicRomBytes) {
        return std::unexpected("Mapper 355 requires exactly 1024 bytes of PIC16C54 firmware");
    }
    PicRom words{};
    for (std::size_t index = 0U; index < words.size(); ++index) {
        const auto low = static_cast<std::uint16_t>(data[index * 2U]);
        const auto high = static_cast<std::uint16_t>(data[index * 2U + 1U]);
        words[index] = static_cast<std::uint16_t>(low | (high << 8U));
        if (words[index] > 0x0FFFU) {
            return std::unexpected("Mapper 355 PIC firmware contains a non-12-bit word");
        }
    }
    return words;
}

Byte Mapper355::read_prg(Address addr) {
    return addr >= 0x8000U ? prg_rom_[addr & 0x7FFFU] : 0U;
}

void Mapper355::write_prg(Address, Byte) {}

bool Mapper355::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper355::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper355::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper355::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper355::mirror_mode() const {
    return mirror_;
}

void Mapper355::observe_cpu_read(Address addr, Byte) {
    pic_.address_pins = addr & 0x7FF0U;
}

void Mapper355::observe_cpu_write(Address addr, Byte) {
    pic_.address_pins = addr & 0x7FF0U;
}

Byte Mapper355::read_register(Byte addr) const {
    addr &= 0x1FU;
    if (addr == 0U) {
        const auto indirect = static_cast<Byte>(pic_.ram[4] & 0x1FU);
        return indirect == 0U ? 0U : read_register(indirect);
    }
    if (addr == 2U) {
        return static_cast<Byte>(pic_.pc);
    }
    if (addr == 5U) {
        const Byte inputs =
            static_cast<Byte>(1U | ((pic_.address_pins >> 5U) & 2U) |
                              ((pic_.address_pins >> 3U) & 4U) | ((pic_.address_pins >> 1U) & 8U));
        const auto mask = static_cast<Byte>(pic_.trisa & 0x0FU);
        return static_cast<Byte>(((pic_.ram[5] & ~mask) | (inputs & mask)) & 0x0FU);
    }
    if (addr == 6U) {
        Byte inputs = 0U;
        inputs |= static_cast<Byte>((pic_.address_pins >> 12U) & 1U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 6U) & 2U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 8U) & 4U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 8U) & 8U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 5U) & 0x10U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 3U) & 0x20U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 7U) & 0x40U);
        inputs |= static_cast<Byte>((pic_.address_pins >> 7U) & 0x80U);
        return static_cast<Byte>((pic_.ram[6] & ~pic_.trisb) | (inputs & pic_.trisb));
    }
    return pic_.ram[addr];
}

void Mapper355::set_pc(std::uint16_t value) {
    pic_.pc = value & 0x01FFU;
    pic_.ram[2] = static_cast<Byte>(pic_.pc);
}

void Mapper355::write_register(Byte addr, Byte value) {
    addr &= 0x1FU;
    if (addr == 0U) {
        const auto indirect = static_cast<Byte>(pic_.ram[4] & 0x1FU);
        if (indirect != 0U) {
            write_register(indirect, value);
        }
        return;
    }
    switch (addr) {
    case 1U:
        pic_.ram[1] = value;
        pic_.timer_divider = 0U;
        break;
    case 2U:
        set_pc(static_cast<std::uint16_t>(((pic_.ram[3] & 0x20U) << 3U) | value));
        break;
    case 3U:
        pic_.ram[3] = value & kStatusMask;
        break;
    case 4U:
        pic_.ram[4] = value & 0x1FU;
        break;
    case 5U:
        pic_.ram[5] = value & 0x0FU;
        break;
    default:
        pic_.ram[addr] = value;
        break;
    }
}

void Mapper355::write_result(Byte addr, Byte destination, Byte value) {
    if (destination == 0U) {
        pic_.w = value;
    } else {
        write_register(addr, value);
    }
}

void Mapper355::set_zero(Byte value) {
    pic_.ram[3] = static_cast<Byte>((pic_.ram[3] & ~kZero) | (value == 0U ? kZero : 0U));
}

void Mapper355::set_add_flags(unsigned left, unsigned right, Byte result) {
    const auto sum = left + right;
    const auto status = static_cast<Byte>(
        (pic_.ram[3] & ~(kCarry | kDigitCarry | kZero)) | (sum > 0xFFU ? kCarry : 0U) |
        (((left & 0x0FU) + (right & 0x0FU)) > 0x0FU ? kDigitCarry : 0U) |
        (result == 0U ? kZero : 0U));
    pic_.ram[3] = status;
}

void Mapper355::set_sub_flags(unsigned left, unsigned right, Byte result) {
    const auto status = static_cast<Byte>(
        (pic_.ram[3] & ~(kCarry | kDigitCarry | kZero)) | (left >= right ? kCarry : 0U) |
        ((left & 0x0FU) >= (right & 0x0FU) ? kDigitCarry : 0U) | (result == 0U ? kZero : 0U));
    pic_.ram[3] = status;
}

void Mapper355::skip_next() {
    set_pc(static_cast<std::uint16_t>(pic_.pc + 1U));
}

void Mapper355::push_stack(std::uint16_t value) {
    if (pic_.stack_depth == 2U) {
        pic_.stack[0] = pic_.stack[1];
        pic_.stack_depth = 1U;
    }
    pic_.stack[pic_.stack_depth] = value & 0x01FFU;
    ++pic_.stack_depth;
}

std::uint16_t Mapper355::pop_stack() {
    if (pic_.stack_depth == 0U) {
        return 0U;
    }
    --pic_.stack_depth;
    return pic_.stack[pic_.stack_depth];
}

void Mapper355::clock_timer() {
    if ((pic_.option & 0x28U) != 0U) {
        return;
    }
    const auto period = static_cast<std::uint16_t>(1U << (pic_.option & 7U));
    ++pic_.timer_divider;
    if (pic_.timer_divider >= period) {
        pic_.timer_divider = 0U;
        ++pic_.ram[1];
    }
}

void Mapper355::execute_instruction() {
    const auto opcode = pic_rom_[pic_.pc];
    set_pc(static_cast<std::uint16_t>(pic_.pc + 1U));

    if (opcode == 0x002U) {
        pic_.option = pic_.w;
        pic_.timer_divider = 0U;
    } else if (opcode == 0x003U) {
        pic_.ram[3] &= static_cast<Byte>(~kPowerDown);
        pic_.sleeping = true;
    } else if (opcode == 0x004U) {
        pic_.ram[3] |= kPowerDown | kTimeOut;
        pic_.timer_divider = 0U;
    } else if (opcode >= 0x006U && opcode <= 0x007U) {
        if ((opcode & 1U) == 0U) {
            pic_.trisa = pic_.w & 0x0FU;
        } else {
            pic_.trisb = pic_.w;
        }
    } else if ((opcode & 0x0FE0U) == 0x0020U) {
        write_register(static_cast<Byte>(opcode), pic_.w);
    } else if ((opcode & 0x0FE0U) == 0x0060U) {
        write_register(static_cast<Byte>(opcode), 0U);
        set_zero(0U);
    } else if ((opcode & 0x0FC0U) == 0x0040U) {
        pic_.w = 0U;
        set_zero(0U);
    } else {
        const auto file = static_cast<Byte>(opcode & 0x1FU);
        const auto destination = static_cast<Byte>((opcode >> 5U) & 1U);
        const auto value = read_register(file);
        switch (opcode & 0x0FC0U) {
        case 0x01C0U: {
            const auto result = static_cast<Byte>(value + pic_.w);
            set_add_flags(value, pic_.w, result);
            write_result(file, destination, result);
            break;
        }
        case 0x0140U: {
            const auto result = static_cast<Byte>(value & pic_.w);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        case 0x0240U: {
            const auto result = static_cast<Byte>(~value);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        case 0x00C0U: {
            const auto result = static_cast<Byte>(value - 1U);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        case 0x02C0U: {
            const auto result = static_cast<Byte>(value - 1U);
            write_result(file, destination, result);
            if (result == 0U) {
                skip_next();
            }
            break;
        }
        case 0x0280U: {
            const auto result = static_cast<Byte>(value + 1U);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        case 0x03C0U: {
            const auto result = static_cast<Byte>(value + 1U);
            write_result(file, destination, result);
            if (result == 0U) {
                skip_next();
            }
            break;
        }
        case 0x0100U: {
            const auto result = static_cast<Byte>(value | pic_.w);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        case 0x0200U:
            set_zero(value);
            write_result(file, destination, value);
            break;
        case 0x0340U: {
            const auto carry = static_cast<Byte>(pic_.ram[3] & kCarry);
            const auto result = static_cast<Byte>((value << 1U) | carry);
            pic_.ram[3] =
                static_cast<Byte>((pic_.ram[3] & ~kCarry) | ((value & 0x80U) != 0U ? kCarry : 0U));
            write_result(file, destination, result);
            break;
        }
        case 0x0300U: {
            const auto carry = static_cast<Byte>((pic_.ram[3] & kCarry) << 7U);
            const auto result = static_cast<Byte>((value >> 1U) | carry);
            pic_.ram[3] = static_cast<Byte>((pic_.ram[3] & ~kCarry) | (value & 1U));
            write_result(file, destination, result);
            break;
        }
        case 0x0080U: {
            const auto result = static_cast<Byte>(value - pic_.w);
            set_sub_flags(value, pic_.w, result);
            write_result(file, destination, result);
            break;
        }
        case 0x0380U: {
            const auto result = static_cast<Byte>((value >> 4U) | (value << 4U));
            write_result(file, destination, result);
            break;
        }
        case 0x0180U: {
            const auto result = static_cast<Byte>(value ^ pic_.w);
            set_zero(result);
            write_result(file, destination, result);
            break;
        }
        default:
            switch (opcode & 0x0F00U) {
            case 0x0400U:
            case 0x0500U:
            case 0x0600U:
            case 0x0700U: {
                const auto bit = static_cast<Byte>((opcode >> 5U) & 7U);
                const auto mask = static_cast<Byte>(1U << bit);
                if ((opcode & 0x0F00U) == 0x0400U) {
                    write_register(file, static_cast<Byte>(value & ~mask));
                } else if ((opcode & 0x0F00U) == 0x0500U) {
                    write_register(file, static_cast<Byte>(value | mask));
                } else {
                    const bool set = (value & mask) != 0U;
                    if (set == ((opcode & 0x0F00U) == 0x0700U)) {
                        skip_next();
                    }
                }
                break;
            }
            default:
                switch (opcode & 0x0F00U) {
                case 0x0800U:
                    pic_.w = static_cast<Byte>(opcode);
                    set_pc(pop_stack());
                    break;
                case 0x0900U:
                    push_stack(pic_.pc);
                    set_pc(static_cast<std::uint16_t>(opcode & 0x00FFU));
                    break;
                case 0x0A00U:
                case 0x0B00U:
                    set_pc(static_cast<std::uint16_t>(opcode & 0x01FFU));
                    break;
                case 0x0C00U:
                    pic_.w = static_cast<Byte>(opcode);
                    break;
                case 0x0D00U:
                    pic_.w |= static_cast<Byte>(opcode);
                    set_zero(pic_.w);
                    break;
                case 0x0E00U:
                    pic_.w &= static_cast<Byte>(opcode);
                    set_zero(pic_.w);
                    break;
                case 0x0F00U:
                    pic_.w ^= static_cast<Byte>(opcode);
                    set_zero(pic_.w);
                    break;
                default:
                    break;
                }
                break;
            }
            break;
        }
    }
    clock_timer();
}

void Mapper355::clock_cpu() {
    pic_.clock_divider = static_cast<Byte>((pic_.clock_divider + 1U) & 3U);
    if (pic_.clock_divider == 0U && !pic_.sleeping) {
        execute_instruction();
    }
}

bool Mapper355::irq_pending() const {
    return (pic_.trisa & 1U) == 0U && (pic_.ram[5] & 1U) == 0U;
}

void Mapper355::power_on() {
    pic_ = PicState{};
    chr_ram_.fill(0U);
    reset();
}

void Mapper355::reset() {
    const auto ram = pic_.ram;
    pic_ = PicState{};
    pic_.ram = ram;
    pic_.ram[3] = kPowerDown | kTimeOut;
    set_pc(0U);
}

std::uint32_t Mapper355::pic_identity() const {
    std::uint32_t hash = 0x811C9DC5U;
    for (const auto word : pic_rom_) {
        hash = (hash ^ (word & 0xFFU)) * 0x01000193U;
        hash = (hash ^ (word >> 8U)) * 0x01000193U;
    }
    return hash;
}

std::uint16_t Mapper355::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper355::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(pic_identity());
    writer.write(pic_.address_pins);
    writer.write(pic_.pc);
    writer.write(pic_.w);
    writer.write_array(pic_.ram);
    writer.write_array(pic_.stack);
    writer.write(pic_.stack_depth);
    writer.write(pic_.trisa);
    writer.write(pic_.trisb);
    writer.write(pic_.option);
    writer.write(pic_.timer_divider);
    writer.write(pic_.clock_divider);
    writer.write(static_cast<Byte>(pic_.sleeping));
    writer.write_array(chr_ram_);
}

void Mapper355::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto identity = reader.read<std::uint32_t>();
    PicState next;
    reader.read(next.address_pins);
    reader.read(next.pc);
    reader.read(next.w);
    reader.read_array(next.ram);
    reader.read_array(next.stack);
    reader.read(next.stack_depth);
    reader.read(next.trisa);
    reader.read(next.trisb);
    reader.read(next.option);
    reader.read(next.timer_divider);
    reader.read(next.clock_divider);
    const auto sleeping = reader.read<Byte>();
    next.sleeping = sleeping != 0U;
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_pc = next.pc <= 0x01FFU && next.stack[0] <= 0x01FFU &&
                          next.stack[1] <= 0x01FFU && next.stack_depth <= 2U;
    const bool valid_ram = (next.ram[3] & 0xC0U) == 0U && (next.ram[4] & 0xE0U) == 0U &&
                           (next.ram[5] & 0xF0U) == 0U && next.ram[2] == (next.pc & 0xFFU);
    const bool valid_pins = (next.address_pins & 0x7FF0U) == next.address_pins;
    const bool valid_clock = next.timer_divider < 128U && next.clock_divider < 4U && sleeping <= 1U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        identity != pic_identity() || next.trisa > 0x0FU || !valid_pc || !valid_ram ||
        !valid_pins || !valid_clock) {
        reader.fail();
        return;
    }

    pic_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
