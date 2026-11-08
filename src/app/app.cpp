#include "app/app.hpp"

#include <chrono>
#include <thread>

namespace mapperbus::app {

App::App(std::unique_ptr<platform::VideoBackend> video,
         std::unique_ptr<platform::AudioBackend> audio,
         std::unique_ptr<platform::InputBackend> input,
         const core::AudioSettings& audio_settings)
    : session_(std::move(video), std::move(audio), std::move(input), audio_settings) {}

App::~App() = default;

core::Result<void> App::initialize(const std::string& rom_path,
                                   std::string_view member,
                                   const std::string& dcs_option_path) {
    auto result = session_.initialize();
    if (!result) {
        return result;
    }

    return session_.open_rom(rom_path, member, dcs_option_path);
}

void App::run(const std::function<void(EmulationSession&)>& after_tick) {
    while (session_.running()) {
        switch (session_.tick()) {
        case TickResult::AudioBackpressure:
        case TickResult::Paused:
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            break;
        case TickResult::FrameAdvanced:
        case TickResult::NoCartridge:
            break;
        case TickResult::Stopped:
            return;
        }

        if (after_tick) {
            after_tick(session_);
        }
    }
}

} // namespace mapperbus::app
