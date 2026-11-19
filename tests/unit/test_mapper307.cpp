#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper307.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 131072U;
constexpr std::size_t kStateSize = 16393U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.prg_rom_banks = 8U;
    result.mapper_number = 307U;
    result.mirror_mode = mirror;
    result.is_nes2 = true;
    result.prg_rom_size = kPrgSize;
    result.prg_ram_size = 8192U;
    result.chr_ram_size = 8192U;
    return result;
}

Byte rom_byte(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>((offset * 29U) ^ (offset >> 7U) ^ (offset >> 12U) ^ salt);
}

std::unique_ptr<Mapper307> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    REQUIRE(Mapper307::validate_geometry(header(mirror), kPrgSize, 0U));
    std::vector<Byte> prg(kPrgSize);
    for (std::size_t offset = 0U; offset < prg.size(); ++offset) {
        prg[offset] = rom_byte(offset, salt);
    }
    return std::make_unique<Mapper307>(header(mirror), std::move(prg), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper307& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select(Mapper307& mapper, Byte reg) {
    mapper.write_prg(0x8000U, reg);
}

void commit(Mapper307& mapper, Byte reg, Byte value) {
    select(mapper, reg);
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 307 accepts only the KS-7037 NES 2.0 memory population", "[mapper307]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper307::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    auto invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper307::validate_geometry(invalid, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper307::validate_geometry(invalid, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 4096UZ, 8192UZ, 32768UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper307::validate_geometry(header(), size, 0U));
        if (size != 0U) {
            REQUIRE_FALSE(Mapper307::validate_geometry(header(), kPrgSize, size));
        }
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 0U;
            break;
        case 2U:
            invalid.prg_ram_size = 4096U;
            break;
        case 3U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 4U:
            invalid.chr_ram_size = 0U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper307::validate_geometry(invalid, kPrgSize, 0U));
    }
    invalid = header();
    invalid.chr_ram_size = 16384U;
    REQUIRE_FALSE(Mapper307::validate_geometry(invalid, kPrgSize, 0U));
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper307::validate_geometry(invalid, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 307 fixed windows and absent devices match the conversion cartridge",
          "[mapper307]") {
    auto mapper = board();
    REQUIRE(state(*mapper).size() == kStateSize);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0xB000U) == 0U);
    for (const Address addr : {0x7000U, 0x7123U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(addr) == rom_byte(15U * 4096U + (addr & 0x0FFFU)));
    }
    for (const Address addr : {0xA000U, 0xA123U, 0xAFFFU}) {
        REQUIRE(mapper->read_prg(addr) == rom_byte(28U * 4096U + (addr & 0x0FFFU)));
    }
    for (const Address addr : {0xE000U, 0xE123U, 0xFFFFU}) {
        REQUIRE(mapper->read_prg(addr) == rom_byte(15U * 8192U + (addr & 0x1FFFU)));
    }
    REQUIRE(mapper->read_prg(0x8000U) == rom_byte(0U));
    REQUIRE(mapper->read_prg(0xC000U) == rom_byte(0U));

    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (const Address addr : {0x6000U, 0x7000U, 0x8000U, 0xB000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->maps_expansion(0x4020U));
    REQUIRE_FALSE(mapper->maps_expansion(0x5FFFU));

    const auto before = state(*mapper);
    mapper->write_expansion(0x4040U, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->acknowledge_irq();
    REQUIRE(state(*mapper) == before);
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 307 decodes every even selector alias and ignores upper data bits",
          "[mapper307]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0xFDU);
        const auto saved = state(*mapper);
        REQUIRE(saved[2U] == 5U);
        for (std::size_t index = 3U; index <= 8U; ++index) {
            REQUIRE(saved[index] == 0U);
        }
    }
    for (const Address addr : {0x7FFFU, 0xA000U, 0xC000U, 0xFFFFU}) {
        const auto before = state(*mapper);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 307 odd aliases update only the selected functional register", "[mapper307]") {
    auto mapper = board();
    for (Byte reg = 0U; reg < 8U; ++reg) {
        for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
            mapper->reset();
            select(*mapper, reg);
            mapper->write_prg(static_cast<Address>(addr), 0xA5U);
            const auto saved = state(*mapper);
            REQUIRE(saved[2U] == reg);
            for (Byte target = 2U; target < 8U; ++target) {
                const Byte expected = target == reg ? (reg < 6U ? 1U : 5U) : 0U;
                CAPTURE(reg, addr, target);
                REQUIRE(saved[target + 1U] == expected);
            }
        }
    }
}

