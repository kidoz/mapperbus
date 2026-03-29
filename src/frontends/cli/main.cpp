#include <cstdlib>

#include "core/emulator.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        mapperbus::core::logger::error("Usage: mapperbus-cli <rom-file>");
        return EXIT_FAILURE;
    }

    mapperbus::core::register_builtin_mappers();

    mapperbus::core::Emulator emulator;
    auto result = emulator.load_cartridge(argv[1]);
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
