#include "core/bus/memory_bus.hpp"

#include "core/apu/apu.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/fds/fds.hpp"
#include "core/input/controller.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

MemoryBus::MemoryBus() {
    ram_.fill(0);
    update_mappings();
}

void MemoryBus::save_state(StateWriter& writer) const {
    writer.write_array(ram_);
    writer.write(open_bus_);
    writer.write(dma_cycles_);
    writer.write(static_cast<Byte>(nmi_line_));
    writer.write(static_cast<Byte>(nmi_pending_));
    writer.write(ppu_clock_phase_);
    writer.write(controller_oe_);
    writer.write(static_cast<Byte>(controller_strobe_request_));
    writer.write(controller_timing_);
    writer.write(controller_wiring_);
    writer.write(static_cast<Byte>(microphone_active_));
    writer.write(microphone_phase_);
    writer.write(expansion_ports_);
    writer.write(controller_mode_);
    writer.write_array(adapter_reports_);
}

void MemoryBus::load_state(StateReader& reader,
                           bool has_interrupt_state,
                           bool has_clock_phase,
                           bool has_io_state,
                           bool has_controller_timing,
                           bool has_controller_wiring,
                           bool has_microphone,
                           bool has_expansion_controllers,
                           bool has_four_score,
                           bool has_hori) {
    reader.read_array(ram_);
    reader.read(open_bus_);
    reader.read(dma_cycles_);
    reset_interrupts();
    if (has_interrupt_state) {
        const auto line = reader.read<Byte>();
        const auto pending = reader.read<Byte>();
        nmi_line_ = line == 1;
        nmi_pending_ = pending == 1;
        if (line > 1 || pending > 1)
            reader.fail();
    } else if (ppu_ != nullptr) {
        nmi_pending_ = ppu_->consume_nmi();
    }
    // Older PAL snapshots used a fixed three-dot clock, so no fractional
    // phase can be recovered from their CPU cycle count. Start at phase zero.
    ppu_clock_phase_ = has_clock_phase ? reader.read<Byte>() : Byte{0};
    if ((region_ == Region::PAL && ppu_clock_phase_ >= kPalTiming.ppu_clock_divider) ||
        (region_ != Region::PAL && ppu_clock_phase_ != 0))
        reader.fail();
    controller_oe_ = has_io_state ? reader.read<Byte>() : Byte{0};
    const auto strobe = has_io_state ? reader.read<Byte>() : Byte{0};
    controller_strobe_request_ = strobe == 1;
    controller_timing_ =
        has_controller_timing ? reader.read<ControllerTiming>() : ControllerTiming::NesAvFamicom;
    controller_wiring_ =
        has_controller_wiring ? reader.read<ControllerWiring>() : ControllerWiring::Nes;
    const auto microphone = has_microphone ? reader.read<Byte>() : Byte{0};
    microphone_active_ = microphone == 1;
    microphone_phase_ = has_microphone ? reader.read<uint16_t>() : uint16_t{0};
    expansion_ports_ = has_expansion_controllers ? reader.read<Byte>() : Byte{0};
    if (expansion_ports_ > 3)
        reader.fail();
    controller_mode_ = has_four_score ? reader.read<ControllerMode>() : ControllerMode::Standard;
    adapter_reports_.fill(0xFFFFFF);
    if (has_four_score)
        reader.read_array(adapter_reports_);
    if (controller_mode_ > (has_hori ? ControllerMode::HoriFourPlayer : ControllerMode::FourScore))
        reader.fail();
    for (const auto report : adapter_reports_)
        if (report > 0xFFFFFF ||
            (controller_mode_ == ControllerMode::Standard && report != 0xFFFFFF))
            reader.fail();
    if (microphone > 1 || microphone_phase_ >= kMicrophonePeriodCycles ||
        (!microphone_active_ && microphone_phase_ != 0))
        reader.fail();
    if (controller_wiring_ > ControllerWiring::RfFamicom ||
        controller_timing_ > ControllerTiming::RfFamicom || controller_oe_ > 2 || strobe > 1 ||
        ((!ntsc_io() || controller_timing_ == ControllerTiming::RfFamicom) && controller_oe_ != 0))
        reader.fail();
    io_access_ = false;
    controller_sample_cycle_ = false;
    irq_sampled_ = poll_irq();
}

void MemoryBus::restore_legacy_controller() {
    controller_oe_ = 0;
    controller_strobe_request_ = controller_ != nullptr && controller_->strobed();
}

