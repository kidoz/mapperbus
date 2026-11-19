#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper342.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 4UZ * 1024UZ * 1024UZ;
constexpr std::size_t kChrRamSize = 512UZ * 1024UZ;
constexpr std::size_t kWramSize = 32UZ * 1024UZ;
constexpr std::size_t kRegistersOffset = 14UZ;
constexpr std::size_t kChrBanksOffset = 26UZ;
constexpr std::size_t kWramLengthOffset = 42UZ;
constexpr std::size_t kWramDataOffset = 50UZ;
constexpr std::size_t kStateOverhead = 96UZ;

INesHeader header(bool fram = true, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 342U;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    h.chr_ram_size = kChrRamSize;
    h.prg_nvram_size = fram ? kWramSize : 0U;
    h.has_battery = fram;
    return h;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) * 19U + (offset >> 14U) * 31U +
                             (offset >> 20U) * 47U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper342> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_ram_size = kChrRamSize,
                                 bool fram = true,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    auto h = header(fram, mirror);
    h.chr_ram_size = chr_ram_size;
    REQUIRE(Mapper342::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper342>(h, rom(prg_size, salt), std::vector<Byte>{});
}

void reg(Mapper342& mapper, unsigned index, Byte value, unsigned alias = 0U) {
    mapper.write_expansion(static_cast<Address>(0x5000U | (alias & 0x0FF8U) | index), value);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, 4> prg_banks(const std::array<Byte, 8>& regs) {
    return {static_cast<Byte>((regs[5] >> 1U) & 0x3EU), 1U, 0xFEU, 0xFFU};
}

std::array<std::uint16_t, 8> chr_banks(const std::array<Byte, 8>& regs) {
    return {static_cast<std::uint16_t>(((regs[3] & 0x1FU) << 3U) | ((regs[5] & 0x80U) << 1U)),
            1U,
            2U,
            3U,
            4U,
            5U,
            6U,
            7U};
}

std::size_t expected_prg(const std::array<Byte, 8>& regs, Address addr, std::size_t size) {
    const auto banks = prg_banks(regs);
    const unsigned mode = (regs[3] >> 5U) & 7U;
    const unsigned slot = (addr - 0x8000U) >> 13U;
    unsigned bank = 0U;
    if (mode == 0U || mode == 1U) {
        const bool first = slot < 2U;
        bank = (banks[(mode == 0U) == first ? 0U : 2U] & 0xFEU) | (slot & 1U);
    } else if (mode == 4U || mode == 5U) {
        constexpr std::array<std::array<unsigned, 4>, 2> slots{
            {{0U, 1U, 2U, 3U}, {2U, 1U, 0U, 3U}}};
        bank = banks[slots[mode - 4U][slot]];
    } else {
        bank = (banks[mode == 6U ? 1U : 0U] & 0xFCU) | slot;
    }
    std::size_t offset = bank * 0x2000UZ + (addr & 0x1FFFU);
    offset &= ~(static_cast<std::size_t>(regs[2] & 0x7FU) << 14U);
    offset |=
        (static_cast<std::size_t>(regs[0]) << 22U) | (static_cast<std::size_t>(regs[1]) << 14U);
    return offset % size;
}

std::size_t expected_chr(const std::array<Byte, 8>& regs, Address addr, std::size_t size) {
    const auto banks = chr_banks(regs);
    const unsigned mode = regs[4] >> 5U;
    const unsigned slot = (addr >> 10U) & 7U;
    const auto paired = [&](unsigned index) { return (banks[index] & 0x1FEU) | (slot & 1U); };
    unsigned bank = 0U;
    if (mode == 0U) {
        bank = (banks[0] & 0x1F8U) | slot;
    } else if (mode == 2U) {
        bank = slot < 2U ? paired(0U) : slot < 4U ? paired(2U) : banks[slot];
    } else if (mode == 3U) {
        bank = slot < 4U ? banks[slot + 4U] : slot < 6U ? paired(0U) : paired(2U);
    } else if (mode == 4U) {
        bank = (banks[slot < 4U ? 0U : 4U] & 0x1FCU) | (slot & 3U);
    } else if (mode == 6U) {
        bank = paired(slot / 2U * 2U);
    } else {
        bank = banks[slot];
    }
    std::size_t offset = bank * 0x400UZ + (addr & 0x3FFU);
    offset &= ~((static_cast<std::size_t>(regs[2] & 0x80U) << 11U) |
                (static_cast<std::size_t>(regs[4] & 0x1FU) << 13U));
    return offset % size;
}

TEST_CASE("Mapper 342 validates mapper submapper ROM and exact memory population", "[mapper342]") {
    for (const auto mirror :
         {MirrorMode::Horizontal, MirrorMode::Vertical, MirrorMode::FourScreen}) {
        for (const std::size_t prg : {0x4000UZ, 0x8000UZ, kPrgSize, 0x1000000UZ, 0x8000000UZ}) {
            for (const std::size_t chr : {0x2000UZ, 0x8000UZ, kChrRamSize}) {
                for (const bool fram : {false, true}) {
                    auto h = header(fram, mirror);
                    h.chr_ram_size = chr;
                    REQUIRE(Mapper342::validate_geometry(h, prg, 0U));
                }
            }
        }
    }

    for (const unsigned id : {0U, 2U, 268U, 341U, 343U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper342::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper342::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper342::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t prg : {0UZ, 1UZ, 0x2000UZ, 0x6000UZ, 0x10000000UZ}) {
        REQUIRE_FALSE(Mapper342::validate_geometry(header(), prg, 0U));
    }
    for (const std::size_t chr : {1UZ, 0x2000UZ, 0x8000UZ}) {
        REQUIRE_FALSE(Mapper342::validate_geometry(header(), kPrgSize, chr));
    }
    for (const std::size_t chr : {0UZ, 0x1000UZ, 0x3000UZ, 0x100000UZ}) {
        auto h = header();
        h.chr_ram_size = chr;
        REQUIRE_FALSE(Mapper342::validate_geometry(h, kPrgSize, 0U));
    }
    for (const auto mirror : {MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper342::validate_geometry(header(true, mirror), kPrgSize, 0U));
    }

    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 1U:
            h.prg_ram_size = 0x4000U;
            h.prg_nvram_size = 0U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x10000U;
            break;
        case 4U:
            h.has_battery = false;
            break;
        case 5U:
            h.prg_nvram_size = 0U;
            break;
        default:
            h.prg_nvram_size = 0U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper342::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 342 decodes every native register alias and locks atomically", "[mapper342]") {
    auto mapper = board();
    for (unsigned index = 0U; index < 8U; ++index) {
        for (unsigned alias = 0U; alias < 0x1000U; alias += 8U) {
            const auto value = static_cast<Byte>(0x25U + index);
            reg(*mapper, index, value, alias);
            const auto saved = state(*mapper);
            CAPTURE(index, alias);
            REQUIRE(saved[kRegistersOffset + index] == value);
        }
    }

    mapper->reset();
    for (unsigned index = 0U; index < 8U; ++index) {
        reg(*mapper, index, static_cast<Byte>(0x30U + index));
    }
    reg(*mapper, 7U, 0x80U);
    const auto locked = state(*mapper);
    for (unsigned index = 0U; index < 8U; ++index) {
        for (unsigned alias = 0U; alias < 0x1000U; alias += 8U) {
            reg(*mapper, index, 0U, alias);
        }
    }
    REQUIRE(state(*mapper) == locked);
    mapper->reset();
    REQUIRE(state(*mapper) != locked);

    const auto initial = state(*mapper);
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x4FFFU, 0x6000U, 0x7FFFU, 0x8000U}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == initial);
    for (const Address addr : {0x5000U, 0x5007U, 0x5FF8U, 0x5FFFU}) {
        REQUIRE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 342 native PRG modes apply bank pins before mask and base", "[mapper342]") {
    auto mapper = board();
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const Byte mode : {0U, 1U, 4U, 5U, 6U, 7U}) {
        std::array<Byte, 8> regs{0x01U, 0x5AU, 0x35U, 0x12U, 0xA7U, 0xB5U, 0x5AU, 0x0FU};
        regs[3] = static_cast<Byte>((mode << 5U) | (regs[3] & 0x1FU));
        for (unsigned index = 0U; index < regs.size(); ++index) {
            reg(*mapper, index, regs[index]);
        }
        for (const auto addr : addresses) {
            CAPTURE(mode, addr);
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(regs, addr, kPrgSize)));
        }
    }

    for (const Byte mode : {2U, 3U}) {
        mapper->reset();
        reg(*mapper, 3U, static_cast<Byte>(mode << 5U));
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(mode == 2U ? 0U : 0x1C000U));
    }
}

