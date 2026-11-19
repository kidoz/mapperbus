#include "core/mappers/mapper468.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

#include "core/mappers/vrc7.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kMaxFmStateSize = 0x008000UZ;
constexpr std::uint16_t kStateTag = 0x4680U;

constexpr std::array<float, 16> kAyVolume = {
    0.000000F,
    0.117188F,
    0.165728F,
    0.234375F,
    0.331456F,
    0.468750F,
    0.662913F,
    0.937500F,
    1.325825F,
    1.875000F,
    2.651650F,
    3.750000F,
    5.303301F,
    7.500000F,
    10.606602F,
    15.000000F,
};
constexpr std::array<float, 32> kAyEnvelope = {
    0.000000F, 0.082864F, 0.098543F, 0.117188F, 0.139360F, 0.165728F,  0.197085F,  0.234375F,
    0.278720F, 0.331456F, 0.394170F, 0.468750F, 0.557441F, 0.662913F,  0.788340F,  0.937500F,
    1.114882F, 1.325825F, 1.576681F, 1.875000F, 2.229763F, 2.651650F,  3.153362F,  3.750000F,
    4.459527F, 5.303301F, 6.306723F, 7.500000F, 8.919053F, 10.606602F, 12.613446F, 15.000000F,
};

std::unique_ptr<Mapper> make_fm_audio() {
    INesHeader header{};
    header.mapper_number = 85U;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    return std::make_unique<Vrc7>(header, std::vector<Byte>{}, std::vector<Byte>{});
}

constexpr std::array<uint16_t, 512> kLut509 = {{
    7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,
    26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
    45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
    64,  65,  66,  67,  68,  69,  70,  71,  72,  0,   1,   73,  74,  75,  76,  77,  78,  79,  80,
    81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  4,   91,  92,  93,  94,  95,  96,  97,  98,
    99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
    118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 2,   3,   130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 5,   140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152,
    153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171,
    172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190,
    191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228,
    229, 230, 231, 232, 233, 234, 235, 236, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248,
    249, 250, 251, 252, 253, 254, 256, 6,   257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267,
    268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286,
    287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305,
    306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324,
    325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343,
    344, 345, 346, 347, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363,
    364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382,
    383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401,
    402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420,
    421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439,
    440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458,
    459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477,
    478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496,
    497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 512, 513, 514, 515, 516, 517,
}};

bool is_byte_flag(Byte value) {
    return value <= 1U;
}

} // namespace

// Original C++23 implementation from NESdev NES 2.0 Mapper 468 oldid 20144.
// Supervisor masking and reset-on-select behavior follow the published board
// behavior. The fixed 8 KiB WRAM and 32 KiB CHR-RAM ceiling bound every state.
Mapper468::Mapper468(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : header_(header), prg_size_(prg_rom.size()), chr_size_(chr_rom.size()),
      chr_ram_size_(header.chr_ram_size), persistent_ram_(header.prg_nvram_size == kWramSize),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), fm_audio_(make_fm_audio()) {
    power_on();
}

Mapper468::~Mapper468() = default;

Result<void> Mapper468::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 468U) {
        return std::unexpected("Mapper 468 requires mapper ID 468");
    }
    if (!header.is_nes2 || header.submapper > 5U) {
        return std::unexpected("Mapper 468 requires NES 2.0 submapper 0 through 5");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize))) {
        return std::unexpected(
            "Mapper 468 requires power-of-two 16 KiB-2 MiB PRG and optional 8 KiB-1 MiB CHR ROM");
    }
    if (header.prg_ram_size > kWramSize || header.prg_nvram_size > kWramSize ||
        (header.prg_ram_size != 0U && header.prg_ram_size != kWramSize) ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
        header.has_battery != (header.prg_nvram_size == kWramSize)) {
        return std::unexpected("Mapper 468 supports only optional 8 KiB PRG RAM or NVRAM");
    }
    if (header.chr_nvram_size != 0U ||
        (chr_size == 0U && header.chr_ram_size != 0x2000U && header.chr_ram_size != 0x8000U) ||
        (chr_size != 0U && header.chr_ram_size != 0U)) {
        return std::unexpected(
            "Mapper 468 requires 8/32 KiB volatile CHR RAM only without CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 468 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 468 does not provide trainer RAM");
    }
    return {};
}

Mapper468::Mode Mapper468::mode() const {
    const Byte submapper = header_.submapper;
    const Byte select = runtime_.supervisor[0U] >> 4U;
    const bool alternate = (runtime_.supervisor[0U] & 0x08U) != 0U;
    if ((submapper == 0U || submapper == 1U) && (select == 4U || select == 6U)) {
        return alternate ? Mode::Bxrom : Mode::Axrom;
    }
    if ((submapper == 0U && select == 9U) ||
        ((submapper == 1U || submapper == 3U) && select == 7U)) {
        return alternate ? Mode::Uxrom : Mode::If12;
    }
    if (submapper == 0U) {
        switch (select) {
        case 0U:
            return Mode::Sxrom;
        case 1U:
            return Mode::Surom;
        case 5U:
            return Mode::Misc;
        case 7U:
            return Mode::Lf36;
        case 8U:
            return Mode::Fxrom;
        case 10U:
            return Mode::Pnrom;
        case 11U:
            return Mode::Unrom512;
        case 12U:
        case 13U:
            return Mode::Gnrom;
        case 14U:
            return Mode::Nanjing;
        default:
            return Mode::Invalid;
        }
    }
    if (submapper == 1U) {
        if (select <= 1U) {
            return Mode::Txrom;
        }
        if (select == 2U) {
            return Mode::Txsrom;
        }
        if (select == 5U) {
            return Mode::Misc;
        }
        if (select == 12U || select == 13U) {
            return Mode::Gnrom;
        }
        if (select == 14U) {
            return Mode::Nanjing;
        }
        return Mode::Invalid;
    }
    if (submapper == 2U) {
        return select <= 3U ? Mode::Vrc24 : Mode::Invalid;
    }
    if (submapper == 3U) {
        if (select <= 1U) {
            return Mode::Vrc6;
        }
        if (select == 2U) {
            return Mode::Sxrom;
        }
        return Mode::Invalid;
    }
    if (submapper == 4U) {
        if (select == 0U) {
            return Mode::Vrc1;
        }
        if (select == 1U) {
            return Mode::Vrc7;
        }
        if (select == 4U) {
            return Mode::Vrc3;
        }
        return Mode::Invalid;
    }
    return submapper == 5U && select == 0U ? Mode::Fme7 : Mode::Invalid;
}

uint32_t Mapper468::outer_bank() const {
    const auto index = header_.submapper == 1U ? 2U : 3U;
    return ((static_cast<uint32_t>(runtime_.supervisor[index]) << 9U) & 0x2000U) |
           ((static_cast<uint32_t>(runtime_.supervisor[1U]) << 5U) & 0x1FE0U) |
           ((static_cast<uint32_t>(runtime_.supervisor[0U]) << 4U) & 0x0010U);
}

std::size_t Mapper468::chr_ram_size() const {
    return chr_ram_size_ != 0U ? chr_ram_size_ : 0x2000U;
}

Byte Mapper468::read_prg8(uint32_t bank, Address addr, uint32_t and_mask) const {
    const uint32_t physical = (bank & and_mask) | (outer_bank() & ~and_mask);
    return prg_rom_[(static_cast<std::size_t>(physical) * 0x2000U + (addr & 0x1FFFU)) % prg_size_];
}

Byte Mapper468::read_chr1k(uint32_t bank, Address addr, uint32_t and_mask) const {
    const std::size_t offset = static_cast<std::size_t>(bank & and_mask) * 0x400U + (addr & 0x3FFU);
    if (chr_size_ != 0U) {
        return chr_rom_[offset % chr_size_];
    }
    return read_chr_ram(offset);
}

Byte Mapper468::read_chr_ram(std::size_t offset) const {
    return chr_ram_[offset % chr_ram_size()];
}

