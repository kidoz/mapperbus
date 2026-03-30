#include "core/emulator.hpp"

namespace mapperbus::core {

Emulator::Emulator() : cpu_(bus_) {
    wire_apu();
}

Emulator::Emulator(const AudioSettings& audio_settings) : cpu_(bus_), apu_(audio_settings) {
    wire_apu();
}

void Emulator::wire_apu() {
    bus_.connect_ppu(&ppu_);
    bus_.connect_apu(&apu_);
    bus_.connect_controller(&controller_);
    bus_.connect_fds(&fds_);

    // Wire DMC memory reader to bus
    apu_.set_memory_reader([this](Address addr) -> Byte { return bus_.read(addr); });

    // Wire expansion audio (mapper + FDS) into APU mixer
    apu_.set_expansion_audio([this]() -> float {
        float output = 0.0f;
        if (cartridge_ && cartridge_->has_expansion_audio()) {
            output += cartridge_->audio_output();
        }
        if (fds_.is_loaded()) {
            output += fds_.audio_output();
        }
        return output;
    });
}

Result<void> Emulator::load_cartridge(const std::string& path) {
    auto result = Cartridge::from_file(path);
    if (!result) {
        return std::unexpected(result.error());
    }
    cartridge_ = std::make_unique<Cartridge>(std::move(*result));
    bus_.connect_cartridge(cartridge_.get());
    ppu_.connect_cartridge(cartridge_.get());

    // Configure region from cartridge header
    region_ = cartridge_->header().region;
    if (region_ == Region::Multi) {
        region_ = Region::NTSC; // default multi-region to NTSC
    }
    ppu_.set_region(region_);
    apu_.set_region(region_);

    return {};
}

void Emulator::reset() {
    ppu_.reset();
    apu_.reset();
    fds_.reset();
    if (cartridge_) {
        cartridge_->reset();
    }
    cpu_.reset();
}

void Emulator::step_frame() {
    ppu_.clear_frame_ready();
    while (!ppu_.frame_ready()) {
        uint32_t cpu_cycles = cpu_.step();
        ppu_.step(cpu_cycles);
        apu_.step(cpu_cycles);

        // Clock expansion audio (mapper + FDS audio channels run at CPU rate)
        for (uint32_t c = 0; c < cpu_cycles; ++c) {
            if (cartridge_ && cartridge_->has_expansion_audio()) {
                cartridge_->clock_audio();
            }
            if (fds_.is_loaded()) {
                fds_.clock_audio();
            }
        }

        // OAM DMA stalls the CPU — advance PPU/APU/mapper by DMA cycles
        uint32_t dma_cycles = bus_.take_dma_cycles();
        if (dma_cycles > 0) {
            ppu_.step(dma_cycles);
            apu_.step(dma_cycles);
            for (uint32_t c = 0; c < dma_cycles; ++c) {
                if (cartridge_ && cartridge_->has_expansion_audio()) {
                    cartridge_->clock_audio();
                }
                if (fds_.is_loaded()) {
                    fds_.clock_audio();
                }
            }
        }

        // DMC memory reads stall the CPU — advance PPU/APU by stall cycles
        uint32_t dmc_stall = apu_.take_dmc_stall_cycles();
        if (dmc_stall > 0) {
            ppu_.step(dmc_stall);
            apu_.step(dmc_stall);
            for (uint32_t c = 0; c < dmc_stall; ++c) {
                if (cartridge_ && cartridge_->has_expansion_audio()) {
                    cartridge_->clock_audio();
                }
                if (fds_.is_loaded()) {
                    fds_.clock_audio();
                }
            }
        }
    }

    // Flush BlipBuffer at end of frame
    apu_.end_audio_frame();
}

} // namespace mapperbus::core
