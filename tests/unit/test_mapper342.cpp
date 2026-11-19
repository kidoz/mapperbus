#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper342.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 4UZ * 1024UZ * 1024UZ;
constexpr std::size_t kChrRamSize = 512UZ * 1024UZ;
constexpr std::size_t kWramSize = 32UZ * 1024UZ;
constexpr std::size_t kRegistersOffset = 14UZ;
constexpr std::size_t kPrgBanksOffset = 22UZ;
constexpr std::size_t kChrBanksOffset = 26UZ;
constexpr std::size_t kWramLengthOffset = 42UZ;
constexpr std::size_t kWramDataOffset = 50UZ;
constexpr std::size_t kStateOverhead = 58UZ;

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
    return {static_cast<Byte>((regs[5] >> 1U) & 0x3EU), 0xFDU, 0xFEU, 0xFFU};
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
    const unsigned mode = regs[4] & 7U;
    const unsigned slot = (addr >> 10U) & 7U;
    const auto paired = [&](unsigned index) { return (banks[index] & 0x1FEU) | (slot & 1U); };
    unsigned bank = 0U;
    if (mode == 0U) {
        bank = (banks[0] & 0x1F8U) | slot;
    } else if (mode == 2U) {
        bank = slot < 2U ? paired(0U) : slot < 4U ? paired(2U) : banks[slot];
    } else if (mode == 3U) {
        bank = slot < 4U ? banks[slot] : slot < 6U ? paired(0U) : paired(2U);
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
            h.prg_ram_size = 0x8000U;
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
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
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
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
    }
}

TEST_CASE("Mapper 342 reset PRG banks are zero, FD, FE and FF", "[mapper342][reset]") {
    auto mapper = board();
    for (const Address addr : {0x8000U, 0x8001U, 0x9FFFU, 0xA000U, 0xBFFFU}) {
        REQUIRE(mapper->read_prg(addr) == cell(addr - 0x8000U));
    }
    for (const Address addr : {0xC000U, 0xC123U, 0xE000U, 0xFFFFU}) {
        REQUIRE(mapper->read_prg(addr) == cell(0x1FC000UZ + (addr - 0xC000U)));
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
        regs[4] = static_cast<Byte>((regs[4] & 0xF8U) | mode);
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
        reg(*mapper, 4U, mode);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
        mapper->write_chr(0x0123U, 0x5CU);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
    }
}

TEST_CASE("Mapper 342 CIRAM controls include four-screen override", "[mapper342]") {
    auto mapper = board(kPrgSize, kChrRamSize, true, 0U, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned control = 0U; control < 0x80U; ++control) {
        reg(*mapper, 7U, static_cast<Byte>(control));
        const auto expected = (control & 0x10U) != 0U      ? MirrorMode::FourScreen
                              : (control & 0x0CU) == 0x00U ? MirrorMode::Vertical
                              : (control & 0x0CU) == 0x04U ? MirrorMode::Horizontal
                              : (control & 0x0CU) == 0x08U ? MirrorMode::SingleLower
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
    for (std::size_t index = 0U; index < 4U; ++index) {
        auto invalid = saved;
        invalid[kPrgBanksOffset + index] ^= 1U;
        reject(invalid);
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        auto invalid = saved;
        invalid[kChrBanksOffset + index * 2U] ^= 1U;
        reject(invalid);
    }
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

TEST_CASE("Mapper 342 partial component exposes no simulated ASIC IRQ audio or flash",
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

} // namespace
