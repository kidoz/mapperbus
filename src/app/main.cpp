#include <cstdlib>
#include <cstring>

#include "app/app.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/gpu_fsr_upscaler.hpp"
#include "frontends/sdl3/gpu_upscaler.hpp"
#include "frontends/sdl3/sdl3_audio.hpp"
#include "frontends/sdl3/sdl3_input.hpp"
#include "frontends/sdl3/sdl3_video.hpp"
#include "platform/video/fsr1.hpp"
#include "platform/video/xbrz.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        mapperbus::core::logger::error("Usage: mapperbus-sdl3 [options] <rom-file>");
        mapperbus::core::logger::info("  --scale N             xBRZ/FSR upscale factor (2-6)");
        mapperbus::core::logger::info("  --gpu                 GPU-accelerated upscaling (xBRZ)");
        mapperbus::core::logger::info("  --gpu-fsr             Use Native GPU MSL FSR1 upscaling");
        mapperbus::core::logger::info("  --fsr                 Use CPU FSR 1.0 upscaling");
        mapperbus::core::logger::info("  --sample-rate N       audio sample rate (default: 96000)");
        mapperbus::core::logger::info("  --resampling MODE     blip or cubic (default: blip)");
        mapperbus::core::logger::info(
            "  --filter-mode MODE    accurate, enhanced, or unfiltered (default: unfiltered)");
        mapperbus::core::logger::info(
            "  --region R            ntsc, pal, or dendy (default: auto)");
        mapperbus::core::logger::info("  --filter-profile P    nes or famicom (default: nes)");
        mapperbus::core::logger::info("  --stereo              enable pseudo-stereo output");
        mapperbus::core::logger::info("  --dither              enable TPDF dithering");
        mapperbus::core::logger::info(
            "  --expansion-mixing M  simple or resistance (default: simple)");
        return EXIT_FAILURE;
    }

    int upscale_factor = 0;
    bool use_gpu = false;
    bool use_gpu_fsr = false;
    bool use_fsr = false;
    const char* region_override = nullptr;
    const char* rom_path = nullptr;
    mapperbus::core::AudioSettings audio_settings;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            upscale_factor = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--gpu") == 0) {
            use_gpu = true;
        } else if (std::strcmp(argv[i], "--gpu-fsr") == 0) {
            use_gpu_fsr = true;
        } else if (std::strcmp(argv[i], "--fsr") == 0) {
            use_fsr = true;
        } else if (std::strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
            audio_settings.sample_rate = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--resampling") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "cubic") == 0) {
                audio_settings.resampling = mapperbus::core::ResamplingMode::CubicHermite;
            } else {
                audio_settings.resampling = mapperbus::core::ResamplingMode::BlipBuffer;
            }
        } else if (std::strcmp(argv[i], "--filter-mode") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "enhanced") == 0) {
                audio_settings.filter_mode = mapperbus::core::FilterMode::Enhanced;
            } else if (std::strcmp(argv[i], "accurate") == 0) {
                audio_settings.filter_mode = mapperbus::core::FilterMode::HardwareAccurate;
            } else {
                audio_settings.filter_mode = mapperbus::core::FilterMode::Unfiltered;
            }
        } else if (std::strcmp(argv[i], "--filter-profile") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "famicom") == 0) {
                audio_settings.filter_profile = mapperbus::core::FilterProfile::Famicom;
            } else {
                audio_settings.filter_profile = mapperbus::core::FilterProfile::NES;
            }
        } else if (std::strcmp(argv[i], "--stereo") == 0) {
            audio_settings.stereo_mode = mapperbus::core::StereoMode::PseudoStereo;
        } else if (std::strcmp(argv[i], "--dither") == 0) {
            audio_settings.dithering_enabled = true;
        } else if (std::strcmp(argv[i], "--expansion-mixing") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "resistance") == 0) {
                audio_settings.expansion_mixing =
                    mapperbus::core::ExpansionMixingMode::ResistanceModeled;
            } else {
                audio_settings.expansion_mixing = mapperbus::core::ExpansionMixingMode::SimpleSum;
            }
        } else if (std::strcmp(argv[i], "--region") == 0 && i + 1 < argc) {
            region_override = argv[++i];
        } else {
            rom_path = argv[i];
        }
    }

    if (rom_path == nullptr) {
        mapperbus::core::logger::error("No ROM file specified");
        return EXIT_FAILURE;
    }

    mapperbus::core::register_builtin_mappers();

    // --- Composition root: create and configure backends ---
    auto video = std::make_unique<mapperbus::frontend::Sdl3Video>();

    if (upscale_factor >= 2 && upscale_factor <= 6) {
        if (use_gpu_fsr) {
            video->set_upscaler(
                std::make_unique<mapperbus::frontend::GpuFsr1Upscaler>(upscale_factor));
        } else if (use_fsr) {
            video->set_upscaler(
                std::make_unique<mapperbus::platform::Fsr1Upscaler>(upscale_factor));
        } else if (use_gpu) {
            video->set_upscaler(std::make_unique<mapperbus::frontend::GpuUpscaler>(upscale_factor));
        } else {
            video->set_upscaler(
                std::make_unique<mapperbus::platform::XbrzUpscaler>(upscale_factor));
        }
    }

    auto audio = std::make_unique<mapperbus::frontend::Sdl3Audio>();
    auto input = std::make_unique<mapperbus::frontend::Sdl3Input>();

    // --- Wire into App and run ---
    mapperbus::app::App app(std::move(video), std::move(audio), std::move(input), audio_settings);
    auto result = app.initialize(rom_path);
    if (!result) {
        mapperbus::core::logger::error("{}", result.error());
        return EXIT_FAILURE;
    }

    if (region_override != nullptr) {
        if (std::strcmp(region_override, "ntsc") == 0) {
            app.session().set_region(mapperbus::core::Region::NTSC);
            mapperbus::core::logger::info("Forcing region to NTSC");
        } else if (std::strcmp(region_override, "pal") == 0) {
            app.session().set_region(mapperbus::core::Region::PAL);
            mapperbus::core::logger::info("Forcing region to PAL");
        } else if (std::strcmp(region_override, "dendy") == 0) {
            app.session().set_region(mapperbus::core::Region::Dendy);
            mapperbus::core::logger::info("Forcing region to Dendy");
        } else {
            mapperbus::core::logger::warn("Unknown region '{}', ignoring", region_override);
        }
    }

    app.run();
    return EXIT_SUCCESS;
}
