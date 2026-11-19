#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper422.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMiB = 1024UZ * 1024UZ;
constexpr std::size_t kPrgSize = 4UZ * kMiB;
constexpr std::size_t kChrSize = 2UZ * kMiB;
constexpr std::size_t kWramSize = 8192UZ;
constexpr std::size_t kStatePrefixSize = 45UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kMiscOffset = 11UZ;
constexpr std::size_t kChrBankOffset = 12UZ;
constexpr std::size_t kModeOffset = 13UZ;
constexpr std::size_t kBankSelectOffset = 14UZ;
constexpr std::size_t kBankModeOffset = 15UZ;
constexpr std::size_t kBanksOffset = 16UZ;
constexpr std::size_t kMirrorOffset = 24UZ;
constexpr std::size_t kIrqCounterOffset = 25UZ;
constexpr std::size_t kIrqReloadOffset = 26UZ;
constexpr std::size_t kIrqFlagsOffset = 27UZ;
constexpr std::size_t kRamFlagsOffset = 28UZ;
constexpr std::size_t kWramLengthOffset = 29UZ;

INesHeader make_header(Byte submapper = 0U,
                       bool battery = false,
                       bool wram = true,
                       bool vertical = true,
                       std::size_t chr_ram = 0U) {
    INesHeader header{};
    header.mapper_number = 422U;
    header.submapper = submapper;
    header.is_nes2 = true;
    header.has_battery = battery;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_ram_size = !battery && wram ? kWramSize : 0U;
    header.prg_nvram_size = battery ? kWramSize : 0U;
    header.chr_ram_size = chr_ram;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 53U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + (bank >> 8U) * 73U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper422> make_board(std::size_t prg_size = kPrgSize,
                                      std::size_t chr_size = kChrSize,
                                      Byte submapper = 0U,
                                      bool battery = false,
                                      bool wram = true,
                                      bool vertical = true,
                                      std::size_t chr_ram = 8192UZ,
                                      bool patterned = true) {
    std::vector<Byte> prg(prg_size);
    std::vector<Byte> chr(chr_size);
    if (patterned) {
        for (std::size_t index = 0U; index < prg.size(); ++index) {
            prg[index] = prg_cell(static_cast<unsigned>(index / 8192U),
                                  static_cast<unsigned>(index % 8192U));
        }
        for (std::size_t index = 0U; index < chr.size(); ++index) {
            chr[index] = chr_cell(static_cast<unsigned>(index / 1024U),
                                  static_cast<unsigned>(index % 1024U));
        }
    }
    auto header = make_header(submapper, battery, wram, vertical, chr_ram);
    if (chr_size != 0U) {
        header.chr_ram_size = 0U;
    }
    REQUIRE(Mapper422::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper422>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned inner_prg_bank(Byte mode, Byte r6, Byte r7, unsigned slot, bool prg_swap = false) {
    switch (mode) {
    case 0U:
    case 4U:
        if (prg_swap) {
            return slot == 0U ? 30U : slot == 1U ? r7 & 31U : slot == 2U ? r6 & 31U : 31U;
        }
        return slot == 0U ? r6 & 31U : slot == 1U ? r7 & 31U : slot == 2U ? 30U : 31U;
    case 1U:
    case 2U:
    case 5U:
    case 6U:
        return ((r6 >> 1U) & 15U) * 2U + (slot & 1U);
    case 3U:
    case 7U:
        return ((r6 >> 2U) & 7U) * 4U + slot;
    case 8U:
    case 12U:
        if (slot >= 2U) {
            return slot + 28U;
        }
        return (((slot == 0U ? r6 : r7) & 12U) << 1U) |
               (((slot == 0U ? r6 : r7) & 2U) != 0U ? 3U : 0U) | ((slot == 0U ? r6 : r7) & 1U);
    case 9U:
    case 13U:
        return slot < 2U ? (r6 & 15U) * 2U + (slot & 1U) : slot + 28U;
    case 10U:
    case 14U: {
        const auto value = slot == 0U ? r6 : r7;
        return slot >= 2U ? slot + 28U
                          : ((value & 4U) << 2U) | ((value & 2U) << 2U) | ((value & 1U) << 2U) |
                                ((value & 2U) << 1U) | (value & 1U);
    }
    default:
        return (r6 & 7U) * 4U + slot;
    }
}

std::size_t outer_prg_offset(
    unsigned inner, Address addr, Byte outer, Byte submapper, Byte misc = 0U) {
    unsigned bank = inner & 31U;
    if ((outer & 0x40U) != 0U) {
        bank = (bank & ~0x10U) | ((outer & 1U) << 4U);
    }
    bank |= ((outer >> 1U) & 3U) << 5U;
    bank |= (outer & 0x10U) << 3U;
    bank |= (~outer & 0x20U) << 3U;
    if (submapper == 1U) {
        bank = (bank & 0x7FU) | ((bank & 0x100U) >> 1U);
    } else if (submapper == 2U) {
        bank = (bank & 0x7FU) | ((misc & 4U) << 5U);
    }
    return static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU);
}

std::size_t expected_chr_offset(Address addr, Byte inner, Byte outer, Byte mode, Byte chr_bank) {
    const auto low = (mode & 0x10U) != 0U
                         ? static_cast<unsigned>((chr_bank & 15U) << 3U) | ((addr >> 10U) & 7U)
                         : inner;
    unsigned bank = low & 0x7FU;
    bank |= (outer & 0x80U) != 0U ? ((outer & 0x10U) << 3U) : (inner & 0x80U);
    bank |= (outer & 0x40U) << 2U;
    bank |= (outer & 0x80U) << 2U;
    bank |= (outer & 0x08U) << 7U;
    return static_cast<std::size_t>(bank) * 1024U + (addr & 0x3FFU);
}

TEST_CASE("Mapper 422 validates ROM RAM submapper and CIRAM geometry", "[mapper422][geometry]") {
    for (std::size_t prg = 32768U; prg <= kPrgSize; prg <<= 1U) {
        REQUIRE(Mapper422::validate_geometry(make_header(), prg, kChrSize));
    }
    for (std::size_t chr = 8192U; chr <= kChrSize; chr <<= 1U) {
        REQUIRE(Mapper422::validate_geometry(make_header(), kPrgSize, chr));
    }
    for (std::size_t ram = 8192U; ram <= 262144U; ram <<= 1U) {
        REQUIRE(
            Mapper422::validate_geometry(make_header(0U, false, true, true, ram), kPrgSize, 0U));
    }
    for (Byte submapper : {1U, 2U}) {
        REQUIRE(Mapper422::validate_geometry(make_header(submapper), 2U * kMiB, kChrSize));
    }
    REQUIRE(Mapper422::validate_geometry(make_header(0U, false, false), kPrgSize, kChrSize));
    REQUIRE(Mapper422::validate_geometry(make_header(0U, true), kPrgSize, kChrSize));

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        CAPTURE(header.mapper_number,
                header.is_nes2,
                header.submapper,
                header.has_battery,
                header.has_trainer,
                header.prg_ram_size,
                header.prg_nvram_size,
                header.chr_ram_size,
                header.chr_nvram_size,
                header.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper422::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg : {0UZ, 16384UZ, 32767UZ, 49152UZ, kPrgSize + 8192U}) {
        reject(make_header(), prg, kChrSize);
    }
    for (std::size_t chr : {1UZ, 4096UZ, 12288UZ, 2U * kChrSize}) {
        reject(make_header(), kPrgSize, chr);
    }
    for (Byte submapper : {1U, 2U}) {
        for (std::size_t prg : {kMiB, kPrgSize}) {
            reject(make_header(submapper), prg, kChrSize);
        }
    }
    for (std::size_t ram : {0U, 4096U, 12288U, 524288U}) {
        reject(make_header(0U, false, true, true, ram), kPrgSize, 0U);
    }
    for (Byte submapper = 3U; submapper < 16U; ++submapper) {
        reject(make_header(submapper), kPrgSize, kChrSize);
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto header = make_header();
        header.mirror_mode = mirror;
        reject(header, kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.mapper_number = 126U;
            break;
        case 1U:
            header.is_nes2 = false;
            break;
        case 2U:
            header.chr_nvram_size = 8192U;
            break;
        case 3U:
            header.prg_ram_size = 4096U;
            break;
        case 4U:
            header.prg_nvram_size = 8192U;
            break;
        case 5U:
            header.has_battery = true;
            break;
        case 6U:
            header.prg_ram_size = 16384U;
            break;
        case 7U:
            header.chr_ram_size = 8192U;
            break;
        default:
            header.has_trainer = true;
            break;
        }
        reject(header, kPrgSize, kChrSize);
    }

    auto initial = make_board(32768U, 8192U, 0U, false, true, false);
    REQUIRE(initial->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(save(*initial).size() == kStatePrefixSize + kWramSize);
    REQUIRE(initial->battery_ram().empty());
    REQUIRE(initial->shared_nvram().empty());
    REQUIRE_FALSE(initial->has_flash_storage());
    REQUIRE_FALSE(initial->has_expansion_audio());
    REQUIRE(initial->audio_output() == 0.0F);
}

TEST_CASE("Mapper 422 outer aliases overlay WRAM and obey independent write gates",
          "[mapper422][decoding][bus][ram]") {
    auto mapper = make_board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->reset();
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x6000U, 0x11U);
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto bytes = save(*mapper);
        constexpr std::array<Byte, 4> expected{0xA5U, 0x01U, 0xA5U, 0xA5U};
        CAPTURE(addr);
        REQUIRE(bytes[kOuterOffset + (addr & 3U)] == expected[addr & 3U]);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == 0xA5U);
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == 0xFFU);
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) == 0U);
    }

    mapper->reset();
    for (Byte control : {0U, 0x40U, 0x80U, 0xC0U}) {
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6000U, 0x5AU);
        mapper->write_prg(0xA001U, control);
        mapper->write_prg(0x6000U, 0xA7U);
        mapper->write_prg(0x6001U, 0x03U);
        REQUIRE(mapper->read_prg(0x6000U) == ((control & 0x40U) == 0U ? 0xA7U : 0x5AU));
        REQUIRE(save(*mapper)[kOuterOffset] == ((control & 0x80U) != 0U ? 0xA7U : 0x5AU));
    }

    auto no_wram = make_board(kPrgSize, kChrSize, 0U, false, false);
    REQUIRE_FALSE(no_wram->maps_prg(0x6000U));
    REQUIRE(no_wram->prg_read_mask(0x6000U) == 0U);
    REQUIRE(no_wram->read_prg(0x6000U) == 0U);
    no_wram->write_prg(0x6000U, 0x20U);
    REQUIRE(save(*no_wram)[kOuterOffset] == 0x20U);
}

