#include "app/emulation_session.hpp"

#include <algorithm>
#include <filesystem>

#include "app/async_rom_loader.hpp"
#include "app/rom_source.hpp"
#include "core/binary_file.hpp"
#include "core/input/vs_cabinet.hpp"
#include "core/logger.hpp"

namespace mapperbus::app {
namespace {
constexpr std::size_t kAudioStagingSamples = 8192;
} // namespace

EmulationSession::EmulationSession(std::unique_ptr<platform::VideoBackend> video,
                                   std::unique_ptr<platform::AudioBackend> audio,
                                   std::unique_ptr<platform::InputBackend> input,
                                   const core::AudioSettings& audio_settings,
                                   AsyncDiskSave::Write disk_write)
    : audio_settings_(audio_settings), emulator_(std::make_shared<core::Emulator>(audio_settings)),
      video_(std::move(video)), audio_(std::move(audio)), input_(std::move(input)),
      disk_saver_(std::move(disk_write)), audio_staging_(kAudioStagingSamples) {}

EmulationSession::~EmulationSession() {
    // GUI Close/Quit already drained persistence. Synchronous consumers still
    // serialize teardown against a snapshot writer if they opted in directly.
    if (background_disk_saves_) {
        if (auto saved = flush_disk_changes(); !saved)
            core::logger::error("{}", saved.error());
    }
    suspend_live_microphone();
    if (audio_) {
        audio_->shutdown();
    }
    if (video_) {
        video_->shutdown();
    }
}

core::Result<void> EmulationSession::initialize() {
    if (initialized_) {
        return {};
    }

    if (!video_ || !audio_ || !input_) {
        return std::unexpected(
            std::string("EmulationSession requires video, audio, and input backends"));
    }

    if (!video_->initialize(core::kScreenWidth, core::kScreenHeight)) {
        return std::unexpected(std::string("Failed to initialize video backend"));
    }

    const int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
    if (!audio_->initialize(
            audio_settings_.sample_rate, audio_settings_.buffer_size_samples, channels)) {
        return std::unexpected(std::string("Failed to initialize audio backend"));
    }
    reconcile_audio_sample_rate();

    initialized_ = true;
    return {};
}

core::Result<void> EmulationSession::load_rom(const std::string& rom_path) {
    return open_rom(rom_path);
}

core::Result<void> EmulationSession::open_rom(const std::string& rom_path,
                                              std::string_view member,
                                              const std::string& dcs_option_path,
                                              std::string_view board_profile,
                                              const CartridgeLaunchOptions& cartridge_options) {
    if (!initialized_) {
        auto init_result = initialize();
        if (!init_result) {
            return init_result;
        }
    }

    if (background_disk_saves_) {
        if (auto saved = flush_disk_changes(); !saved)
            return saved;
    }
    finish_rewind();
    auto result = load_rom_source(*emulator_,
                                  rom_path,
                                  member,
                                  fds_bios_path_,
                                  {},
                                  {},
                                  dcs_option_path,
                                  {},
                                  board_profile,
                                  cartridge_options);
    if (!result) {
        return std::unexpected(result.error());
    }

    clear_rewind();
    suspend_live_microphone();
    emulator_->set_microphone_active(false);
    emulator_->power_on();
    turbo_input_.reset();
    configure_cartridge_keyboard();
    // Go through set_paused() so a previously suspended audio device is
    // resumed; assigning paused_ directly would leave the new ROM silent.
    set_paused(false);
    running_ = true;
    rom_loaded_ = true;
    current_rom_path_ = rom_path;
    current_rom_member_ = std::move(*result);
    disk_save_frames_ = 0;
    disk_save_error_.clear();
    set_state_storage(state_storage_);
    reset_audio_fill_ratio_history();
    return {};
}

void EmulationSession::adopt_rom(PreparedRom media) {
    clear_rewind();
    // Outgoing persistence was checked by the worker. Retirement performs no I/O.
    emulator_->set_persistence_enabled(false);
    emulator_ = std::move(media.emulator);
    emulator_->set_persistence_enabled(true);
    suspend_live_microphone();
    turbo_input_.reset();
    configure_cartridge_keyboard();
    set_paused(false);
    running_ = true;
    rom_loaded_ = true;
    current_rom_path_ = std::move(media.path);
    current_rom_member_ = std::move(media.member);
    state_directory_ = std::move(media.state_directory);
    disk_save_frames_ = 0;
    disk_save_error_.clear();
    reset_audio_fill_ratio_history();
}

core::Result<void> EmulationSession::insert_disk_side(int side) {
    if (!emulator_->is_fds())
        return std::unexpected("No FDS game is loaded");
    if (side < 0 || side >= emulator_->fds().side_count())
        return std::unexpected("Invalid disk side");
    if (auto saved = background_disk_saves_ ? request_disk_save() : flush_disk_changes(); !saved)
        return saved;
    clear_rewind();
    emulator_->fds().insert_side(side);
    return {};
}
core::Result<void> EmulationSession::eject_disk() {
    if (!emulator_->is_fds())
        return std::unexpected("No FDS game is loaded");
    if (auto saved = background_disk_saves_ ? request_disk_save() : flush_disk_changes(); !saved)
        return saved;
    clear_rewind();
    emulator_->fds().eject();
    return {};
}
core::Result<void> EmulationSession::set_disk_write_protected(bool protected_disk) {
    if (!emulator_->is_fds())
        return std::unexpected("No FDS game is loaded");
    clear_rewind();
    emulator_->fds().set_write_protected(protected_disk);
    return {};
}
core::Result<void> EmulationSession::export_disk(const std::string& path) {
    return emulator_->export_disk(path);
}

core::Result<void> EmulationSession::flush_disk_changes() {
    auto fence = disk_saver_.begin_handoff();
    fence();
    auto result = emulator_->flush_disk_changes();
    finish_background_persistence(result);
    return result;
}

void EmulationSession::finish_background_persistence(const core::Result<void>& result) {
    core::Result<void> disk_result;
    if (!result && emulator_->is_fds() && emulator_->fds().dirty())
        disk_result = std::unexpected(result.error());
    disk_saver_.finish_handoff(disk_result);
    disk_save_error_ = disk_saver_.error();
}

void EmulationSession::poll_disk_saves() {
    if (auto result = disk_saver_.poll()) {
        // An older write must not clear dirty data while another snapshot can
        // still overwrite it (including a game that changed A -> B -> A).
        if (*result && !disk_saver_.busy())
            emulator_->acknowledge_disk_save(**result);
        disk_save_error_ = disk_saver_.error();
    }
}

core::Result<void> EmulationSession::request_disk_save() {
    if (auto snapshot = emulator_->disk_save_snapshot()) {
        auto result = disk_saver_.submit(std::move(*snapshot));
        if (!result)
            disk_save_error_ = result.error();
        return result;
    }
    return {};
}

core::Result<void> EmulationSession::retry_disk_save() {
    if (disk_saver_.busy())
        return std::unexpected("A disk save is already in progress");
    disk_saver_.clear_error();
    disk_save_error_.clear();
    return request_disk_save();
}

core::Result<void> EmulationSession::close_rom() {
    if (!rom_loaded_)
        return {};
    finish_rewind();
    auto fence = disk_saver_.begin_handoff();
    fence();
    auto saved = emulator_->unload_cartridge();
    finish_background_persistence(saved);
    if (!saved)
        return saved;
    suspend_live_microphone();
    emulator_->set_microphone_active(false);
    turbo_input_.reset();
    configure_cartridge_keyboard();
    clear_rewind();
    rom_loaded_ = false;
    running_ = false;
    // Resume a suspended device as well as clearing the session flag.
    set_paused(false);
    current_rom_path_.clear();
    current_rom_member_.clear();
    state_directory_.clear();
    reset_audio_fill_ratio_history();
    return {};
}

TickResult EmulationSession::tick(bool allow_rewind) {
    if (background_disk_saves_)
        poll_disk_saves();
    if (!running_) {
        suspend_live_microphone();
        return TickResult::Stopped;
    }

    input_->poll();
    if (input_->should_quit()) {
        request_stop();
        return TickResult::Stopped;
    }

    if (!rom_loaded_) {
        suspend_live_microphone();
        return TickResult::NoCartridge;
    }

    if (paused_) {
        suspend_live_microphone();
        return TickResult::Paused;
    }

    if (allow_rewind && input_->rewind_held()) {
        const bool starting = !rewinding();
        if (rewind_.preview(std::chrono::steady_clock::now())) {
            if (starting) {
                suspend_live_microphone();
                audio_->pause();
                audio_->clear();
            }
            video_->render(display_frame());
            return TickResult::RewindPreview;
        }
        if (rewinding())
            return TickResult::RewindWaiting;
    } else {
        finish_rewind();
    }

    if (audio_->queued_samples() > max_queued_samples()) {
        live_microphone_.discard();
        return TickResult::AudioBackpressure;
    }

    sync_input();
    emulator_->step_frame();
    if (++disk_save_frames_ >= 120) {
        disk_save_frames_ = 0;
        if (!emulator_->fds().disk_active()) {
            if (background_disk_saves_) {
                if (disk_save_error_.empty())
                    (void)request_disk_save();
            } else {
                auto saved = flush_disk_changes();
                if (!saved)
                    core::logger::error("{}", saved.error());
            }
        }
    }
    submit_current_frame();
    return TickResult::FrameAdvanced;
}

TickResult EmulationSession::step_frame() {
    finish_rewind();
    if (!running_) {
        suspend_live_microphone();
        return TickResult::Stopped;
    }

    if (!rom_loaded_) {
        suspend_live_microphone();
        return TickResult::NoCartridge;
    }

    input_->poll();
    if (input_->should_quit()) {
        request_stop();
        return TickResult::Stopped;
    }

    sync_input();
    emulator_->step_frame();
    submit_current_frame();
    return TickResult::FrameAdvanced;
}

void EmulationSession::reset() {
    suspend_live_microphone();
    clear_rewind();
    emulator_->reset();
    turbo_input_.reset();
    configure_cartridge_keyboard();
}

void EmulationSession::power_cycle() {
    if (!rom_loaded_) {
        return;
    }
    suspend_live_microphone();
    clear_rewind();
    emulator_->power_on();
    turbo_input_.reset();
    configure_cartridge_keyboard();
    set_paused(false);
}

void EmulationSession::request_stop() {
    finish_rewind();
    suspend_live_microphone();
    running_ = false;
}

void EmulationSession::set_paused(bool paused) {
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    finish_rewind();
    // Suspend/resume the device so its buffer does not run dry during the
    // pause (a dry device underruns into hard silence and then snaps to
    // full amplitude on resume — an audible pop).
    if (paused) {
        suspend_live_microphone();
        audio_->pause();
    } else {
        audio_->resume();
    }
}

void EmulationSession::clear_microphone_playback() {
    if (audio_ && live_microphone_.settings().audio_enabled) {
        audio_->clear();
        reset_audio_fill_ratio_history();
    }
}

void EmulationSession::suspend_live_microphone() {
    if (live_microphone_.recording())
        clear_microphone_playback();
    live_microphone_.suspend();
    emulator_->set_cartridge_microphone_sample(0);
}

void EmulationSession::configure_live_microphone(LiveMicrophoneSettings settings) {
    const auto& previous = live_microphone_.settings();
    if (settings.enabled != previous.enabled || settings.device != previous.device ||
        settings.audio_enabled != previous.audio_enabled ||
        settings.volume_percent != previous.volume_percent)
        clear_microphone_playback();
    live_microphone_.configure(std::move(settings));
}

void EmulationSession::set_region(core::Region region) {
    if (emulator_->region() != region)
        clear_rewind();
    emulator_->set_region(region);
}

int EmulationSession::audio_queued_samples() const {
    return audio_ ? audio_->queued_samples() : 0;
}

int EmulationSession::audio_low_watermark_samples() const {
    // SDL consumes whole device buffers. At 44.1/48 kHz a 2048-frame
    // read spans more than the nominal 25 ms reserve, so keep at least one
    // complete read queued before the GUI stops its audio preroll.
    const int device_frames = audio_ ? audio_->buffer_size_frames() : 0;
    return std::max(audio_settings_.sample_rate / 40, device_frames);
}

int EmulationSession::audio_high_watermark_samples() const {
    return max_queued_samples();
}

core::Result<void> EmulationSession::apply_audio_settings(const core::AudioSettings& settings) {
    suspend_live_microphone();
    clear_rewind();
    const auto previous_settings = audio_settings_;
    audio_settings_ = settings;
    emulator_->apply_audio_settings(audio_settings_);

    if (!initialized_) {
        return {};
    }

    auto result = reinitialize_audio_backend();
    if (result) {
        reset_audio_fill_ratio_history();
        return {};
    }

    const std::string& original_error = result.error();
    audio_settings_ = previous_settings;
    emulator_->apply_audio_settings(audio_settings_);
    auto restore_result = reinitialize_audio_backend();
    if (!restore_result) {
        running_ = false;
        return std::unexpected(original_error + "; failed to restore previous audio settings: " +
                               restore_result.error());
    }
    return std::unexpected(original_error);
}

void EmulationSession::set_state_storage(const StateStorageSettings& settings) {
    state_storage_ = settings;
    if (rom_loaded_) {
        state_directory_ =
            state_directory_for_game(state_storage_, current_rom_path_, game_identity());
    }
}

std::string EmulationSession::game_identity() const {
    return rom_loaded_ ? core::sha256_hex(emulator_->content_hash()) : std::string{};
}

std::string EmulationSession::state_path_for_slot(int slot) const {
    if (state_directory_.empty())
        return {};
    return (state_directory_ / ("slot-" + std::to_string(slot) + ".state")).string();
}

core::Result<std::string> EmulationSession::state_load_path_for_slot(int slot) const {
    if (!rom_loaded_ || slot < 0 || slot >= kSaveStateSlotCount) {
        return std::unexpected(std::string("Load a ROM and choose a slot between 1 and 10"));
    }
    const auto legacy = current_rom_member_.empty() && !emulator_->is_fds()
                            ? legacy_state_path(current_rom_path_, slot)
                            : std::filesystem::path{};
    auto resolved = resolve_state_load_path(state_path_for_slot(slot), legacy);
    if (!resolved)
        return std::unexpected(resolved.error());
    return std::move(resolved->path);
}

core::Result<void> EmulationSession::save_state(int slot) {
    finish_rewind();
    if (slot < 0 || slot >= kSaveStateSlotCount) {
        return std::unexpected(std::string("State slot must be between 1 and 10"));
    }
    if (!rom_loaded_) {
        return std::unexpected(std::string("No ROM loaded to save state from"));
    }
    if (auto directory = ensure_state_directory(state_directory_); !directory) {
        return directory;
    }
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    return emulator_->save_state_to_file(state_path_for_slot(slot));
}

core::Result<void> EmulationSession::load_state(int slot) {
    const auto path = state_load_path_for_slot(slot);
    if (!path) {
        return std::unexpected(path.error());
    }
    const bool legacy = *path == legacy_state_path(current_rom_path_, slot).string();
    if (legacy) {
        core::logger::warn(
            "Importing a legacy sibling state; old versions cannot verify ROM identity: {}", *path);
    }
    auto data =
        core::read_binary_file_capped(*path, emulator_->state_size_limit(), "save-state file");
    if (!data)
        return std::unexpected(data.error());
    return load_state_data(*data, legacy);
}

core::Result<void> EmulationSession::load_state_data(std::span<const core::Byte> data,
                                                     bool allow_legacy) {
    if (!rom_loaded_)
        return std::unexpected("No ROM loaded to load state into");
    if (!emulator_->load_state(data, allow_legacy))
        return std::unexpected("Save-state is invalid or does not match the loaded ROM");
    clear_rewind();
    after_state_restore();
    return {};
}

void EmulationSession::after_state_restore() {
    suspend_live_microphone();
    turbo_input_.reset();
    configure_cartridge_keyboard();
    reset_audio_fill_ratio_history();
}

void EmulationSession::finish_rewind() {
    if (!rewinding())
        return;
    if (rewind_.restore(*emulator_)) {
        after_state_restore();
    } else {
        core::logger::error("Could not restore rewind checkpoint");
        video_->render(emulator_->frame_buffer());
    }
    if (!paused_)
        audio_->resume();
}

void EmulationSession::clear_rewind() {
    if (rewinding())
        audio_->resume();
    rewind_.clear();
}

const core::FrameBuffer& EmulationSession::display_frame() const {
    if (const auto* picture = rewind_.picture())
        return *picture;
    return emulator_->frame_buffer();
}

core::Result<void> EmulationSession::set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) {
    if (!video_) {
        return std::unexpected(std::string("No video backend available"));
    }

    if (!initialized_) {
        video_->set_upscaler(std::move(upscaler));
        return {};
    }

    video_->shutdown();
    video_->set_upscaler(std::move(upscaler));
    auto result = reinitialize_video_backend();
    if (!result) {
        running_ = false;
        return result;
    }

    if (rom_loaded_) {
        video_->render(display_frame());
    }

    return {};
}

