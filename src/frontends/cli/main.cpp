#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>

#include "app/configuration.hpp"
#include "app/rom_source.hpp"
#include "core/emulator.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"

int main(int argc, char* argv[]) {
    std::string path;
    std::string member;
    auto bios = mapperbus::app::load_mapperbus_configuration().fds_bios_path;
    int frames = 60;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if ((argument == "--entry" || argument == "--fds-bios" || argument == "--frames") &&
            index + 1 < argc) {
            const std::string_view value(argv[++index]);
            if (argument == "--entry")
                member = value;
            else if (argument == "--fds-bios")
                bios = value;
            else {
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), frames);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    frames < 1 || frames > 1'000'000) {
                    mapperbus::core::logger::error(
                        "--frames requires an integer from 1 to 1000000");
                    return EXIT_FAILURE;
                }
            }
        } else if (!argument.starts_with("--") && path.empty())
            path = argument;
        else {
            mapperbus::core::logger::error("Unknown or incomplete argument: {}", argument);
            return EXIT_FAILURE;
        }
    }
    if (path.empty()) {
        mapperbus::core::logger::error("Usage: mapperbus-cli <rom-file|archive> [--entry NAME] "
                                       "[--fds-bios PATH] [--frames N]");
        return EXIT_FAILURE;
    }
    mapperbus::core::register_builtin_mappers();
    mapperbus::core::Emulator emulator;
    auto result = mapperbus::app::load_rom_source(emulator, path, member, bios);
    if (!result) {
        mapperbus::core::logger::error("{}", result.error());
        return EXIT_FAILURE;
    }
    emulator.reset();
    for (int frame = 0; frame < frames; ++frame)
        emulator.step_frame();
    if (auto saved = emulator.flush_disk_changes(); !saved) {
        mapperbus::core::logger::error("{}", saved.error());
        return EXIT_FAILURE;
    }
    mapperbus::core::logger::info("Ran {} frames successfully.", frames);
    return EXIT_SUCCESS;
}
