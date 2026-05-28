#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <unordered_set>

#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"

using namespace mapperbus::core;

TEST_CASE("Generated NROM renders visible content after 180 frames", "[integration][render]") {
    register_builtin_mappers();

    const std::filesystem::path rom_path = mapperbus::tests::write_visible_nrom_test_rom("render");
    REQUIRE(std::filesystem::exists(rom_path));

    Emulator emulator;
    auto result = emulator.load_cartridge(rom_path.string());
    REQUIRE(result.has_value());

    emulator.reset();

    for (int frame = 0; frame < 180; ++frame) {
        emulator.step_frame();
    }

    const auto& framebuffer = emulator.frame_buffer();
    const auto& ppu = emulator.ppu();

    std::unordered_set<std::uint32_t> unique_colors;
    unique_colors.reserve(16);

    std::size_t non_black_pixels = 0;
    for (std::uint32_t pixel : framebuffer.pixels) {
        unique_colors.insert(pixel);
        if (pixel != 0xFF000000) {
            ++non_black_pixels;
        }
        if (unique_colors.size() >= 8 && non_black_pixels >= 2048) {
            break;
        }
    }

    std::size_t non_zero_vram =
        std::count_if(ppu.vram().begin(), ppu.vram().end(), [](Byte value) { return value != 0; });
    std::size_t non_zero_palette = std::count_if(
        ppu.palette_ram().begin(), ppu.palette_ram().end(), [](Byte value) { return value != 0; });

    INFO("unique_colors=" << unique_colors.size() << " non_black_pixels=" << non_black_pixels
                          << " non_zero_vram=" << non_zero_vram << " non_zero_palette="
                          << non_zero_palette << " ppuctrl=" << static_cast<int>(ppu.ppuctrl())
                          << " ppumask=" << static_cast<int>(ppu.ppumask()));

    REQUIRE(unique_colors.size() >= 3);
    REQUIRE(non_black_pixels >= 2048);
}
