#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper126.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMiB = 1024U * 1024U;
constexpr std::size_t kPrgSize = 4U * kMiB;
constexpr std::size_t kChrSize = 2U * kMiB;

INesHeader make_header(Byte submapper = 0U,
                       bool battery = false,
                       bool wram = true,
                       bool vertical = true,
                       std::size_t chr_ram = 0U,
                       bool legacy = false) {
    INesHeader header{};
    header.mapper_number = 126U;
    header.submapper = submapper;
    header.is_nes2 = !legacy;
    header.has_battery = battery;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    if (legacy) {
        header.prg_ram_size = 8192U;
        header.chr_ram_size = 8192U;
    } else {
        header.prg_ram_size = !battery && wram ? 8192U : 0U;
        header.prg_nvram_size = battery ? 8192U : 0U;
        header.chr_ram_size = chr_ram;
    }
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 53U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + (bank >> 8U) * 73U + offset + (offset >> 8U));
}

std::unique_ptr<Tec9719> make_board(std::size_t prg_size = kPrgSize,
                                    std::size_t chr_size = kChrSize,
                                    Byte submapper = 0U,
                                    bool battery = false,
                                    bool wram = true,
                                    bool vertical = true,
                                    std::size_t chr_ram = 8192U,
                                    bool legacy = false,
                                    bool patterned = true) {
    std::vector<Byte> prg(prg_size);
    std::vector<Byte> chr(chr_size);
    if (patterned) {
        for (std::size_t i = 0U; i < prg.size(); ++i)
            prg[i] = prg_cell(static_cast<unsigned>(i / 8192U), static_cast<unsigned>(i % 8192U));
        for (std::size_t i = 0U; i < chr.size(); ++i)
            chr[i] = chr_cell(static_cast<unsigned>(i / 1024U), static_cast<unsigned>(i % 1024U));
    }
    auto header = make_header(submapper, battery, wram, vertical, chr_ram, legacy);
    if (chr_size != 0U)
        header.chr_ram_size = 0U;
    return std::make_unique<Tec9719>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper& mapper, std::span<const Byte> bytes, bool expected = true) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok() == expected);
}

void bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned inner_prg_bank(Byte mode, Byte r6, Byte r7, unsigned slot, bool prg_swap = false) {
    constexpr std::array<Byte, 16> kModes{
        {0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U, 12U, 13U, 14U, 15U, 12U, 13U, 14U, 15U}};
    switch (kModes[mode]) {
    case 0U:
        if (prg_swap)
            return slot == 0U ? 30U : slot == 1U ? r7 & 31U : slot == 2U ? r6 & 31U : 31U;
        return slot == 0U ? r6 & 31U : slot == 1U ? r7 & 31U : slot == 2U ? 30U : 31U;
    case 1U:
    case 2U:
        return ((r6 >> 1U) & 15U) * 2U + (slot & 1U);
    case 3U:
        return ((r6 >> 2U) & 7U) * 4U + slot;
    case 12U:
        if (slot >= 2U)
            return slot + 28U;
        return (((slot == 0U ? r6 : r7) & 12U) << 1U) |
               (((slot == 0U ? r6 : r7) & 2U) != 0U ? 3U : 0U) | ((slot == 0U ? r6 : r7) & 1U);
    case 13U:
        return slot < 2U ? (r6 & 15U) * 2U + (slot & 1U) : slot + 28U;
    case 14U:
        if (slot >= 2U)
            return slot + 28U;
        return (((slot == 0U ? r6 : r7) & 4U) << 2U) | (((slot == 0U ? r6 : r7) & 2U) << 2U) |
               (((slot == 0U ? r6 : r7) & 2U) << 1U) | (((slot == 0U ? r6 : r7) & 1U) * 3U);
    default:
        return (r6 & 7U) * 4U + slot;
    }
}

