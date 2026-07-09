#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

/// Builds a minimal iNES image and writes it to a temp path.
/// `prg_banks`/`chr_banks` are in NES units (16 KB PRG, 8 KB CHR).
/// `prg_program` is written at $8000 (or $C000 for 32 KB PRG). The reset
/// vector points to the program start. CHR is zero-filled. Use this to
/// synthesize CPU/mapper test ROMs without external dependencies.
[[nodiscard]] inline std::filesystem::path build_ines_rom(
    std::string_view name,
    uint8_t mapper,
    uint8_t prg_banks,
    uint8_t chr_banks,
    std::span<const uint8_t> prg_program,
    mapperbus::core::MirrorMode mirror = mapperbus::core::MirrorMode::Vertical) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("mapperbus-" + std::string(name) + ".nes");

    constexpr std::size_t kPrgBankSize = 16 * 1024;
    constexpr std::size_t kChrBankSize = 8 * 1024;

    std::vector<uint8_t> prg(static_cast<std::size_t>(prg_banks) * kPrgBankSize, 0xEA);
    const std::size_t prog_start = (prg_banks == 1) ? 0 : kPrgBankSize;
    if (!prg_program.empty()) {
        const std::size_t max_copy = prg.size() - prog_start - 6;
        const std::size_t n = std::min(prg_program.size(), max_copy);
        std::copy_n(prg_program.begin(), n, prg.begin() + prog_start);
    }

    // Reset/IRQ/NMI vectors all point at the program start ($8000 or $C000).
    const uint16_t reset_addr = static_cast<uint16_t>(0x8000 + prog_start);
    const std::size_t vec = prg.size() - 6;
    prg[vec + 0] = static_cast<uint8_t>(reset_addr & 0xFF);
    prg[vec + 1] = static_cast<uint8_t>(reset_addr >> 8);
    prg[vec + 2] = prg[vec + 0];
    prg[vec + 3] = prg[vec + 1];
    prg[vec + 4] = prg[vec + 0];
    prg[vec + 5] = prg[vec + 1];

    std::vector<uint8_t> chr(static_cast<std::size_t>(chr_banks) * kChrBankSize, 0);

    const uint8_t mirror_flag = (mirror == mapperbus::core::MirrorMode::Vertical) ? 0x01 : 0x00;
    std::array<uint8_t, 16> header = {
        'N',
        'E',
        'S',
        0x1A,
        prg_banks,
        chr_banks,
        static_cast<uint8_t>((mapper & 0x0F) << 4 | mirror_flag),
        static_cast<uint8_t>(mapper & 0xF0),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), header.size());
    file.write(reinterpret_cast<const char*>(prg.data()), static_cast<std::streamsize>(prg.size()));
    if (!chr.empty()) {
        file.write(reinterpret_cast<const char*>(chr.data()),
                   static_cast<std::streamsize>(chr.size()));
    }
    return path;
}

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
