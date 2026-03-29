#include "core/cartridge/rom_database.hpp"

#include <algorithm>
#include <array>

namespace mapperbus::core {

// ROM database: CRC32 (headerless) -> Region
// Only non-NTSC entries are needed since NTSC is the default.
// Sorted by CRC32 for binary search.
//
// Sources: NesCartDB (bootgod), No-Intro DAT files.
// To expand: add entries and keep sorted, or use a build-time script
// to generate from a No-Intro DAT/NesCartDB XML export.

// clang-format off
static constexpr std::array kDatabase = std::to_array<RomDatabaseEntry>({
    // --- PAL-exclusive and PAL-version games ---
    // CRC32 is of ROM data WITHOUT the 16-byte iNES header.

    // Action 52 (Europe)
    {0x0D178000, Region::PAL},
    // Asterix (Europe)
    {0x0EE6B355, Region::PAL},
    // Noah's Ark (Europe)
    {0x12200B3A, Region::PAL},
    // Parasol Stars (Europe)
    {0x1394DED0, Region::PAL},
    // International Cricket (Australia)
    {0x14A01857, Region::PAL},
    // Kick Off (Europe)
    {0x16AA4E2D, Region::PAL},
    // Road Fighter (Europe)
    {0x174E9850, Region::PAL},
    // Ferrari Grand Prix Challenge (Europe)
    {0x1B1C5E3E, Region::PAL},
    // Elite (Europe)
    {0x1D0F4D6B, Region::PAL},
    // Trolls in Crazyland (Europe)
    {0x1E438D52, Region::PAL},
    // Micro Machines (Europe) (Aladdin Deck Enhancer)
    {0x20B5FE3A, Region::PAL},
    // Dropzone (Europe)
    {0x211CB526, Region::PAL},
    // Aussie Rules Footy (Australia)
    {0x2389C7C3, Region::PAL},
    // Crackout (Europe)
    {0x25CA388A, Region::PAL},
    // Parodius (Europe)
    {0x28BFAA2C, Region::PAL},
    // Mr. Gimmick (Europe)
    {0x2F698C4D, Region::PAL},
    // Shadow Warriors (Europe) [Ninja Gaiden]
    {0x308DA987, Region::PAL},
    // Probotector (Europe) [Contra]
    {0x30B7E747, Region::PAL},
    // Aladdin (Europe)
    {0x3616C7DD, Region::PAL},
    // Pac-Man (PAL)
    {0x39D14568, Region::PAL},
    // Darkman (Europe)
    {0x3D1C3137, Region::PAL},
    // Super Turrican (Europe)
    {0x3DCADA1A, Region::PAL},
    // James Bond Jr (Europe)
    {0x3E1271D5, Region::PAL},
    // The Smurfs (Europe)
    {0x42B06E5D, Region::PAL},
    // Tecmo World Wrestling (Europe)
    {0x4584D3AC, Region::PAL},
    // Mario Bros. (PAL)
    {0x47110D3D, Region::PAL},
    // Bucky O'Hare (Europe)
    {0x4A8B3B37, Region::PAL},
    // Super Mario Bros. (PAL)
    {0x4E74BEF0, Region::PAL},
    // Asterix (Europe) (alt)
    {0x529DE626, Region::PAL},
    // Shadow of the Ninja (Europe)
    {0x589B0A0F, Region::PAL},
    // Probotector II (Europe) [Super C]
    {0x5B837E8D, Region::PAL},
    // Solstice (Europe)
    {0x5B8E811B, Region::PAL},
    // Banana Prince (Europe)
    {0x5DE61639, Region::PAL},
    // Dr. Mario (PAL)
    {0x6150517C, Region::PAL},
    // Goal! (Europe)
    {0x6260AF36, Region::PAL},
    // New Zealand Story (Europe)
    {0x67751094, Region::PAL},
    // Duck Tales (Europe)
    {0x68263A8D, Region::PAL},
    // Super Mario Bros. 3 (Europe)
    {0x69E6B579, Region::PAL},
    // Chip 'n Dale (Europe)
    {0x6D65CAE0, Region::PAL},
    // Kirby's Adventure (Europe)
    {0x6E2EE719, Region::PAL},
    // Battletoads (Europe)
    {0x6FA2EF28, Region::PAL},
    // Mega Man 2 (Europe)
    {0x718BAD86, Region::PAL},
    // Lifeforce (Europe) [Salamander]
    {0x730E89D4, Region::PAL},
    // Bubble Bobble (Europe)
    {0x755D327C, Region::PAL},
    // Adventure Island II (Europe)
    {0x77F0D6F4, Region::PAL},
    // Castlevania (Europe)
    {0x7975BE09, Region::PAL},
    // Tiny Toon Adventures (Europe)
    {0x7D554E23, Region::PAL},
    // Metroid (Europe)
    {0x7E562C4E, Region::PAL},
    // Legend of Zelda (PAL)
    {0x80C8DF3F, Region::PAL},
    // Teenage Mutant Hero Turtles (Europe) [TMNT]
    {0x82137C0E, Region::PAL},
    // Metal Gear (Europe)
    {0x836FD0C7, Region::PAL},
    // Tetris (PAL)
    {0x8667817E, Region::PAL},
    // Blaster Master (Europe)
    {0x885ACC77, Region::PAL},
    // Donkey Kong (PAL)
    {0x890ECAC7, Region::PAL},
    // Mega Man 3 (Europe)
    {0x8C204628, Region::PAL},
    // Zelda II (PAL)
    {0x8F907E87, Region::PAL},
    // Micro Machines (Europe)
    {0x90C773C1, Region::PAL},
    // Maniac Mansion (Europe)
    {0x93B8FC49, Region::PAL},
    // Double Dragon (Europe)
    {0x95741798, Region::PAL},
    // Batman (Europe)
    {0x97E4D794, Region::PAL},
    // Toki (Europe)
    {0x9BB668DE, Region::PAL},
    // Super Mario Bros. 2 (PAL)
    {0x9D2CE347, Region::PAL},
    // Contra Force (Europe)
    {0x9E947E85, Region::PAL},
    // Ninja Gaiden II (Europe)
    {0xA2194B42, Region::PAL},
    // Castlevania III (Europe)
    {0xA48DE04C, Region::PAL},
    // Punch-Out!! (Europe)
    {0xA5E68C1D, Region::PAL},
    // Excitebike (PAL)
    {0xAB21AB5F, Region::PAL},
    // Mega Man 4 (Europe)
    {0xAE0ABB68, Region::PAL},
    // Battletoads & Double Dragon (Europe)
    {0xB228FA7E, Region::PAL},
    // Konami Hyper Soccer (Europe)
    {0xB5D10B6E, Region::PAL},
    // Double Dragon II (Europe)
    {0xBDE93999, Region::PAL},
    // Snake Rattle 'n' Roll (Europe)
    {0xC0A9E58D, Region::PAL},
    // Ghosts'n Goblins (Europe)
    {0xC2EF3422, Region::PAL},
    // Mega Man 5 (Europe)
    {0xC9556B36, Region::PAL},
    // StarTropics (Europe)
    {0xCAF9EF04, Region::PAL},
    // Gradius (Europe)
    {0xCB8F9B28, Region::PAL},
    // Fire Emblem Gaiden (Japan - NTSC override for test)
    // (not included — Japan games are NTSC by default)

    // --- Dendy-specific games ---
    // Dendy games are rare in standard ROM sets; most use NTSC timing.
    // Add entries here if specific Dendy-only releases are identified.
});
// clang-format on

std::optional<Region> lookup_region_by_crc32(uint32_t crc32) {
    // Binary search on sorted CRC32 values
    auto it = std::lower_bound(
        kDatabase.begin(),
        kDatabase.end(),
        crc32,
        [](const RomDatabaseEntry& entry, uint32_t value) { return entry.crc32 < value; });

    if (it != kDatabase.end() && it->crc32 == crc32) {
        return it->region;
    }
    return std::nullopt;
}

} // namespace mapperbus::core
