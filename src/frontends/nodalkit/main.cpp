#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>

#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_controller.hpp"

int main(int argc, char** argv) {
    mapperbus::core::register_builtin_mappers();

    nk::Application app({.app_id = "dev.mapperbus.gui", .app_name = "mapperbus"});
    // Let the theme resolution pipeline pick the platform family (GNOME /
    // Windows 11 / macOS 26), color scheme, and system accent. Density comes
    // from the saved configuration once the controller loads it.
    app.set_theme_selection(nk::ThemeSelection{});

    nk::Window window({
        .title = "mapperbus",
        .width = 1120,
        .height = 900,
    });

    mapperbus::frontend::MapperBusGuiController controller(app, window);
    if (argc > 1 && argv[1] != nullptr) {
        controller.open_initial_rom(argv[1]);
    }

    window.present();
    return app.run();
}