void MemoryBus::set_controller_oe(Byte port) {
    if (controller_oe_ != port && controller_oe_ != 0 && controller_ != nullptr)
        controller_->clock(controller_oe_ - 1);
    if (controller_oe_ != port && controller_oe_ != 0 && expansion_controller_ != nullptr)
        expansion_controller_->clock(controller_oe_ - 1);
    if (controller_oe_ != port && controller_oe_ != 0 &&
        additional_expansion_controller_ != nullptr)
        additional_expansion_controller_->clock(controller_oe_ - 1);
    if (controller_oe_ != port && controller_oe_ != 0)
        clock_adapter(controller_oe_ - 1);
    controller_oe_ = port;
}

void MemoryBus::set_controller_mode(ControllerMode mode) {
    if (controller_mode_ == mode)
        return;
    set_controller_oe(0);
    controller_mode_ = mode;
    adapter_reports_.fill(0xFFFFFF);
    latch_adapter();
}

void MemoryBus::latch_adapter() {
    if (controller_mode_ == ControllerMode::Standard)
        return;
    const bool hori = controller_mode_ == ControllerMode::HoriFourPlayer;
    const auto* first_pair = hori ? expansion_controller_ : controller_;
    const auto* second_pair = hori ? additional_expansion_controller_ : expansion_controller_;
    for (int port = 0; port < 2; ++port) {
        const auto first = first_pair != nullptr ? first_pair->buttons(port) : Byte{0};
        const auto second = second_pair != nullptr ? second_pair->buttons(port) : Byte{0};
        // Raw serial identification bit: Hori read18/read19, Four Score read19/read18.
        const uint32_t signature = hori ? (0x04U << port) : (0x08U >> port);
        adapter_reports_[port] =
            static_cast<uint32_t>(first) | static_cast<uint32_t>(second) << 8U | signature << 16U;
    }
}

void MemoryBus::clock_adapter(int port) {
    if (controller_mode_ != ControllerMode::Standard &&
        (controller_ == nullptr || !controller_->strobed()))
        adapter_reports_[port] = (adapter_reports_[port] >> 1U) | 0x800000U;
}

void MemoryBus::set_controller_wiring(ControllerWiring wiring) {
    if (controller_wiring_ == wiring)
        return;
    controller_wiring_ = wiring;
    if (controller_ != nullptr && controller_->strobed())
        write_controller_strobe(1);
}

void MemoryBus::write_controller_strobe(Byte value) {
    // Falling strobe captures host updates made while strobe was high too.
    if ((value & 1U) != 0 || (controller_ != nullptr && controller_->strobed()))
        latch_adapter();
    if (controller_ != nullptr)
        controller_->write(value, controller_wiring_ == ControllerWiring::RfFamicom ? 0xF3 : 0xFF);
    if (expansion_controller_ != nullptr)
        expansion_controller_->write(value);
    if (additional_expansion_controller_ != nullptr)
        additional_expansion_controller_->write(value);
}

Byte MemoryBus::read_controller(int player, Byte external, bool timed) {
    if (controller_ == nullptr && controller_wiring_ == ControllerWiring::Nes &&
        expansion_ports_ == 0 && controller_mode_ == ControllerMode::Standard)
        return external;
    auto bit = controller_ == nullptr ? Byte{0}
               : timed && ntsc_io()   ? controller_->peek(player)
                                      : controller_->read(player);
    const auto expansion_bit = expansion_controller_ == nullptr ? Byte{0}
                               : timed && ntsc_io() ? expansion_controller_->peek(player)
                                                    : expansion_controller_->read(player);
    Byte expansion =
        controller_mode_ == ControllerMode::Standard && (expansion_ports_ & (1U << player)) != 0
            ? static_cast<Byte>(expansion_bit << 1U)
            : Byte{0};
    if (controller_mode_ != ControllerMode::Standard) {
        const bool hori = controller_mode_ == ControllerMode::HoriFourPlayer;
        const auto* first_pair = hori ? expansion_controller_ : controller_;
        const auto serial = controller_ != nullptr && controller_->strobed()
                                ? (first_pair != nullptr ? first_pair->buttons(player) & 1U : 0U)
                                : adapter_reports_[player] & 1U;
        if (hori)
            expansion = static_cast<Byte>(serial << 1U);
        else
            bit = static_cast<Byte>(serial);
        if (!(timed && ntsc_io()))
            clock_adapter(player);
    }
    // RF $4016 D3/D4 float; D1 is the external controller input and D2
    // is the live microphone, independent of the pad shifter and strobe.
    const Byte floating =
        controller_wiring_ == ControllerWiring::RfFamicom && player == 0 ? 0xF8 : 0xE0;
    const Byte microphone = player == 0 && microphone_level() ? 4 : 0;
    return static_cast<Byte>((external & floating) | bit | expansion | microphone);
}