TEST_CASE("Mapper 422 MMC3 writes cover high aliases and xROM address rewiring",
          "[mapper422][decoding][bus][irq]") {
    auto mapper = make_board(kPrgSize, kChrSize, 0U, false, false, true, 8192U, false);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        const auto port = addr & 0xE001U;
        if (port == 0x8001U) {
            mapper->write_prg(0x8000U, 3U);
        }
        if (port == 0xE000U) {
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
        }
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto bytes = save(*mapper);
        switch (port) {
        case 0x8000U:
            REQUIRE(bytes[kBankSelectOffset] == 5U);
            REQUIRE(bytes[kBankModeOffset] == 0x80U);
            break;
        case 0x8001U:
            REQUIRE(bytes[kBanksOffset + 3U] == 0xA5U);
            break;
        case 0xA000U:
            REQUIRE(bytes[kMirrorOffset] == static_cast<Byte>(MirrorMode::Horizontal));
            break;
        case 0xA001U:
            REQUIRE(bytes[kRamFlagsOffset] == 0x80U);
            break;
        case 0xC000U:
            REQUIRE(bytes[kIrqReloadOffset] == 0xA5U);
            break;
        case 0xC001U:
            REQUIRE(bytes[kIrqCounterOffset] == 0U);
            REQUIRE((bytes[kIrqFlagsOffset] & 1U) != 0U);
            break;
        case 0xE000U:
            REQUIRE(bytes[kIrqFlagsOffset] == 0U);
            REQUIRE_FALSE(mapper->irq_pending());
            break;
        default:
            REQUIRE((bytes[kIrqFlagsOffset] & 2U) != 0U);
            break;
        }
    }

    for (Byte mode = 8U; mode < 16U; ++mode) {
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            mapper->reset();
            bank(*mapper, 6U, 0U);
            mapper->write_prg(0x6003U, mode);
            mapper->write_prg(static_cast<Address>(addr), 0x35U);
            const auto bytes = save(*mapper);
            const bool latch = (mode & 0x0DU) == 0x0DU || addr < 0xA000U;
            if (latch) {
                REQUIRE(bytes[kBanksOffset + 6U] == 0x35U);
            } else if (addr < 0xC000U) {
                REQUIRE(bytes[kRamFlagsOffset] == 0U);
            } else if (addr < 0xE000U) {
                REQUIRE((bytes[kIrqFlagsOffset] & 1U) != 0U);
            } else {
                REQUIRE((bytes[kIrqFlagsOffset] & 2U) != 0U);
            }
        }
    }
}

