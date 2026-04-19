#include "app/session_actions.hpp"

namespace mapperbus::app {

SessionActions::SessionActions(EmulationSession& session) : session_(session) {}

core::Result<void> SessionActions::open_rom(const std::string& rom_path) {
    return session_.open_rom(rom_path);
}

void SessionActions::close_rom() {
    session_.close_rom();
}

void SessionActions::pause() {
    session_.set_paused(true);
}

void SessionActions::resume() {
    session_.set_paused(false);
}

void SessionActions::toggle_pause() {
    session_.set_paused(!session_.paused());
}

void SessionActions::reset() {
    session_.reset();
}

void SessionActions::power_cycle() {
    session_.power_cycle();
}

void SessionActions::stop() {
    session_.request_stop();
}

void SessionActions::set_region(core::Region region) {
    session_.set_region(region);
}

core::Result<void> SessionActions::apply_audio_settings(const core::AudioSettings& settings) {
    return session_.apply_audio_settings(settings);
}

core::Result<void> SessionActions::set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) {
    return session_.set_upscaler(std::move(upscaler));
}

TickResult SessionActions::tick() {
    return session_.tick();
}

TickResult SessionActions::step_frame() {
    return session_.step_frame();
}

int SessionActions::audio_queued_samples() const {
    return session_.audio_queued_samples();
}

int SessionActions::audio_low_watermark_samples() const {
    return session_.audio_low_watermark_samples();
}

int SessionActions::audio_high_watermark_samples() const {
    return session_.audio_high_watermark_samples();
}

SessionSnapshot SessionActions::snapshot() const {
    return {
        .initialized = session_.initialized(),
        .running = session_.running(),
        .paused = session_.paused(),
        .has_cartridge = session_.has_cartridge(),
        .rom_path = session_.current_rom_path(),
        .region = session_.emulator().region(),
        .audio_settings = session_.audio_settings(),
    };
}

} // namespace mapperbus::app
