#pragma once

#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {

class MemoryBus;

class Cpu {
  public:
    explicit Cpu(MemoryBus& bus);

    void reset();
    uint32_t step();
    uint64_t cycles() const {
        return total_cycles_;
    }

  private:
    enum StatusFlag : Byte {
        Carry = 0x01,
        Zero = 0x02,
        InterruptDisable = 0x04,
        Decimal = 0x08,
        Break = 0x10,
        Unused = 0x20,
        Overflow = 0x40,
        Negative = 0x80,
    };

    struct AddressResult {
        Address address;
        bool page_crossed = false;
    };

    MemoryBus& bus_;

    Byte a_ = 0;
    Byte x_ = 0;
    Byte y_ = 0;
    Byte sp_ = 0xFD;
    Word pc_ = 0;
    Byte status_ = 0x24;

    uint64_t total_cycles_ = 0;

    Byte read(Address addr) const;
    void write(Address addr, Byte value);

    Byte fetch_byte();
    Word fetch_word();
    Word read_word(Address addr) const;
    Word read_word_zero_page(Byte addr) const;
    Word read_word_indirect_bug(Address addr) const;

    void push(Byte value);
    Byte pop();
    void push_word(Word value);
    Word pop_word();

    bool get_flag(StatusFlag flag) const;
    void set_flag(StatusFlag flag, bool value);
    void set_zero_negative(Byte value);

    void service_interrupt(Word vector, bool set_break_flag);
    void adc(Byte value);
    void sbc(Byte value);
    void compare(Byte lhs, Byte rhs);

    Byte asl(Byte value);
    Byte lsr(Byte value);
    Byte rol(Byte value);
    Byte ror(Byte value);

    AddressResult absolute_indexed(Byte index) const;
    AddressResult indirect_indexed(Byte zero_page_addr) const;
};

} // namespace mapperbus::core
