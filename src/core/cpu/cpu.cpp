#include "core/cpu/cpu.hpp"

#include <cstdlib>

#include "core/bus/memory_bus.hpp"
#include "core/logger.hpp"

namespace mapperbus::core {

Cpu::Cpu(MemoryBus& bus) : bus_(bus) {}

void Cpu::reset() {
    a_ = 0;
    x_ = 0;
    y_ = 0;
    sp_ = 0xFD;
    status_ = 0x24;
    total_cycles_ = 0;

    // Read reset vector at $FFFC-$FFFD
    Byte lo = bus_.read(0xFFFC);
    Byte hi = bus_.read(0xFFFD);
    pc_ = static_cast<Word>((hi << 8) | lo);
}

Byte Cpu::read(Address addr) const {
    return bus_.read(addr);
}

void Cpu::write(Address addr, Byte value) {
    bus_.write(addr, value);
}

Byte Cpu::fetch_byte() {
    return read(pc_++);
}

Word Cpu::fetch_word() {
    Byte lo = fetch_byte();
    Byte hi = fetch_byte();
    return static_cast<Word>((hi << 8) | lo);
}

Word Cpu::read_word(Address addr) const {
    Byte lo = read(addr);
    Byte hi = read(addr + 1);
    return static_cast<Word>((hi << 8) | lo);
}

Word Cpu::read_word_zero_page(Byte addr) const {
    Byte lo = read(addr);
    Byte hi = read(static_cast<Byte>(addr + 1));
    return static_cast<Word>((hi << 8) | lo);
}

Word Cpu::read_word_indirect_bug(Address addr) const {
    Byte lo = read(addr);
    Address hi_addr = static_cast<Address>((addr & 0xFF00) | static_cast<Byte>(addr + 1));
    Byte hi = read(hi_addr);
    return static_cast<Word>((hi << 8) | lo);
}

void Cpu::push(Byte value) {
    write(static_cast<Address>(0x0100 | sp_), value);
    --sp_;
}

Byte Cpu::pop() {
    ++sp_;
    return read(static_cast<Address>(0x0100 | sp_));
}

void Cpu::push_word(Word value) {
    push(static_cast<Byte>(value >> 8));
    push(static_cast<Byte>(value & 0x00FF));
}

Word Cpu::pop_word() {
    Byte lo = pop();
    Byte hi = pop();
    return static_cast<Word>((hi << 8) | lo);
}

bool Cpu::get_flag(StatusFlag flag) const {
    return (status_ & flag) != 0;
}

void Cpu::set_flag(StatusFlag flag, bool value) {
    if (value) {
        status_ |= flag;
    } else {
        status_ &= ~flag;
    }
}

void Cpu::set_zero_negative(Byte value) {
    set_flag(Zero, value == 0);
    set_flag(Negative, (value & 0x80) != 0);
}

void Cpu::service_interrupt(Word vector, bool set_break_flag) {
    push_word(pc_);
    Byte pushed_status = static_cast<Byte>(status_ | Unused);
    if (set_break_flag) {
        pushed_status |= Break;
    } else {
        pushed_status &= ~Break;
    }
    push(pushed_status);
    set_flag(InterruptDisable, true);
    pc_ = read_word(vector);
}

void Cpu::adc(Byte value) {
    uint16_t sum = static_cast<uint16_t>(a_) + value + (get_flag(Carry) ? 1u : 0u);
    Byte result = static_cast<Byte>(sum & 0x00FF);

    set_flag(Carry, sum > 0x00FF);
    set_flag(Overflow, ((a_ ^ result) & (value ^ result) & 0x80) != 0);

    a_ = result;
    set_zero_negative(a_);
}

void Cpu::sbc(Byte value) {
    adc(static_cast<Byte>(value ^ 0xFF));
}

void Cpu::compare(Byte lhs, Byte rhs) {
    uint16_t result = static_cast<uint16_t>(lhs) - rhs;
    set_flag(Carry, lhs >= rhs);
    set_zero_negative(static_cast<Byte>(result & 0x00FF));
}

Byte Cpu::asl(Byte value) {
    set_flag(Carry, (value & 0x80) != 0);
    value = static_cast<Byte>(value << 1);
    set_zero_negative(value);
    return value;
}

Byte Cpu::lsr(Byte value) {
    set_flag(Carry, (value & 0x01) != 0);
    value = static_cast<Byte>(value >> 1);
    set_zero_negative(value);
    return value;
}

Byte Cpu::rol(Byte value) {
    bool carry_in = get_flag(Carry);
    set_flag(Carry, (value & 0x80) != 0);
    value = static_cast<Byte>((value << 1) | (carry_in ? 1 : 0));
    set_zero_negative(value);
    return value;
}

Byte Cpu::ror(Byte value) {
    bool carry_in = get_flag(Carry);
    set_flag(Carry, (value & 0x01) != 0);
    value = static_cast<Byte>((value >> 1) | (carry_in ? 0x80 : 0x00));
    set_zero_negative(value);
    return value;
}

Cpu::AddressResult Cpu::absolute_indexed(Byte index) const {
    Word base = read_word(pc_);
    Word address = static_cast<Word>(base + index);
    return {.address = address, .page_crossed = (base & 0xFF00) != (address & 0xFF00)};
}

Cpu::AddressResult Cpu::indirect_indexed(Byte zero_page_addr) const {
    Word base = read_word_zero_page(zero_page_addr);
    Word address = static_cast<Word>(base + y_);
    return {.address = address, .page_crossed = (base & 0xFF00) != (address & 0xFF00)};
}

uint32_t Cpu::step() {
    if (bus_.poll_nmi()) {
        service_interrupt(0xFFFA, false);
        total_cycles_ += 7;
        return 7;
    }

    if (!get_flag(InterruptDisable) && bus_.poll_irq()) {
        service_interrupt(0xFFFE, false);
        total_cycles_ += 7;
        return 7;
    }

    auto zero_page = [this]() -> Address { return fetch_byte(); };
    auto zero_page_x = [this]() -> Address { return static_cast<Byte>(fetch_byte() + x_); };
    auto zero_page_y = [this]() -> Address { return static_cast<Byte>(fetch_byte() + y_); };
    auto absolute = [this]() -> Address { return fetch_word(); };
    auto absolute_x = [this]() -> AddressResult {
        Word base = fetch_word();
        Word address = static_cast<Word>(base + x_);
        return {.address = address, .page_crossed = (base & 0xFF00) != (address & 0xFF00)};
    };
    auto absolute_y = [this]() -> AddressResult {
        Word base = fetch_word();
        Word address = static_cast<Word>(base + y_);
        return {.address = address, .page_crossed = (base & 0xFF00) != (address & 0xFF00)};
    };
    auto indirect_x = [this]() -> Address {
        Byte base = fetch_byte();
        Byte ptr = static_cast<Byte>(base + x_);
        return read_word_zero_page(ptr);
    };
    auto indirect_y = [this]() -> AddressResult {
        Byte base = fetch_byte();
        return indirect_indexed(base);
    };
    auto branch = [this](bool condition) -> uint32_t {
        int8_t offset = static_cast<int8_t>(fetch_byte());
        if (!condition) {
            return 2;
        }
        Word previous = pc_;
        pc_ = static_cast<Word>(pc_ + offset);
        return 2 + 1 + (((previous & 0xFF00) != (pc_ & 0xFF00)) ? 1 : 0);
    };

    Byte opcode = fetch_byte();
    uint32_t cycles = 0;

    switch (opcode) {
    case 0x00:
        ++pc_;
        service_interrupt(0xFFFE, true);
        cycles = 7;
        break;
    case 0x01: {
        a_ |= read(indirect_x());
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x05:
        a_ |= read(zero_page());
        set_zero_negative(a_);
        cycles = 3;
        break;
    case 0x06: {
        Address addr = zero_page();
        Byte value = asl(read(addr));
        write(addr, value);
        cycles = 5;
        break;
    }
    case 0x08:
        push(static_cast<Byte>(status_ | Break | Unused));
        cycles = 3;
        break;
    case 0x09:
        a_ |= fetch_byte();
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0x0A:
        a_ = asl(a_);
        cycles = 2;
        break;
    case 0x0C:
        pc_ += 2;
        cycles = 4;
        break;
    case 0x0D:
        a_ |= read(absolute());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x0E: {
        Address addr = absolute();
        Byte value = asl(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x10:
        cycles = branch(!get_flag(Negative));
        break;
    case 0x11: {
        auto addr = indirect_y();
        a_ |= read(addr.address);
        set_zero_negative(a_);
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x14:
    case 0x34:
    case 0x54:
    case 0x74:
    case 0xD4:
    case 0xF4:
        fetch_byte();
        cycles = 4;
        break;
    case 0x15:
        a_ |= read(zero_page_x());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x16: {
        Address addr = zero_page_x();
        Byte value = asl(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x18:
        set_flag(Carry, false);
        cycles = 2;
        break;
    case 0x19: {
        auto addr = absolute_y();
        a_ |= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x1A:
        cycles = 2;
        break;
    case 0x1C:
    case 0x3C:
    case 0x5C:
    case 0x7C:
    case 0xDC:
    case 0xFC: {
        auto addr = absolute_x();
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x1D: {
        auto addr = absolute_x();
        a_ |= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x1E: {
        auto addr = absolute_x();
        Byte value = asl(read(addr.address));
        write(addr.address, value);
        cycles = 7;
        break;
    }
    case 0x20: {
        Word target = fetch_word();
        push_word(static_cast<Word>(pc_ - 1));
        pc_ = target;
        cycles = 6;
        break;
    }
    case 0x21: {
        a_ &= read(indirect_x());
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x24: {
        Byte value = read(zero_page());
        set_flag(Zero, (a_ & value) == 0);
        set_flag(Overflow, (value & 0x40) != 0);
        set_flag(Negative, (value & 0x80) != 0);
        cycles = 3;
        break;
    }
    case 0x25:
        a_ &= read(zero_page());
        set_zero_negative(a_);
        cycles = 3;
        break;
    case 0x26: {
        Address addr = zero_page();
        Byte value = rol(read(addr));
        write(addr, value);
        cycles = 5;
        break;
    }
    case 0x28:
        status_ = static_cast<Byte>((pop() & ~Break) | Unused);
        cycles = 4;
        break;
    case 0x29:
        a_ &= fetch_byte();
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0x2A:
        a_ = rol(a_);
        cycles = 2;
        break;
    case 0x2C: {
        Byte value = read(absolute());
        set_flag(Zero, (a_ & value) == 0);
        set_flag(Overflow, (value & 0x40) != 0);
        set_flag(Negative, (value & 0x80) != 0);
        cycles = 4;
        break;
    }
    case 0x2D:
        a_ &= read(absolute());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x2E: {
        Address addr = absolute();
        Byte value = rol(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x30:
        cycles = branch(get_flag(Negative));
        break;
    case 0x31: {
        auto addr = indirect_y();
        a_ &= read(addr.address);
        set_zero_negative(a_);
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x35:
        a_ &= read(zero_page_x());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x36: {
        Address addr = zero_page_x();
        Byte value = rol(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x38:
        set_flag(Carry, true);
        cycles = 2;
        break;
    case 0x39: {
        auto addr = absolute_y();
        a_ &= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x3A:
        cycles = 2;
        break;
    case 0x3D: {
        auto addr = absolute_x();
        a_ &= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x3E: {
        auto addr = absolute_x();
        Byte value = rol(read(addr.address));
        write(addr.address, value);
        cycles = 7;
        break;
    }
    case 0x40:
        status_ = static_cast<Byte>((pop() & ~Break) | Unused);
        pc_ = pop_word();
        cycles = 6;
        break;
    case 0x41: {
        a_ ^= read(indirect_x());
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x44:
    case 0x64:
        fetch_byte();
        cycles = 3;
        break;
    case 0x45:
        a_ ^= read(zero_page());
        set_zero_negative(a_);
        cycles = 3;
        break;
    case 0x46: {
        Address addr = zero_page();
        Byte value = lsr(read(addr));
        write(addr, value);
        cycles = 5;
        break;
    }
    case 0x48:
        push(a_);
        cycles = 3;
        break;
    case 0x49:
        a_ ^= fetch_byte();
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0x4A:
        a_ = lsr(a_);
        cycles = 2;
        break;
    case 0x4C:
        pc_ = absolute();
        cycles = 3;
        break;
    case 0x4D:
        a_ ^= read(absolute());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x4E: {
        Address addr = absolute();
        Byte value = lsr(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x50:
        cycles = branch(!get_flag(Overflow));
        break;
    case 0x51: {
        auto addr = indirect_y();
        a_ ^= read(addr.address);
        set_zero_negative(a_);
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x55:
        a_ ^= read(zero_page_x());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x56: {
        Address addr = zero_page_x();
        Byte value = lsr(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x58:
        set_flag(InterruptDisable, false);
        cycles = 2;
        break;
    case 0x59: {
        auto addr = absolute_y();
        a_ ^= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x5A:
        cycles = 2;
        break;
    case 0x5D: {
        auto addr = absolute_x();
        a_ ^= read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x5E: {
        auto addr = absolute_x();
        Byte value = lsr(read(addr.address));
        write(addr.address, value);
        cycles = 7;
        break;
    }
    case 0x60:
        pc_ = static_cast<Word>(pop_word() + 1);
        cycles = 6;
        break;
    case 0x61:
        adc(read(indirect_x()));
        cycles = 6;
        break;
    case 0x65:
        adc(read(zero_page()));
        cycles = 3;
        break;
    case 0x66: {
        Address addr = zero_page();
        Byte value = ror(read(addr));
        write(addr, value);
        cycles = 5;
        break;
    }
    case 0x68:
        a_ = pop();
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0x69:
        adc(fetch_byte());
        cycles = 2;
        break;
    case 0x6A:
        a_ = ror(a_);
        cycles = 2;
        break;
    case 0x6C:
        pc_ = read_word_indirect_bug(absolute());
        cycles = 5;
        break;
    case 0x6D:
        adc(read(absolute()));
        cycles = 4;
        break;
    case 0x6E: {
        Address addr = absolute();
        Byte value = ror(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x70:
        cycles = branch(get_flag(Overflow));
        break;
    case 0x71: {
        auto addr = indirect_y();
        adc(read(addr.address));
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x75:
        adc(read(zero_page_x()));
        cycles = 4;
        break;
    case 0x76: {
        Address addr = zero_page_x();
        Byte value = ror(read(addr));
        write(addr, value);
        cycles = 6;
        break;
    }
    case 0x78:
        set_flag(InterruptDisable, true);
        cycles = 2;
        break;
    case 0x79: {
        auto addr = absolute_y();
        adc(read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x7A:
        cycles = 2;
        break;
    case 0x7D: {
        auto addr = absolute_x();
        adc(read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0x7E: {
        auto addr = absolute_x();
        Byte value = ror(read(addr.address));
        write(addr.address, value);
        cycles = 7;
        break;
    }
    case 0x80:
    case 0x82:
    case 0x89:
    case 0xC2:
    case 0xE2:
        fetch_byte();
        cycles = 2;
        break;
    case 0x81:
        write(indirect_x(), a_);
        cycles = 6;
        break;
    case 0x84:
        write(zero_page(), y_);
        cycles = 3;
        break;
    case 0x85:
        write(zero_page(), a_);
        cycles = 3;
        break;
    case 0x86:
        write(zero_page(), x_);
        cycles = 3;
        break;
    case 0x88:
        --y_;
        set_zero_negative(y_);
        cycles = 2;
        break;
    case 0x8A:
        a_ = x_;
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0x8C:
        write(absolute(), y_);
        cycles = 4;
        break;
    case 0x8D:
        write(absolute(), a_);
        cycles = 4;
        break;
    case 0x8E:
        write(absolute(), x_);
        cycles = 4;
        break;
    case 0x90:
        cycles = branch(!get_flag(Carry));
        break;
    case 0x91:
        write(indirect_y().address, a_);
        cycles = 6;
        break;
    case 0x94:
        write(zero_page_x(), y_);
        cycles = 4;
        break;
    case 0x95:
        write(zero_page_x(), a_);
        cycles = 4;
        break;
    case 0x96:
        write(zero_page_y(), x_);
        cycles = 4;
        break;
    case 0x98:
        a_ = y_;
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0x99:
        write(absolute_y().address, a_);
        cycles = 5;
        break;
    case 0x9A:
        sp_ = x_;
        cycles = 2;
        break;
    case 0x9D:
        write(absolute_x().address, a_);
        cycles = 5;
        break;
    case 0xA0:
        y_ = fetch_byte();
        set_zero_negative(y_);
        cycles = 2;
        break;
    case 0xA1:
        a_ = read(indirect_x());
        set_zero_negative(a_);
        cycles = 6;
        break;
    case 0xA2:
        x_ = fetch_byte();
        set_zero_negative(x_);
        cycles = 2;
        break;
    case 0xA4:
        y_ = read(zero_page());
        set_zero_negative(y_);
        cycles = 3;
        break;
    case 0xA5:
        a_ = read(zero_page());
        set_zero_negative(a_);
        cycles = 3;
        break;
    case 0xA6:
        x_ = read(zero_page());
        set_zero_negative(x_);
        cycles = 3;
        break;
    case 0xA8:
        y_ = a_;
        set_zero_negative(y_);
        cycles = 2;
        break;
    case 0xA9:
        a_ = fetch_byte();
        set_zero_negative(a_);
        cycles = 2;
        break;
    case 0xAA:
        x_ = a_;
        set_zero_negative(x_);
        cycles = 2;
        break;
    case 0xAC:
        y_ = read(absolute());
        set_zero_negative(y_);
        cycles = 4;
        break;
    case 0xAD:
        a_ = read(absolute());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0xAE:
        x_ = read(absolute());
        set_zero_negative(x_);
        cycles = 4;
        break;
    case 0xB0:
        cycles = branch(get_flag(Carry));
        break;
    case 0xB1: {
        auto addr = indirect_y();
        a_ = read(addr.address);
        set_zero_negative(a_);
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xB4:
        y_ = read(zero_page_x());
        set_zero_negative(y_);
        cycles = 4;
        break;
    case 0xB5:
        a_ = read(zero_page_x());
        set_zero_negative(a_);
        cycles = 4;
        break;
    case 0xB6:
        x_ = read(zero_page_y());
        set_zero_negative(x_);
        cycles = 4;
        break;
    case 0xB8:
        set_flag(Overflow, false);
        cycles = 2;
        break;
    case 0xB9: {
        auto addr = absolute_y();
        a_ = read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xBA:
        x_ = sp_;
        set_zero_negative(x_);
        cycles = 2;
        break;
    case 0xBC: {
        auto addr = absolute_x();
        y_ = read(addr.address);
        set_zero_negative(y_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xBD: {
        auto addr = absolute_x();
        a_ = read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xBE: {
        auto addr = absolute_y();
        x_ = read(addr.address);
        set_zero_negative(x_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xC0:
        compare(y_, fetch_byte());
        cycles = 2;
        break;
    case 0xC1:
        compare(a_, read(indirect_x()));
        cycles = 6;
        break;
    case 0xC4:
        compare(y_, read(zero_page()));
        cycles = 3;
        break;
    case 0xC5:
        compare(a_, read(zero_page()));
        cycles = 3;
        break;
    case 0xC6: {
        Address addr = zero_page();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 5;
        break;
    }
    case 0xC8:
        ++y_;
        set_zero_negative(y_);
        cycles = 2;
        break;
    case 0xC9:
        compare(a_, fetch_byte());
        cycles = 2;
        break;
    case 0xCA:
        --x_;
        set_zero_negative(x_);
        cycles = 2;
        break;
    case 0xCC:
        compare(y_, read(absolute()));
        cycles = 4;
        break;
    case 0xCD:
        compare(a_, read(absolute()));
        cycles = 4;
        break;
    case 0xCE: {
        Address addr = absolute();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 6;
        break;
    }
    case 0xD0:
        cycles = branch(!get_flag(Zero));
        break;
    case 0xD1: {
        auto addr = indirect_y();
        compare(a_, read(addr.address));
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xD5:
        compare(a_, read(zero_page_x()));
        cycles = 4;
        break;
    case 0xD6: {
        Address addr = zero_page_x();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 6;
        break;
    }
    case 0xD8:
        set_flag(Decimal, false);
        cycles = 2;
        break;
    case 0xD9: {
        auto addr = absolute_y();
        compare(a_, read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xDA:
        cycles = 2;
        break;
    case 0xDD: {
        auto addr = absolute_x();
        compare(a_, read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xDE: {
        auto addr = absolute_x();
        Byte value = static_cast<Byte>(read(addr.address) - 1);
        write(addr.address, value);
        set_zero_negative(value);
        cycles = 7;
        break;
    }
    case 0xE0:
        compare(x_, fetch_byte());
        cycles = 2;
        break;
    case 0xE1:
        sbc(read(indirect_x()));
        cycles = 6;
        break;
    case 0xE4:
        compare(x_, read(zero_page()));
        cycles = 3;
        break;
    case 0xE5:
        sbc(read(zero_page()));
        cycles = 3;
        break;
    case 0xE6: {
        Address addr = zero_page();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 5;
        break;
    }
    case 0xE8:
        ++x_;
        set_zero_negative(x_);
        cycles = 2;
        break;
    case 0xE9:
    case 0xEB:
        sbc(fetch_byte());
        cycles = 2;
        break;
    case 0xEA:
        cycles = 2;
        break;
    case 0xEC:
        compare(x_, read(absolute()));
        cycles = 4;
        break;
    case 0xED:
        sbc(read(absolute()));
        cycles = 4;
        break;
    case 0xEE: {
        Address addr = absolute();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 6;
        break;
    }
    case 0xF0:
        cycles = branch(get_flag(Zero));
        break;
    case 0xF1: {
        auto addr = indirect_y();
        sbc(read(addr.address));
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xF5:
        sbc(read(zero_page_x()));
        cycles = 4;
        break;
    case 0xF6: {
        Address addr = zero_page_x();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        set_zero_negative(value);
        cycles = 6;
        break;
    }
    case 0xF8:
        set_flag(Decimal, true);
        cycles = 2;
        break;
    case 0xF9: {
        auto addr = absolute_y();
        sbc(read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xFA:
        cycles = 2;
        break;
    case 0xFD: {
        auto addr = absolute_x();
        sbc(read(addr.address));
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xFE: {
        auto addr = absolute_x();
        Byte value = static_cast<Byte>(read(addr.address) + 1);
        write(addr.address, value);
        set_zero_negative(value);
        cycles = 7;
        break;
    }
    // ===== Unofficial / undocumented opcodes =====
    // (NOP variants already handled above in the main switch)

    // --- LAX: Load A and X from memory ---
    case 0xA7: { // LAX zpg
        Byte value = read(zero_page());
        a_ = x_ = value;
        set_zero_negative(a_);
        cycles = 3;
        break;
    }
    case 0xB7: { // LAX zpg,Y
        Byte value = read(zero_page_y());
        a_ = x_ = value;
        set_zero_negative(a_);
        cycles = 4;
        break;
    }
    case 0xAF: { // LAX abs
        Byte value = read(absolute());
        a_ = x_ = value;
        set_zero_negative(a_);
        cycles = 4;
        break;
    }
    case 0xBF: { // LAX abs,Y
        auto addr = absolute_y();
        a_ = x_ = read(addr.address);
        set_zero_negative(a_);
        cycles = 4 + (addr.page_crossed ? 1 : 0);
        break;
    }
    case 0xA3: { // LAX (ind,X)
        Byte value = read(indirect_x());
        a_ = x_ = value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0xB3: { // LAX (ind),Y
        auto addr = indirect_y();
        a_ = x_ = read(addr.address);
        set_zero_negative(a_);
        cycles = 5 + (addr.page_crossed ? 1 : 0);
        break;
    }

    // --- SAX: Store A AND X ---
    case 0x87: // SAX zpg
        write(zero_page(), static_cast<Byte>(a_ & x_));
        cycles = 3;
        break;
    case 0x97: // SAX zpg,Y
        write(zero_page_y(), static_cast<Byte>(a_ & x_));
        cycles = 4;
        break;
    case 0x8F: // SAX abs
        write(absolute(), static_cast<Byte>(a_ & x_));
        cycles = 4;
        break;
    case 0x83: // SAX (ind,X)
        write(indirect_x(), static_cast<Byte>(a_ & x_));
        cycles = 6;
        break;

    // --- DCP: Decrement memory then Compare with A ---
    case 0xC7: { // DCP zpg
        Address addr = zero_page();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        compare(a_, value);
        cycles = 5;
        break;
    }
    case 0xD7: { // DCP zpg,X
        Address addr = zero_page_x();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        compare(a_, value);
        cycles = 6;
        break;
    }
    case 0xCF: { // DCP abs
        Address addr = absolute();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        compare(a_, value);
        cycles = 6;
        break;
    }
    case 0xDF: { // DCP abs,X
        auto ar = absolute_x();
        Byte value = static_cast<Byte>(read(ar.address) - 1);
        write(ar.address, value);
        compare(a_, value);
        cycles = 7;
        break;
    }
    case 0xDB: { // DCP abs,Y
        auto ar = absolute_y();
        Byte value = static_cast<Byte>(read(ar.address) - 1);
        write(ar.address, value);
        compare(a_, value);
        cycles = 7;
        break;
    }
    case 0xC3: { // DCP (ind,X)
        Address addr = indirect_x();
        Byte value = static_cast<Byte>(read(addr) - 1);
        write(addr, value);
        compare(a_, value);
        cycles = 8;
        break;
    }
    case 0xD3: { // DCP (ind),Y
        auto ar = indirect_y();
        Byte value = static_cast<Byte>(read(ar.address) - 1);
        write(ar.address, value);
        compare(a_, value);
        cycles = 8;
        break;
    }

    // --- ISB (ISC): Increment memory then SBC ---
    case 0xE7: { // ISB zpg
        Address addr = zero_page();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        sbc(value);
        cycles = 5;
        break;
    }
    case 0xF7: { // ISB zpg,X
        Address addr = zero_page_x();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        sbc(value);
        cycles = 6;
        break;
    }
    case 0xEF: { // ISB abs
        Address addr = absolute();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        sbc(value);
        cycles = 6;
        break;
    }
    case 0xFF: { // ISB abs,X
        auto ar = absolute_x();
        Byte value = static_cast<Byte>(read(ar.address) + 1);
        write(ar.address, value);
        sbc(value);
        cycles = 7;
        break;
    }
    case 0xFB: { // ISB abs,Y
        auto ar = absolute_y();
        Byte value = static_cast<Byte>(read(ar.address) + 1);
        write(ar.address, value);
        sbc(value);
        cycles = 7;
        break;
    }
    case 0xE3: { // ISB (ind,X)
        Address addr = indirect_x();
        Byte value = static_cast<Byte>(read(addr) + 1);
        write(addr, value);
        sbc(value);
        cycles = 8;
        break;
    }
    case 0xF3: { // ISB (ind),Y
        auto ar = indirect_y();
        Byte value = static_cast<Byte>(read(ar.address) + 1);
        write(ar.address, value);
        sbc(value);
        cycles = 8;
        break;
    }

    // --- SLO: ASL memory then ORA with A ---
    case 0x07: { // SLO zpg
        Address addr = zero_page();
        Byte value = asl(read(addr));
        write(addr, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 5;
        break;
    }
    case 0x17: { // SLO zpg,X
        Address addr = zero_page_x();
        Byte value = asl(read(addr));
        write(addr, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x0F: { // SLO abs
        Address addr = absolute();
        Byte value = asl(read(addr));
        write(addr, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x1F: { // SLO abs,X
        auto ar = absolute_x();
        Byte value = asl(read(ar.address));
        write(ar.address, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x1B: { // SLO abs,Y
        auto ar = absolute_y();
        Byte value = asl(read(ar.address));
        write(ar.address, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x03: { // SLO (ind,X)
        Address addr = indirect_x();
        Byte value = asl(read(addr));
        write(addr, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }
    case 0x13: { // SLO (ind),Y
        auto ar = indirect_y();
        Byte value = asl(read(ar.address));
        write(ar.address, value);
        a_ |= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }

    // --- RLA: ROL memory then AND with A ---
    case 0x27: { // RLA zpg
        Address addr = zero_page();
        Byte value = rol(read(addr));
        write(addr, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 5;
        break;
    }
    case 0x37: { // RLA zpg,X
        Address addr = zero_page_x();
        Byte value = rol(read(addr));
        write(addr, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x2F: { // RLA abs
        Address addr = absolute();
        Byte value = rol(read(addr));
        write(addr, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x3F: { // RLA abs,X
        auto ar = absolute_x();
        Byte value = rol(read(ar.address));
        write(ar.address, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x3B: { // RLA abs,Y
        auto ar = absolute_y();
        Byte value = rol(read(ar.address));
        write(ar.address, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x23: { // RLA (ind,X)
        Address addr = indirect_x();
        Byte value = rol(read(addr));
        write(addr, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }
    case 0x33: { // RLA (ind),Y
        auto ar = indirect_y();
        Byte value = rol(read(ar.address));
        write(ar.address, value);
        a_ &= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }

    // --- SRE (LSE): LSR memory then EOR with A ---
    case 0x47: { // SRE zpg
        Address addr = zero_page();
        Byte value = lsr(read(addr));
        write(addr, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 5;
        break;
    }
    case 0x57: { // SRE zpg,X
        Address addr = zero_page_x();
        Byte value = lsr(read(addr));
        write(addr, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x4F: { // SRE abs
        Address addr = absolute();
        Byte value = lsr(read(addr));
        write(addr, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 6;
        break;
    }
    case 0x5F: { // SRE abs,X
        auto ar = absolute_x();
        Byte value = lsr(read(ar.address));
        write(ar.address, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x5B: { // SRE abs,Y
        auto ar = absolute_y();
        Byte value = lsr(read(ar.address));
        write(ar.address, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 7;
        break;
    }
    case 0x43: { // SRE (ind,X)
        Address addr = indirect_x();
        Byte value = lsr(read(addr));
        write(addr, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }
    case 0x53: { // SRE (ind),Y
        auto ar = indirect_y();
        Byte value = lsr(read(ar.address));
        write(ar.address, value);
        a_ ^= value;
        set_zero_negative(a_);
        cycles = 8;
        break;
    }

    // --- RRA: ROR memory then ADC with A ---
    case 0x67: { // RRA zpg
        Address addr = zero_page();
        Byte value = ror(read(addr));
        write(addr, value);
        adc(value);
        cycles = 5;
        break;
    }
    case 0x77: { // RRA zpg,X
        Address addr = zero_page_x();
        Byte value = ror(read(addr));
        write(addr, value);
        adc(value);
        cycles = 6;
        break;
    }
    case 0x6F: { // RRA abs
        Address addr = absolute();
        Byte value = ror(read(addr));
        write(addr, value);
        adc(value);
        cycles = 6;
        break;
    }
    case 0x7F: { // RRA abs,X
        auto ar = absolute_x();
        Byte value = ror(read(ar.address));
        write(ar.address, value);
        adc(value);
        cycles = 7;
        break;
    }
    case 0x7B: { // RRA abs,Y
        auto ar = absolute_y();
        Byte value = ror(read(ar.address));
        write(ar.address, value);
        adc(value);
        cycles = 7;
        break;
    }
    case 0x63: { // RRA (ind,X)
        Address addr = indirect_x();
        Byte value = ror(read(addr));
        write(addr, value);
        adc(value);
        cycles = 8;
        break;
    }
    case 0x73: { // RRA (ind),Y
        auto ar = indirect_y();
        Byte value = ror(read(ar.address));
        write(ar.address, value);
        adc(value);
        cycles = 8;
        break;
    }

    // --- ANC: AND immediate then copy N to C ---
    case 0x0B:
    case 0x2B: {
        a_ &= fetch_byte();
        set_zero_negative(a_);
        set_flag(Carry, (a_ & 0x80) != 0);
        cycles = 2;
        break;
    }

    // --- ALR: AND immediate then LSR A ---
    case 0x4B: {
        a_ &= fetch_byte();
        a_ = lsr(a_);
        cycles = 2;
        break;
    }

    // --- ARR: AND immediate then ROR A (special flags) ---
    case 0x6B: {
        a_ &= fetch_byte();
        a_ = ror(a_);
        set_flag(Carry, (a_ & 0x40) != 0);
        set_flag(Overflow, ((a_ >> 6) ^ (a_ >> 5)) & 0x01);
        cycles = 2;
        break;
    }

    // --- AXS (SBX): A AND X minus immediate (no borrow) ---
    case 0xCB: {
        Byte imm = fetch_byte();
        Byte ax = static_cast<Byte>(a_ & x_);
        uint16_t result = static_cast<uint16_t>(ax) - imm;
        x_ = static_cast<Byte>(result & 0xFF);
        set_flag(Carry, ax >= imm);
        set_zero_negative(x_);
        cycles = 2;
        break;
    }

    // --- KIL/JAM: halt CPU (treat as abort) ---
    case 0x02:
    case 0x12:
    case 0x22:
    case 0x32:
    case 0x42:
    case 0x52:
    case 0x62:
    case 0x72:
    case 0x92:
    case 0xB2:
    case 0xD2:
    case 0xF2:
        logger::error("CPU JAM at {:04X}", pc_ - 1);
        cycles = 2;
        break;

    // --- Remaining unstable/rarely-used unofficial opcodes: treat as NOP ---
    default:
        logger::warn("Unknown opcode {:02X} at {:04X}, treating as NOP", opcode, pc_ - 1);
        cycles = 2;
        break;
    }

    cycles += bus_.take_dma_cycles();
    total_cycles_ += cycles;
    status_ |= Unused;
    return cycles;
}

} // namespace mapperbus::core
