#pragma once

#include <cstdint>
#include <optional>

#include "core/types.hpp"

namespace mapperbus::core {

class MemoryBus;
class StateWriter;
class StateReader;

class Cpu {
  public:
    explicit Cpu(MemoryBus& bus);

    void reset();
    uint32_t step();

    /// Advances the master cycle counter without executing an instruction.
    /// Used for cycles the CPU was stalled by external hardware (DMC sample
    /// fetches), so OAM-DMA odd/even parity stays aligned.
    void add_stall_cycles(uint32_t cycles) {
        total_cycles_ += cycles;
    }

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);
    [[nodiscard]] uint64_t cycles() const {
        return total_cycles_;
    }

    // Register inspection (for testing and future debugger support).
    [[nodiscard]] Byte a() const {
        return a_;
    }
    [[nodiscard]] Byte x() const {
        return x_;
    }
    [[nodiscard]] Byte y() const {
        return y_;
    }
    [[nodiscard]] Byte sp() const {
        return sp_;
    }
    [[nodiscard]] Word pc() const {
        return pc_;
    }
    [[nodiscard]] Byte status() const {
        return status_;
    }
    /// True after a KIL/JAM opcode until reset() — the CPU stays parked.
    [[nodiscard]] bool jammed() const {
        return jammed_;
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
    // Set by KIL/JAM opcodes; the CPU stays halted until reset().
    bool jammed_ = false;
    // Transient within step(); snapshots are taken at instruction boundaries.
    mutable uint32_t bus_cycles_ = 0;
    uint32_t last_write_cycle_ = 0;
    // Multiple RMW writes replace the page before the next read can halt.
    std::optional<Byte> oam_dma_page_;

    void clock_bus(Address addr, uint32_t cycles = 1) const;
    void dummy_read(Address addr, uint32_t cycles = 1) const;
    void finish_bus_cycles(uint32_t cycles) const;
    Byte read(Address addr) const;
    Byte read_modify(Address addr);
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
    AddressResult indirect_indexed(Byte zero_page_addr, bool always_dummy = false) const;
};

} // namespace mapperbus::core