TEST_CASE("Mapper 307 switchable PRG banks use four data bits and fixed pages never move",
          "[mapper307]") {
    auto mapper = board();
    for (Byte reg : {6U, 7U}) {
        const Address base = reg == 6U ? 0x8000U : 0xC000U;
        for (unsigned value = 0U; value < 256U; ++value) {
            commit(*mapper, reg, static_cast<Byte>(value));
            const std::size_t bank_offset = (value & 15U) * 8192U;
            for (unsigned offset = 0U; offset < 8192U; ++offset) {
                const auto addr = static_cast<Address>(base + offset);
                CAPTURE(reg, value, offset);
                REQUIRE(mapper->read_prg(addr) == rom_byte(bank_offset + offset));
            }
            REQUIRE(mapper->read_prg(0x7123U) == rom_byte(15U * 4096U + 0x0123U));
            REQUIRE(mapper->read_prg(0xA123U) == rom_byte(28U * 4096U + 0x0123U));
            REQUIRE(mapper->read_prg(0xE123U) == rom_byte(15U * 8192U + 0x0123U));
        }
    }
    for (Byte reg : {0U, 1U}) {
        const auto before = state(*mapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            commit(*mapper, reg, static_cast<Byte>(value));
            auto after = state(*mapper);
            after[2U] = before[2U];
            REQUIRE(after == before);
        }
    }
}

TEST_CASE("Mapper 307 exposes two independent volatile 4 KiB PRG-RAM windows", "[mapper307]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < 4096U; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset),
                          static_cast<Byte>(offset * 3U + 1U));
        mapper->write_prg(static_cast<Address>(0xB000U + offset),
                          static_cast<Byte>(offset * 5U + 7U));
    }
    for (unsigned offset = 0U; offset < 4096U; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 3U + 1U));
        REQUIRE(mapper->read_prg(static_cast<Address>(0xB000U + offset)) ==
                static_cast<Byte>(offset * 5U + 7U));
    }

    const auto before = state(*mapper);
    for (unsigned offset = 0U; offset < 4096U; ++offset) {
        mapper->write_prg(static_cast<Address>(0x7000U + offset), static_cast<Byte>(offset));
        REQUIRE(mapper->read_prg(static_cast<Address>(0x7000U + offset)) ==
                rom_byte(15U * 4096U + offset));
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 307 CHR RAM is unbanked, writable, and not persistent", "[mapper307]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(addr * 11U + (addr >> 8U)));
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 11U + (addr >> 8U)));
    }
    commit(*mapper, 6U, 13U);
    commit(*mapper, 7U, 9U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 11U + (addr >> 8U)));
    }
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 307 CIRAM registers independently select all four nametables",
          "[mapper307][mirroring]") {
    auto mapper = board();
    constexpr std::array<Byte, 4> kTableRegisters{2U, 4U, 3U, 5U};
    for (Byte reg = 2U; reg <= 5U; ++reg) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            commit(*mapper, reg, static_cast<Byte>(value));
            for (unsigned table = 0U; table < 4U; ++table) {
                const auto mapped = mapper->nametable_address(
                    static_cast<Address>(0x2000U + table * 0x0400U + 0x0155U));
                REQUIRE(mapped);
                const unsigned expected_bank = kTableRegisters[table] == reg ? (value & 1U) : 0U;
                REQUIRE(*mapped == expected_bank * 0x0400U + 0x0155U);
            }
        }
    }

    for (unsigned bits = 0U; bits < 16U; ++bits) {
        for (unsigned table = 0U; table < 4U; ++table) {
            commit(*mapper, kTableRegisters[table], static_cast<Byte>((bits >> table) & 1U));
        }
        for (unsigned table = 0U; table < 4U; ++table) {
            for (unsigned offset = 0U; offset < 1024U; ++offset) {
                const auto addr = static_cast<Address>(0x2000U + table * 1024U + offset);
                const auto mirror_addr = static_cast<Address>(0x3000U + table * 1024U + offset);
                const auto expected = static_cast<Address>(((bits >> table) & 1U) * 1024U + offset);
                REQUIRE(mapper->nametable_address(addr) == expected);
                if (mirror_addr < 0x3F00U) {
                    REQUIRE(mapper->nametable_address(mirror_addr) == expected);
                } else {
                    REQUIRE_FALSE(mapper->nametable_address(mirror_addr));
                }
            }
        }
    }
    REQUIRE_FALSE(mapper->nametable_address(0x1FFFU));
    REQUIRE_FALSE(mapper->nametable_address(0x3F00U));
}