TEST_CASE("Mapper 422 implements every PRG banking mode and MMC3 swap",
          "[mapper422][banking][bus]") {
    auto mapper = make_board();
    mapper->write_prg(0x6000U, 0x20U);
    for (Byte mode = 0U; mode < 16U; ++mode) {
        for (Byte prg_swap : {0U, 0x40U}) {
            for (unsigned value = 0U; value < 64U; ++value) {
                mapper->reset();
                mapper->write_prg(0x6000U, 0x20U);
                bank(*mapper, 6U, static_cast<Byte>(value), prg_swap);
                bank(*mapper, 7U, static_cast<Byte>(63U - value), prg_swap);
                mapper->write_prg(0x6003U, mode);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto expected = inner_prg_bank(mode,
                                                         static_cast<Byte>(value),
                                                         static_cast<Byte>(63U - value),
                                                         slot,
                                                         prg_swap != 0U);
                    const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                    CAPTURE(mode, prg_swap, value, slot, expected);
                    REQUIRE(mapper->read_prg(addr) == prg_cell(expected));
                }
            }
        }
    }
}

TEST_CASE("Mapper 422 applies outer PRG pins inversion two-chip selects and SL0",
          "[mapper422][banking][bus]") {
    auto mapper = make_board();
    bank(*mapper, 6U, 0x15U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x6000U, static_cast<Byte>(outer));
        for (unsigned slot : {0U, 2U}) {
            const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
            const auto inner = inner_prg_bank(0U, 0x15U, 0U, slot);
            const auto offset = outer_prg_offset(inner, addr, static_cast<Byte>(outer), 0U);
            CAPTURE(outer, slot, offset);
            REQUIRE(mapper->read_prg(addr) == prg_cell(static_cast<unsigned>(offset / 8192U)));
        }
    }

    for (Byte submapper : {1U, 2U}) {
        auto chips = make_board(2U * kMiB, kChrSize, submapper);
        bank(*chips, 6U, 0x15U);
        for (unsigned outer = 0U; outer < 256U; ++outer) {
            chips->write_prg(0x6000U, static_cast<Byte>(outer));
            for (Byte misc : {0U, 4U}) {
                chips->write_prg(0x6001U, misc);
                const auto offset =
                    outer_prg_offset(0x15U, 0x8000U, static_cast<Byte>(outer), submapper, misc);
                CAPTURE(submapper, outer, misc, offset);
                REQUIRE(chips->read_prg(0x8000U) ==
                        prg_cell(static_cast<unsigned>(offset / 8192U)));
            }
        }
    }

    mapper->reset();
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    mapper->write_prg(0x6000U, 0x20U);
    const auto normal_even = mapper->read_prg(0x8000U);
    const auto normal_odd = mapper->read_prg(0x8001U);
    REQUIRE(normal_even != normal_odd);
    mapper->write_prg(0x6001U, 1U);
    REQUIRE(mapper->read_prg(0x8000U) == normal_odd);
    REQUIRE(mapper->read_prg(0x8001U) == normal_odd);
    mapper->reset();
    mapper->write_prg(0x6000U, 0x20U);
    mapper->write_prg(0x6001U, 1U);
    REQUIRE(mapper->read_prg(0x8000U) == normal_odd);
    REQUIRE(mapper->configure_board_jumpers(0U));
    REQUIRE(mapper->read_prg(0x8001U) == normal_even);
}

