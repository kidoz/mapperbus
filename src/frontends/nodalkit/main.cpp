#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/style/theme_selection.h>

#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_controller.hpp"

int main(int argc, char** argv) {
    mapperbus::core::register_builtin_mappers();

    nk::Application app({.app_id = "dev.mapperbus.gui", .app_name = "mapperbus"});
    nk::ThemeSelection theme_selection;
    if (app.system_preferences().platform_family == nk::PlatformFamily::Linux) {
        theme_selection.family = nk::ThemeFamily::LinuxGnome;
        theme_selection.density = nk::ThemeDensity::Comfortable;
        theme_selection.accent_color_override = nk::Color::from_rgb(38, 126, 122);
    }
    app.set_theme_selection(theme_selection);

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