std::size_t outer_prg_offset(
    unsigned inner, Address addr, Byte outer, Byte submapper, Byte misc = 0U) {
    std::size_t offset = static_cast<std::size_t>(inner) * 8192U + (addr & 0x1FFFU);
    if ((outer & 0x40U) != 0U)
        offset = (offset & ~0x20000U) | (static_cast<std::size_t>(outer & 1U) << 17U);
    offset &= 0x03FFFFU;
    offset |= static_cast<std::size_t>((outer >> 1U) & 3U) << 18U;
    offset |= static_cast<std::size_t>((outer >> 4U) & 1U) << 20U;
    offset |= static_cast<std::size_t>((~outer >> 5U) & 1U) << 21U;
    if (submapper == 1U)
        return (offset & 0x0FFFFFU) | ((offset & 0x200000U) >> 1U);
    if (submapper == 2U)
        return (offset & 0x0FFFFFU) | (static_cast<std::size_t>((misc >> 2U) & 1U) << 20U);
    return offset;
}

std::size_t expected_chr_offset(
    const Tec9719&, Address addr, Byte inner, Byte outer, Byte mode, Byte chr_bank) {
    const auto low = (mode & 0x10U) != 0U
                         ? static_cast<unsigned>((chr_bank & 15U) << 3U) | ((addr >> 10U) & 7U)
                         : inner;
    std::size_t offset = static_cast<std::size_t>(low & 0x7FU) * 1024U + (addr & 0x3FFU);
    offset |= (outer & 0x80U) != 0U ? static_cast<std::size_t>((outer >> 3U) & 1U) << 17U
                                    : static_cast<std::size_t>(inner & 0x80U) << 10U;
    offset |= static_cast<std::size_t>(outer & 0x80U) << 11U;
    offset |= static_cast<std::size_t>(outer & 0x40U) << 13U;
    offset |= static_cast<std::size_t>(outer & 8U) << 17U;
    return offset;
}

TEST_CASE("Mapper 126 validates ROM, RAM, submapper and CIRAM geometry", "[mapper126]") {
    for (std::size_t prg = 32768U; prg <= kPrgSize; prg <<= 1U)
        REQUIRE(Tec9719::validate_geometry(make_header(), prg, kChrSize));
    for (std::size_t chr = 8192U; chr <= kChrSize; chr <<= 1U)
        REQUIRE(Tec9719::validate_geometry(make_header(), kPrgSize, chr));
    for (std::size_t ram = 8192U; ram <= 262144U; ram <<= 1U)
        REQUIRE(Tec9719::validate_geometry(make_header(0U, false, true, true, ram), kPrgSize, 0U));
    for (Byte submapper : {1U, 2U})
        REQUIRE(Tec9719::validate_geometry(make_header(submapper), 2U * kMiB, kChrSize));
    REQUIRE(Tec9719::validate_geometry(make_header(0U, false, false), kPrgSize, kChrSize));
    REQUIRE(Tec9719::validate_geometry(make_header(0U, true), kPrgSize, kChrSize));
    REQUIRE(Tec9719::validate_geometry(
        make_header(0U, false, true, true, 8192U, true), kPrgSize, kChrSize));

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        REQUIRE_FALSE(Tec9719::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg : {0U, 16384U, 32767U, 49152U, 4194304U + 8192U})
        reject(make_header(), prg, kChrSize);
    for (std::size_t chr : {1U, 4096U, 12288U, 4194304U})
        reject(make_header(), kPrgSize, chr);
    for (Byte submapper : {1U, 2U})
        for (std::size_t prg : {kMiB, kPrgSize})
            reject(make_header(submapper), prg, kChrSize);
    for (std::size_t ram : {0U, 4096U, 12288U, 524288U})
        reject(make_header(0U, false, true, true, ram), kPrgSize, 0U);
    for (Byte submapper = 3U; submapper < 16U; ++submapper)
        reject(make_header(submapper), kPrgSize, kChrSize);
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto header = make_header();
        header.mirror_mode = mirror;
        reject(header, kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.mapper_number = 4U;
            break;
        case 1U:
            header.chr_nvram_size = 8192U;
            break;
        case 2U:
            header.prg_ram_size = 4096U;
            break;
        case 3U:
            header.prg_nvram_size = 8192U;
            break;
        case 4U:
            header.has_battery = true;
            break;
        case 5U:
            header.prg_ram_size = 16384U;
            break;
        default:
            header.chr_ram_size = 8192U;
            break;
        }
        reject(header, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 126 outer registers cover every low alias and overlay WRAM", "[mapper126]") {
    auto mapper = make_board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->reset();
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x6000U, 0x11U);
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto bytes = save(*mapper);
        constexpr std::array<Byte, 4> kExpected{0xA5U, 0x01U, 0xA5U, 0xA5U};
        INFO("addr=" << addr);
        REQUIRE(bytes[10U + (addr & 3U)] == kExpected[addr & 3U]);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == 0xA5U);
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
    }

    mapper->reset();
    for (Byte control : {0U, 0x40U, 0x80U, 0xC0U}) {
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6000U, 0x5AU);
        mapper->write_prg(0xA001U, control);
        mapper->write_prg(0x6000U, 0xA7U);
        mapper->write_prg(0x6001U, 0x03U);
        REQUIRE(mapper->read_prg(0x6000U) == ((control & 0x40U) == 0U ? 0xA7U : 0x5AU));
        REQUIRE(save(*mapper)[10U] == ((control & 0x80U) != 0U ? 0xA7U : 0x5AU));
        REQUIRE(mapper->maps_prg(0x6000U));
    }

    auto no_wram = make_board(kPrgSize, kChrSize, 0U, false, false);
    REQUIRE_FALSE(no_wram->maps_prg(0x6000U));
    no_wram->write_prg(0x6000U, 0x20U);
    REQUIRE(save(*no_wram)[10U] == 0x20U);
}