Byte Mapper468::mmc3_chr_bank(Address addr) const {
    const auto slot =
        ((addr >> 10U) & 7U) ^ ((runtime_.inner.mmc3.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((runtime_.inner.mmc3.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return runtime_.inner.mmc3.banks[slot - 2U];
}

MirrorMode Mapper468::vrc_mirror(Byte value) const {
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    return kModes[value & 3U];
}

bool Mapper468::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return mode() != Mode::Invalid;
    }
    if (addr < 0x6000U || addr >= 0x8000U) {
        return false;
    }
    switch (mode()) {
    case Mode::Sxrom:
    case Mode::Surom:
        return (runtime_.inner.mmc1.prg & 0x10U) == 0U;
    case Mode::Fxrom:
    case Mode::Pnrom:
    case Mode::Nanjing:
    case Mode::Vrc24:
    case Mode::Vrc6:
    case Mode::Vrc7:
    case Mode::Vrc3:
    case Mode::Lf36:
        return true;
    case Mode::Txrom:
    case Mode::Txsrom:
    case Mode::Fme7:
        return runtime_.inner.ram_enabled != 0U;
    default:
        return false;
    }
}

Byte Mapper468::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper468::read_prg(Address addr) {
    if (!maps_prg(addr)) {
        return 0U;
    }
    if (addr < 0x8000U) {
        if (mode() == Mode::Lf36) {
            const uint32_t bank = 6U + (((addr - 0x6000U) >> 13U) & 1U);
            const uint32_t outer = outer_bank() | (runtime_.supervisor[0U] & 0x08U);
            return prg_rom_[(static_cast<std::size_t>(bank | outer) * 0x2000U + (addr & 0x1FFFU)) %
                            prg_size_];
        }
        return wram_[addr & 0x1FFFU];
    }

    const auto selected = mode();
    const auto& inner = runtime_.inner;
    const Byte flags = runtime_.supervisor[0U];
    switch (selected) {
    case Mode::Sxrom: {
        const Byte control_mode = (inner.mmc1.control >> 2U) & 3U;
        uint32_t bank = inner.mmc1.prg & 0x0FU;
        if (control_mode < 2U) {
            bank = (bank & 0x0EU) | ((addr >> 14U) & 1U);
        } else if (control_mode == 2U) {
            bank = addr < 0xC000U ? 0U : bank;
        } else if (addr >= 0xC000U) {
            bank = 0x0FU;
        }
        const uint32_t and_mask =
            (flags & 0x02U) != 0U ? ((flags & 0x08U) != 0U ? 0x03U : 0x07U) : 0x0FU;
        const uint32_t outer = (outer_bank() >> 1U) | (flags & 0x06U);
        const uint32_t physical = (bank & and_mask) | (outer & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Surom: {
        const Byte control_mode = (inner.mmc1.control >> 2U) & 3U;
        uint32_t bank = (inner.mmc1.prg & 0x0FU) | (inner.mmc1.chr0 & 0x10U);
        if (control_mode < 2U) {
            bank = (bank & 0x1EU) | ((addr >> 14U) & 1U);
        } else if (control_mode == 2U) {
            bank = addr < 0xC000U ? (inner.mmc1.chr0 & 0x10U) : bank;
        } else if (addr >= 0xC000U) {
            bank |= 0x0FU;
        }
        const uint32_t physical = (bank & 0x1FU) | ((outer_bank() >> 1U) & ~0x1FU);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Axrom:
    case Mode::Bxrom:
    case Mode::Gnrom: {
        uint32_t value = inner.latch;
        uint32_t and_mask = 0U;
        uint32_t outer = outer_bank() >> 2U;
        if (selected == Mode::Gnrom) {
            and_mask = (flags & 0x08U) != 0U ? 0x01U : 0x03U;
            if ((header_.submapper == 1U && (flags & 0x08U) == 0U) ||
                (header_.submapper != 1U && (outer_bank() & 0x2000U) != 0U)) {
                value = ((value >> 4U) & 0x0FU) | ((value << 4U) & 0xF0U);
            }
            outer |= (flags >> 1U) & 0x02U;
        } else {
            and_mask = (flags & 0x20U) != 0U ? 0x0FU : ((flags & 0x02U) != 0U ? 0x03U : 0x07U);
        }
        const uint32_t physical = (value & and_mask) | (outer & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x8000U + (addr & 0x7FFFU)) %
                        prg_size_];
    }
    case Mode::Misc: {
        if ((flags & 0x02U) != 0U) {
            const uint32_t bank = ((inner.custom[2U] << 1U) & 0x0EU) | (flags & 1U);
            const uint32_t physical = (bank & 0x0FU) | ((outer_bank() >> 1U) & ~0x0FU);
            return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                            prg_size_];
        }
        const uint32_t physical = (inner.custom[2U] & 0x07U) | ((outer_bank() >> 2U) & ~0x07U);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x8000U + (addr & 0x7FFFU)) %
                        prg_size_];
    }
    case Mode::Lf36: {
        const auto slot = (addr - 0x8000U) >> 13U;
        const uint32_t bank = slot == 2U ? (inner.custom[0U] & 7U) : 4U + slot;
        const uint32_t physical = bank | outer_bank() | (flags & 0x08U);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x2000U + (addr & 0x1FFFU)) %
                        prg_size_];
    }
    case Mode::Fxrom: {
        const uint32_t and_mask = (flags & 0x02U) != 0U ? 0x07U : 0x0FU;
        const uint32_t bank = addr < 0xC000U ? inner.mmc24.prg : and_mask;
        const uint32_t physical = (bank & and_mask) | ((outer_bank() >> 1U) & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Uxrom: {
        const uint32_t and_mask = (flags & 0x02U) != 0U                              ? 0x07U
                                  : header_.submapper == 1U && (flags & 0x04U) == 0U ? 0x1FU
                                                                                     : 0x0FU;
        const uint32_t bank = addr < 0xC000U ? inner.latch : and_mask;
        const uint32_t physical = (bank & and_mask) | ((outer_bank() >> 1U) & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::If12: {
        const uint32_t and_mask = (flags & 0x02U) != 0U ? 0x07U : 0x0FU;
        const uint32_t bank = addr < 0xC000U ? inner.custom[1U] : and_mask;
        const uint32_t physical = (bank & and_mask) | ((outer_bank() >> 1U) & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Unrom512: {
        const uint32_t bank = addr < 0xC000U ? (inner.latch & 0x1FU) : 0x1FU;
        const uint32_t physical = (bank & 0x1FU) | ((outer_bank() >> 1U) & ~0x1FU);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Nanjing: {
        const uint32_t bank = ((inner.custom[2U] << 4U) & 0x30U) | (inner.custom[0U] & 0x0FU) |
                              ((inner.custom[3U] & 0x04U) != 0U ? 0U : 0x03U) |
                              (outer_bank() >> 2U);
        return prg_rom_[(static_cast<std::size_t>(bank) * 0x8000U + (addr & 0x7FFFU)) % prg_size_];
    }
    case Mode::Pnrom: {
        const uint32_t slot = (addr - 0x8000U) >> 13U;
        const uint32_t bank = slot == 0U ? inner.mmc24.prg : 0x0CU + slot;
        return read_prg8(bank, addr, 0x0FU);
    }
    case Mode::Txrom:
    case Mode::Txsrom: {
        const auto slot = (addr - 0x8000U) >> 13U;
        const bool prg_mode = (inner.mmc3.bank_select & 0x40U) != 0U;
        uint32_t and_mask = (flags & 0x08U) == 0U                     ? 0x7FU
                            : (flags & 0x04U) == 0U                   ? 0x3FU
                            : (flags & 0x02U) == 0U                   ? 0x1FU
                            : (runtime_.supervisor[2U] & 0x02U) != 0U ? 0x07U
                                                                      : 0x0FU;
        const uint32_t outer = outer_bank() | ((runtime_.supervisor[2U] & 1U) != 0U ? 0x0CU : 0U);
        uint32_t bank = and_mask;
        if (slot == 0U) {
            bank = prg_mode ? and_mask - 1U : inner.mmc3.banks[6U];
        } else if (slot == 1U) {
            bank = inner.mmc3.banks[7U];
        } else if (slot == 2U) {
            bank = prg_mode ? inner.mmc3.banks[6U] : and_mask - 1U;
        }
        const uint32_t physical = (bank & and_mask) | (outer & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x2000U + (addr & 0x1FFFU)) %
                        prg_size_];
    }
    case Mode::Vrc24: {
        const auto slot = (addr - 0x8000U) >> 13U;
        const uint32_t and_mask = (flags & 0x02U) != 0U ? 0x0FU : 0x1FU;
        const bool vrc2 = (runtime_.supervisor[0U] >> 4U & 1U) == 0U;
        const bool swap = !vrc2 && (inner.vrc.mode & 0x02U) != 0U;
        uint32_t bank = and_mask;
        if (slot == 0U) {
            bank = swap ? and_mask - 1U : inner.vrc.prg[0U];
        } else if (slot == 1U) {
            bank = inner.vrc.prg[1U];
        } else if (slot == 2U) {
            bank = swap ? inner.vrc.prg[0U] : and_mask - 1U;
        }
        const uint32_t physical = (bank & and_mask) | (outer_bank() & ~and_mask);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x2000U + (addr & 0x1FFFU)) %
                        prg_size_];
    }
    case Mode::Vrc6: {
        const uint32_t and_mask = (flags & 0x02U) != 0U ? 0x0FU : 0x1FU;
        if (addr < 0xC000U) {
            const uint32_t physical =
                (inner.vrc.prg[0U] & and_mask) | ((outer_bank() >> 1U) & ~and_mask);
            return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                            prg_size_];
        }
        const uint32_t bank = addr < 0xE000U ? inner.vrc.prg[1U] : and_mask;
        return read_prg8(bank, addr, and_mask);
    }
    case Mode::Vrc1:
    case Mode::Vrc7: {
        const auto slot = (addr - 0x8000U) >> 13U;
        const uint32_t and_mask = (flags & 0x08U) == 0U   ? 0x7FU
                                  : (flags & 0x04U) == 0U ? 0x3FU
                                  : (flags & 0x02U) == 0U ? 0x1FU
                                                          : 0x0FU;
        const uint32_t bank = slot < 3U ? inner.vrc.prg[slot] : and_mask;
        return read_prg8(bank, addr, and_mask);
    }
    case Mode::Vrc3: {
        const uint32_t bank = addr < 0xC000U ? inner.vrc.prg[0U] : 0x07U;
        const uint32_t physical = (bank & 0x07U) | ((outer_bank() >> 1U) & ~0x07U);
        return prg_rom_[(static_cast<std::size_t>(physical) * 0x4000U + (addr & 0x3FFFU)) %
                        prg_size_];
    }
    case Mode::Fme7: {
        const auto slot = (addr - 0x8000U) >> 13U;
        const uint32_t and_mask = (flags & 0x02U) != 0U ? 0x0FU : 0x1FU;
        const uint32_t bank = slot == 3U ? and_mask : inner.vrc.prg[slot];
        return read_prg8(bank, addr, and_mask);
    }
    default:
        return 0U;
    }
}

void Mapper468::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if ((mode() == Mode::Sxrom || mode() == Mode::Surom) && consecutive && addr >= 0x8000U &&
        (value & 0x80U) == 0U) {
        return;
    }
    write_prg(addr, value);
}

void Mapper468::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (maps_prg(addr) && mode() != Mode::Lf36 &&
            ((mode() != Mode::Txrom && mode() != Mode::Txsrom) ||
             runtime_.inner.ram_write_protected == 0U)) {
            wram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    auto& inner = runtime_.inner;
    const Byte flags = runtime_.supervisor[0U];
    switch (mode()) {
    case Mode::Sxrom:
    case Mode::Surom:
        write_mmc1(addr, value);
        break;
    case Mode::Axrom:
    case Mode::Bxrom:
    case Mode::Uxrom:
    case Mode::Unrom512:
    case Mode::Gnrom:
        inner.latch = value;
        break;
    case Mode::Misc:
        switch ((addr >> 12U) & 7U) {
        case 0U:
        case 2U:
        case 3U:
            inner.custom[0U] = value;
            break;
        case 1U:
            inner.custom[(flags & 0x08U) != 0U ? 0U : 1U] = value;
            break;
        case 6U:
        case 7U:
            inner.custom[2U] = value;
            break;
        default:
            break;
        }
        break;
    case Mode::Lf36:
        if (addr < 0xC000U) {
            inner.custom[1U] = static_cast<Byte>((addr >> 13U) & 1U);
            if (inner.custom[1U] == 0U) {
                inner.custom[2U] = 0U;
                inner.custom[3U] = 0U;
            }
        } else if (addr >= 0xE000U) {
            inner.custom[0U] = value;
        }
        break;
    case Mode::If12:
        inner.custom[(addr >> 14U) & 1U] = value;
        break;
    case Mode::Fxrom:
    case Mode::Pnrom:
        write_mmc24(addr, value);
        break;
    case Mode::Txrom:
    case Mode::Txsrom:
        write_mmc3(addr, value);
        break;
    case Mode::Vrc24:
        write_vrc24(addr, value);
        break;
    case Mode::Vrc6:
        write_vrc6(addr, value);
        break;
    case Mode::Vrc7:
        write_vrc7(addr, value);
        break;
    case Mode::Vrc1: {
        const auto group = static_cast<unsigned>(addr >> 12U);
        if (group == 8U || group == 10U || group == 12U) {
            inner.vrc.prg[(group - 8U) / 2U] = value & 0x0FU;
        } else if (group == 9U) {
            inner.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            inner.vrc.chr[0U] = (inner.vrc.chr[0U] & 0x0FU) | ((value & 2U) << 3U);
            inner.vrc.chr[1U] = (inner.vrc.chr[1U] & 0x0FU) | ((value & 4U) << 2U);
        } else if (group >= 14U) {
            auto& bank = inner.vrc.chr[group - 14U];
            bank = (bank & 0x10U) | (value & 0x0FU);
        }
        break;
    }
    case Mode::Vrc3: {
        const auto group = static_cast<unsigned>(addr >> 12U);
        if (group >= 8U && group <= 11U) {
            const auto shift = (group - 8U) * 4U;
            inner.irq.latch = static_cast<uint16_t>((inner.irq.latch & ~(0x0FU << shift)) |
                                                    ((value & 0x0FU) << shift));
        } else if (group == 12U) {
            inner.irq.control = value & 7U;
            inner.irq.enabled = (value >> 1U) & 1U;
            inner.irq.after_ack = value & 1U;
            inner.irq.pending = 0U;
            if (inner.irq.enabled != 0U) {
                inner.irq.counter = inner.irq.latch;
                inner.irq.prescaler = 341U;
            }
        } else if (group == 13U) {
            inner.irq.pending = 0U;
            inner.irq.enabled = inner.irq.after_ack;
        } else if (group == 15U) {
            inner.vrc.prg[0U] = value & 7U;
        }
        break;
    }
    case Mode::Fme7:
        write_fme7(addr, value);
        break;
    default:
        break;
    }
}

Byte Mapper468::read_chr(Address addr) {
    addr &= 0x1FFFU;
    const auto& inner = runtime_.inner;
    const Byte flags = runtime_.supervisor[0U];
    switch (mode()) {
    case Mode::Sxrom:
        if ((inner.mmc1.control & 0x10U) != 0U) {
            const unsigned half = addr >> 12U;
            const Byte bank = half == 0U ? inner.mmc1.chr0 : inner.mmc1.chr1;
            return read_chr1k(
                static_cast<uint32_t>(bank & 0x1FU) * 4U + (addr & 0x0FFFU) / 0x400U, addr, 0x7CU);
        }
        return read_chr1k(
            static_cast<uint32_t>(inner.mmc1.chr0 & 0x1EU) * 4U + (addr >> 10U), addr, 0x7FU);
    case Mode::Surom:
        if ((inner.mmc1.control & 0x10U) != 0U) {
            const unsigned half = addr >> 12U;
            const Byte bank = half == 0U ? inner.mmc1.chr0 : inner.mmc1.chr1;
            return read_chr1k(
                static_cast<uint32_t>(bank & 0x0FU) * 4U + (addr & 0x0FFFU) / 0x400U, addr, 0x3CU);
        }
        return read_chr1k(
            static_cast<uint32_t>(inner.mmc1.chr0 & 0x0EU) * 4U + (addr >> 10U), addr, 0x3FU);
    case Mode::Misc:
        return read_chr1k(
            static_cast<std::size_t>(inner.custom[0U] & ((flags & 0x02U) != 0U ? 0x03U : 0x0FU))
                << 3U,
            addr,
            0xFFU);
    case Mode::If12:
        return read_chr1k(
            static_cast<std::size_t>((inner.custom[0U] >> 1U) & 0x0FU) << 3U, addr, 0xFFU);
    case Mode::Unrom512:
        return read_chr1k(static_cast<std::size_t>(inner.latch >> 5U) << 3U, addr, 0xFFU);
    case Mode::Gnrom: {
        uint32_t value = inner.latch;
        if ((header_.submapper == 1U && (flags & 0x08U) == 0U) ||
            (header_.submapper != 1U && (outer_bank() & 0x2000U) != 0U)) {
            value = ((value >> 4U) & 0x0FU) | ((value << 4U) & 0xF0U);
        }
        return read_chr1k((value >> 4U) << 3U, addr, 0xFFU);
    }
    case Mode::Nanjing:
        if ((inner.custom[0U] & 0x80U) != 0U) {
            return read_chr_ram((runtime_.nanjing_upper_half != 0U ? 0x1000U : 0U) +
                                (addr & 0x0FFFU));
        }
        return read_chr_ram(addr);
    case Mode::Fxrom:
    case Mode::Pnrom: {
        const unsigned half = addr >> 12U;
        const unsigned latch = inner.mmc24.latches[half];
        const Byte bank = inner.mmc24.chr[half + latch * 2U];
        const std::size_t offset = static_cast<std::size_t>(bank) * 0x1000U + (addr & 0x0FFFU);
        const Byte result = chr_size_ != 0U ? chr_rom_[offset % chr_size_] : read_chr_ram(offset);
        if (mode() == Mode::Fxrom) {
            if ((addr & 0x0FF8U) == 0x0FD8U) {
                runtime_.inner.mmc24.latches[half] = 0U;
            } else if ((addr & 0x0FF8U) == 0x0FE8U) {
                runtime_.inner.mmc24.latches[half] = 1U;
            }
        } else if (addr == 0x0FD8U || addr == 0x1FD8U) {
            runtime_.inner.mmc24.latches[half] = 0U;
        } else if (addr == 0x0FE8U || addr == 0x1FE8U) {
            runtime_.inner.mmc24.latches[half] = 1U;
        }
        return result;
    }
    case Mode::Txrom:
        return read_chr1k(mmc3_chr_bank(addr), addr, (flags & 0x10U) != 0U ? 0xFFU : 0x7FU);
    case Mode::Txsrom:
        return read_chr1k(mmc3_chr_bank(addr), addr, 0x7FU);
    case Mode::Vrc24: {
        const bool vrc2 = ((runtime_.supervisor[0U] >> 4U) & 1U) == 0U;
        const uint16_t bank = inner.vrc.chr[addr >> 10U];
        return read_chr1k(vrc2 ? bank >> 1U : bank, addr, 0x1FFU);
    }
    case Mode::Vrc6:
        return read_chr1k(inner.vrc.chr[addr >> 10U], addr, 0xFFU);
    case Mode::Vrc1: {
        const unsigned half = addr >> 12U;
        const uint16_t bank = inner.vrc.chr[half];
        return read_chr1k(
            static_cast<uint32_t>(bank & 0x1FU) * 4U + (addr & 0x0FFFU) / 0x400U, addr, 0x7CU);
    }
    case Mode::Vrc7:
        return read_chr1k(inner.vrc.chr[addr >> 10U], addr, (flags & 0x10U) != 0U ? 0xFFU : 0x7FU);
    case Mode::Fme7:
        return read_chr1k(inner.vrc.chr[addr >> 10U], addr, 0xFFU);
    case Mode::Axrom:
    case Mode::Bxrom:
    case Mode::Lf36:
    case Mode::Uxrom:
    case Mode::Vrc3:
        return read_chr1k(addr >> 10U, addr, 0x7FU);
    default:
        return read_chr_ram(addr);
    }
}

void Mapper468::write_chr(Address addr, Byte value) {
    if (chr_size_ != 0U) {
        return;
    }
    addr &= 0x1FFFU;
    const auto& inner = runtime_.inner;
    switch (mode()) {
    case Mode::Nanjing:
        if ((inner.custom[0U] & 0x80U) != 0U) {
            chr_ram_[((runtime_.nanjing_upper_half != 0U ? 0x1000U : 0U) + (addr & 0x0FFFU)) %
                     chr_ram_size()] = value;
        } else {
            chr_ram_[addr % chr_ram_size()] = value;
        }
        return;
    case Mode::Sxrom:
        if ((inner.mmc1.control & 0x10U) != 0U) {
            const unsigned half = addr >> 12U;
            const Byte bank = half == 0U ? inner.mmc1.chr0 : inner.mmc1.chr1;
            const std::size_t offset =
                static_cast<std::size_t>(bank & 0x1FU) * 0x1000U + (addr & 0x0FFFU);
            chr_ram_[offset % chr_ram_size()] = value;
        } else {
            const std::size_t offset =
                static_cast<std::size_t>(inner.mmc1.chr0 & 0x1EU) * 0x1000U + addr;
            chr_ram_[offset % chr_ram_size()] = value;
        }
        return;
    case Mode::Surom:
        if ((inner.mmc1.control & 0x10U) != 0U) {
            const unsigned half = addr >> 12U;
            const Byte bank = half == 0U ? inner.mmc1.chr0 : inner.mmc1.chr1;
            const std::size_t offset =
                static_cast<std::size_t>(bank & 0x0FU) * 0x1000U + (addr & 0x0FFFU);
            chr_ram_[offset % chr_ram_size()] = value;
        } else {
            const std::size_t offset =
                static_cast<std::size_t>(inner.mmc1.chr0 & 0x0EU) * 0x1000U + addr;
            chr_ram_[offset % chr_ram_size()] = value;
        }
        return;
    case Mode::Misc:
        chr_ram_[(static_cast<std::size_t>(
                      inner.custom[0U] &
                      ((runtime_.supervisor[0U] & 0x02U) != 0U ? 0x03U : 0x0FU)) *
                      0x2000U +
                  addr) %
                 chr_ram_size()] = value;
        return;
    case Mode::If12:
        chr_ram_[(static_cast<std::size_t>((inner.custom[0U] >> 1U) & 0x0FU) * 0x2000U + addr) %
                 chr_ram_size()] = value;
        return;
    case Mode::Unrom512:
        chr_ram_[(static_cast<std::size_t>(inner.latch >> 5U) * 0x2000U + addr) % chr_ram_size()] =
            value;
        return;
    case Mode::Gnrom: {
        uint32_t decoded = inner.latch;
        if ((header_.submapper == 1U && (runtime_.supervisor[0U] & 0x08U) == 0U) ||
            (header_.submapper != 1U && (outer_bank() & 0x2000U) != 0U)) {
            decoded = ((decoded >> 4U) & 0x0FU) | ((decoded << 4U) & 0xF0U);
        }
        const uint32_t bank = decoded >> 4U;
        chr_ram_[(static_cast<std::size_t>(bank) * 0x2000U + addr) % chr_ram_size()] = value;
        return;
    }
    case Mode::Fxrom:
    case Mode::Pnrom: {
        const unsigned half = addr >> 12U;
        const Byte bank = inner.mmc24.chr[half + inner.mmc24.latches[half] * 2U];
        chr_ram_[(static_cast<std::size_t>(bank) * 0x1000U + (addr & 0x0FFFU)) % chr_ram_size()] =
            value;
        return;
    }
    case Mode::Txrom:
    case Mode::Txsrom: {
        const std::size_t offset =
            static_cast<std::size_t>(mmc3_chr_bank(addr)) * 0x400U + (addr & 0x3FFU);
        chr_ram_[offset % chr_ram_size()] = value;
        return;
    }
    case Mode::Vrc24: {
        const bool vrc2 = ((runtime_.supervisor[0U] >> 4U) & 1U) == 0U;
        const uint16_t bank = inner.vrc.chr[addr >> 10U];
        const std::size_t offset =
            static_cast<std::size_t>(vrc2 ? bank >> 1U : bank) * 0x400U + (addr & 0x3FFU);
        chr_ram_[offset % chr_ram_size()] = value;
        return;
    }
    case Mode::Vrc6:
    case Mode::Vrc7:
    case Mode::Fme7: {
        const std::size_t offset =
            static_cast<std::size_t>(inner.vrc.chr[addr >> 10U]) * 0x400U + (addr & 0x3FFU);
        chr_ram_[offset % chr_ram_size()] = value;
        return;
    }
    case Mode::Vrc1: {
        const unsigned half = addr >> 12U;
        const std::size_t offset =
            static_cast<std::size_t>(inner.vrc.chr[half] & 0x1FU) * 0x1000U + (addr & 0x0FFFU);
        chr_ram_[offset % chr_ram_size()] = value;
        return;
    }
    default:
        chr_ram_[addr % chr_ram_size()] = value;
        return;
    }
}

MirrorMode Mapper468::mirror_mode() const {
    const auto& inner = runtime_.inner;
    const Byte flags = runtime_.supervisor[0U];
    switch (mode()) {
    case Mode::Axrom:
        return (inner.latch & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    case Mode::Misc:
        if ((flags & 0x08U) != 0U) {
            return (flags & 0x04U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
        return (inner.custom[1U] & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    case Mode::If12:
        return (inner.custom[0U] & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case Mode::Gnrom:
        return (flags & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case Mode::Bxrom:
    case Mode::Lf36:
    case Mode::Uxrom:
    case Mode::Unrom512:
    case Mode::Nanjing:
    case Mode::Vrc3:
        return (flags & 0x04U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    case Mode::Txsrom:
        if (inner.mmc3.mirror == 2U) {
            return MirrorMode::SingleLower;
        }
        return vrc_mirror(inner.mmc3.mirror);
    case Mode::Invalid:
        return header_.mirror_mode;
    default:
        return inner.mirror;
    }
}

std::optional<Address> Mapper468::nametable_address(Address addr) const {
    if (mode() != Mode::Txsrom || runtime_.inner.mmc3.mirror != 2U) {
        return std::nullopt;
    }
    return static_cast<Address>(((mmc3_chr_bank(addr & 0x0FFFU) & 0x80U) << 3U) | (addr & 0x3FFU));
}

bool Mapper468::maps_expansion(Address addr) const {
    return addr == 0x5301U || addr == 0x5601U;
}

Byte Mapper468::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x80U : 0U;
}

Byte Mapper468::read_expansion(Address addr) {
    return maps_expansion(addr) && runtime_.eeprom.output != 0U ? 0x80U : 0U;
}

void Mapper468::write_expansion(Address addr, Byte value) {
    if (addr == 0x5700U) {
        runtime_.supervisor[0U] = value;
        activate_mode();
        return;
    }
    if (addr == 0x5701U || addr == 0x5702U) {
        runtime_.supervisor[addr & 3U] = value;
        return;
    }
    if (addr == 0x5301U) {
        if (header_.submapper == 0U) {
            set_eeprom_pins((value >> 2U) & 1U, (value >> 1U) & 1U, value & 1U);
        }
        return;
    }
    if (addr == 0x5601U) {
        if (header_.submapper == 1U) {
            set_eeprom_pins((value >> 4U) & 1U, (value >> 1U) & 1U, value & 1U);
        }
        if ((runtime_.supervisor[3U] & 0x80U) == 0U) {
            runtime_.supervisor[3U] = value;
        }
        return;
    }
    if (mode() == Mode::Nanjing && addr >= 0x5000U && addr < 0x5400U) {
        runtime_.inner.custom[(addr >> 8U) & 3U] = value;
    }
}

void Mapper468::write_mmc1(Address addr, Byte value) {
    auto& mmc1 = runtime_.inner.mmc1;
    if ((value & 0x80U) != 0U) {
        mmc1.shift = 0x10U;
        mmc1.write_count = 0U;
        mmc1.control |= 0x0CU;
        return;
    }
    mmc1.shift = static_cast<Byte>((mmc1.shift >> 1U) | ((value & 1U) << 4U));
    ++mmc1.write_count;
    if (mmc1.write_count != 5U) {
        return;
    }
    const Byte data = mmc1.shift & 0x1FU;
    if (addr < 0xA000U) {
        mmc1.control = data;
        constexpr std::array kModes = {MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper,
                                       MirrorMode::Vertical,
                                       MirrorMode::Horizontal};
        runtime_.inner.mirror = kModes[data & 3U];
    } else if (addr < 0xC000U) {
        mmc1.chr0 = data;
    } else if (addr < 0xE000U) {
        mmc1.chr1 = data;
    } else {
        mmc1.prg = data;
    }
    mmc1.shift = 0x10U;
    mmc1.write_count = 0U;
}

void Mapper468::write_mmc24(Address addr, Byte value) {
    auto& mmc24 = runtime_.inner.mmc24;
    const auto group = static_cast<unsigned>(addr >> 12U);
    if (group == 10U) {
        mmc24.prg = value & 0x0FU;
    } else if (group >= 11U && group <= 14U) {
        mmc24.chr[group - 11U] = value & 0x1FU;
    } else if (group == 15U && mode() == Mode::Fxrom) {
        runtime_.inner.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
}

void Mapper468::write_mmc3(Address addr, Byte value) {
    auto& mmc3 = runtime_.inner.mmc3;
    auto& irq = runtime_.inner.irq;
    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            mmc3.bank_select = value & 0xC7U;
        } else {
            const unsigned index = mmc3.bank_select & 7U;
            mmc3.banks[index] = index >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            mmc3.mirror = value & 3U;
            runtime_.inner.mirror = vrc_mirror(value);
        } else {
            runtime_.inner.ram_enabled = (value & 0x80U) != 0U;
            runtime_.inner.ram_write_protected = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            irq.latch = value;
        } else {
            irq.counter = 0U;
            irq.reload = 1U;
        }
    } else if (even) {
        irq.enabled = 0U;
        irq.pending = 0U;
    } else {
        irq.enabled = 1U;
    }
}

void Mapper468::write_vrc24(Address addr, Byte value) {
    auto& vrc = runtime_.inner.vrc;
    auto& irq = runtime_.inner.irq;
    const Byte select = runtime_.supervisor[0U] >> 4U;
    const bool vrc2 = (select & 1U) == 0U;
    Byte index = addr & 3U;
    if (select >= 2U) {
        index = static_cast<Byte>(((addr >> 1U) & 1U) | ((addr & 1U) << 1U));
    }
    const auto group = static_cast<unsigned>(addr >> 12U);
    if (group == 8U || group == 10U) {
        vrc.prg[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        if (vrc2 || index == 0U) {
            runtime_.inner.mirror = vrc_mirror(value);
        } else if (index == 2U) {
            vrc.mode = value & 3U;
        }
    } else if (group >= 11U && group <= 14U) {
        auto& bank = vrc.chr[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U) {
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x0FU) | ((value & (vrc2 ? 0x0FU : 0x1FU)) << 4U);
        }
    } else if (group == 15U && !vrc2) {
        switch (index) {
        case 0U:
            irq.latch = (irq.latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            irq.latch = (irq.latch & 0x0FU) | ((value & 0x0FU) << 4U);
            break;
        case 2U:
            irq.control = value & 7U;
            irq.enabled = (value >> 1U) & 1U;
            irq.after_ack = value & 1U;
            irq.pending = 0U;
            if (irq.enabled != 0U) {
                irq.counter = irq.latch;
                irq.prescaler = 341U;
            }
            break;
        case 3U:
            irq.pending = 0U;
            irq.enabled = irq.after_ack;
            break;
        default:
            break;
        }
    }
}

void Mapper468::write_vrc6(Address addr, Byte value) {
    auto& vrc = runtime_.inner.vrc;
    auto& irq = runtime_.inner.irq;
    auto& pulse1 = runtime_.inner.pulse1;
    auto& pulse2 = runtime_.inner.pulse2;
    auto& saw = runtime_.inner.saw;
    switch (addr & 0xF003U) {
    case 0x8000U:
    case 0x8001U:
    case 0x8002U:
    case 0x8003U:
        vrc.prg[0U] = value & 0x0FU;
        break;
    case 0xC000U:
    case 0xC001U:
    case 0xC002U:
    case 0xC003U:
        vrc.prg[1U] = value & 0x1FU;
        break;
    case 0x9000U:
        pulse1.mode = (value & 0x80U) != 0U;
        pulse1.duty = (value >> 4U) & 7U;
        pulse1.volume = value & 0x0FU;
        break;
    case 0x9001U:
        pulse1.period = (pulse1.period & 0x0F00U) | value;
        break;
    case 0x9002U:
        pulse1.period = (pulse1.period & 0x00FFU) | ((value & 0x0FU) << 8U);
        pulse1.enabled = (value & 0x80U) != 0U;
        break;
    case 0x9003U: {
        const Byte halted = value & 1U;
        const Byte shift = (value & 4U) != 0U ? 8U : ((value & 2U) != 0U ? 4U : 0U);
        pulse1.halted = pulse2.halted = saw.halted = halted;
        pulse1.frequency_shift = pulse2.frequency_shift = saw.frequency_shift = shift;
        break;
    }
    case 0xA000U:
        pulse2.mode = (value & 0x80U) != 0U;
        pulse2.duty = (value >> 4U) & 7U;
        pulse2.volume = value & 0x0FU;
        break;
    case 0xA001U:
        pulse2.period = (pulse2.period & 0x0F00U) | value;
        break;
    case 0xA002U:
        pulse2.period = (pulse2.period & 0x00FFU) | ((value & 0x0FU) << 8U);
        pulse2.enabled = (value & 0x80U) != 0U;
        break;
    case 0xB000U:
        saw.rate = value & 0x3FU;
        break;
    case 0xB001U:
        saw.period = (saw.period & 0x0F00U) | value;
        break;
    case 0xB002U:
        saw.period = (saw.period & 0x00FFU) | ((value & 0x0FU) << 8U);
        saw.enabled = (value & 0x80U) != 0U;
        break;
    case 0xB003U:
        runtime_.inner.mirror = vrc_mirror(value >> 2U);
        break;
    case 0xD000U:
    case 0xD001U:
    case 0xD002U:
    case 0xD003U:
        vrc.chr[addr & 3U] = value;
        break;
    case 0xE000U:
    case 0xE001U:
    case 0xE002U:
    case 0xE003U:
        vrc.chr[4U + (addr & 3U)] = value;
        break;
    case 0xF000U:
        irq.latch = value;
        break;
    case 0xF001U:
        irq.after_ack = value & 1U;
        irq.enabled = (value >> 1U) & 1U;
        irq.control = value & 7U;
        irq.prescaler = 341U;
        if (irq.enabled != 0U) {
            irq.counter = irq.latch;
        }
        irq.pending = 0U;
        break;
    case 0xF002U:
        irq.enabled = irq.after_ack;
        irq.pending = 0U;
        break;
    default:
        break;
    }
}

void Mapper468::write_vrc7(Address addr, Byte value) {
    auto& vrc = runtime_.inner.vrc;
    auto& irq = runtime_.inner.irq;
    switch (addr) {
    case 0x8000U:
    case 0x8010U:
    case 0x9000U:
        vrc.prg[addr == 0x8000U ? 0U : (addr == 0x8010U ? 1U : 2U)] = value & 0x3FU;
        break;
    case 0x9010U:
    case 0x9030U:
        fm_audio_->write_prg(addr, value);
        break;
    case 0xA000U:
    case 0xA010U:
    case 0xB000U:
    case 0xB010U:
    case 0xC000U:
    case 0xC010U:
    case 0xD000U:
    case 0xD010U:
        vrc.chr[(addr - 0xA000U) >> 4U] = value;
        break;
    case 0xE000U:
        runtime_.inner.mirror = vrc_mirror(value);
        fm_audio_->write_prg(addr, value);
        break;
    case 0xE010U:
        irq.latch = value;
        break;
    case 0xF000U:
        irq.after_ack = value & 1U;
        irq.enabled = (value >> 1U) & 1U;
        if (irq.enabled != 0U) {
            irq.counter = irq.latch;
            irq.prescaler = 341U;
        }
        irq.pending = 0U;
        break;
    case 0xF010U:
        irq.enabled = irq.after_ack;
        irq.pending = 0U;
        break;
    default:
        break;
    }
}

void Mapper468::write_fme7(Address addr, Byte value) {
    auto& vrc = runtime_.inner.vrc;
    auto& irq = runtime_.inner.irq;
    auto& audio = runtime_.inner.fme7;
    if (addr < 0xA000U) {
        vrc.command = value & 0x0FU;
        return;
    }
    if (addr < 0xC000U) {
        if (vrc.command <= 7U) {
            vrc.chr[vrc.command] = value;
        } else if (vrc.command == 8U) {
            runtime_.inner.ram_enabled = (value & 0xC0U) == 0xC0U;
            vrc.prg[0U] = value & 0x3FU;
        } else if (vrc.command >= 9U && vrc.command <= 11U) {
            vrc.prg[vrc.command - 8U] = value & 0x3FU;
        } else if (vrc.command == 12U) {
            runtime_.inner.mirror = vrc_mirror(value);
        } else if (vrc.command == 13U) {
            irq.enabled = value & 1U;
            irq.counter_enabled = (value & 0x80U) != 0U;
            irq.pending = 0U;
        } else if (vrc.command == 14U) {
            irq.counter = (irq.counter & 0xFF00U) | value;
        } else {
            irq.counter = (irq.counter & 0x00FFU) | (static_cast<uint16_t>(value) << 8U);
        }
        return;
    }
    if (addr < 0xE000U) {
        audio.ay_register = value & 0x0FU;
        return;
    }

    const Byte reg = audio.ay_register;
    if (reg <= 5U) {
        auto& channel = audio.channels[reg / 2U];
        if ((reg & 1U) == 0U) {
            channel.period = (channel.period & 0x0F00U) | value;
        } else {
            channel.period = (channel.period & 0x00FFU) | ((value & 0x0FU) << 8U);
        }
    } else if (reg == 6U) {
        audio.noise_period = value & 0x1FU;
    } else if (reg == 7U) {
        for (unsigned index = 0U; index < 3U; ++index) {
            audio.channels[index].tone_enabled = (value & (1U << index)) == 0U;
            audio.channels[index].noise_enabled = (value & (1U << (index + 3U))) == 0U;
        }
    } else if (reg >= 8U && reg <= 10U) {
        auto& channel = audio.channels[reg - 8U];
        channel.volume = value & 0x0FU;
        channel.use_envelope = (value & 0x10U) != 0U;
    } else if (reg == 11U) {
        audio.envelope_period = (audio.envelope_period & 0xFF00U) | value;
    } else if (reg == 12U) {
        audio.envelope_period = (audio.envelope_period & 0x00FFU) | value;
    } else if (reg == 13U) {
        audio.envelope_shape = value & 0x0FU;
        audio.envelope_step = 0U;
        audio.envelope_attack = (value & 0x04U) != 0U;
        audio.envelope_holding = 0U;
        audio.envelope_level = audio.envelope_attack != 0U ? 0U : 31U;
        audio.envelope_timer = audio.envelope_period;
    }
}

void Mapper468::power_on() {
    reset();
    if (!persistent_ram_) {
        wram_.fill(0U);
    }
    chr_ram_.fill(0U);
}

void Mapper468::reset() {
    runtime_ = {};
    runtime_.supervisor = {
        0x0FU, 0xFFU, static_cast<Byte>(header_.submapper == 1U ? 0x10U : 0U), 0U};
    activate_mode();
}

void Mapper468::activate_mode() {
    runtime_.inner = {};
    runtime_.inner.mirror = header_.mirror_mode;
    runtime_.inner.mmc3.mirror = header_.mirror_mode == MirrorMode::Horizontal ? 1U : 0U;
    if (mode() == Mode::Txrom || mode() == Mode::Txsrom) {
        runtime_.inner.ram_enabled = 1U;
    }
    fm_audio_->reset();
}

bool Mapper468::irq_pending() const {
    if (mode() == Mode::Lf36) {
        return runtime_.inner.custom[1U] != 0U && (runtime_.inner.custom[3U] & 0x10U) != 0U;
    }
    return runtime_.inner.irq.pending != 0U;
}

void Mapper468::acknowledge_irq() {
    if (mode() != Mode::Lf36) {
        runtime_.inner.irq.pending = 0U;
    }
}

void Mapper468::clock_irq_counter() {
    if (mode() != Mode::Txrom && mode() != Mode::Txsrom) {
        return;
    }
    auto& irq = runtime_.inner.irq;
    if (irq.counter == 0U || irq.reload != 0U) {
        irq.counter = irq.latch & 0xFFU;
        irq.reload = 0U;
    } else {
        --irq.counter;
    }
    if (irq.counter == 0U && irq.enabled != 0U) {
        irq.pending = 1U;
    }
}

void Mapper468::tick_vrc_irq() {
    auto& irq = runtime_.inner.irq;
    if (irq.enabled == 0U) {
        return;
    }
    if ((irq.control & 0x04U) == 0U && mode() != Mode::Vrc7) {
        if (irq.prescaler > 3U) {
            irq.prescaler -= 3U;
            return;
        }
        irq.prescaler += 338U;
    }
    if (irq.counter == 0xFFU) {
        irq.counter = irq.latch & 0xFFU;
        irq.pending = 1U;
    } else {
        ++irq.counter;
    }
}

void Mapper468::tick_vrc3_irq() {
    auto& irq = runtime_.inner.irq;
    if (irq.enabled == 0U) {
        return;
    }
    if ((irq.control & 0x04U) == 0U) {
        if (irq.prescaler > 3U) {
            irq.prescaler -= 3U;
            return;
        }
        irq.prescaler += 338U;
    }
    const uint16_t mask = (irq.control & 0x04U) != 0U ? 0x00FFU : 0xFFFFU;
    if ((irq.counter & mask) == mask) {
        irq.counter = (irq.counter & ~mask) | (irq.latch & mask);
        irq.pending = 1U;
    } else {
        ++irq.counter;
    }
}

void Mapper468::clock_cpu() {
    if (mode() == Mode::Vrc24) {
        tick_vrc_irq();
    } else if (mode() == Mode::Vrc3) {
        tick_vrc3_irq();
    } else if (mode() == Mode::Lf36 && runtime_.inner.custom[1U] != 0U) {
        auto& custom = runtime_.inner.custom;
        if (++custom[2U] == 0U) {
            ++custom[3U];
        }
    }
}

void Mapper468::on_ppu_hblank() {
    if (mode() == Mode::Nanjing) {
        runtime_.nanjing_upper_half = runtime_.ppu_scanline >= 127U;
    }
    if (runtime_.ppu_scanline < 311U) {
        ++runtime_.ppu_scanline;
    }
}

void Mapper468::on_ppu_frame_start() {
    runtime_.ppu_scanline = 0U;
    runtime_.nanjing_upper_half = 0U;
}

bool Mapper468::has_expansion_audio() const {
    return mode() == Mode::Vrc6 || mode() == Mode::Vrc7 || mode() == Mode::Fme7;
}

void Mapper468::tick_vrc6_audio() {
    const auto tick_pulse = [](Vrc6PulseState& pulse) {
        if (pulse.enabled == 0U) {
            pulse.step = 0U;
            pulse.timer = pulse.period >> pulse.frequency_shift;
        } else if (pulse.halted == 0U) {
            if (pulse.timer == 0U) {
                pulse.timer = pulse.period >> pulse.frequency_shift;
                pulse.step = (pulse.step + 1U) & 0x0FU;
            } else {
                --pulse.timer;
            }
        }
    };
    tick_pulse(runtime_.inner.pulse1);
    tick_pulse(runtime_.inner.pulse2);
    auto& saw = runtime_.inner.saw;
    if (saw.enabled == 0U) {
        saw.step = 0U;
        saw.accumulator = 0U;
        saw.timer = saw.period >> saw.frequency_shift;
    } else if (saw.halted == 0U) {
        if (saw.timer == 0U) {
            saw.timer = saw.period >> saw.frequency_shift;
            ++saw.step;
            if (saw.step >= 14U) {
                saw.step = 0U;
                saw.accumulator = 0U;
            } else if ((saw.step & 1U) == 0U) {
                saw.accumulator += saw.rate;
            }
        } else {
            --saw.timer;
        }
    }
}

void Mapper468::clock_fme7_envelope() {
    auto& audio = runtime_.inner.fme7;
    if (audio.envelope_holding != 0U) {
        return;
    }
    if (audio.envelope_step == 31U) {
        if ((audio.envelope_shape & 0x08U) == 0U) {
            audio.envelope_holding = 1U;
            audio.envelope_level = 0U;
            return;
        }
        if ((audio.envelope_shape & 0x01U) != 0U) {
            audio.envelope_holding = 1U;
            const bool high = (audio.envelope_shape & 0x02U) != 0U ? audio.envelope_attack == 0U
                                                                   : audio.envelope_attack != 0U;
            audio.envelope_level = high ? 31U : 0U;
            return;
        }
        if ((audio.envelope_shape & 0x02U) != 0U) {
            audio.envelope_attack ^= 1U;
        }
        audio.envelope_step = 0U;
    } else {
        ++audio.envelope_step;
    }
    audio.envelope_level =
        audio.envelope_attack != 0U ? audio.envelope_step : 31U - audio.envelope_step;
}

void Mapper468::tick_fme7_audio() {
    auto& audio = runtime_.inner.fme7;
    if (++audio.divider < 16U) {
        return;
    }
    audio.divider = 0U;
    for (auto& channel : audio.channels) {
        if (channel.timer == 0U) {
            channel.timer = std::max<uint16_t>(channel.period, 1U) - 1U;
            channel.output ^= 1U;
        } else {
            --channel.timer;
        }
    }
    if (audio.noise_timer == 0U) {
        audio.noise_timer = static_cast<uint16_t>(2U * (audio.noise_period + 1U) - 1U);
        const uint32_t feedback = (audio.noise_lfsr ^ (audio.noise_lfsr >> 3U)) & 1U;
        audio.noise_lfsr = (audio.noise_lfsr >> 1U) | (feedback << 16U);
        if (audio.noise_lfsr == 0U) {
            audio.noise_lfsr = 1U;
        }
    } else {
        --audio.noise_timer;
    }
    if (audio.envelope_timer == 0U) {
        audio.envelope_timer = audio.envelope_period;
        clock_fme7_envelope();
    } else {
        --audio.envelope_timer;
    }
}

void Mapper468::clock_audio() {
    if (mode() == Mode::Vrc6) {
        tick_vrc_irq();
        tick_vrc6_audio();
    } else if (mode() == Mode::Vrc7) {
        tick_vrc_irq();
        fm_audio_->clock_audio();
    } else if (mode() == Mode::Fme7) {
        auto& irq = runtime_.inner.irq;
        if (irq.counter_enabled != 0U) {
            if (irq.counter == 0U) {
                if (irq.enabled != 0U) {
                    irq.pending = 1U;
                }
            } else {
                --irq.counter;
            }
        }
        tick_fme7_audio();
    }
}

float Mapper468::audio_output() const {
    if (mode() == Mode::Vrc7) {
        return fm_audio_->audio_output();
    }
    if (mode() == Mode::Vrc6) {
        const auto pulse_output = [](const Vrc6PulseState& pulse) {
            if (pulse.enabled == 0U) {
                return 0.0F;
            }
            return pulse.mode != 0U || pulse.step <= pulse.duty ? static_cast<float>(pulse.volume)
                                                                : 0.0F;
        };
        const float saw = runtime_.inner.saw.enabled != 0U
                              ? static_cast<float>(runtime_.inner.saw.accumulator >> 3U)
                              : 0.0F;
        return (pulse_output(runtime_.inner.pulse1) + pulse_output(runtime_.inner.pulse2) + saw) *
               0.006F;
    }
    if (mode() == Mode::Fme7) {
        const auto& audio = runtime_.inner.fme7;
        const bool noise_high = (audio.noise_lfsr & 1U) != 0U;
        float output = 0.0F;
        for (const auto& channel : audio.channels) {
            const bool tone = channel.tone_enabled == 0U || channel.output != 0U;
            const bool noise = channel.noise_enabled == 0U || noise_high;
            if (tone && noise) {
                output += channel.use_envelope != 0U ? kAyEnvelope[audio.envelope_level & 0x1FU]
                                                     : kAyVolume[channel.volume & 0x0FU];
            }
        }
        return output * 0.005F;
    }
    return 0.0F;
}

std::span<const Byte> Mapper468::battery_ram() const {
    return persistent_ram_ ? std::span<const Byte>(wram_) : std::span<const Byte>{};
}

void Mapper468::set_battery_ram(std::span<const Byte> data) {
    if (persistent_ram_ && data.size() == wram_.size()) {
        std::ranges::copy(data, wram_.begin());
    }
}

void Mapper468::set_eeprom_pins(Byte select, Byte clock, Byte data) {
    auto& eeprom = runtime_.eeprom;
    if (select != 0U) {
        eeprom.phase = 0U;
    } else if (eeprom.clock == 0U && clock != 0U) {
        if (eeprom.phase < 8U) {
            eeprom.command = static_cast<Byte>((eeprom.command << 1U) | (data != 0U ? 1U : 0U));
            ++eeprom.phase;
            if (eeprom.phase == 8U && (eeprom.command & 0xF0U) != 0x50U &&
                (eeprom.command & 0xF0U) != 0xA0U) {
                eeprom.phase = 0U;
            }
        } else {
            const Byte mask = static_cast<Byte>(1U << (15U - eeprom.phase));
            const Byte address = eeprom.command & 0x0FU;
            if ((eeprom.command & 0xF0U) == 0xA0U) {
                eeprom.memory[address] = static_cast<Byte>((eeprom.memory[address] & ~mask) |
                                                           ((data != 0U ? 1U : 0U) * mask));
                const unsigned index =
                    eeprom.memory[0U] | eeprom.memory[1U] | ((eeprom.memory[2U] << 8U) & 0x1FFU);
                const auto decoded =
                    static_cast<Byte>(kLut509[index] >> ((address & 1U) == 0U ? 8U : 0U));
                eeprom.output = (decoded & mask) != 0U;
            } else if ((eeprom.command & 0xF0U) == 0x50U) {
                eeprom.output = (eeprom.memory[address] & mask) != 0U;
            }
            if (++eeprom.phase == 16U) {
                eeprom.phase = 0U;
            }
        }
    }
    eeprom.clock = clock != 0U;
}

bool Mapper468::valid_mode_state(const RuntimeState& state) const {
    const auto& inner = state.inner;
    const auto& mmc1 = inner.mmc1;
    const auto& mmc24 = inner.mmc24;
    const auto& mmc3 = inner.mmc3;
    const auto& irq = inner.irq;
    const auto& audio = inner.fme7;
    const auto valid_pulse = [](const Vrc6PulseState& pulse) {
        return pulse.duty <= 7U && pulse.volume <= 15U && is_byte_flag(pulse.mode) &&
               is_byte_flag(pulse.enabled) && is_byte_flag(pulse.halted) &&
               (pulse.frequency_shift == 0U || pulse.frequency_shift == 4U ||
                pulse.frequency_shift == 8U) &&
               pulse.period <= 0x0FFFU && pulse.timer <= 0x0FFFU && pulse.step <= 15U;
    };
    const auto valid_ay_channel = [](const AyChannelState& channel) {
        return channel.period <= 0x0FFFU && channel.volume <= 15U &&
               is_byte_flag(channel.use_envelope) && is_byte_flag(channel.tone_enabled) &&
               is_byte_flag(channel.noise_enabled) && is_byte_flag(channel.output);
    };
    return inner.mirror <= MirrorMode::SingleUpper && mmc1.shift <= 0x1FU &&
           mmc1.write_count <= 4U && mmc1.control <= 0x1FU && mmc1.chr0 <= 0x1FU &&
           mmc1.chr1 <= 0x1FU && mmc1.prg <= 0x1FU && mmc24.prg <= 0x0FU &&
           std::ranges::all_of(mmc24.chr, [](Byte bank) { return bank <= 0x1FU; }) &&
           std::ranges::all_of(mmc24.latches, is_byte_flag) && (mmc3.bank_select & 0x38U) == 0U &&
           mmc3.mirror <= 3U && mmc3.banks[6U] <= 0x3FU && mmc3.banks[7U] <= 0x3FU &&
           std::ranges::all_of(inner.vrc.chr, [](uint16_t bank) { return bank <= 0x1FFU; }) &&
           inner.vrc.command <= 0x0FU && inner.vrc.mode <= 3U && inner.vrc.microwire <= 1U &&
           irq.prescaler <= 341U && irq.control <= 7U && is_byte_flag(irq.enabled) &&
           is_byte_flag(irq.after_ack) && is_byte_flag(irq.pending) &&
           is_byte_flag(irq.counter_enabled) && is_byte_flag(irq.reload) &&
           (irq.pending == 0U || irq.enabled != 0U) && is_byte_flag(inner.ram_enabled) &&
           is_byte_flag(inner.ram_write_protected) && valid_pulse(inner.pulse1) &&
           valid_pulse(inner.pulse2) && is_byte_flag(inner.saw.enabled) &&
           is_byte_flag(inner.saw.halted) &&
           (inner.saw.frequency_shift == 0U || inner.saw.frequency_shift == 4U ||
            inner.saw.frequency_shift == 8U) &&
           inner.saw.rate <= 0x3FU && inner.saw.period <= 0x0FFFU && inner.saw.timer <= 0x0FFFU &&
           inner.saw.step < 14U && std::ranges::all_of(audio.channels, valid_ay_channel) &&
           audio.ay_register <= 0x0FU && audio.divider < 16U && audio.noise_period <= 0x1FU &&
           audio.noise_lfsr >= 1U && audio.noise_lfsr <= 0x1FFFFU &&
           audio.envelope_shape <= 0x0FU && audio.envelope_step <= 31U &&
           audio.envelope_level <= 31U && is_byte_flag(audio.envelope_attack) &&
           is_byte_flag(audio.envelope_holding) && is_byte_flag(state.eeprom.clock) &&
           state.eeprom.phase <= 15U && is_byte_flag(state.eeprom.output) &&
           state.ppu_scanline <= 311U && is_byte_flag(state.nanjing_upper_half);
}

void Mapper468::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(header_.submapper);
    writer.write(static_cast<std::uint32_t>(prg_size_));
    writer.write(static_cast<std::uint32_t>(chr_size_));
    writer.write(static_cast<Byte>(header_.mirror_mode));
    writer.write(static_cast<std::uint32_t>(chr_ram_size_));
    writer.write<Byte>(persistent_ram_ ? 1U : 0U);
    writer.write_array(runtime_.supervisor);

    const auto& inner = runtime_.inner;
    writer.write_array(inner.custom);
    writer.write(inner.latch);
    writer.write(static_cast<Byte>(inner.mirror));
    writer.write(inner.mmc1.shift);
    writer.write(inner.mmc1.write_count);
    writer.write(inner.mmc1.control);
    writer.write(inner.mmc1.chr0);
    writer.write(inner.mmc1.chr1);
    writer.write(inner.mmc1.prg);
    writer.write(inner.mmc24.prg);
    writer.write_array(inner.mmc24.chr);
    writer.write_array(inner.mmc24.latches);
    writer.write(inner.mmc3.bank_select);
    writer.write_array(inner.mmc3.banks);
    writer.write(inner.mmc3.mirror);
    writer.write_array(inner.vrc.prg);
    writer.write_array(inner.vrc.chr);
    writer.write(inner.vrc.command);
    writer.write(inner.vrc.mode);
    writer.write(inner.vrc.microwire);
    writer.write(inner.irq.latch);
    writer.write(inner.irq.counter);
    writer.write(inner.irq.prescaler);
    writer.write(inner.irq.control);
    writer.write(inner.irq.enabled);
    writer.write(inner.irq.after_ack);
    writer.write(inner.irq.pending);
    writer.write(inner.irq.counter_enabled);
    writer.write(inner.irq.reload);
    writer.write(inner.ram_enabled);
    writer.write(inner.ram_write_protected);

    const auto save_pulse = [&writer](const Vrc6PulseState& pulse) {
        writer.write(pulse.duty);
        writer.write(pulse.volume);
        writer.write(pulse.mode);
        writer.write(pulse.enabled);
        writer.write(pulse.halted);
        writer.write(pulse.frequency_shift);
        writer.write(pulse.period);
        writer.write(pulse.timer);
        writer.write(pulse.step);
    };
    save_pulse(inner.pulse1);
    save_pulse(inner.pulse2);
    writer.write(inner.saw.rate);
    writer.write(inner.saw.enabled);
    writer.write(inner.saw.halted);
    writer.write(inner.saw.frequency_shift);
    writer.write(inner.saw.period);
    writer.write(inner.saw.timer);
    writer.write(inner.saw.accumulator);
    writer.write(inner.saw.step);

    for (const auto& channel : inner.fme7.channels) {
        writer.write(channel.period);
        writer.write(channel.timer);
        writer.write(channel.volume);
        writer.write(channel.use_envelope);
        writer.write(channel.tone_enabled);
        writer.write(channel.noise_enabled);
        writer.write(channel.output);
    }
    writer.write(inner.fme7.ay_register);
    writer.write(inner.fme7.divider);
    writer.write(inner.fme7.noise_period);
    writer.write(inner.fme7.noise_timer);
    writer.write(inner.fme7.noise_lfsr);
    writer.write(inner.fme7.envelope_period);
    writer.write(inner.fme7.envelope_timer);
    writer.write(inner.fme7.envelope_shape);
    writer.write(inner.fme7.envelope_step);
    writer.write(inner.fme7.envelope_level);
    writer.write(inner.fme7.envelope_attack);
    writer.write(inner.fme7.envelope_holding);
    writer.write_array(runtime_.eeprom.memory);
    writer.write(runtime_.eeprom.clock);
    writer.write(runtime_.eeprom.phase);
    writer.write(runtime_.eeprom.command);
    writer.write(runtime_.eeprom.output);
    writer.write(runtime_.ppu_scanline);
    writer.write(runtime_.nanjing_upper_half);
    writer.write_array(wram_);
    writer.write_array(chr_ram_);

    StateWriter audio_writer;
    fm_audio_->save_state(audio_writer);
    const auto audio_state = audio_writer.take();
    writer.write_bytes(audio_state);
}

void Mapper468::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto submapper = reader.read<Byte>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto saved_chr_ram_size = reader.read<std::uint32_t>();
    const auto persistent = reader.read<Byte>();
    RuntimeState next = runtime_;
    reader.read_array(next.supervisor);

    auto& inner = next.inner;
    reader.read_array(inner.custom);
    reader.read(inner.latch);
    const auto mirror = reader.read<Byte>();
    reader.read(inner.mmc1.shift);
    reader.read(inner.mmc1.write_count);
    reader.read(inner.mmc1.control);
    reader.read(inner.mmc1.chr0);
    reader.read(inner.mmc1.chr1);
    reader.read(inner.mmc1.prg);
    reader.read(inner.mmc24.prg);
    reader.read_array(inner.mmc24.chr);
    reader.read_array(inner.mmc24.latches);
    reader.read(inner.mmc3.bank_select);
    reader.read_array(inner.mmc3.banks);
    reader.read(inner.mmc3.mirror);
    reader.read_array(inner.vrc.prg);
    reader.read_array(inner.vrc.chr);
    reader.read(inner.vrc.command);
    reader.read(inner.vrc.mode);
    reader.read(inner.vrc.microwire);
    reader.read(inner.irq.latch);
    reader.read(inner.irq.counter);
    reader.read(inner.irq.prescaler);
    reader.read(inner.irq.control);
    reader.read(inner.irq.enabled);
    reader.read(inner.irq.after_ack);
    reader.read(inner.irq.pending);
    reader.read(inner.irq.counter_enabled);
    reader.read(inner.irq.reload);
    reader.read(inner.ram_enabled);
    reader.read(inner.ram_write_protected);

    const auto load_pulse = [&reader](Vrc6PulseState& pulse) {
        reader.read(pulse.duty);
        reader.read(pulse.volume);
        reader.read(pulse.mode);
        reader.read(pulse.enabled);
        reader.read(pulse.halted);
        reader.read(pulse.frequency_shift);
        reader.read(pulse.period);
        reader.read(pulse.timer);
        reader.read(pulse.step);
    };
    load_pulse(inner.pulse1);
    load_pulse(inner.pulse2);
    reader.read(inner.saw.rate);
    reader.read(inner.saw.enabled);
    reader.read(inner.saw.halted);
    reader.read(inner.saw.frequency_shift);
    reader.read(inner.saw.period);
    reader.read(inner.saw.timer);
    reader.read(inner.saw.accumulator);
    reader.read(inner.saw.step);

    for (auto& channel : inner.fme7.channels) {
        reader.read(channel.period);
        reader.read(channel.timer);
        reader.read(channel.volume);
        reader.read(channel.use_envelope);
        reader.read(channel.tone_enabled);
        reader.read(channel.noise_enabled);
        reader.read(channel.output);
    }
    reader.read(inner.fme7.ay_register);
    reader.read(inner.fme7.divider);
    reader.read(inner.fme7.noise_period);
    reader.read(inner.fme7.noise_timer);
    reader.read(inner.fme7.noise_lfsr);
    reader.read(inner.fme7.envelope_period);
    reader.read(inner.fme7.envelope_timer);
    reader.read(inner.fme7.envelope_shape);
    reader.read(inner.fme7.envelope_step);
    reader.read(inner.fme7.envelope_level);
    reader.read(inner.fme7.envelope_attack);
    reader.read(inner.fme7.envelope_holding);
    reader.read_array(next.eeprom.memory);
    reader.read(next.eeprom.clock);
    reader.read(next.eeprom.phase);
    reader.read(next.eeprom.command);
    reader.read(next.eeprom.output);
    reader.read(next.ppu_scanline);
    reader.read(next.nanjing_upper_half);
    std::array<Byte, 0x2000> staged_wram{};
    std::array<Byte, 0x8000> staged_chr_ram{};
    reader.read_array(staged_wram);
    reader.read_array(staged_chr_ram);
    std::vector<Byte> audio_state;
    reader.read_bytes(audio_state, kMaxFmStateSize);

    if (!reader.ok() || tag != kStateTag || submapper != header_.submapper ||
        prg_size != prg_size_ || chr_size != chr_size_ || initial_mirror > 3U ||
        static_cast<MirrorMode>(initial_mirror) != header_.mirror_mode ||
        saved_chr_ram_size != chr_ram_size_ || persistent > 1U ||
        (persistent != 0U) != persistent_ram_ || mirror > 3U || !valid_mode_state(next)) {
        reader.fail();
        return;
    }

    auto staged_audio = make_fm_audio();
    StateReader audio_reader(audio_state);
    staged_audio->load_state(audio_reader);
    if (!audio_reader.ok() || audio_reader.remaining() != 0U) {
        reader.fail();
        return;
    }

    inner.mirror = static_cast<MirrorMode>(mirror);
    runtime_ = std::move(next);
    wram_ = staged_wram;
    chr_ram_ = staged_chr_ram;
    fm_audio_ = std::move(staged_audio);
}

} // namespace mapperbus::core
