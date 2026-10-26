#include "core/emulator.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

#include "core/binary_file.hpp"
#include "core/logger.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

namespace {
// "MBST" — MapperBus STate. Bump kStateVersion on any layout change.
constexpr std::array<Byte, 4> kStateMagic = {'M', 'B', 'S', 'T'};
constexpr std::uint32_t kStateVersion = 3; // v3: VRC7 audio_halted_
                                           // v2: audio-accuracy state (FDS envelopes, VRC7
                                           // operators, 5B noise/env, MMC5 PCM, VRC6 $9003)

[[nodiscard]] std::string battery_path_for(const std::string& rom_path) {
    return std::filesystem::path(rom_path).replace_extension(".sav").string();
}

/// Header fields shared by the probe in load_state and the apply in
/// restore_state. Does not modify the machine. Logs and returns nullopt
/// when the blob cannot be applied.
[[nodiscard]] std::optional<Region> decode_state_header(std::span<const Byte> data,
                                                        std::uint16_t expected_mapper) {
    StateReader reader(data);

    std::array<Byte, 4> magic{};
    reader.read_array(magic);
    if (!reader.ok() || magic != kStateMagic) {
        logger::warn("Save-state rejected: bad magic");
        return std::nullopt;
    }
    if (reader.read<std::uint32_t>() != kStateVersion) {
        logger::warn("Save-state rejected: unsupported version");
        return std::nullopt;
    }
    const auto mapper_number = reader.read<std::uint16_t>();
    if (!reader.ok() || mapper_number != expected_mapper) {
        logger::warn("Save-state rejected: mapper mismatch (state does not match loaded ROM)");
        return std::nullopt;
    }
    const auto region = reader.read<Region>();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return region;
}
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
    // Persist the outgoing cartridge's battery RAM before replacing it.
    flush_battery_ram();
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

    rom_path_ = path;

    // Auto-load a sibling .sav for battery-backed carts (best effort).
    if (cartridge_->has_battery()) {
        const std::string sav = battery_path_for(rom_path_);
        if (std::filesystem::exists(sav)) {
            auto loaded = load_battery_ram(sav);
            if (!loaded) {
                logger::warn("Failed to load battery save: {}", loaded.error());
            } else {
                logger::info("Loaded battery save: {}", sav);
            }
        }
    }

    return {};
}

Result<void> Emulator::load_disk(const std::string& path) {
    // Optional 16-byte fwNES header plus the capped disk payload.
    auto data = read_binary_file_capped(path, Fds::kMaxDiskImageBytes + 16, "FDS disk image");
    if (!data) {
        return std::unexpected(data.error());
    }
    if (!fds_.load_disk(*data)) {
        return std::unexpected("Invalid FDS disk image (not a whole number of sides): " + path);
    }
    logger::info("Mounted FDS disk: {} ({} side(s))", path, fds_.side_count());
    return {};
}

void Emulator::flush_battery_ram() {
    // Persist battery-backed RAM of the currently loaded cartridge, if any.
    if (cartridge_ && cartridge_->has_battery() && !rom_path_.empty()) {
        auto saved = save_battery_ram(battery_path_for(rom_path_));
        if (!saved) {
            logger::warn("Failed to write battery save: {}", saved.error());
        }
    }
}

void Emulator::unload_cartridge() {
    // Persist battery-backed RAM before tearing the cartridge down.
    flush_battery_ram();
    rom_path_.clear();

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
        step_instruction();
    }

    // Flush BlipBuffer at end of frame
    apu_.end_audio_frame();
}

bool Emulator::step_instruction() {
    uint32_t cpu_cycles = cpu_.step();
    ppu_.step(cpu_cycles);
    apu_.step(cpu_cycles);
    fds_.step(cpu_cycles);
    clock_expansion_audio(cpu_cycles);

    // DMC memory reads stall the CPU — advance the CPU cycle counter and the
    // PPU/APU/mapper clocks by the stall so OAM-DMA odd/even parity and the
    // rest of the machine stay in step.
    uint32_t dmc_stall = apu_.take_dmc_stall_cycles();
    if (dmc_stall > 0) {
        cpu_.add_stall_cycles(dmc_stall);
        ppu_.step(dmc_stall);
        apu_.step(dmc_stall);
        clock_expansion_audio(dmc_stall);
    }

    return ppu_.frame_ready();
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

bool Emulator::restore_state(std::span<const Byte> data) {
    const std::uint16_t expected =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    const auto region = decode_state_header(data, expected);
    if (!region) {
        return false;
    }

    StateReader reader(data);
    std::array<Byte, 4> magic{};
    reader.read_array(magic);
    (void)reader.read<std::uint32_t>();
    (void)reader.read<std::uint16_t>();
    (void)reader.read<Region>();

    // Header validated — region affects subsystem timing tables, so apply it
    // before deserializing the timing-dependent subsystems.
    set_region(*region);

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
        logger::warn("Save-state rejected: truncated or failed validation");
        return false;
    }
    return true;
}

bool Emulator::load_state(std::span<const Byte> data) {
    if (data.size() > kMaxStateBytes) {
        logger::warn("Save-state rejected: exceeds maximum size");
        return false;
    }

    const std::uint16_t expected =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    // Reject a bad header before snapshotting. The body can still fail after
    // this probe; that path restores the snapshot taken below.
    if (!decode_state_header(data, expected)) {
        return false;
    }

    const std::vector<Byte> snapshot = save_state();
    if (restore_state(data)) {
        return true;
    }
    if (!restore_state(snapshot)) {
        logger::error("Save-state rollback failed; machine state may be inconsistent");
    }
    return false;
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
    auto blob = read_binary_file_capped(path, kMaxStateBytes, "save-state file");
    if (!blob) {
        return std::unexpected(blob.error());
    }
    if (!load_state(*blob)) {
        return std::unexpected("Save-state is invalid or does not match the loaded ROM: " + path);
    }
    return {};
}

Result<void> Emulator::save_battery_ram(const std::string& path) const {
    if (!cartridge_ || !cartridge_->has_battery()) {
        return {}; // Nothing to persist.
    }
    const std::span<const Byte> ram = cartridge_->battery_ram();
    if (ram.empty()) {
        return {};
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open battery save for writing: " + path);
    }
    file.write(reinterpret_cast<const char*>(ram.data()), static_cast<std::streamsize>(ram.size()));
    if (!file) {
        return std::unexpected("Failed to write battery save: " + path);
    }
    return {};
}

Result<void> Emulator::load_battery_ram(const std::string& path) {
    if (!cartridge_) {
        return std::unexpected("No cartridge loaded");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open battery save: " + path);
    }
    std::vector<Byte> data((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    cartridge_->set_battery_ram(data);
    return {};
}

} // namespace mapperbus::core