Byte MemoryBus::read_cpu(Address addr) {
    return read_cycle(addr, addr, false);
}

Byte MemoryBus::open_bus() const {
    return cartridge_ != nullptr ? static_cast<Byte>(open_bus_ & cartridge_->cpu_open_bus_mask())
                                 : open_bus_;
}

Byte MemoryBus::read_cycle(Address addr, Address cpu_address, bool dma) {
    io_access_ = true;
    const bool split_decode = dma && ntsc_io();
    const auto internal =
        split_decode ? static_cast<Address>((cpu_address & 0xFFE0U) | (addr & 0x1FU)) : addr;
    const bool apu_enabled = (internal & 0xFFE0U) == 0x4000;
    const bool joypad = apu_enabled && (internal == 0x4016 || internal == 0x4017);
    set_controller_oe(ntsc_io() && joypad ? static_cast<Byte>(internal - 0x4015) : Byte{0});
    // External memory still sees the DMA address, even when the internal APU
    // decoder selects a register. Its $4015 read never drives external open bus.
    Byte external = open_bus();
    if (addr < 0x4000 || addr >= 0x4018)
        external = (this->*(read_map_[static_cast<uint32_t>(addr) >> 10U]))(addr);
    if (internal == 0x4015 && apu_enabled) {
        open_bus_ = external;
        return static_cast<Byte>((external & 0x20U) |
                                 (apu_ != nullptr ? apu_->read_register(0x4015) : Byte{0}));
    }
    if (joypad)
        external = read_controller(internal - 0x4016, external, true);
    open_bus_ = external;
    return external;
}

Byte MemoryBus::read(Address addr) {
    Byte value = (this->*(read_map_[addr >> 10]))(addr);
    if (addr != 0x4015)
        open_bus_ = value;
    return value;
}

void MemoryBus::write(Address addr, Byte value) {
    open_bus_ = value;
    (this->*(write_map_[addr >> 10]))(addr, value);
}

void MemoryBus::write_cpu(Address addr, Byte value, bool consecutive) {
    io_access_ = true;
    set_controller_oe(0);
    open_bus_ = value;
    if (addr == 0x4016 && ntsc_io()) {
        controller_strobe_request_ = (value & 1U) != 0;
        return;
    }
    if (addr >= 0x6000 && cartridge_ != nullptr)
        cartridge_->write_prg_cpu(addr, value, consecutive);
    else if (addr != 0x4014)
        (this->*(write_map_[static_cast<uint32_t>(addr) >> 10U]))(addr, value);
}

void MemoryBus::clock_cpu_bus(Address addr, uint32_t cycles) {
    for (uint32_t cycle = 0; cycle < cycles; ++cycle) {
        begin_cpu_cycle(addr);
        end_cpu_cycle();
    }
}

void MemoryBus::begin_cpu_cycle(Address addr) {
    if (microphone_active_)
        microphone_phase_ =
            static_cast<uint16_t>((microphone_phase_ + 1U) % kMicrophonePeriodCycles);
    io_access_ = false;
    controller_sample_cycle_ = apu_ == nullptr || apu_->dma_get_cycle();
    // phi1 detector outputs reflect the previous cycle's phi2 pin levels.
    irq_sampled_ = poll_irq();
    const bool nmi = ppu_ != nullptr && ppu_->nmi_line();
    nmi_pending_ = nmi_pending_ || (nmi && !nmi_line_);
    nmi_line_ = nmi;
    clock_ppu(timing_for_region(region_).cpu_access_master_clock);
    if (fds_ != nullptr && fds_->is_loaded()) {
        fds_->clock_cpu_cycle(addr);
        fds_->step(1);
    }
    if (apu_ != nullptr)
        apu_->step(1);
    if (cartridge_ != nullptr)
        cartridge_->clock_cpu();
    if (cartridge_ != nullptr && cartridge_->has_expansion_audio())
        cartridge_->clock_audio();
    if (fds_ != nullptr && fds_->is_loaded())
        fds_->clock_audio();
}

void MemoryBus::end_cpu_cycle() {
    // RF Famicom gates /OE to half a CPU cycle, so even adjacent reads
    // release the line and clock the shift register once per read.
    if (!io_access_ || controller_timing_ == ControllerTiming::RfFamicom)
        set_controller_oe(0);
    if (controller_sample_cycle_)
        write_controller_strobe(static_cast<Byte>(controller_strobe_request_));
    const auto& timing = timing_for_region(region_);
    clock_ppu(timing.cpu_clock_divider - timing.cpu_access_master_clock);
}