TEST_CASE("Mapper 342 reset PRG banks are zero, one, FE and FF with a 128 KiB menu limit",
          "[mapper342][reset]") {
    auto mapper = board();
    for (const Address addr : {0x8000U, 0x8001U, 0x9FFFU, 0xA000U, 0xBFFFU}) {
        REQUIRE(mapper->read_prg(addr) == cell(addr - 0x8000U));
    }
    for (const Address addr : {0xC000U, 0xC123U, 0xE000U, 0xFFFFU}) {
        REQUIRE(mapper->read_prg(addr) == cell(0x1C000UZ + (addr - 0xC000U)));
    }
}

TEST_CASE("Mapper 342 native CHR modes mask RAM pins and honor write enable", "[mapper342]") {
    auto mapper = board();
    for (const Byte mode : {0U, 2U, 3U, 4U, 6U, 7U}) {
        mapper->power_on();
        reg(*mapper, 7U, 0x02U);
        for (unsigned segment = 0U; segment < kChrRamSize / 0x2000U; ++segment) {
            reg(*mapper, 3U, static_cast<Byte>(segment & 0x1FU));
            reg(*mapper, 5U, (segment & 0x20U) != 0U ? 0x80U : 0U);
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                mapper->write_chr(static_cast<Address>(addr), cell(segment * 0x2000U + addr));
            }
        }
        std::array<Byte, 8> regs{0U, 0U, 0x80U, 0x12U, 0xA8U, 0x80U, 0U, 0x02U};
        regs[4] = static_cast<Byte>((regs[4] & 31U) | (mode << 5U));
        for (unsigned index = 0U; index < regs.size(); ++index) {
            reg(*mapper, index, regs[index]);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * 0x400U + 0x123U);
            CAPTURE(mode, addr);
            REQUIRE(mapper->read_chr(addr) == cell(expected_chr(regs, addr, kChrRamSize)));
        }
    }

    mapper->power_on();
    reg(*mapper, 3U, 0x12U);
    reg(*mapper, 5U, 0x80U);
    mapper->write_chr(0x0123U, 0xA7U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    reg(*mapper, 7U, 0x02U);
    mapper->write_chr(0x0123U, 0xA7U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    for (const Byte mode : {1U, 5U}) {
        reg(*mapper, 4U, static_cast<Byte>(mode << 5U));
        mapper->write_chr(0x0123, 0xA7);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
        mapper->write_chr(0x0123U, 0x5CU);
        REQUIRE(mapper->read_chr(0x0123U) == 0x5CU);
    }
}

TEST_CASE("Mapper 342 CIRAM controls include four-screen override", "[mapper342]") {
    auto mapper = board(kPrgSize, kChrRamSize, true, 0U, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned control = 0U; control < 0x80U; ++control) {
        reg(*mapper, 7U, static_cast<Byte>(control));
        const auto expected = (control & 0x20U) != 0U      ? MirrorMode::FourScreen
                              : (control & 0x18U) == 0x00U ? MirrorMode::Vertical
                              : (control & 0x18U) == 0x08U ? MirrorMode::Horizontal
                              : (control & 0x18U) == 0x10U ? MirrorMode::SingleLower
                                                           : MirrorMode::SingleUpper;
        CAPTURE(control);
        REQUIRE(mapper->mirror_mode() == expected);
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
}

TEST_CASE("Mapper 342 pages optional FRAM and preserves it across reset and power", "[mapper342]") {
    auto mapper = board();
    REQUIRE(mapper->battery_ram().size() == kWramSize);
    for (const Address addr : {0x5FFFU, 0x6000U, 0x7123U, 0x7FFFU, 0x8000U}) {
        REQUIRE(mapper->maps_prg(addr) == (addr >= 0x8000U));
        if (addr < 0x8000U) {
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
        }
    }

    const std::vector<Byte> imported(kWramSize, 0xA7U);
    mapper->set_battery_ram(imported);
    reg(*mapper, 7U, 0x01U);
    for (Byte page = 0U; page < 4U; ++page) {
        reg(*mapper, 5U, page);
        for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
            CAPTURE(page, addr);
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            REQUIRE(mapper->read_prg(addr) == 0xA7U);
            mapper->write_prg(addr, static_cast<Byte>(page * 0x40U + addr));
        }
    }
    for (Byte page = 0U; page < 4U; ++page) {
        reg(*mapper, 5U, page);
        for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
            REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(page * 0x40U + addr));
        }
    }
    reg(*mapper, 7U, 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    mapper->write_prg(0x6000U, 0U);
    mapper->reset();
    reg(*mapper, 7U, 1U);
    reg(*mapper, 5U, 3U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xC0U);
    mapper->power_on();
    reg(*mapper, 7U, 1U);
    reg(*mapper, 5U, 3U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xC0U);

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(state(*mapper) == before);
    auto absent = board(kPrgSize, kChrRamSize, false);
    REQUIRE(absent->battery_ram().empty());
    reg(*absent, 7U, 1U);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x6000U) == 0U);
}