void EmulationSession::sync_input() {
    sync_cartridge_keyboard();
    using core::Button;
    constexpr Button kButtons[] = {Button::A,
                                   Button::B,
                                   Button::Select,
                                   Button::Start,
                                   Button::Up,
                                   Button::Down,
                                   Button::Left,
                                   Button::Right};

    const bool was_recording = live_microphone_.recording();
    const bool rf = emulator_->controller_wiring() == core::ControllerWiring::RfFamicom;
    const bool cartridge_mic = emulator_->has_cartridge_microphone_adc();
    const bool live = live_microphone_.poll(!paused_ && (rf || cartridge_mic));
    if (cartridge_mic)
        emulator_->set_cartridge_microphone_sample(live_microphone_.adc_sample());
    if (was_recording && !live_microphone_.recording())
        clear_microphone_playback();
    auto* cabinet = emulator_->vs_cabinet();
    bool coin = false;
    bool microphone = rf && live;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        const bool mic_button =
            input_->is_action_pressed(player, platform::InputAction::Microphone);
        microphone |= mic_button;
        coin |= mic_button;
        // Consume each turbo action even when ordinary A/B is held, so its
        // phase remains independent of the normal binding.
        const bool turbo_a = turbo_input_.next(
            player, 0, input_->is_action_pressed(player, platform::InputAction::TurboA));
        const bool turbo_b = turbo_input_.next(
            player, 1, input_->is_action_pressed(player, platform::InputAction::TurboB));
        for (auto button : kButtons) {
            bool pressed = input_->is_action_pressed(player, platform::input_action(button));
            if (cabinet && player < 2 && (button == Button::Start || button == Button::Select)) {
                // Cabinet pushbuttons 1/2 are serial bit 2 on the right/left sticks,
                // independently of which joystick the game assigns to player one.
                const auto action = button == Button::Select ? platform::InputAction::Start
                                                             : platform::InputAction::Select;
                pressed = input_->is_action_pressed(cabinet->player_for_port(player), action);
            }
            if (button == Button::A)
                pressed |= turbo_a;
            if (button == Button::B)
                pressed |= turbo_b;
            auto& controller = player < 2   ? emulator_->controller()
                               : player < 4 ? emulator_->expansion_controller()
                                            : emulator_->additional_expansion_controller();
            controller.set_button_state(player % 2, button, pressed);
        }
    }
    if (cabinet != nullptr) {
        constexpr core::Byte kF10HidUsage = 0x43;
        cabinet->set_service(input_->keyboard_key_down(kF10HidUsage));
        cabinet->set_coin(coin);
    }
    emulator_->set_microphone_active(microphone);
}