TEST_CASE("Mapper 307 reset clears every latch but retains both volatile RAM arrays",
          "[mapper307][reset]") {
    auto mapper = board();
    select(*mapper, 7U);
    for (Byte reg = 2U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg * 3U));
    }
    select(*mapper, 7U);
    for (const Address addr : {0x6000U, 0x6123U, 0x6FFFU, 0xB000U, 0xB123U, 0xBFFFU}) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU}) {
        mapper->write_chr(addr, static_cast<Byte>(addr ^ 0x5AU));
    }
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto warm = state(*mapper);
    REQUIRE(warm[2U] == 0U);
    for (std::size_t index = 3U; index <= 8U; ++index) {
        REQUIRE(warm[index] == 0U);
    }
    REQUIRE(std::equal(warm.begin() + 9, warm.end(), programmed.begin() + 9));

    select(*mapper, 7U);
    for (Byte reg = 2U; reg < 8U; ++reg) {
        commit(*mapper, reg, 0xFFU);
    }
    mapper->power_on();
    const auto cold = state(*mapper);
    REQUIRE(cold == state(*board()));
    REQUIRE(std::all_of(cold.begin() + 9, cold.end(), [](Byte value) { return value == 0U; }));
}

TEST_CASE("Mapper 307 fixed-size states replay RAM without copying immutable ROM",
          "[mapper307][state]") {
    auto mapper = board(MirrorMode::Vertical, 0U);
    for (Byte reg = 2U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg * 5U + 1U));
    }
    select(*mapper, 6U);
    for (unsigned offset = 0U; offset < 4096U; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset + 3U));
        mapper->write_prg(static_cast<Address>(0xB000U + offset), static_cast<Byte>(offset + 9U));
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(addr * 7U + 2U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);

    auto replay = board(MirrorMode::Horizontal, 0U);
    load(*replay, saved);
    REQUIRE(state(*replay) == saved);
    REQUIRE(replay->read_prg(0x8123U) == mapper->read_prg(0x8123U));
    REQUIRE(replay->read_prg(0x6123U) == mapper->read_prg(0x6123U));
    REQUIRE(replay->read_chr(0x0123U) == mapper->read_chr(0x0123U));
    REQUIRE(replay->nametable_address(0x2C55U) == mapper->nametable_address(0x2C55U));

    auto different_rom = board(MirrorMode::Vertical, 0x80U);
    load(*different_rom, saved);
    REQUIRE(state(*different_rom) == saved);
    REQUIRE(different_rom->read_prg(0x7000U) == rom_byte(15U * 4096U, 0x80U));
    REQUIRE(different_rom->read_prg(0xE000U) == rom_byte(15U * 8192U, 0x80U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    replay->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*replay) == saved);
}

TEST_CASE("Mapper 307 rejects every truncated or invalid state without mutation",
          "[mapper307][state]") {
    auto mapper = board();
    for (Byte reg = 2U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg + 1U));
    }
    select(*mapper, 5U);
    for (const Address addr : {0x6000U, 0x6FFFU, 0xB000U, 0xBFFFU}) {
        mapper->write_prg(addr, static_cast<Byte>(addr * 3U));
    }
    for (const Address addr : {0x0000U, 0x0800U, 0x1FFFU}) {
        mapper->write_chr(addr, static_cast<Byte>(addr + 0x39U));
    }
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    std::vector<std::pair<std::size_t, Byte>> invalid{
        {0U, 0xFFU}, {1U, 0xFFU}, {2U, 8U}, {2U, 0xFFU}};
    for (std::size_t offset = 3U; offset <= 6U; ++offset) {
        invalid.emplace_back(offset, 2U);
        invalid.emplace_back(offset, 0xFFU);
    }
    for (std::size_t offset = 7U; offset <= 8U; ++offset) {
        invalid.emplace_back(offset, 16U);
        invalid.emplace_back(offset, 0xFFU);
    }
    for (const auto [offset, value] : invalid) {
        auto damaged = saved;
        damaged[offset] = value;
        CAPTURE(offset, value);
        StateReader reader(damaged);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
}

} // namespace