TEST_CASE("Mapper 342 reset restores native defaults and cold power clears only CHR",
          "[mapper342]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    reg(*mapper, 0U, 0x12U);
    reg(*mapper, 3U, 0xE5U);
    reg(*mapper, 4U, 7U);
    reg(*mapper, 5U, 0xFFU);
    reg(*mapper, 7U, 0x1FU);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_prg(0x6000U, 0x5CU);
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    const auto warm = state(*mapper);
    REQUIRE(warm != initial);
    REQUIRE(std::equal(initial.begin(), initial.begin() + kWramDataOffset, warm.begin()));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    reg(*mapper, 3U, 0xE5U);
    reg(*mapper, 4U, 7U);
    reg(*mapper, 5U, 0xFFU);
    reg(*mapper, 7U, 0x1FU);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5CU);

    mapper->power_on();
    REQUIRE(state(*mapper) != initial);
    REQUIRE(std::equal(initial.begin(), initial.begin() + kWramDataOffset, state(*mapper).begin()));
    reg(*mapper, 3U, 0xE5U);
    reg(*mapper, 4U, 7U);
    reg(*mapper, 5U, 0xFFU);
    reg(*mapper, 7U, 0x1FU);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5CU);
}

TEST_CASE("Mapper 342 snapshots replay bounded RAM and registers without ROM",
          "[mapper342][state]") {
    auto mapper = board();
    constexpr std::array<Byte, 8> regs{0x12U, 0x34U, 0x65U, 0x32U, 0x93U, 0xADU, 0x5AU, 0x0FU};
    for (unsigned index = 0U; index < regs.size(); ++index) {
        reg(*mapper, index, regs[index]);
    }
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_prg(0x6123U, 0x5CU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateOverhead + kWramSize + kChrRamSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(std::equal(regs.begin(), regs.end(), saved.begin() + kRegistersOffset));

    auto copy = board(kPrgSize, kChrRamSize, true, 0x80U, MirrorMode::FourScreen);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_chr(0x0123U) == 0xA7U);
    REQUIRE(copy->read_prg(0x6123U) == 0x5CU);
    REQUIRE(copy->read_prg(0x8000U) == cell(expected_prg(regs, 0x8000U, kPrgSize), 0x80U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 342 rejects bounded malformed and incompatible states atomically",
          "[mapper342][state]") {
    auto mapper = board();
    for (unsigned index = 0U; index < 8U; ++index) {
        reg(*mapper, index, static_cast<Byte>(0x20U + index));
    }
    reg(*mapper, 7U, 0x0BU);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_prg(0x6123U, 0x5CU);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (const std::size_t length : {0UZ,
                                     1UZ,
                                     13UZ,
                                     14UZ,
                                     21UZ,
                                     22UZ,
                                     25UZ,
                                     26UZ,
                                     41UZ,
                                     42UZ,
                                     49UZ,
                                     50UZ,
                                     kWramDataOffset + kWramSize - 1UZ,
                                     kWramDataOffset + kWramSize,
                                     saved.size() - 1UZ}) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const std::size_t offset : {2UZ, 6UZ, 10UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        auto invalid = saved;
        invalid[kChrBanksOffset + index * 2U + 1U] |= 0x02U;
        reject(invalid);
    }
    auto invalid_low_m2 = saved;
    invalid_low_m2.back() = 4U;
    reject(invalid_low_m2);
    for (const std::size_t offset : {kWramLengthOffset, kWramDataOffset + kWramSize}) {
        auto invalid = saved;
        std::fill(invalid.begin() + static_cast<std::ptrdiff_t>(offset),
                  invalid.begin() + static_cast<std::ptrdiff_t>(offset + 8U),
                  0xFFU);
        reject(invalid);
    }

    const auto incompatible = [&](std::unique_ptr<Mapper342> other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    incompatible(board(kPrgSize / 2U));
    incompatible(board(kPrgSize, kChrRamSize / 2U));
    incompatible(board(kPrgSize, kChrRamSize, false));
}

TEST_CASE("Mapper 342 native menu keeps IRQ idle and does not expose audio or flash programming",
          "[mapper342]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU}) {
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("COOLGIRL native menu locks outer pins while game cores own inner pins", "[mapper342]") {
    auto mapper = board();
    const auto start = [&](Byte core, Byte prg_mode, Byte chr_mode = 0U) {
        mapper->reset();
        reg(*mapper, 2, 0);
        reg(*mapper, 3, static_cast<Byte>(prg_mode << 5U));
        reg(*mapper, 4, static_cast<Byte>(chr_mode << 5U));
        reg(*mapper, 6, core);
        reg(*mapper, 7, 0x82); // lock native and enable pattern writes
    };
    start(1, 0);
    mapper->write_prg(0x8000, 5);
    REQUIRE(mapper->read_prg(0x8123) == cell(5U * 0x4000U + 0x123U));
    reg(*mapper, 5, 0);
    REQUIRE(mapper->read_prg(0x8123) == cell(5U * 0x4000U + 0x123U));
    start(0x21, 0); // Fire Hawk's $9000 is a mirror register
    mapper->write_prg(0x9000, 0x10);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(mapper->read_prg(0x8123) == cell(0x123));
    start(2, 0);
    mapper->write_prg(0x8000, 3);
    mapper->write_chr(0x123, 0xA7);
    mapper->write_prg(0x8000, 0);
    REQUIRE(mapper->read_chr(0x123) != 0xA7);
    mapper->write_prg(0xFFFF, 3);
    REQUIRE(mapper->read_chr(0x123) == 0xA7);
    start(8, 7);
    mapper->write_prg(0x9999, 0x15);
    REQUIRE(mapper->read_prg(0x8123) == cell(5U * 0x8000U + 0x123U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    start(16, 0);
    const auto serial = [&](Address addr, Byte data) {
        for (unsigned bit = 0; bit < 5; ++bit) {
            mapper->write_prg_cpu(addr, (data >> bit) & 1U, false);
            mapper->write_prg_cpu(addr, 0x80, true); // RMW trailing strobe is filtered
        }
    };
    serial(0x8000, 0x1E); // fixed last, 4K CHR, vertical
    serial(0xE000, 5);
    REQUIRE(mapper->read_prg(0x8123) == cell(5U * 0x4000U + 0x123));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    serial(0x8000, 0x0B); // fixed first, switched high, horizontal
    REQUIRE(mapper->read_prg(0xC123) == cell(5U * 0x4000U + 0x123));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    start(20, 4, 2);
    mapper->write_prg(0x8000, 6);
    mapper->write_prg(0x8001, 9);
    REQUIRE(mapper->read_prg(0x8123) == cell(9U * 0x2000U + 0x123));
    mapper->write_prg(0x8000, 0x46);
    REQUIRE(mapper->read_prg(0xC123) == cell(9U * 0x2000U + 0x123));
    mapper->write_prg(0x8000, 2);
    mapper->write_prg(0x8001, 0x11);
    mapper->write_chr(0x1004, 0x7A);
    mapper->write_prg(0x8000, 0x82);
    REQUIRE(mapper->read_chr(4) == 0x7A); // inverted 1K banks move to $0000
    mapper->write_prg(0xA000, 1);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("COOLGIRL CPLD IRQ filters A12 against three M2 rising edges", "[mapper342]") {
    auto mapper = board();
    reg(*mapper, 6, 20);
    mapper->write_prg(0xC000, 2);
    mapper->write_prg(0xC001, 0xFF);
    mapper->write_prg(0xE001, 0);
    const auto edge = [&](unsigned cycles) {
        mapper->observe_ppu_a12(false);
        for (unsigned i = 0; i < cycles; ++i)
            mapper->clock_cpu();
        mapper->observe_ppu_a12(true);
        mapper->clock_cpu_falling_edge();
    };
    edge(3); // reload 2 and arm
    edge(2); // rejected pulse
    edge(3); // 2 -> 1
    REQUIRE_FALSE(mapper->irq_pending());
    const auto before_last = state(*mapper);
    edge(3); // 1 -> 0
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000, 0);
    REQUIRE_FALSE(mapper->irq_pending());
    StateReader reader(before_last);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    edge(3);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000, 0);
    mapper->write_prg(0xC000, 0);
    mapper->write_prg(0xC001, 0);
    mapper->write_prg(0xE001, 0);
    edge(3);
    REQUIRE_FALSE(mapper->irq_pending()); // cannot arm on a zero reload
}

TEST_CASE("COOLGIRL loads a trimmed flash profile with volatile SRAM through the registry",
          "[mapper342]") {
    constexpr std::size_t size = 2240UZ * 1024UZ;
    std::vector<Byte> image(size + 16U, 0xFFU);
    std::array<Byte, 16> h{
        'N', 'E', 'S', 0x1A, static_cast<Byte>(size / 16384U), 0U, 0x60, 0x58, 1, 0, 9, 12};
    std::ranges::copy(h, image.begin());
    register_builtin_mappers();
    auto cartridge = Cartridge::from_data(image);
    if (!cartridge)
        FAIL(cartridge.error());
    auto hdr = header(false);
    hdr.prg_ram_size = kWramSize;
    hdr.chr_ram_size = 256U * 1024U;
    Mapper342 mapper(hdr, rom(size), {});
    REQUIRE(mapper.battery_ram().empty());
    reg(mapper, 7, 1);
    mapper.write_prg(0x6000, 0xA7);
    mapper.reset();
    reg(mapper, 7, 1);
    REQUIRE(mapper.read_prg(0x6000) == 0xA7);
    mapper.power_on();
    reg(mapper, 7, 1);
    REQUIRE(mapper.read_prg(0x6000) == 0);
    reg(mapper, 1, 200);
    REQUIRE(mapper.read_prg(0x8123) == 0xFF); // unprogrammed tail, no modulo by file length
}

TEST_CASE("COOLGIRL default optional CPLD cores decode game registers and interrupts",
          "[mapper342]") {
    auto mapper = board();
    const auto start = [&](Byte id) {
        mapper->reset();
        reg(*mapper, 2, 0);
        reg(*mapper, 3, 4U << 5U);
        reg(*mapper, 4, 7U << 5U);
        reg(*mapper, 6, id);
        reg(*mapper, 7, 0x83);
    };
    start(13); // J.Y. PRG, CHR, mirror and IRQ through the shared A12 counter.
    mapper->write_prg(0x8002, 0x15);
    REQUIRE(mapper->read_prg(0xC123) == cell(0xD5U * 8192U + 0x123U));
    mapper->write_prg(0xD001, 3);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    mapper->write_prg(0xC006, 0x81);
    mapper->write_prg(0xC005, 0x80);
    mapper->write_prg(0xC003, 0);
    for (int edge = 0; edge < 2; ++edge) {
        mapper->observe_ppu_a12(false);
        for (int m2 = 0; m2 < 3; ++m2)
            mapper->clock_cpu();
        mapper->observe_ppu_a12(true);
        mapper->clock_cpu_falling_edge();
    }
    REQUIRE(mapper->irq_pending());
    start(14); // Irem H3001 counter expires when it reaches zero.
    mapper->write_prg(0x9005, 0);
    mapper->write_prg(0x9006, 2);
    mapper->write_prg(0x9004, 0);
    mapper->write_prg(0x9003, 0x80);
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x9004, 0);
    REQUIRE_FALSE(mapper->irq_pending());
    start(15); // CV3 firmware subset, split banks and /RD scanline detection.
    mapper->write_expansion(0x5115, 9);
    REQUIRE(mapper->read_prg(0x8123) == cell(8U * 8192U + 0x123));
    REQUIRE(mapper->read_prg(0xA123) == cell(9U * 8192U + 0x123));
    mapper->write_expansion(0x5105, 0xFF);
    REQUIRE(mapper->mirror_mode() == MirrorMode::FourScreen);
    REQUIRE(mapper->write_nametable(0x2C42, 0xF1));
    REQUIRE(mapper->read_nametable(0x2C42) == 0xF1);
    mapper->write_expansion(0x5203, 0);
    mapper->write_expansion(0x5204, 0x80);
    mapper->observe_ppu_read(0x0010);
    for (unsigned nt = 0; nt < 4; ++nt)
        mapper->observe_ppu_read(0x2000U);
    REQUIRE(mapper->irq_pending());
    REQUIRE(mapper->read_expansion(0x5204) == 0xC0);
    REQUIRE_FALSE(mapper->irq_pending());
    for (int cycle = 0; cycle < 16; ++cycle)
        mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->read_expansion(0x5204) == 0U);
    start(23); // FDS conversions substitute an 8K ROM socket for WRAM.
    mapper->write_prg(0xE000, 8);
    mapper->write_prg(0x6123, 0x42);
    REQUIRE(mapper->read_prg(0x6123) == cell(8U * 8192U + 0x123));
    start(24); // VRC4a/c combined pin decode (A1/A6 low selector, A2/A7 high).
    mapper->write_prg(0xF000, 0xF);
    mapper->write_prg(0xF040, 0xF);
    mapper->write_prg(0xF004, 2);
    for (int cycle = 0; cycle < 113; ++cycle)
        mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF0C0, 0);
    REQUIRE_FALSE(mapper->irq_pending());
    start(25); // FME-7 enables a separate downcounter and a ROM/RAM low window.
    const auto fme = [&](Byte command, Byte data) {
        mapper->write_prg(0x8000, command);
        mapper->write_prg(0xA000, data);
    };
    fme(8, 8);
    REQUIRE(mapper->read_prg(0x6123) == cell(8U * 8192U + 0x123));
    fme(8, 0xC0);
    mapper->write_prg(0x6123, 0x8F);
    REQUIRE(mapper->read_prg(0x6123) == 0x8F);
    fme(14, 1);
    fme(15, 0);
    fme(13, 0x81);
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    fme(13, 0);
    REQUIRE_FALSE(mapper->irq_pending());
}

} // namespace