void EmulationSession::submit_current_frame() {
    const auto& frame = emulator_->frame_buffer();
    video_->render(frame);

    const std::size_t count = emulator_->drain_audio(audio_staging_.data(), audio_staging_.size());
    if (count > 0) {
        const int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
        live_microphone_.mix_into(
            {audio_staging_.data(), count}, audio_settings_.sample_rate, channels);
        audio_->queue_samples({audio_staging_.data(), count});
    }

    const float fill_ratio = std::clamp(static_cast<float>(audio_->queued_samples()) /
                                            static_cast<float>(max_queued_samples()),
                                        0.0f,
                                        1.0f);
    emulator_->update_audio_rate_control(smooth_audio_fill_ratio(fill_ratio));
    rewind_.record(*emulator_, frame);
}

float EmulationSession::smooth_audio_fill_ratio(float fill_ratio) {
    audio_fill_ratio_sum_ -= audio_fill_ratio_history_[audio_fill_ratio_index_];
    audio_fill_ratio_history_[audio_fill_ratio_index_] = fill_ratio;
    audio_fill_ratio_sum_ += fill_ratio;

    audio_fill_ratio_index_ = (audio_fill_ratio_index_ + 1) % audio_fill_ratio_history_.size();
    audio_fill_ratio_count_ =
        std::min(audio_fill_ratio_count_ + 1, audio_fill_ratio_history_.size());

    return audio_fill_ratio_sum_ / static_cast<float>(audio_fill_ratio_count_);
}