TEST_CASE("Mapper 422 maps MMC3 CHR and outer A17-A20 in both CHR modes",
          "[mapper422][banking][chr]") {
    auto mapper = make_board();
    for (Byte inversion : {0U, 0x80U}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            std::array<Byte, 6> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = static_cast<Byte>(value + index * 19U);
                bank(*mapper, index, banks[index], inversion);
            }
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto logical = slot ^ (inversion != 0U ? 4U : 0U);
                const auto expected = logical < 4U ? (banks[logical / 2U] & 0xFEU) | (logical & 1U)
                                                   : banks[logical - 2U];
                REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == chr_cell(expected));
            }
        }
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto before = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == before);
    }

    for (Byte mode : {0U, 0x10U}) {
        for (unsigned chr_bank = 0U; chr_bank < 16U; ++chr_bank) {
            mapper->reset();
            bank(*mapper, 2U, 0x80U);
            bank(*mapper, 3U, 0x55U);
            bank(*mapper, 4U, 0xAAU);
            bank(*mapper, 5U, 0xC3U);
            mapper->write_prg(0x6002U, static_cast<Byte>(chr_bank));
            mapper->write_prg(0x6003U, mode);
            for (unsigned outer = 0U; outer < 256U; ++outer) {
                mapper->write_prg(0x6000U, static_cast<Byte>(outer));
                for (Address addr : {0x1000U, 0x1400U, 0x1C00U}) {
                    const Byte inner = addr == 0x1000U ? 0x80U : addr == 0x1400U ? 0x55U : 0xC3U;
                    const auto offset = expected_chr_offset(
                        addr, inner, static_cast<Byte>(outer), mode, static_cast<Byte>(chr_bank));
                    CAPTURE(mode, chr_bank, outer, addr, offset);
                    REQUIRE(mapper->read_chr(addr) ==
                            chr_cell(static_cast<unsigned>(offset / 1024U), offset & 0x3FFU));
                }
            }
        }
    }
}