void MemoryBus::clock_ppu(unsigned master_clocks) {
    const auto divider = timing_for_region(region_).ppu_clock_divider;
    const auto elapsed = ppu_clock_phase_ + master_clocks;
    ppu_clock_phase_ = static_cast<Byte>(elapsed % divider);
    if (ppu_ != nullptr)
        ppu_->step_dots(elapsed / divider);
}

uint32_t MemoryBus::run_dmc_dma(Address halt_address) {
    if (apu_ == nullptr || !apu_->dmc_dma_ready())
        return 0;
    return run_dma(halt_address, std::nullopt, false);
}

uint32_t MemoryBus::run_oam_dma(Byte page, Address halt_address, bool odd_alignment) {
    if (ppu_ == nullptr)
        return run_dmc_dma(halt_address);
    return run_dma(halt_address, page, odd_alignment);
}

uint32_t MemoryBus::run_dma(Address halt_address, std::optional<Byte> page, bool odd_alignment) {
    enum class DmcPhase { Idle, Halt, Abort, Dummy, Get };
    DmcPhase dmc = DmcPhase::Idle;
    bool oam_halt = page.has_value();
    bool oam_byte_ready = false;
    Byte oam_byte = 0;
    uint16_t offset = 0;
    uint32_t cycles = 0;
    // The fallback preserves isolated CPU/bus fixtures without an APU. With an
    // APU connected, its serialized cycle phase is the only DMA cadence source.
    bool get_cycle = odd_alignment;
    do {
        if (apu_ != nullptr) {
            get_cycle = apu_->dma_get_cycle();
            if (dmc == DmcPhase::Idle && apu_->dmc_dma_ready())
                dmc = apu_->start_dmc_dma() ? DmcPhase::Abort : DmcPhase::Halt;
        }
        const auto phase = dmc;
        if (apu_ != nullptr && phase == DmcPhase::Get && get_cycle) {
            const Address address = apu_->dmc_dma_address();
            begin_cpu_cycle(address);
            apu_->complete_dmc_dma(read_cycle(address, halt_address, true));
            dmc = DmcPhase::Idle;
        } else if (page && !oam_halt && oam_byte_ready && !get_cycle) {
            begin_cpu_cycle(0x2004);
            write_cpu(0x2004, oam_byte);
            oam_byte_ready = false;
            ++offset;
            if (offset == 256)
                page.reset();
        } else if (page && !oam_halt && !oam_byte_ready && get_cycle) {
            const auto address = static_cast<Address>((static_cast<uint32_t>(*page) << 8U) |
                                                      static_cast<uint32_t>(offset));
            begin_cpu_cycle(address);
            oam_byte = read_cycle(address, halt_address, true);
            oam_byte_ready = true;
        } else {
            begin_cpu_cycle(halt_address);
            // PAL performs the initial halted read too; repeated dummy/alignment
            // reads remain suppressed pending separate 2A07 validation.
            if (region_ != Region::PAL || cycles == 0)
                (void)read_cpu(halt_address);
        }
        end_cpu_cycle();
        // Halt/dummy setup overlaps OAM accesses. Only the DMC get steals a
        // transfer cycle; OAM then realigns before its next source read.
        if (phase == DmcPhase::Abort)
            dmc = DmcPhase::Idle;
        else if (phase == DmcPhase::Halt)
            dmc = DmcPhase::Dummy;
        else if (phase == DmcPhase::Dummy)
            dmc = DmcPhase::Get;
        oam_halt = false;
        get_cycle = !get_cycle;
        ++cycles;
    } while (page || dmc != DmcPhase::Idle || (apu_ != nullptr && apu_->dmc_dma_ready()));
    return cycles;
}

void MemoryBus::update_mappings() {
    for (int i = 0; i < 8; ++i) {
        read_map_[i] = &MemoryBus::read_ram;
        write_map_[i] = &MemoryBus::write_ram;
    }
    for (int i = 8; i < 16; ++i) {
        read_map_[i] = &MemoryBus::read_ppu;
        write_map_[i] = &MemoryBus::write_ppu;
    }
    read_map_[16] = &MemoryBus::read_apu_io;
    write_map_[16] = &MemoryBus::write_apu_io;
    for (int i = 17; i < 24; ++i) {
        read_map_[i] = &MemoryBus::read_expansion;
        write_map_[i] = &MemoryBus::write_expansion;
    }
    for (int i = 24; i < 64; ++i) {
        read_map_[i] = &MemoryBus::read_prg;
        write_map_[i] = &MemoryBus::write_prg;
    }
}

