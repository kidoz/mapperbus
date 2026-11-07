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
    /// Timed CPU access, with controller output-enable edges.
    Byte read_cpu(Address addr);
    void write(Address addr, Byte value);
    /// CPU writes latch $4014; Cpu runs its DMA after the instruction's last
    /// write, at the next read cycle. Other writes have their normal effects.
    void write_cpu(Address addr, Byte value, bool consecutive = false);
    /// Advance machine clocks for CPU/DMA cycles without performing an access.
    /// Ordinary read/write remain untimed debugger/test operations.
    void clock_cpu_bus(Address addr, uint32_t cycles = 1);
    /// Advance to the region's selected CPU/DMA access point. PAL uses a
    /// fractional divider; NTSC/Dendy retain the two-dot/one-dot split.
    void begin_cpu_cycle(Address addr);
    void end_cpu_cycle();
    uint32_t run_oam_dma(Byte page, Address halt_address, bool odd_alignment);
    /// Service a ready DMC request before this eligible CPU read.
    uint32_t run_dmc_dma(Address halt_address);
    void set_region(Region region) {
        if (region_ != region)
            reset_clock();
        region_ = region;
    }
    void reset_clock() {
        ppu_clock_phase_ = 0;
        microphone_phase_ = 0;
        set_controller_oe(0);
    }
    void set_controller_timing(ControllerTiming timing) {
        if (controller_timing_ != timing)
            set_controller_oe(0);
        controller_timing_ = timing;
    }
    [[nodiscard]] ControllerTiming controller_timing() const {
        return controller_timing_;
    }
    void set_controller_wiring(ControllerWiring wiring);
    [[nodiscard]] ControllerWiring controller_wiring() const {
        return controller_wiring_;
    }
    /// Synthetic noise carrier: one period per 1024 emulated CPU cycles.
    static constexpr uint16_t kMicrophonePeriodCycles = 1024;
    void set_microphone_active(bool active) {
        if (microphone_active_ != active)
            microphone_phase_ = 0;
        microphone_active_ = active;
    }
    [[nodiscard]] bool microphone_active() const {
        return microphone_active_;
    }
    [[nodiscard]] bool microphone_level() const {
        return controller_wiring_ == ControllerWiring::RfFamicom && microphone_active_ &&
               microphone_phase_ < kMicrophonePeriodCycles / 2;
    }

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader,
                    bool has_interrupt_state = true,
                    bool has_clock_phase = true,
                    bool has_io_state = true,
                    bool has_controller_timing = true,
                    bool has_controller_wiring = true,
                    bool has_microphone = true,
                    bool has_expansion_controllers = true);
    void set_expansion_controllers(Byte ports) {
        expansion_ports_ = ports & 3U;
    }
    [[nodiscard]] Byte expansion_controllers() const {
        return expansion_ports_;
    }
    void connect_expansion_controller(Controller* controller) {
        expansion_controller_ = controller;
    }
    void restore_legacy_controller();
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
        set_controller_oe(0);
        controller_ = controller;
        restore_legacy_controller();
        update_mappings();
    }
    void connect_fds(Fds* fds) {
        fds_ = fds;
        update_mappings();
    }

  private:
    uint32_t run_dma(Address halt_address, std::optional<Byte> page, bool odd_alignment);
    void update_mappings();
    void clock_ppu(unsigned master_clocks);
    Byte read_cycle(Address addr, Address cpu_address, bool dma);
    void set_controller_oe(Byte port);
    Byte read_controller(int player, Byte external, bool timed);
    void write_controller_strobe(Byte value);
    [[nodiscard]] bool ntsc_io() const {
        return region_ == Region::NTSC || region_ == Region::Multi;
    }

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
    ControllerTiming controller_timing_ = ControllerTiming::NesAvFamicom;
    ControllerWiring controller_wiring_ = ControllerWiring::Nes;
    Byte expansion_ports_ = 0;
    bool microphone_active_ = false;
    uint16_t microphone_phase_ = 0;
    Byte ppu_clock_phase_ = 0; // elapsed master clocks since the last PPU dot
    Byte controller_oe_ = 0;   // 0: neither, 1/2: continuous read of that port
    bool controller_strobe_request_ = false;
    bool controller_sample_cycle_ = false; // transient, sample at the get-to-put boundary
    bool io_access_ = false;               // transient, current cycle performed a bus access
    bool irq_sampled_ = false;
    bool nmi_line_ = false;
    bool nmi_pending_ = false;

    Cartridge* cartridge_ = nullptr;
    Ppu* ppu_ = nullptr;
    Apu* apu_ = nullptr;
    Controller* controller_ = nullptr;
    Controller* expansion_controller_ = nullptr;
    Fds* fds_ = nullptr;
};

} // namespace mapperbus::core