TEST_CASE("Mapper 126 normal MMC3 writes cover every high alias", "[mapper126]") {
    auto mapper = make_board(kPrgSize, kChrSize, 0U, false, false, true, 8192U, false, false);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        const auto port = addr & 0xE001U;
        if (port == 0x8001U)
            mapper->write_prg(0x8000U, 3U);
        if (port == 0xE000U) {
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
        }
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto bytes = save(*mapper);
        switch (port) {
        case 0x8000U:
            REQUIRE(bytes[14U] == 5U);
            REQUIRE(bytes[15U] == 0x80U);
            break;
        case 0x8001U:
            REQUIRE(bytes[19U] == 0xA5U);
            break;
        case 0xA000U:
            REQUIRE(bytes[24U] == static_cast<Byte>(MirrorMode::Horizontal));
            break;
        case 0xA001U:
            REQUIRE(bytes[28U] == 0x80U);
            break;
        case 0xC000U:
            REQUIRE(bytes[26U] == 0xA5U);
            break;
        case 0xC001U:
            REQUIRE(bytes[25U] == 0U);
            REQUIRE((bytes[27U] & 1U) != 0U);
            break;
        case 0xE000U:
            REQUIRE(bytes[27U] == 0U);
            REQUIRE_FALSE(mapper->irq_pending());
            break;
        default:
            REQUIRE((bytes[27U] & 2U) != 0U);
            break;
        }
    }
}

TEST_CASE("Mapper 126 implements every PRG banking mode and MMC3 PRG swap", "[mapper126]") {
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
                    INFO("mode=" << +mode << " swap=" << +prg_swap << " r6=" << value
                                 << " slot=" << slot);
                    REQUIRE(mapper->read_prg(addr) == prg_cell(expected));
                }
            }
        }
    }
}

TEST_CASE("Mapper 126 rewires xROM write addresses in modes 8 through F", "[mapper126]") {
    auto mapper = make_board(kPrgSize, kChrSize, 0U, false, false, true, 8192U, false, false);
    for (Byte mode = 8U; mode < 16U; ++mode) {
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            mapper->reset();
            bank(*mapper, 6U, 0U);
            mapper->write_prg(0x6003U, mode);
            mapper->write_prg(static_cast<Address>(addr), 0x35U);
            const auto bytes = save(*mapper);
            const bool latch = (mode & 0x0DU) == 0x0DU || addr < 0xA000U;
            if (latch) {
                REQUIRE(bytes[22U] == 0x35U);
            } else if (addr < 0xC000U) {
                REQUIRE(bytes[28U] == 0U);
            } else if (addr < 0xE000U) {
                REQUIRE((bytes[27U] & 1U) != 0U);
            } else {
                REQUIRE((bytes[27U] & 2U) != 0U);
            }
        }
    }
}

