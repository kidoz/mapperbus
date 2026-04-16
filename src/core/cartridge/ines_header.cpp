#include "core/cartridge/ines_header.hpp"

#include <algorithm>
#include <cctype>

namespace mapperbus::core {

Result<INesHeader> parse_ines_header(std::span<const Byte> rom_data) {
    if (rom_data.size() < 16) {
        return std::unexpected("ROM data too small for iNES header");
    }

    // Verify magic: "NES\x1A"
    if (rom_data[0] != 'N' || rom_data[1] != 'E' || rom_data[2] != 'S' || rom_data[3] != 0x1A) {
        return std::unexpected("Invalid iNES magic bytes");
    }

    INesHeader header{};
    header.prg_rom_banks = rom_data[4];
    header.chr_rom_banks = rom_data[5];

    Byte flags6 = rom_data[6];
    Byte flags7 = rom_data[7];

    header.has_battery = (flags6 & 0x02) != 0;
    header.has_trainer = (flags6 & 0x04) != 0;

    // Mirror mode
    if (flags6 & 0x08) {
        header.mirror_mode = MirrorMode::FourScreen;
    } else if (flags6 & 0x01) {
        header.mirror_mode = MirrorMode::Vertical;
    } else {
        header.mirror_mode = MirrorMode::Horizontal;
    }

    // Mapper number from flags6 and flags7
    uint16_t mapper_lo = (flags6 >> 4) & 0x0F;
    uint16_t mapper_hi = flags7 & 0xF0;
    header.mapper_number = mapper_hi | mapper_lo;

    // NES 2.0 detection
    header.is_nes2 = ((flags7 & 0x0C) == 0x08);
    header.submapper = 0;
    if (header.is_nes2) {
        header.mapper_number =
            static_cast<uint16_t>(header.mapper_number | ((rom_data[8] & 0x0F) << 8));
        header.submapper = static_cast<uint8_t>(rom_data[8] >> 4);
    }

    // Region / CPU-PPU timing
    header.region = Region::NTSC; // default
    if (header.is_nes2) {
        // NES 2.0: byte 12, bits 0-1 encode CPU/PPU timing
        Byte timing = rom_data[12] & 0x03;
        switch (timing) {
        case 0x00:
            header.region = Region::NTSC;
            break;
        case 0x01:
            header.region = Region::PAL;
            break;
        case 0x02:
            header.region = Region::Multi;
            break;
        case 0x03:
            header.region = Region::Dendy;
            break;
        }
    } else {
        // iNES 1.0: byte 9, bit 0 (rarely set, weak signal)
        if (rom_data[9] & 0x01) {
            header.region = Region::PAL;
        }
    }

    if (header.prg_rom_banks == 0) {
        return std::unexpected("PRG ROM bank count is zero");
    }

    return header;
}

std::optional<Region> detect_region_from_filename(const std::string& filename) {
    // Extract just the filename from the path
    auto last_slash = filename.find_last_of("/\\");
    std::string name =
        (last_slash != std::string::npos) ? filename.substr(last_slash + 1) : filename;

    // Convert to lowercase for case-insensitive matching
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // Look for region tags in parentheses: (U), (USA), (J), (JPN), (E), (EUR), (Europe), etc.
    // PAL tags — check first since some multi-region tags contain both
    constexpr std::string_view pal_tags[] = {
        "(e)",      "(eur)",   "(europe)",  "(pal)",    "(a)",     "(aus)",    "(australia)",
        "(g)",      "(ger)",   "(germany)", "(f)",      "(fra)",   "(france)", "(s)",
        "(spa)",    "(spain)", "(i)",       "(ita)",    "(italy)", "(sw)",     "(swe)",
        "(sweden)", "(no)",    "(nor)",     "(norway)", "(dk)",    "(den)",    "(denmark)"};

    constexpr std::string_view ntsc_tags[] = {
        "(u)", "(usa)", "(j)", "(jpn)", "(japan)", "(k)", "(kor)", "(korea)", "(ntsc)"};

    // Check for explicit multi-region tags first
    constexpr std::string_view multi_tags[] = {"(w)", "(world)"};
    for (auto tag : multi_tags) {
        if (lower.find(tag) != std::string::npos) {
            return Region::Multi;
        }
    }

    bool has_ntsc = false;
    bool has_pal = false;

    for (auto tag : ntsc_tags) {
        if (lower.find(tag) != std::string::npos) {
            has_ntsc = true;
            break;
        }
    }

    for (auto tag : pal_tags) {
        if (lower.find(tag) != std::string::npos) {
            has_pal = true;
            break;
        }
    }

    if (has_ntsc && has_pal) {
        return Region::Multi;
    }
    if (has_pal) {
        return Region::PAL;
    }
    if (has_ntsc) {
        return Region::NTSC;
    }

    return std::nullopt;
}

} // namespace mapperbus::core
