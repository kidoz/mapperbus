#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <nk/platform/windows_interop.h>
#include <windows.h>
#endif

#include <cstdlib>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>
#include <string>
#include <string_view>
#include <utility>

#if defined(MAPPERBUS_HAVE_SDL3_AUDIO) || defined(MAPPERBUS_HAVE_SDL3_GAMEPAD)
#include <SDL3/SDL.h>
#endif

#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_controller.hpp"

int main(int argc, char** argv) try {
    std::string_view rom_path;
    std::string dcs_option_path;
    mapperbus::app::CartridgeLaunchOptions cartridge_options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--dcs-option" || argument == "--game-doctor-bios") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).empty() ||
                std::string_view(argv[i + 1]).starts_with("--")) {
                mapperbus::core::logger::error("{} requires a ROM path", argument);
                return EXIT_FAILURE;
            }
            (argument == "--dcs-option" ? dcs_option_path
                                        : cartridge_options.game_doctor_bios_path) = argv[++i];
        } else if (argument == "--vs-dips") {
            cartridge_options.vs_dips =
                i + 1 < argc ? mapperbus::app::parse_vs_dips(argv[++i]) : std::nullopt;
            if (!cartridge_options.vs_dips) {
                mapperbus::core::logger::error("--vs-dips requires a byte value (0–255)");
                return EXIT_FAILURE;
            }
        } else if (!argument.starts_with("--") && rom_path.empty()) {
            rom_path = argument;
        } else {
            mapperbus::core::logger::error("Usage: mapperbus-gui [ROM [--dcs-option PATH] "
                                           "[--game-doctor-bios PATH] [--vs-dips BYTE]]");
            return EXIT_FAILURE;
        }
    }
    if (rom_path.empty() &&
        (!dcs_option_path.empty() || !cartridge_options.game_doctor_bios_path.empty() ||
         cartridge_options.vs_dips)) {
        mapperbus::core::logger::error("Cartridge launch options require a ROM path");
        return EXIT_FAILURE;
    }
#if defined(MAPPERBUS_HAVE_SDL3_AUDIO) || defined(MAPPERBUS_HAVE_SDL3_GAMEPAD)
    // NodalKit owns the event loop. SDL quit events produced by its POSIX
    // signal handlers would otherwise go unconsumed in this frontend.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#endif
    mapperbus::core::register_builtin_mappers();

    nk::Application app({.app_id = "dev.mapperbus.gui", .app_name = "MapperBus"});
    // Let the theme resolution pipeline pick the platform family (GNOME /
    // Windows 11 / macOS 26), color scheme, and system accent. Density comes
    // from the saved configuration once the controller loads it.
    app.set_theme_selection(nk::ThemeSelection{});

    nk::Window window({
        .title = "MapperBus",
        .width = 1120,
        .height = 900,
    });

    mapperbus::frontend::MapperBusGuiController controller(app, window);
    if (!rom_path.empty()) {
        controller.open_initial_rom(rom_path, dcs_option_path, std::move(cartridge_options));
    }

    window.present();
#if defined(_WIN32)
    // NodalKit leaves application artwork to the executable. LR_SHARED keeps
    // these resource handles valid for the process lifetime without ownership.
    if (const auto hwnd = nk::window_hwnd(window)) {
        const auto instance = GetModuleHandleW(nullptr);
        const auto large = LoadImageW(instance,
                                      MAKEINTRESOURCEW(1),
                                      IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON),
                                      LR_SHARED);
        const auto small_icon = LoadImageW(instance,
                                           MAKEINTRESOURCEW(1),
                                           IMAGE_ICON,
                                           GetSystemMetrics(SM_CXSMICON),
                                           GetSystemMetrics(SM_CYSMICON),
                                           LR_SHARED);
        if (large) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        }
        if (small_icon) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
        }
    }
#endif
    return app.run();
} catch (...) {
    return EXIT_FAILURE;
}