Byte MemoryBus::read_ram(Address addr) {
    return ram_[addr & 0x07FF];
}

Byte MemoryBus::read_ppu(Address addr) {
    return ppu_ ? ppu_->read_register(addr & 0x0007) : 0;
}

Byte MemoryBus::read_apu_io(Address addr) {
    if (addr < 0x4018) {
        if (addr == 0x4016 || addr == 0x4017) {
            return read_controller(addr - 0x4016, open_bus(), false);
        }
        if (addr == 0x4015)
            return static_cast<Byte>((open_bus() & 0x20U) |
                                     (apu_ != nullptr ? apu_->read_register(addr) : Byte{0}));
        return open_bus(); // other APU registers, including $4014, are write-only
    }
    // $4020-$409F: FDS disk drive ($4030-$4033) and audio ($4040-$4092)
    if (addr >= 0x4020 && addr <= 0x409F && fds_ && fds_->is_loaded()) {
        return fds_->read(addr);
    }
    return open_bus();
}

Byte MemoryBus::read_expansion(Address addr) {
    if (cartridge_) {
        if (!cartridge_->maps_expansion(addr)) {
            return open_bus();
        }
        const auto mask = cartridge_->expansion_read_mask(addr);
        return static_cast<Byte>((cartridge_->read_expansion(addr) & mask) | (open_bus() & ~mask));
    }
    return open_bus();
}

Byte MemoryBus::read_prg(Address addr) {
    if (cartridge_) {
        if (!cartridge_->maps_prg(addr)) {
            return open_bus();
        }
        const auto mask = cartridge_->prg_read_mask(addr);
        return static_cast<Byte>((cartridge_->read_prg(addr) & mask) | (open_bus() & ~mask));
    }
    return open_bus();
}

Byte MemoryBus::read_open_bus(Address) {
    return open_bus();
}

void MemoryBus::write_ram(Address addr, Byte value) {
    ram_[addr & 0x07FF] = value;
}

void MemoryBus::write_ppu(Address addr, Byte value) {
    if (ppu_) {
        ppu_->write_register(addr & 0x0007, value);
    }
}

void MemoryBus::write_apu_io(Address addr, Byte value) {
    if (addr < 0x4018) {
        if (addr == 0x4014) {
            if (ppu_) {
                Address base = static_cast<Address>(value << 8);
                for (uint16_t offset = 0; offset < 0x100; ++offset) {
                    ppu_->write_oam_dma(read(static_cast<Address>(base + offset)));
                }
                dma_cycles_ += 513;
            }
            return;
        }
        if (addr == 0x4016) {
            controller_strobe_request_ = (value & 1U) != 0;
            write_controller_strobe(value);
            return;
        }
        if (apu_) {
            apu_->write_register(addr, value);
        }
        return;
    }
    // $4020-$409F: FDS disk drive ($4020-$4026) and audio ($4040-$4089)
    if (addr >= 0x4020 && addr <= 0x409F && fds_ && fds_->is_loaded()) {
        fds_->write(addr, value);
    }
}

void MemoryBus::write_expansion(Address addr, Byte value) {
    if (cartridge_) {
        cartridge_->write_expansion(addr, value);
    }
}

void MemoryBus::write_prg(Address addr, Byte value) {
    if (cartridge_) {
        cartridge_->write_prg(addr, value);
    }
}

void MemoryBus::write_open_bus(Address, Byte) {}

void MemoryBus::reset_interrupts() {
    nmi_line_ = ppu_ != nullptr && ppu_->nmi_line();
    nmi_pending_ = false;
    irq_sampled_ = false;
}

void MemoryBus::acknowledge_nmi() {
    nmi_pending_ = false;
    if (ppu_ != nullptr)
        (void)ppu_->consume_nmi();
}

bool MemoryBus::poll_irq() {
    bool irq = false;
    if (cartridge_) {
        irq |= cartridge_->irq_pending();
    }
    if (apu_) {
        irq |= apu_->irq_pending();
    }
    if (fds_) {
        irq |= fds_->irq_pending();
    }
    return irq;
}

uint32_t MemoryBus::take_dma_cycles() {
    uint32_t cycles = dma_cycles_;
    dma_cycles_ = 0;
    return cycles;
}

} // namespace mapperbus::core
