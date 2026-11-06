#include <cstdlib>
#include <string_view>

#include "app/rom_source.hpp"
#include "core/emulator.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2 && !(argc == 4 && std::string_view(argv[2]) == "--entry")) {
        mapperbus::core::logger::error(
            "Usage: mapperbus-cli <rom-file|archive.zip|archive.7z> [--entry member.nes]");
        return EXIT_FAILURE;
    }

    mapperbus::core::register_builtin_mappers();

    mapperbus::core::Emulator emulator;
    auto result = mapperbus::app::load_rom_source(
        emulator, argv[1], argc == 4 ? std::string_view(argv[3]) : std::string_view{});
    if (!result) {
        mapperbus::core::logger::error("{}", result.error());
        return EXIT_FAILURE;
    }

    emulator.reset();

    constexpr int kFrames = 60;
    for (int i = 0; i < kFrames; ++i) {
        emulator.step_frame();
    }

    mapperbus::core::logger::info("Ran {} frames successfully.", kFrames);
    return EXIT_SUCCESS;
}
