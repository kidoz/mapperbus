#include <charconv>
#include <cstdlib>
#include <optional>
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
    std::string dcs_option;
    std::string board_profile;
    std::string datach_save;
    std::string barcode;
    uint32_t barcode_cycles = 1000U;
    int barcode_frame = 0;
    bool barcode_timing = false;
    const auto configuration = mapperbus::app::load_mapperbus_configuration();
    auto bios = configuration.fds_bios_path;
    mapperbus::app::ControllerHardwareOverrides controller_overrides;
    std::optional<mapperbus::core::Region> region_override;
    int frames = 60;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if ((argument == "--entry" || argument == "--fds-bios" || argument == "--dcs-option" ||
             argument == "--frames" || argument == "--region" || argument == "--barcode" ||
             argument == "--barcode-frame" || argument == "--barcode-cycles" ||
             argument == "--datach-save" || argument == "--board-profile" ||
             argument == "--controller-timing" || argument == "--controller-wiring" ||
             argument == "--controller-mode" || argument == "--expansion-controllers") &&
            index + 1 < argc) {
            const std::string_view value(argv[++index]);
            if (argument == "--entry")
                member = value;
            else if (argument == "--fds-bios")
                bios = value;
            else if (argument == "--board-profile") {
                if (value.empty() || value.starts_with("--")) {
                    mapperbus::core::logger::error("--board-profile requires a profile name");
                    return EXIT_FAILURE;
                }
                board_profile = value;
            } else if (argument == "--datach-save" || argument == "--barcode") {
                if (value.empty() || value.starts_with("--")) {
                    mapperbus::core::logger::error("{} requires a value", argument);
                    return EXIT_FAILURE;
                }
                (argument == "--barcode" ? barcode : datach_save) = value;
            } else if (argument == "--barcode-frame" || argument == "--barcode-cycles") {
                int number = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    number < (argument == "--barcode-frame" ? 0 : 1) || number > 1'000'000) {
                    mapperbus::core::logger::error("Invalid Datach timing value for {}", argument);
                    return EXIT_FAILURE;
                }
                barcode_timing = true;
                if (argument == "--barcode-frame")
                    barcode_frame = number;
                else
                    barcode_cycles = static_cast<uint32_t>(number);
            } else if (argument == "--dcs-option") {
                if (value.empty() || value.starts_with("--")) {
                    mapperbus::core::logger::error("--dcs-option requires a ROM path");
                    return EXIT_FAILURE;
                }
                dcs_option = value;
            } else if (argument == "--region") {
                region_override = mapperbus::app::parse_region_override(value);
                if (!region_override) {
                    mapperbus::core::logger::error("--region requires ntsc, pal, or hybrid");
                    return EXIT_FAILURE;
                }
            } else if (argument == "--controller-timing") {
                const auto timing = mapperbus::app::parse_controller_timing(value);
                if (!timing) {
                    mapperbus::core::logger::error(
                        "--controller-timing requires nes-av-famicom or rf-famicom");
                    return EXIT_FAILURE;
                }
                controller_overrides.timing = *timing;
            } else if (argument == "--controller-wiring") {
                const auto wiring = mapperbus::app::parse_controller_wiring(value);
                if (!wiring) {
                    mapperbus::core::logger::error(
                        "--controller-wiring requires nes or rf-famicom");
                    return EXIT_FAILURE;
                }
                controller_overrides.wiring = *wiring;
            } else if (argument == "--controller-mode") {
                const auto mode = mapperbus::app::parse_controller_mode(value);
                if (!mode) {
                    mapperbus::core::logger::error(
                        "--controller-mode requires standard, four-score, or hori-4-player");
                    return EXIT_FAILURE;
                }
                controller_overrides.mode = *mode;
            } else if (argument == "--expansion-controllers") {
                const auto ports = mapperbus::app::parse_expansion_controllers(value);
                if (!ports) {
                    mapperbus::core::logger::error(
                        "--expansion-controllers requires none, 1, 2, or both");
                    return EXIT_FAILURE;
                }
                controller_overrides.expansion_controllers = *ports;
            } else {
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
    if ((barcode.empty() && barcode_timing) || (!barcode.empty() && barcode_frame >= frames)) {
        mapperbus::core::logger::error(
            "Barcode timing requires --barcode and a frame before --frames");
        return EXIT_FAILURE;
    }
    if (path.empty()) {
        mapperbus::core::logger::error(
            "Usage: mapperbus-cli <rom-file|archive> [--entry NAME] "
            "[--fds-bios PATH] [--dcs-option PATH] [--board-profile NAME] [--frames N] [--region "
            "ntsc|pal|hybrid] "
            "[--controller-timing nes-av-famicom|rf-famicom] "
            "[--controller-wiring nes|rf-famicom] [--controller-mode "
            "standard|four-score|hori-4-player] "
            "[--expansion-controllers none|1|2|both] "
            "[--barcode DIGITS] [--barcode-frame N] [--barcode-cycles N] [--datach-save PATH]");
        return EXIT_FAILURE;
    }
    mapperbus::core::register_builtin_mappers();
    mapperbus::core::Emulator emulator;
    auto result = mapperbus::app::load_rom_source(
        emulator, path, member, bios, {}, {}, dcs_option, datach_save, board_profile);
    if (!result) {
        mapperbus::core::logger::error("{}", result.error());
        return EXIT_FAILURE;
    }
    mapperbus::app::apply_controller_hardware(
        emulator,
        mapperbus::app::controller_hardware_for_game(
            configuration,
            mapperbus::core::sha256_hex(emulator.content_hash()),
            controller_overrides));
    if (region_override)
        emulator.set_region(*region_override);
    emulator.power_on();
    for (int frame = 0; frame < frames; ++frame) {
        if (!barcode.empty() && frame == barcode_frame) {
            if (auto scanned = emulator.scan_barcode(barcode, barcode_cycles); !scanned) {
                mapperbus::core::logger::error("{}", scanned.error());
                return EXIT_FAILURE;
            }
        }
        emulator.step_frame();
    }
    if (auto saved = emulator.flush_persistent_data(); !saved) {
        mapperbus::core::logger::error("{}", saved.error());
        return EXIT_FAILURE;
    }
    mapperbus::core::logger::info("Ran {} frames successfully.", frames);
    return EXIT_SUCCESS;
}