TEST_CASE("Mapper 126 applies outer PRG pins, inversion and two-chip selects", "[mapper126]") {
    auto mapper = make_board();
    bank(*mapper, 6U, 0x15U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x6000U, static_cast<Byte>(outer));
        for (unsigned slot : {0U, 2U}) {
            const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
            const auto inner = inner_prg_bank(0U, 0x15U, 0U, slot);
            const auto offset = outer_prg_offset(inner, addr, static_cast<Byte>(outer), 0U);
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
                REQUIRE(chips->read_prg(0x8000U) ==
                        prg_cell(static_cast<unsigned>(offset / 8192U)));
            }
        }
    }
}

TEST_CASE("Mapper 126 SL0 replaces PRG A0 only when enabled", "[mapper126]") {
    auto mapper = make_board();
    mapper->write_prg(0x6000U, 0x20U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
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

TEST_CASE("Mapper 126 maps paired and inverted MMC3 CHR banks", "[mapper126]") {
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
}

TEST_CASE("Mapper 126 applies CHR A17-A20 and 8 KiB banking in every outer mode", "[mapper126]") {
    auto mapper = make_board();
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
                    const auto offset = expected_chr_offset(*mapper,
                                                            addr,
                                                            inner,
                                                            static_cast<Byte>(outer),
                                                            mode,
                                                            static_cast<Byte>(chr_bank));
                    REQUIRE(mapper->read_chr(addr) ==
                            chr_cell(static_cast<unsigned>(offset / 1024U), offset & 0x3FFU));
                }
            }
        }
    }
}

TEST_CASE("Mapper 126 CHR lock bits retain only their documented fields", "[mapper126]") {
    auto mapper = make_board();
    const auto chr_register = [&] { return save(*mapper)[12U]; };
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
}

TEST_CASE("Mapper 126 mode lock excludes CHR register and reset reopens outer registers",
          "[mapper126]") {
    auto mapper = make_board();
    mapper->write_prg(0x6000U, 0x20U);
    mapper->write_prg(0x6001U, 3U);
    mapper->write_prg(0x6003U, 0xBFU);
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x6003U, 0U);
    mapper->write_prg(0x6002U, 0x0FU);
    const auto bytes = save(*mapper);
    REQUIRE(bytes[10U] == 0x20U);
    REQUIRE(bytes[11U] == 3U);
    REQUIRE(bytes[13U] == 0xBFU);
    REQUIRE(bytes[12U] == 0x0FU);
    mapper->reset();
    const auto cleared = save(*mapper);
    REQUIRE(cleared[10U] == 0U);
    REQUIRE(cleared[11U] == 0U);
    REQUIRE(cleared[12U] == 0U);
    REQUIRE(cleared[13U] == 0U);
}

TEST_CASE("Mapper 126 supports MMC3, single-screen and ANROM CIRAM controls", "[mapper126]") {
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
    }
}

TEST_CASE("Mapper 126 CHR RAM is banked, writable and bounded", "[mapper126]") {
    for (std::size_t size : {8192U, 32768U, 262144U}) {
        auto mapper = make_board(kPrgSize, 0U, 0U, false, true, true, size);
        mapper->write_prg(0x6000U, 0x20U);
        for (Byte value = 0U; value < 16U; ++value) {
            mapper->write_prg(0x6002U, value);
            mapper->write_prg(0x6003U, 0x10U);
            for (unsigned addr = 0U; addr < 8192U; addr += 257U) {
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(value + addr));
                const auto offset = (static_cast<std::size_t>(value) * 8192U + addr) % size;
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        static_cast<Byte>(value + addr));
                REQUIRE(offset < size);
            }
        }
    }
}