TEST_CASE("Mapper 422 CHR lock fields and mode lock retain documented writability",
          "[mapper422][decoding][reset]") {
    auto mapper = make_board();
    const auto chr_register = [&] { return save(*mapper)[kChrBankOffset]; };
    mapper->write_prg(0x6002U, 0x1FU);
    REQUIRE(chr_register() == 0x1FU);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0x02U);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0U);
    mapper->write_prg(0x6002U, 0x2CU);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0x04U);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0U);
    mapper->write_prg(0x6002U, 0x48U);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0x08U);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0U);
    mapper->write_prg(0x6002U, 0xFFU);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0x7EU);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0x0EU);
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(chr_register() == 0U);

    mapper->write_prg(0x6000U, 0x20U);
    mapper->write_prg(0x6001U, 3U);
    mapper->write_prg(0x6003U, 0xBFU);
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x6003U, 0U);
    mapper->write_prg(0x6002U, 0x0FU);
    const auto bytes = save(*mapper);
    REQUIRE(bytes[kOuterOffset] == 0x20U);
    REQUIRE(bytes[kMiscOffset] == 3U);
    REQUIRE(bytes[kModeOffset] == 0xBFU);
    REQUIRE(bytes[kChrBankOffset] == 0x0FU);
    mapper->reset();
    const auto cleared = save(*mapper);
    REQUIRE(cleared[kOuterOffset] == 0U);
    REQUIRE(cleared[kMiscOffset] == 0U);
    REQUIRE(cleared[kChrBankOffset] == 0U);
    REQUIRE(cleared[kModeOffset] == 0U);
}

