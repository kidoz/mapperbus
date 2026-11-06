#include "core/emulator.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>

#include "core/binary_file.hpp"
#include "core/logger.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

namespace {
// "MBST" — MapperBus STate. Bump kStateVersion on any layout change.
constexpr std::array<Byte, 4> kStateMagic = {'M', 'B', 'S', 'T'};
// v13 adds controller line/strobe state and the PPU I/O latch.
// v12 added DMC stop latches and queued reload state. v11 added the CPU/PPU
// clock remainder. v10 added PPU frame parity and the retained dot-skip decision.
// v9 added interrupt decisions/detectors and PPU/APU race state. v8 added DMC
// requests/cadence; v7 the FDS watchdog; v6 audio latches; v5 drive state; v4 SHA-256.
constexpr std::uint32_t kStateVersion = 13;

[[nodiscard]] std::string battery_path_for(const std::string& rom_path) {
    return std::filesystem::path(rom_path).replace_extension(".sav").string();
}

/// Header fields shared by the probe in load_state and the apply in
/// restore_state. Does not modify the machine. Logs and returns nullopt
/// when the blob cannot be applied.
[[nodiscard]] std::optional<Region> decode_state_header(std::span<const Byte> data,
                                                        std::uint16_t expected_mapper,
                                                        const Sha256Digest& expected_hash,
                                                        bool allow_legacy = false) {
    StateReader reader(data);

    std::array<Byte, 4> magic{};
    reader.read_array(magic);
    if (!reader.ok() || magic != kStateMagic) {
        logger::warn("Save-state rejected: bad magic");
        return std::nullopt;
    }
    const auto version = reader.read<std::uint32_t>();
    if (version != kStateVersion && version != 12 && version != 11 && version != 10 &&
        version != 9 && version != 8 && version != 7 && version != 6 && version != 5 &&
        version != 4 && !(version == 3 && allow_legacy)) {
        logger::warn("Save-state rejected: unsupported version");
        return std::nullopt;
    }
    const auto mapper_number = reader.read<std::uint16_t>();
    if (!reader.ok() || mapper_number != expected_mapper) {
        logger::warn("Save-state rejected: mapper mismatch (state does not match loaded ROM)");
        return std::nullopt;
    }
    const auto region = reader.read<Region>();
    if (!reader.ok() || region > Region::Multi) {
        logger::warn("Save-state rejected: invalid region");
        return std::nullopt;
    }
    if (version >= 4) {
        Sha256Digest hash{};
        reader.read_array(hash);
        if (!reader.ok() || hash != expected_hash) {
            logger::warn("Save-state rejected: ROM content identity mismatch");
            return std::nullopt;
        }
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

Emulator::~Emulator() {
    flush_battery_ram();
    if (auto saved = flush_disk_changes(); !saved)
        logger::error("{}", saved.error());
}

void Emulator::wire_apu() {
    bus_.connect_ppu(&ppu_);
    bus_.connect_apu(&apu_);
    bus_.connect_controller(&controller_);
    bus_.connect_fds(&fds_);

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
    return load_cartridge(std::move(*result), battery_path_for(path));
}

Result<void> Emulator::load_cartridge(Cartridge cartridge, const std::string& battery_path) {
    // Commit only after parsing the replacement and persisting outgoing media.
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    flush_battery_ram();
    fds_.unload();
    disk_hash_ = {};
    disk_changes_path_.clear();
    cartridge_ = std::make_unique<Cartridge>(std::move(cartridge));
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
    bus_.set_region(region_);

    battery_save_path_ = battery_path;

    // Auto-load the configured .sav for battery-backed carts (best effort).
    if (cartridge_->has_battery() && !battery_save_path_.empty()) {
        const std::string& sav = battery_save_path_;
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
    disk_hash_ = rom_sha256(*data);
    logger::info("Mounted FDS disk: {} ({} side(s))", path, fds_.side_count());
    return {};
}

Result<void> Emulator::load_fds(std::span<const Byte> bios,
                                std::span<const Byte> disk,
                                const std::string& changes_path) {
    auto cartridge = Cartridge::from_fds_bios(bios, fds_);
    if (!cartridge)
        return std::unexpected(cartridge.error());
    Fds candidate;
    if (!candidate.load_disk(disk))
        return std::unexpected("Invalid FDS disk image");
    const auto identity = rom_sha256(candidate.disk_image());
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    if (!changes_path.empty()) {
        std::error_code error;
        const bool exists = std::filesystem::exists(changes_path, error);
        if (error)
            return std::unexpected("Cannot access FDS disk save: " + error.message());
        if (exists) {
            auto saved = read_binary_file_capped(
                changes_path, Fds::kMaxDiskImageBytes + 72, "FDS disk save");
            if (!saved)
                return std::unexpected(saved.error());
            // MBFD v1, immutable source identity, payload digest, raw .fds sides.
            if (saved->size() < 72 || (*saved)[0] != 'M' || (*saved)[1] != 'B' ||
                (*saved)[2] != 'F' || (*saved)[3] != 'D' || (*saved)[4] != 1 || (*saved)[5] != 0 ||
                (*saved)[6] != 0 || (*saved)[7] != 0 ||
                !std::equal(identity.begin(), identity.end(), saved->begin() + 8)) {
                return std::unexpected("FDS disk save does not match this disk or format version");
            }
            const auto payload = std::span<const Byte>(*saved).subspan(72);
            const auto digest = rom_sha256(payload);
            if (!std::equal(digest.begin(), digest.end(), saved->begin() + 40) ||
                !candidate.apply_disk_changes(payload)) {
                return std::unexpected("FDS disk save is corrupt or has invalid geometry");
            }
        }
    }
    auto loaded = load_cartridge(std::move(*cartridge), {});
    if (!loaded)
        return loaded;
    fds_ = std::move(candidate);
    fds_.set_write_protected(false);
    disk_hash_ = identity;
    disk_changes_path_ = changes_path;
    return {};
}

Result<void> Emulator::flush_disk_changes() {
    fds_.synchronize_disk();
    if (!is_fds() || !fds_.dirty() || disk_changes_path_.empty())
        return {};
    std::vector<Byte> data{'M', 'B', 'F', 'D', 1, 0, 0, 0};
    data.insert(data.end(), disk_hash_.begin(), disk_hash_.end());
    const auto payload = fds_.disk_image();
    const auto digest = rom_sha256(payload);
    data.insert(data.end(), digest.begin(), digest.end());
    data.insert(data.end(), payload.begin(), payload.end());
    auto result = write_binary_file_atomic(disk_changes_path_, data, "FDS disk save");
    if (result)
        fds_.mark_clean();
    return result;
}

Result<void> Emulator::export_disk(const std::string& path) {
    fds_.synchronize_disk();
    if (!is_fds())
        return std::unexpected("No FDS game is loaded");
    return write_binary_file_atomic(path, fds_.disk_image(), "FDS disk image");
}

void Emulator::flush_battery_ram() {
    // Persist battery-backed RAM of the currently loaded cartridge, if any.
    if (cartridge_ && cartridge_->has_battery() && !battery_save_path_.empty()) {
        auto saved = save_battery_ram(battery_save_path_);
        if (!saved) {
            logger::warn("Failed to write battery save: {}", saved.error());
        }
    }
}

void Emulator::unload_cartridge() {
    // Persist battery-backed RAM before tearing the cartridge down.
    flush_battery_ram();
    if (auto saved = flush_disk_changes(); !saved)
        logger::error("{}", saved.error());
    fds_.unload();
    disk_hash_ = {};
    disk_changes_path_.clear();
    battery_save_path_.clear();

    cartridge_.reset();
    bus_.connect_cartridge(nullptr);
    ppu_.connect_cartridge(nullptr);
    region_ = Region::NTSC;
    ppu_.set_region(region_);
    apu_.set_region(region_);
    bus_.set_region(region_);
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

void Emulator::step_frame() {
    ppu_.clear_frame_ready();
    while (!ppu_.frame_ready()) {
        step_instruction();
    }

    // Flush BlipBuffer at end of frame
    apu_.end_audio_frame();
}

bool Emulator::step_instruction() {
    (void)cpu_.step();
    return ppu_.frame_ready();
}

Sha256Digest Emulator::content_hash() const {
    const auto cartridge_hash = cartridge_ ? cartridge_->content_hash() : Sha256Digest{};
    if (disk_hash_ == Sha256Digest{}) {
        return cartridge_hash;
    }
    if (!cartridge_) {
        return disk_hash_;
    }
    std::array<Byte, 64> combined{};
    std::ranges::copy(cartridge_hash, combined.begin());
    std::ranges::copy(disk_hash_, combined.begin() + 32);
    return rom_sha256(combined);
}

std::vector<Byte> Emulator::save_state() const {
    StateWriter writer;
    writer.write_array(kStateMagic);
    writer.write(kStateVersion);
    const std::uint16_t mapper_number =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    writer.write(mapper_number);
    writer.write(region_);
    writer.write_array(content_hash());

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

bool Emulator::restore_state(std::span<const Byte> data, bool allow_legacy) {
    const std::uint16_t expected =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    const auto region = decode_state_header(data, expected, content_hash(), allow_legacy);
    if (!region) {
        return false;
    }

    StateReader reader(data);
    std::array<Byte, 4> magic{};
    reader.read_array(magic);
    const auto version = reader.read<std::uint32_t>();
    (void)reader.read<std::uint16_t>();
    (void)reader.read<Region>();
    if (version >= 4) {
        Sha256Digest hash{};
        reader.read_array(hash);
    }

    // Header validated — region affects subsystem timing tables, so apply it
    // before deserializing the timing-dependent subsystems.
    set_region(*region);

    cpu_.load_state(reader, version >= 9);
    ppu_.load_state(reader, version >= 9, version >= 10, cpu_.cycles(), version >= 13);
    apu_.load_state(reader, version >= 8, cpu_.cycles(), version >= 9, version >= 12);
    if (version >= 8 && apu_.cpu_cycles() != cpu_.cycles())
        reader.fail();
    bus_.load_state(reader, version >= 9, version >= 11, version >= 13);
    controller_.load_state(reader);
    if (version < 13)
        bus_.restore_legacy_controller();
    fds_.load_state(reader, version >= 5, version >= 6, version >= 7);
    if (cartridge_) {
        cartridge_->load_state(reader);
    }

    if (version < 9)
        cpu_.restore_legacy_interrupts();

    if (!reader.ok()) {
        logger::warn("Save-state rejected: truncated or failed validation");
        return false;
    }
    return true;
}

bool Emulator::load_state(std::span<const Byte> data, bool allow_legacy) {
    if (data.size() > kMaxStateBytes) {
        logger::warn("Save-state rejected: exceeds maximum size");
        return false;
    }

    const std::uint16_t expected =
        cartridge_ ? cartridge_->header().mapper_number : std::uint16_t{0xFFFF};
    // Reject a bad header before snapshotting. The body can still fail after
    // this probe; that path restores the snapshot taken below.
    if (!decode_state_header(data, expected, content_hash(), allow_legacy)) {
        return false;
    }

    const std::vector<Byte> snapshot = save_state();
    if (restore_state(data, allow_legacy)) {
        return true;
    }
    if (!restore_state(snapshot)) {
        logger::error("Save-state rollback failed; machine state may be inconsistent");
    }
    return false;
}

Result<void> Emulator::save_state_to_file(const std::string& path) const {
    const std::vector<Byte> blob = save_state();
    return write_binary_file_atomic(path, blob, "save-state file");
}

Result<void> Emulator::load_state_from_file(const std::string& path, bool allow_legacy) {
    auto blob = read_binary_file_capped(path, kMaxStateBytes, "save-state file");
    if (!blob) {
        return std::unexpected(blob.error());
    }
    if (!load_state(*blob, allow_legacy)) {
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
    return write_binary_file_atomic(path, ram, "battery save");
}

Result<void> Emulator::load_battery_ram(const std::string& path) {
    if (!cartridge_) {
        return std::unexpected("No cartridge loaded");
    }
    auto data = read_binary_file_capped(path, cartridge_->battery_ram().size(), "battery save");
    if (!data) {
        return std::unexpected(data.error());
    }
    cartridge_->set_battery_ram(*data);
    return {};
}

} // namespace mapperbus::core