TEST_CASE("Mapper 126 MMC3 IRQ reload, acknowledgement and zero-latch repeats survive replay",
          "[mapper126]") {
    auto mapper = make_board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = save(*mapper);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    load(*mapper, partial);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 126 reset clears registers and cold power clears only volatile RAM",
          "[mapper126]") {
    for (bool battery : {false, true}) {
        auto mapper = make_board(kPrgSize, 0U, 0U, battery, true, false, 8192U);
        const std::vector<Byte> imported(8192U, 0x3CU);
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
        REQUIRE(mapper->battery_ram().size() == (battery ? 8192U : 0U));

        mapper->reset();
        REQUIRE(mapper->read_prg(0x7000U) == 0xA7U);
        REQUIRE(mapper->read_chr(0U) == 0x5AU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(mapper->irq_pending());
        const auto bytes = save(*mapper);
        REQUIRE(bytes[10U] == 0U);
        REQUIRE(bytes[11U] == 0U);
        REQUIRE(bytes[12U] == 0U);
        REQUIRE(bytes[13U] == 0U);
        REQUIRE(bytes[14U] == 0U);
        REQUIRE(bytes[15U] == 0U);
        REQUIRE(bytes[28U] == 0x80U);

        mapper->power_on();
        REQUIRE(mapper->read_prg(0x7000U) == (battery ? 0xA7U : 0U));
        REQUIRE(mapper->read_chr(0U) == 0U);
        REQUIRE((save(*mapper)[0U] & 8U) != 0U);
    }
}

TEST_CASE("Mapper 126 save-state replay is complete and rejection is atomic", "[mapper126]") {
    auto mapper = make_board(2U * kMiB, 0U, 2U, true, true, true, 32768U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x6000U, 0xD5U);
    mapper->write_prg(0x6001U, 7U);
    mapper->write_prg(0x6002U, 0x6BU);
    mapper->write_prg(0x6003U, 0x35U);
    bank(*mapper, 0U, 0xFEU, 0xC0U);
    bank(*mapper, 1U, 0x81U, 0xC0U);
    bank(*mapper, 2U, 0x92U, 0xC0U);
    bank(*mapper, 3U, 0xA3U, 0xC0U);
    bank(*mapper, 4U, 0xB4U, 0xC0U);
    bank(*mapper, 5U, 0xC5U, 0xC0U);
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
    REQUIRE(expected.size() == 45U + 8192U + 32768U);

    mapper->power_on();
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x7000U, 0U);
    mapper->write_chr(0U, 0U);
    load(*mapper, expected);
    REQUIRE(save(*mapper) == expected);
    REQUIRE(mapper->read_prg(0x7123U) == 0xA7U);
    REQUIRE(mapper->read_chr(0x123U) == 0x5AU);

    for (std::size_t length :
         std::array<std::size_t, 9>{0U, 1U, 9U, 10U, 28U, 29U, 36U, 37U, expected.size() - 1U}) {
        StateReader reader(std::span(expected).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == expected);
    }
    const std::array<std::pair<std::size_t, Byte>, 16> bad_fields{{
        {0U, 0U},
        {4U, 0U},
        {6U, 1U},
        {11U, 8U},
        {13U, 0x40U},
        {14U, 8U},
        {15U, 1U},
        {22U, 0x40U},
        {23U, 0x40U},
        {24U, 2U},
        {27U, 8U},
        {27U, 4U},
        {28U, 1U},
        {30U, 0U},
        {29U + 8U + 8192U + 1U, 0U},
        {1U, 0U},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto bad = expected;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        INFO("offset=" << offset << " value=" << +value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == expected);
    }

    auto excessive_ram = expected;
    std::fill_n(excessive_ram.begin() + 29, 8, 0xFFU);
    StateReader excessive_reader(excessive_ram);
    mapper->load_state(excessive_reader);
    REQUIRE_FALSE(excessive_reader.ok());
    REQUIRE(save(*mapper) == expected);

    const auto mismatch = [&](std::unique_ptr<Tec9719>& other) {
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
}

} // namespace
