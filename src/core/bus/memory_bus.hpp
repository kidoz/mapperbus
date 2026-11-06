#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/types.hpp"

namespace mapperbus::core {

class Cartridge;
class Fds;
class Ppu;
class Apu;
class Controller;
class StateWriter;
class StateReader;

class MemoryBus {
  public:
    MemoryBus();

    Byte read(Address addr);
    void write(Address addr, Byte value);
    /// CPU writes latch $4014; Cpu runs its DMA after the instruction's last
    /// write, at the next read cycle. Other writes have their normal effects.
    void write_cpu(Address addr, Byte value, bool consecutive = false);
    /// Advance machine clocks for CPU/DMA cycles without performing an access.
    /// Ordinary read/write remain untimed debugger/test operations.
    void clock_cpu_bus(Address addr, uint32_t cycles = 1);
    /// CPU/DMA access falls between the second and third PPU dots of a cycle.
    void begin_cpu_cycle(Address addr);
    void end_cpu_cycle();
    uint32_t run_oam_dma(Byte page, Address halt_address, bool odd_alignment);
    /// Service a ready DMC request before this eligible CPU read.
    uint32_t run_dmc_dma(Address halt_address);
    void set_region(Region region) {
        region_ = region;
    }

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader, bool has_interrupt_state = true);
    /// Detector outputs for the current CPU cycle: IRQ level and latched NMI edge.
    [[nodiscard]] Byte interrupt_sample() const {
        return static_cast<Byte>((irq_sampled_ ? 1U : 0U) | (nmi_pending_ ? 2U : 0U));
    }
    void acknowledge_nmi();
    void reset_interrupts();
    bool poll_irq();
    uint32_t take_dma_cycles();
    [[nodiscard]] Byte open_bus() const {
        return open_bus_;
    }

    void connect_cartridge(Cartridge* cartridge) {
        cartridge_ = cartridge;
        update_mappings();
    }
    void connect_ppu(Ppu* ppu) {
        ppu_ = ppu;
        update_mappings();
    }
    void connect_apu(Apu* apu) {
        apu_ = apu;
        update_mappings();
    }
    void connect_controller(Controller* controller) {
        controller_ = controller;
        update_mappings();
    }
    void connect_fds(Fds* fds) {
        fds_ = fds;
        update_mappings();
    }

  private:
    uint32_t run_dma(Address halt_address, std::optional<Byte> page, bool odd_alignment);
    void update_mappings();

    using ReadFunc = Byte (MemoryBus::*)(Address);
    using WriteFunc = void (MemoryBus::*)(Address, Byte);
    std::array<ReadFunc, 64> read_map_{};
    std::array<WriteFunc, 64> write_map_{};

    Byte read_ram(Address addr);
    Byte read_ppu(Address addr);
    Byte read_apu_io(Address addr);
    Byte read_expansion(Address addr);
    Byte read_prg(Address addr);
    Byte read_open_bus(Address addr);

    void write_ram(Address addr, Byte value);
    void write_ppu(Address addr, Byte value);
    void write_apu_io(Address addr, Byte value);
    void write_expansion(Address addr, Byte value);
    void write_prg(Address addr, Byte value);
    void write_open_bus(Address addr, Byte value);

    std::array<Byte, kRamSize> ram_{};
    Byte open_bus_ = 0;
    uint32_t dma_cycles_ = 0;
    Region region_ = Region::NTSC;
    bool irq_sampled_ = false;
    bool nmi_line_ = false;
    bool nmi_pending_ = false;

    Cartridge* cartridge_ = nullptr;
    Ppu* ppu_ = nullptr;
    Apu* apu_ = nullptr;
    Controller* controller_ = nullptr;
    Fds* fds_ = nullptr;
};

} // namespace mapperbus::core