void EmulationSession::reset_audio_fill_ratio_history() {
    audio_fill_ratio_history_.fill(0.0f);
    audio_fill_ratio_index_ = 0;
    audio_fill_ratio_count_ = 0;
    audio_fill_ratio_sum_ = 0.0f;
}

int EmulationSession::max_queued_samples() const {
    // Allow a refill above the reserve without immediately applying
    // backpressure; the DRC's default 50% target also stays at that reserve.
    return audio_low_watermark_samples() * 2;
}

core::Result<void> EmulationSession::reinitialize_audio_backend() {
    if (!audio_) {
        return std::unexpected(std::string("No audio backend available"));
    }

    audio_->shutdown();

    const int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
    if (!audio_->initialize(
            audio_settings_.sample_rate, audio_settings_.buffer_size_samples, channels)) {
        return std::unexpected(std::string("Failed to reinitialize audio backend"));
    }
    reconcile_audio_sample_rate();

    return {};
}

void EmulationSession::reconcile_audio_sample_rate() {
    const int device_rate = audio_->actual_sample_rate();
    if (device_rate <= 0 || device_rate == audio_settings_.sample_rate) {
        return;
    }
    // The device could not honor the requested rate and is silently resampling.
    // Reconfigure the APU to the real device rate so SDL3's hidden internal
    // resampler is taken out of the path — its variable buffer latency would
    // otherwise destabilize the DRC feedback loop (heard as crackle).
    audio_settings_.sample_rate = device_rate;
    emulator_->apply_audio_settings(audio_settings_);
}

core::Result<void> EmulationSession::reinitialize_video_backend() {
    if (!video_) {
        return std::unexpected(std::string("No video backend available"));
    }

    if (!video_->initialize(core::kScreenWidth, core::kScreenHeight)) {
        return std::unexpected(std::string("Failed to reinitialize video backend"));
    }

    return {};
}

} // namespace mapperbus::app
