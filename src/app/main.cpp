#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>

#include "app/app.hpp"
#include "app/configuration.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/gpu_fsr_upscaler.hpp"
#include "frontends/sdl3/gpu_upscaler.hpp"
#include "frontends/sdl3/sdl3_audio.hpp"
#include "frontends/sdl3/sdl3_input.hpp"
#include "frontends/sdl3/sdl3_microphone.hpp"
#include "frontends/sdl3/sdl3_video.hpp"
#include "platform/video/fsr1.hpp"
#include "platform/video/xbrz.hpp"

namespace {

enum class RuntimeScalerMode {
    Xbrz,
    Fsr,
    GpuXbrz,
    GpuFsr,
};

RuntimeScalerMode runtime_scaler_mode_from_flags(bool use_gpu, bool use_gpu_fsr, bool use_fsr) {
    if (use_gpu_fsr) {
        return RuntimeScalerMode::GpuFsr;
    }
    if (use_fsr) {
        return RuntimeScalerMode::Fsr;
    }
    if (use_gpu) {
        return RuntimeScalerMode::GpuXbrz;
    }
    return RuntimeScalerMode::Xbrz;
}

RuntimeScalerMode next_runtime_scaler_mode(RuntimeScalerMode mode) {
    switch (mode) {
    case RuntimeScalerMode::Xbrz:
        return RuntimeScalerMode::Fsr;
    case RuntimeScalerMode::Fsr:
        return RuntimeScalerMode::GpuXbrz;
    case RuntimeScalerMode::GpuXbrz:
        return RuntimeScalerMode::GpuFsr;
    case RuntimeScalerMode::GpuFsr:
    default:
        return RuntimeScalerMode::Xbrz;
    }
}

std::string_view runtime_scaler_mode_label(RuntimeScalerMode mode) {
    switch (mode) {
    case RuntimeScalerMode::Fsr:
        return "CPU FSR";
    case RuntimeScalerMode::GpuXbrz:
        return "GPU xBRZ";
    case RuntimeScalerMode::GpuFsr:
        return "GPU FSR";
    case RuntimeScalerMode::Xbrz:
    default:
        return "CPU xBRZ";
    }
}

std::unique_ptr<mapperbus::platform::Upscaler> make_runtime_upscaler(RuntimeScalerMode mode,
                                                                     int factor) {
    if (factor < 2 || factor > 6) {
        return nullptr;
    }

    switch (mode) {
    case RuntimeScalerMode::Fsr:
        return std::make_unique<mapperbus::platform::Fsr1Upscaler>(factor);
    case RuntimeScalerMode::GpuXbrz:
        return std::make_unique<mapperbus::frontend::GpuUpscaler>(factor);
    case RuntimeScalerMode::GpuFsr:
        return std::make_unique<mapperbus::frontend::GpuFsr1Upscaler>(factor);
    case RuntimeScalerMode::Xbrz:
    default:
        return std::make_unique<mapperbus::platform::XbrzUpscaler>(factor);
    }
}

} // namespace

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
            "  --filter-mode MODE    accurate, enhanced, or unfiltered (default: accurate)");
        mapperbus::core::logger::info(
            "  --region R            ntsc, pal, or dendy (default: auto)");
        mapperbus::core::logger::info(
            "  --controller-timing T nes-av-famicom (default) or rf-famicom");
        mapperbus::core::logger::info("  --controller-wiring W nes (default) or rf-famicom");
        mapperbus::core::logger::info("  --expansion-controllers P none (default), 1, 2, or both");
        mapperbus::core::logger::info(
            "  --gamepad3/4 N / --gamepad3-map / --gamepad4-map external pad profiles");
        mapperbus::core::logger::info(
            "  --live-microphone     enable host microphone activity for RF wiring");
        mapperbus::core::logger::info("  --no-live-microphone  disable host capture for this run");
        mapperbus::core::logger::info("  --microphone-device N recording device name, or default");
        mapperbus::core::logger::info(
            "  --microphone-sensitivity N 1-100; higher detects quieter sound");
        mapperbus::core::logger::info("  --microphone-audio    mix captured sound into playback");
        mapperbus::core::logger::info("  --no-microphone-audio disable microphone playback");
        mapperbus::core::logger::info(
            "  --microphone-volume N playback level, 0-100 (default: 25)");
        mapperbus::core::logger::info("  --filter-profile P    nes or famicom (default: nes)");
        mapperbus::core::logger::info("  --stereo              enable pseudo-stereo output");
        mapperbus::core::logger::info("  --dither              enable TPDF dithering");
        mapperbus::core::logger::info(
            "  --expansion-mixing M  simple or resistance (default: simple)");
        mapperbus::core::logger::info("  --gamepad N           SDL gamepad index (default: 0)");
        mapperbus::core::logger::info(
            "  --gamepad-deadzone N  analog axis deadzone (default: 12000)");
        mapperbus::core::logger::info(
            "  --gamepad-map MAP     e.g. a=east,b=south,start=start,select=back");
        mapperbus::core::logger::info("  --gamepad2 N          SDL gamepad index for player 2");
        mapperbus::core::logger::info(
            "  --gamepad2-map MAP    player 2 gamepad mapping (same format as --gamepad-map)");
        mapperbus::core::logger::info(
            "Runtime scaler hotkeys: 0/1 native, 2-6 scale factor, F9 cycle scaler mode");
        mapperbus::core::logger::info(
            "Save-state: F5 save, F7 load; Ctrl+1..9/0 selects slots 1..10 (Shift+Fx: slot 2)");
        mapperbus::core::logger::info(
            "  --entry NAME          ROM member inside a .zip or .7z archive");
        mapperbus::core::logger::info("  --fds-bios PATH       8 KiB Famicom Disk System BIOS");
        mapperbus::core::logger::info(
            "FDS: F6 next side, Shift+F6 eject/insert, F8 toggle write protection");
        mapperbus::core::logger::info("  --state-dir PATH      absolute save-state root override");
        mapperbus::core::logger::info(
            "  --portable-states     store states beside the ROM collection");
        mapperbus::core::logger::info("Window: F11 toggle fullscreen, V toggle vsync");
        return EXIT_FAILURE;
    }

    int upscale_factor = 0;
    bool use_gpu = false;
    bool use_gpu_fsr = false;
    bool use_fsr = false;
    const char* region_override = nullptr;
    const char* rom_path = nullptr;
    std::string_view rom_member;
    std::string fds_bios;
    auto mapperbus_config = mapperbus::app::load_mapperbus_configuration();
    auto microphone_settings = mapperbus_config.microphone;
    auto controller_timing = mapperbus_config.input.controller_timing;
    auto controller_wiring = mapperbus_config.input.controller_wiring;
    auto expansion_controllers = mapperbus_config.input.expansion_controllers;
    auto state_storage = mapperbus_config.save_states;
    fds_bios = mapperbus_config.fds_bios_path;
    mapperbus::core::AudioSettings audio_settings = mapperbus_config.audio;
    mapperbus::frontend::Sdl3InputConfig input_config = mapperbus_config.input.gamepad;
    mapperbus::frontend::Sdl3InputConfig input_config2 = mapperbus_config.input.player2_gamepad;
    auto input_config3 = mapperbus_config.input.gamepad_profile(2);
    auto input_config4 = mapperbus_config.input.gamepad_profile(3);

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
        } else if (std::strcmp(argv[i], "--gamepad") == 0 && i + 1 < argc) {
            input_config.gamepad_index = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--gamepad-deadzone") == 0 && i + 1 < argc) {
            input_config.axis_deadzone =
                static_cast<std::int16_t>(std::clamp(std::atoi(argv[++i]), 1, 32767));
        } else if (std::strcmp(argv[i], "--gamepad-map") == 0 && i + 1 < argc) {
            auto result = mapperbus::frontend::apply_gamepad_mapping(input_config, argv[++i]);
            if (!result) {
                mapperbus::core::logger::error("Invalid gamepad map: {}", result.error());
                return EXIT_FAILURE;
            }
        } else if (std::strcmp(argv[i], "--gamepad2") == 0 && i + 1 < argc) {
            input_config2.gamepad_index = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--gamepad2-map") == 0 && i + 1 < argc) {
            auto result = mapperbus::frontend::apply_gamepad_mapping(input_config2, argv[++i]);
            if (!result) {
                mapperbus::core::logger::error("Invalid gamepad2 map: {}", result.error());
                return EXIT_FAILURE;
            }
        } else if (std::strcmp(argv[i], "--gamepad3") == 0 ||
                   std::strcmp(argv[i], "--gamepad4") == 0) {
            auto& target = std::strcmp(argv[i], "--gamepad3") == 0 ? input_config3 : input_config4;
            const std::string_view value = i + 1 < argc ? argv[++i] : "";
            int index = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                index < 0) {
                mapperbus::core::logger::error("--gamepad3/4 requires a nonnegative device index");
                return EXIT_FAILURE;
            }
            target.gamepad_index = index;
        } else if (std::strcmp(argv[i], "--gamepad3-map") == 0 ||
                   std::strcmp(argv[i], "--gamepad4-map") == 0) {
            auto& target =
                std::strcmp(argv[i], "--gamepad3-map") == 0 ? input_config3 : input_config4;
            if (i + 1 >= argc) {
                mapperbus::core::logger::error("Missing external gamepad mapping");
                return EXIT_FAILURE;
            }
            auto mapped = mapperbus::frontend::apply_gamepad_mapping(target, argv[++i]);
            if (!mapped) {
                mapperbus::core::logger::error("{}", mapped.error());
                return EXIT_FAILURE;
            }
        } else if (std::strcmp(argv[i], "--expansion-controllers") == 0) {
            const auto ports = i + 1 < argc ? mapperbus::app::parse_expansion_controllers(argv[++i])
                                            : std::nullopt;
            if (!ports) {
                mapperbus::core::logger::error(
                    "--expansion-controllers requires none, 1, 2, or both");
                return EXIT_FAILURE;
            }
            expansion_controllers = *ports;
        } else if (std::strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
            state_storage.directory = argv[++i];
            if (!state_storage.directory.is_absolute()) {
                mapperbus::core::logger::error("--state-dir requires an absolute path");
                return EXIT_FAILURE;
            }
        } else if (std::strcmp(argv[i], "--portable-states") == 0) {
            state_storage.mode = mapperbus::app::StateStorageMode::Portable;
            state_storage.directory.clear();
        } else if (std::strcmp(argv[i], "--fds-bios") == 0 && i + 1 < argc) {
            fds_bios = argv[++i];
        } else if (std::strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            rom_member = argv[++i];
        } else if (std::strcmp(argv[i], "--live-microphone") == 0) {
            microphone_settings.enabled = true;
        } else if (std::strcmp(argv[i], "--no-live-microphone") == 0) {
            microphone_settings.enabled = false;
        } else if (std::strcmp(argv[i], "--microphone-device") == 0 && i + 1 < argc) {
            const std::string_view device(argv[++i]);
            microphone_settings.device = device == "default" ? "" : std::string(device);
        } else if (std::strcmp(argv[i], "--microphone-sensitivity") == 0) {
            const std::string_view value = i + 1 < argc ? argv[++i] : "";
            int sensitivity = 0;
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), sensitivity);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                sensitivity < 1 || sensitivity > 100) {
                mapperbus::core::logger::error(
                    "--microphone-sensitivity requires an integer from 1 to 100");
                return EXIT_FAILURE;
            }
            microphone_settings.sensitivity = sensitivity;
        } else if (std::strcmp(argv[i], "--microphone-device") == 0) {
            mapperbus::core::logger::error("--microphone-device requires a device name");
            return EXIT_FAILURE;
        } else if (std::strcmp(argv[i], "--microphone-audio") == 0) {
            microphone_settings.audio_enabled = true;
        } else if (std::strcmp(argv[i], "--no-microphone-audio") == 0) {
            microphone_settings.audio_enabled = false;
        } else if (std::strcmp(argv[i], "--microphone-volume") == 0) {
            const std::string_view value = i + 1 < argc ? argv[++i] : "";
            int volume = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), volume);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                volume < 0 || volume > 100) {
                mapperbus::core::logger::error(
                    "--microphone-volume requires an integer from 0 to 100");
                return EXIT_FAILURE;
            }
            microphone_settings.volume_percent = volume;
        } else if (std::strcmp(argv[i], "--controller-timing") == 0) {
            const auto timing =
                i + 1 < argc ? mapperbus::app::parse_controller_timing(argv[++i]) : std::nullopt;
            if (!timing) {
                mapperbus::core::logger::error(
                    "--controller-timing requires nes-av-famicom or rf-famicom");
                return EXIT_FAILURE;
            }
            controller_timing = *timing;
        } else if (std::strcmp(argv[i], "--controller-wiring") == 0) {
            const auto wiring =
                i + 1 < argc ? mapperbus::app::parse_controller_wiring(argv[++i]) : std::nullopt;
            if (!wiring) {
                mapperbus::core::logger::error("--controller-wiring requires nes or rf-famicom");
                return EXIT_FAILURE;
            }
            controller_wiring = *wiring;
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
    mapperbus::frontend::Sdl3FourPlayerInputConfig profiles;
    profiles.players = {input_config, input_config2, input_config3, input_config4};
    for (int player = 0; player < 4; ++player)
        profiles.keyboards[player] = mapperbus_config.input.keyboard_profile(player);
    auto input = std::make_unique<mapperbus::frontend::Sdl3Input>(profiles);
    auto* sdl_input = input.get();

    // --- Wire into App and run ---
    mapperbus::app::App app(std::move(video), std::move(audio), std::move(input), audio_settings);
    app.session().set_turbo_period(mapperbus_config.input.turbo_period_frames);
    app.session().set_state_storage(state_storage);
    app.session().set_fds_bios_path(fds_bios);
    app.session().set_controller_timing(controller_timing);
    app.session().set_controller_wiring(controller_wiring);
    app.session().set_expansion_controllers(expansion_controllers);
    app.session().set_microphone_backend(std::make_unique<mapperbus::frontend::Sdl3Microphone>());
    app.session().configure_live_microphone(microphone_settings);
    auto result = app.initialize(rom_path, rom_member);
    if (!result) {
        mapperbus::core::logger::error("{}", result.error());
        return EXIT_FAILURE;
    }

    sdl_input->set_active_slot(
        mapperbus::app::selected_state_slot(mapperbus_config, app.session().game_identity()));
    mapperbus::core::logger::info("Active state slot: {} (Ctrl+1..9/0 to select)",
                                  sdl_input->active_slot() + 1);
    const auto remember_slot = [&](int slot) {
        sdl_input->set_active_slot(slot);
        mapperbus::app::remember_state_slot(mapperbus_config, app.session().game_identity(), slot);
        if (auto saved = mapperbus::app::save_mapperbus_configuration(mapperbus_config); !saved) {
            mapperbus::core::logger::error("Could not remember state slot: {}", saved.error());
        }
    };

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

    RuntimeScalerMode runtime_scaler_mode =
        runtime_scaler_mode_from_flags(use_gpu, use_gpu_fsr, use_fsr);
    int runtime_scale_factor = (upscale_factor >= 2 && upscale_factor <= 6) ? upscale_factor : 0;

    app.run([&](mapperbus::app::EmulationSession& session) {
        if (const auto window_command = sdl_input->consume_window_command()) {
            if (window_command.kind ==
                mapperbus::frontend::Sdl3WindowCommandKind::ToggleFullscreen) {
                session.toggle_fullscreen();
            } else if (window_command.kind ==
                       mapperbus::frontend::Sdl3WindowCommandKind::ToggleVsync) {
                static bool vsync_on = false;
                vsync_on = !vsync_on;
                session.set_vsync(vsync_on);
            }
        }

        if (const auto session_command = sdl_input->consume_session_command()) {
            const auto kind = session_command.kind;
            using Command = mapperbus::frontend::Sdl3SessionCommandKind;
            if (kind == Command::NextDiskSide || kind == Command::EjectDisk ||
                kind == Command::ToggleDiskProtection) {
                const auto& drive = session.emulator().fds();
                mapperbus::core::Result<void> changed;
                if (kind == Command::NextDiskSide)
                    changed = session.insert_disk_side((drive.current_side() + 1) %
                                                       std::max(1, drive.side_count()));
                else if (kind == Command::EjectDisk)
                    changed = drive.current_side() < 0 ? session.insert_disk_side(0)
                                                       : session.eject_disk();
                else
                    changed = session.set_disk_write_protected(!drive.write_protected());
                if (!changed)
                    mapperbus::core::logger::error("{}", changed.error());
                else
                    mapperbus::core::logger::info("FDS side: {}, write protected: {}",
                                                  drive.current_side() + 1,
                                                  drive.write_protected());
            }
            const int slot = session_command.slot;
            if (session_command.kind == mapperbus::frontend::Sdl3SessionCommandKind::SlotSelect) {
                remember_slot(slot);
                mapperbus::core::logger::info("Selected state slot {}. F5 saves; F7 loads.",
                                              slot + 1);
            } else if (session_command.kind ==
                       mapperbus::frontend::Sdl3SessionCommandKind::SaveState) {
                if (auto saved = session.save_state(slot)) {
                    remember_slot(slot);
                    mapperbus::core::logger::info(
                        "Saved state (slot {}): {}", slot + 1, session.state_path_for_slot(slot));
                } else {
                    mapperbus::core::logger::error("{}", saved.error());
                }
            } else if (session_command.kind ==
                       mapperbus::frontend::Sdl3SessionCommandKind::LoadState) {
                const auto state_path = session.state_load_path_for_slot(slot);
                if (auto loaded = session.load_state(slot)) {
                    remember_slot(slot);
                    mapperbus::core::logger::info(
                        "Loaded state (slot {}): {}", slot + 1, state_path.value_or(""));
                } else {
                    mapperbus::core::logger::error("{}", loaded.error());
                }
            }
        }

        const auto command = sdl_input->consume_scaler_command();
        if (!command) {
            return;
        }

        if (command.kind == mapperbus::frontend::Sdl3ScalerCommandKind::CycleMode) {
            runtime_scaler_mode = next_runtime_scaler_mode(runtime_scaler_mode);
            mapperbus::core::logger::info("Runtime scaler mode: {}",
                                          runtime_scaler_mode_label(runtime_scaler_mode));
            if (runtime_scale_factor < 2) {
                return;
            }
        } else if (command.kind == mapperbus::frontend::Sdl3ScalerCommandKind::Native) {
            runtime_scale_factor = 0;
        } else if (command.kind == mapperbus::frontend::Sdl3ScalerCommandKind::SetFactor) {
            runtime_scale_factor = std::clamp(command.factor, 2, 6);
        }

        auto scaler_result =
            session.set_upscaler(make_runtime_upscaler(runtime_scaler_mode, runtime_scale_factor));
        if (!scaler_result) {
            mapperbus::core::logger::error("Failed to change scaler: {}", scaler_result.error());
            return;
        }

        if (runtime_scale_factor < 2) {
            mapperbus::core::logger::info("Runtime scaler disabled");
        } else {
            mapperbus::core::logger::info("Runtime scaler: {} {}x",
                                          runtime_scaler_mode_label(runtime_scaler_mode),
                                          runtime_scale_factor);
        }
    });
    if (auto saved = app.session().flush_disk_changes(); !saved) {
        mapperbus::core::logger::error("{}", saved.error());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
