#pragma once

#include "core/types.hpp"

namespace mapperbus::core {

class StateWriter;
class StateReader;

class Mapper {
  public:
    virtual ~Mapper() = default;

    virtual Byte read_prg(Address addr) = 0;
    virtual void write_prg(Address addr, Byte value) = 0;
    virtual bool maps_prg(Address addr) const {
        return addr >= 0x8000;
    }
    virtual Byte read_chr(Address addr) = 0;
    virtual void write_chr(Address addr, Byte value) = 0;

    virtual MirrorMode mirror_mode() const = 0;
    virtual void reset() = 0;

    // Save-state hooks. Each mapper serializes its mutable runtime state
    // (bank registers, latches, IRQ counters, CHR/PRG RAM, audio phase).
    // Immutable ROM data is never serialized.
    virtual void save_state(StateWriter&) const {}
    virtual void load_state(StateReader&) {}

    virtual bool irq_pending() const {
        return false;
    }
    virtual void acknowledge_irq() {}
    virtual void clock_irq_counter() {}

    // Expansion audio support (Famicom cartridges only)
    virtual bool has_expansion_audio() const {
        return false;
    }
    virtual void clock_audio() {}
    virtual float audio_output() const {
        return 0.0f;
    }
    virtual bool maps_expansion(Address /*addr*/) const {
        return false;
    }
    virtual Byte read_expansion(Address /*addr*/) {
        return 0;
    }
    virtual void write_expansion(Address /*addr*/, Byte /*value*/) {}
};

} // namespace mapperbus::core
