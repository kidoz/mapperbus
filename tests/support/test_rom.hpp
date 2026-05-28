#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace mapperbus::tests {

[[nodiscard]] inline std::filesystem::path write_visible_nrom_test_rom(std::string_view name) {
    constexpr std::size_t kPrgSize = 16 * 1024;
    constexpr std::size_t kChrSize = 8 * 1024;
    constexpr std::uint16_t kResetAddress = 0x8000;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("mapperbus-" + std::string(name) + ".nes");

    std::vector<std::uint8_t> prg(kPrgSize, 0xEA);
    const std::uint8_t program[] = {
        0x78,             // SEI
        0xD8,             // CLD
        0xA2, 0xFF,       // LDX #$FF
        0x9A,             // TXS
        0xA9, 0x00,       // LDA #$00
        0x8D, 0x00, 0x20, // STA $2000
        0x8D, 0x01, 0x20, // STA $2001
        0x2C, 0x02, 0x20, // BIT $2002
        0x10, 0xFB,       // BPL wait
        0xA9, 0x3F,       // LDA #$3F
        0x8D, 0x06, 0x20, // STA $2006
        0xA9, 0x00,       // LDA #$00
        0x8D, 0x06, 0x20, // STA $2006
        0xA9, 0x0F,       // LDA #$0F
        0x8D, 0x07, 0x20, // STA $2007
        0xA9, 0x30,       // LDA #$30
        0x8D, 0x07, 0x20, // STA $2007
        0xA9, 0x16,       // LDA #$16
        0x8D, 0x07, 0x20, // STA $2007
        0xA9, 0x27,       // LDA #$27
        0x8D, 0x07, 0x20, // STA $2007
        0xA9, 0x00,       // LDA #$00
        0x8D, 0x05, 0x20, // STA $2005
        0x8D, 0x05, 0x20, // STA $2005
        0x8D, 0x00, 0x20, // STA $2000
        0xA9, 0x08,       // LDA #$08
        0x8D, 0x01, 0x20, // STA $2001
        0x4C, 0x40, 0x80, // JMP $8040
    };
    std::copy(std::begin(program), std::end(program), prg.begin());

    const std::size_t vector_offset = kPrgSize - 6;
    prg[vector_offset + 0] = static_cast<std::uint8_t>(kResetAddress & 0x00FF);
    prg[vector_offset + 1] = static_cast<std::uint8_t>(kResetAddress >> 8);
    prg[vector_offset + 2] = static_cast<std::uint8_t>(kResetAddress & 0x00FF);
    prg[vector_offset + 3] = static_cast<std::uint8_t>(kResetAddress >> 8);
    prg[vector_offset + 4] = static_cast<std::uint8_t>(kResetAddress & 0x00FF);
    prg[vector_offset + 5] = static_cast<std::uint8_t>(kResetAddress >> 8);

    std::vector<std::uint8_t> chr(kChrSize, 0);
    for (std::size_t row = 0; row < 8; ++row) {
        chr[row] = 0xAA;
        chr[row + 8] = 0xCC;
    }

    constexpr std::array<std::uint8_t, 16> kHeader = {
        'N',
        'E',
        'S',
        0x1A,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(kHeader.data()), kHeader.size());
    file.write(reinterpret_cast<const char*>(prg.data()), static_cast<std::streamsize>(prg.size()));
    file.write(reinterpret_cast<const char*>(chr.data()), static_cast<std::streamsize>(chr.size()));
    return path;
}

} // namespace mapperbus::tests