TEST_CASE("Mapper 422 supports MMC3 single-screen ANROM CIRAM and bounded CHR RAM",
          "[mapper422][ciram][ram][chr]") {
    for (bool vertical : {false, true}) {
        auto mapper = make_board(kPrgSize, kChrSize, 0U, false, true, vertical);
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        for (Byte value = 0U; value < 4U; ++value) {
            mapper->write_prg(0x6001U, 0U);
            mapper->write_prg(0xA000U, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            mapper->write_prg(0x6001U, 2U);
            mapper->write_prg(0xA000U, value);
            const auto expected =
                (value & 2U) == 0U
                    ? ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical)
                    : ((value & 1U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower);
            REQUIRE(mapper->mirror_mode() == expected);
        }
        mapper->write_prg(0x6003U, 0x20U);
        for (Byte value : {0U, 0x10U}) {
            mapper->reset();
            bank(*mapper, 6U, value);
            mapper->write_prg(0x6003U, 0x20U);
            REQUIRE(mapper->mirror_mode() ==
                    (value == 0U ? MirrorMode::SingleLower : MirrorMode::SingleUpper));
        }
        for (Address addr : {0x0000U, 0x1234U, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }

    for (std::size_t size : {8192U, 32768U, 262144U}) {
        auto mapper = make_board(kPrgSize, 0U, 0U, false, true, true, size);
        mapper->write_prg(0x6000U, 0x20U);
        for (Byte value = 0U; value < 16U; ++value) {
            mapper->write_prg(0x6002U, value);
            mapper->write_prg(0x6003U, 0x10U);
            for (unsigned addr = 0U; addr < 8192U; addr += 257U) {
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(value + addr));
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        static_cast<Byte>(value + addr));
            }
        }
    }
}

TEST_CASE("Mapper 422 normal MMC3 IRQ and absent expansion audio survive replay",
          "[mapper422][irq][audio]") {
    auto mapper = make_board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }

    const auto before = save(*mapper);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    REQUIRE(save(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 422 reset clears registers and cold power clears only volatile RAM",
          "[mapper422][reset][ram][irq]") {
    for (bool battery : {false, true}) {
        auto mapper = make_board(kPrgSize, 0U, 0U, battery, true, false, 8192U);
        const std::vector<Byte> imported(kWramSize, 0x3CU);
        mapper->set_battery_ram(imported);
        REQUIRE(mapper->read_prg(0x7000U) == (battery ? 0x3CU : 0U));
        mapper->write_chr(0U, 0x5AU);
        mapper->write_prg(0x6000U, 0x20U);
        mapper->write_prg(0x6001U, 3U);
        mapper->write_prg(0x6002U, 0xFFU);
        mapper->write_prg(0x6003U, 0xBFU);
        bank(*mapper, 6U, 9U, 0xC0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        mapper->write_prg(0x7000U, 0xA7U);
        REQUIRE(mapper->configure_board_jumpers(1U));
        REQUIRE(mapper->battery_ram().size() == (battery ? kWramSize : 0U));

        mapper->reset();
        REQUIRE(mapper->read_prg(0x7000U) == 0xA7U);
        REQUIRE(mapper->read_chr(0U) == 0x5AU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(mapper->irq_pending());
        const auto bytes = save(*mapper);
        REQUIRE(bytes[kOuterOffset] == 0U);
        REQUIRE(bytes[kMiscOffset] == 0U);
        REQUIRE(bytes[kChrBankOffset] == 0U);
        REQUIRE(bytes[kModeOffset] == 0U);
        REQUIRE(bytes[kBankSelectOffset] == 0U);
        REQUIRE(bytes[kBankModeOffset] == 0U);
        REQUIRE(bytes[kRamFlagsOffset] == 0x80U);

        mapper->power_on();
        REQUIRE(mapper->read_prg(0x7000U) == (battery ? 0xA7U : 0U));
        REQUIRE(mapper->read_chr(0U) == 0U);
        REQUIRE((save(*mapper)[0U] & 8U) != 0U);
    }
}

TEST_CASE("Mapper 422 bounded snapshots replay state without ROM and reject damage atomically",
          "[mapper422][state][ram][irq]") {
    auto mapper = make_board(2U * kMiB, 0U, 2U, true, true, true, 32768U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x6000U, 0xD5U);
    mapper->write_prg(0x6001U, 7U);
    mapper->write_prg(0x6002U, 0x6BU);
    mapper->write_prg(0x6003U, 0x35U);
    for (Byte index = 0U; index < 6U; ++index) {
        bank(*mapper, index, static_cast<Byte>(0xFEU + index * 0x11U), 0xC0U);
    }
    bank(*mapper, 6U, 0x26U, 0xC0U);
    bank(*mapper, 7U, 0x17U, 0xC0U);
    mapper->write_prg(0xA000U, 3U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 4U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0x7123U, 0xA7U);
    mapper->write_chr(0x123U, 0x5AU);
    const auto expected = save(*mapper);
    REQUIRE(expected.size() == kStatePrefixSize + kWramSize + 32768U);

    mapper->power_on();
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x7000U, 0U);
    mapper->write_chr(0U, 0U);
    load(*mapper, expected);
    REQUIRE(save(*mapper) == expected);
    REQUIRE(mapper->read_prg(0x7123U) == 0xA7U);
    REQUIRE(mapper->read_chr(0x123U) == 0x5AU);

    auto with_tail = expected;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(save(*mapper) == expected);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == expected);
    };
    for (std::size_t length = 0U; length < kStatePrefixSize; ++length) {
        CAPTURE(length);
        reject(std::span(expected).first(length));
    }
    for (std::size_t length : {kStatePrefixSize + 1U, expected.size() - 1U}) {
        reject(std::span(expected).first(length));
    }
    const std::array<std::pair<std::size_t, Byte>, 15> bad_fields{{
        {0U, 0U},
        {4U, 0U},
        {6U, 1U},
        {kMiscOffset, 8U},
        {kModeOffset, 0x40U},
        {kBankSelectOffset, 8U},
        {kBankModeOffset, 1U},
        {kBanksOffset + 6U, 0x40U},
        {kBanksOffset + 7U, 0x40U},
        {kMirrorOffset, 2U},
        {kIrqFlagsOffset, 8U},
        {kIrqFlagsOffset, 4U},
        {kRamFlagsOffset, 1U},
        {kWramLengthOffset + 1U, 0U},
        {kWramLengthOffset + 8U + kWramSize + 1U, 0U},
    }};
    for (const auto& [offset, value] : bad_fields) {
        auto bad = expected;
        bad[offset] = value;
        CAPTURE(offset, value);
        reject(bad);
    }
    auto excessive_ram = expected;
    std::fill_n(excessive_ram.begin() + static_cast<std::ptrdiff_t>(kWramLengthOffset), 8, 0xFFU);
    reject(excessive_ram);

    const auto mismatch = [&](std::unique_ptr<Mapper422>& other) {
        const auto before = save(*other);
        StateReader reader(expected);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*other) == before);
    };
    auto submapper = make_board(2U * kMiB, 0U, 1U, true, true, true, 32768U);
    mismatch(submapper);
    auto mirror = make_board(2U * kMiB, 0U, 2U, true, true, false, 32768U);
    mismatch(mirror);
    auto jumper = make_board(2U * kMiB, 0U, 2U, true, true, true, 32768U);
    mismatch(jumper);
    auto chr_ram = make_board(2U * kMiB, 0U, 2U, true, true, true, 8192U);
    mismatch(chr_ram);
    auto rom = make_board(2U * kMiB, kChrSize, 2U, true, true, true, 32768U);
    mismatch(rom);
    auto no_wram = make_board(2U * kMiB, 0U, 2U, false, false, true, 32768U);
    REQUIRE(no_wram->configure_board_jumpers(1U));
    mismatch(no_wram);

    auto changed = expected;
    changed[kOuterOffset] = 0xA5U;
    changed[kWramLengthOffset + 8U + 123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(save(*mapper) == changed);
}

} // namespace
