#include "core/emulator.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/logger.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

namespace {
// "MBST" — MapperBus STate. Bump kStateVersion on any layout change.
constexpr std::array<Byte, 4> kStateMagic = {'M', 'B', 'S', 'T'};
constexpr std::uint32_t kStateVersion = 1;
} // namespace

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
        mapperbus::core::logger::info("Multi-region ROM detected, defaulting to NTSC");
        region_ = Region::NTSC; // default multi-region to NTSC
    } else if (region_ == Region::NTSC) {
        mapperbus::core::logger::info("Auto-detected NTSC ROM");
    } else if (region_ == Region::PAL) {
        mapperbus::core::logger::info("Auto-detected PAL ROM");
    } else if (region_ == Region::Dendy) {
        mapperbus::core::logger::info("Auto-detected Dendy ROM");
    }

    ppu_.set_region(region_);
    apu_.set_region(region_);

    return {};
}

void Emulator::unload_cartridge() {
    cartridge_.reset();
    bus_.connect_cartridge(nullptr);
    ppu_.connect_cartridge(nullptr);
    region_ = Region::NTSC;
    ppu_.set_region(region_);
    apu_.set_region(region_);
    ppu_.reset();
    apu_.reset();
    fds_.reset();
    cpu_.reset();
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

void Emulator::clock_expansion_audio(uint32_t cycles) {
    for (uint32_t c = 0; c < cycles; ++c) {
        if (cartridge_ && cartridge_->has_expansion_audio()) {
            cartridge_->clock_audio();
        }
        if (fds_.is_loaded()) {
            fds_.clock_audio();
        }
    }
}

void Emulator::step_frame() {
    ppu_.clear_frame_ready();
    while (!ppu_.frame_ready()) {
        uint32_t cpu_cycles = cpu_.step();
        ppu_.step(cpu_cycles);
        apu_.step(cpu_cycles);
        clock_expansion_audio(cpu_cycles);

        // OAM DMA stalls the CPU — advance PPU/APU/mapper by DMA cycles
        uint32_t dma_cycles = bus_.take_dma_cycles();
        if (dma_cycles > 0) {
            ppu_.step(dma_cycles);
            apu_.step(dma_cycles);
            clock_expansion_audio(dma_cycles);
        }

        // DMC memory reads stall the CPU — advance PPU/APU by stall cycles
        uint32_t dmc_stall = apu_.take_dmc_stall_cycles();
        if (dmc_stall > 0) {
            ppu_.step(dmc_stall);
            apu_.step(dmc_stall);
            clock_expansion_audio(dmc_stall);
        }
    }

    // Flush BlipBuffer at end of frame
    apu_.end_audio_frame();
}

std::vector<Byte> Emulator::save_state() const {
    StateWriter writer;
    writer.write_array(kStateMagic);
    writer.write(kStateVersion);
    const std::uint16_t mapper_number =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    writer.write(mapper_number);
    writer.write(region_);

    cpu_.save_state(writer);
    ppu_.save_state(writer);
    apu_.save_state(writer);
    bus_.save_state(writer);
    controller_.save_state(writer);
    fds_.save_state(writer);
    if (cartridge_) {
        cartridge_->save_state(writer);
    }
    return writer.take();
}

bool Emulator::load_state(std::span<const Byte> data) {
    StateReader reader(data);

    std::array<Byte, 4> magic{};
    reader.read_array(magic);
    if (!reader.ok() || magic != kStateMagic) {
        logger::warn("Save-state rejected: bad magic");
        return false;
    }
    if (reader.read<std::uint32_t>() != kStateVersion) {
        logger::warn("Save-state rejected: unsupported version");
        return false;
    }
    const auto mapper_number = reader.read<std::uint16_t>();
    const std::uint16_t expected =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    if (!reader.ok() || mapper_number != expected) {
        logger::warn("Save-state rejected: mapper mismatch (state does not match loaded ROM)");
        return false;
    }
    const auto region = reader.read<Region>();
    if (!reader.ok()) {
        return false;
    }

    // Header validated — region affects subsystem timing tables, so apply it
    // before deserializing the timing-dependent subsystems.
    set_region(region);

    cpu_.load_state(reader);
    ppu_.load_state(reader);
    apu_.load_state(reader);
    bus_.load_state(reader);
    controller_.load_state(reader);
    fds_.load_state(reader);
    if (cartridge_) {
        cartridge_->load_state(reader);
    }

    if (!reader.ok()) {
        logger::error("Save-state load failed: blob truncated; machine state may be inconsistent");
        return false;
    }
    return true;
}

Result<void> Emulator::save_state_to_file(const std::string& path) const {
    const std::vector<Byte> blob = save_state();
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open save-state file for writing: " + path);
    }
    file.write(reinterpret_cast<const char*>(blob.data()),
               static_cast<std::streamsize>(blob.size()));
    if (!file) {
        return std::unexpected("Failed to write save-state file: " + path);
    }
    return {};
}

Result<void> Emulator::load_state_from_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open save-state file: " + path);
    }
    std::vector<Byte> blob((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (!load_state(blob)) {
        return std::unexpected("Save-state is invalid or does not match the loaded ROM: " + path);
    }
    return {};
}

} // namespace mapperbus::core
