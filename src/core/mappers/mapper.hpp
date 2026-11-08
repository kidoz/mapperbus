#pragma once

#include <optional>
#include <span>

#include "core/types.hpp"

namespace mapperbus::core {

class StateWriter;
class StateReader;

class Mapper {
  public:
    virtual ~Mapper() = default;

    virtual Byte read_prg(Address addr) = 0;
    virtual void write_prg(Address addr, Byte value) = 0;
    /// CPU bus context for chips which filter consecutive write cycles.
    virtual void write_prg_cpu(Address addr, Byte value, bool /*consecutive*/) {
        write_prg(addr, value);
    }
    virtual bool maps_prg(Address addr) const {
        return addr >= 0x8000;
    }
    virtual Byte prg_read_mask(Address /*addr*/) const {
        return 0xFF;
    }
    virtual Byte read_chr(Address addr) = 0;
    virtual void write_chr(Address addr, Byte value) = 0;

    virtual MirrorMode mirror_mode() const = 0;
    /// Board-specific CIRAM wiring. Return an index in the console's 2 KiB
    /// nametable RAM, or nullopt to use the ordinary mirror_mode arrangement.
    virtual std::optional<Address> nametable_address(Address /*addr*/) const {
        return std::nullopt;
    }
    /// Override PPU nametable reads; nullopt leaves the access to CIRAM.
    /// Addresses are normalized to $2000-$2FFF, including $3000 mirrors.
    virtual std::optional<Byte> read_nametable(Address /*addr*/) const {
        return std::nullopt;
    }
    /// Return true when the board consumes the write (including ROM protection).
    virtual bool write_nametable(Address /*addr*/, Byte /*value*/) {
        return false;
    }
    virtual void reset() = 0;

    // Save-state hooks. Each mapper serializes its mutable runtime state
    // (bank registers, latches, IRQ counters, CHR/PRG RAM, audio phase).
    // Immutable ROM data is never serialized.
    virtual void save_state(StateWriter&) const {}
    virtual void load_state(StateReader&) {}

    // Battery-backed PRG-RAM ($6000-$7FFF). Mappers without persistent RAM
    // return an empty span; the cartridge only persists this when the iNES
    // header marks the board as battery-backed.
    [[nodiscard]] virtual std::span<const Byte> battery_ram() const {
        return {};
    }
    virtual void set_battery_ram(std::span<const Byte> /*data*/) {}

    virtual bool irq_pending() const {
        return false;
    }
    virtual void acknowledge_irq() {}
    virtual void clock_irq_counter() {}
    /// One M2/CPU cycle, including stalled and DMA cycles. Independent of audio.
    virtual void clock_cpu() {}

    // Called by the PPU at the start of each rendered frame (pre-render
    // scanline) while rendering is enabled. MMC5 uses it to reset its
    // in-frame scanline IRQ counter; other mappers ignore it.
    virtual void on_ppu_frame_start() {}

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
    virtual Byte expansion_read_mask(Address /*addr*/) const {
        return 0xFF;
    }
    virtual Byte read_expansion(Address /*addr*/) {
        return 0;
    }
    virtual void write_expansion(Address /*addr*/, Byte /*value*/) {}
};

} // namespace mapperbus::core
