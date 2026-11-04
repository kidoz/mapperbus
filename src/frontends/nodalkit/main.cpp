#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <nk/platform/windows_interop.h>
#include <windows.h>
#endif

#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>

#if defined(MAPPERBUS_HAVE_SDL3_AUDIO) || defined(MAPPERBUS_HAVE_SDL3_GAMEPAD)
#include <SDL3/SDL.h>
#endif

#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_controller.hpp"

int main(int argc, char** argv) {
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
    if (argc > 1 && argv[1] != nullptr) {
        controller.open_initial_rom(argv[1]);
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
        const auto small = LoadImageW(instance,
                                      MAKEINTRESOURCEW(1),
                                      IMAGE_ICON,
                                      GetSystemMetrics(SM_CXSMICON),
                                      GetSystemMetrics(SM_CYSMICON),
                                      LR_SHARED);
        if (large) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        }
        if (small) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        }
    }
#endif
    return app.run();
}
