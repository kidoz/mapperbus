#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/mappers/mapper304.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::array kPrgSizes = {32768UZ, 40960UZ, 49152UZ};
constexpr std::size_t kChrBytes = 8192U;

INesHeader header(std::size_t chr_size = kChrBytes, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 304U;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    h.chr_ram_size = chr_size == 0U ? kChrBytes : 0U;
    return h;
}

std::vector<Byte> prg_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = static_cast<Byte>((offset / 8192U) * 41U + offset * 13U + (offset >> 8U));
    }
    return data;
}

std::vector<Byte> chr_rom(std::size_t size = kChrBytes) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = static_cast<Byte>(offset * 29U + (offset >> 8U) + 0x53U);
    }
    return data;
}

Mapper304 board(std::size_t prg_size = 49152U,
                std::size_t chr_size = kChrBytes,
                MirrorMode mirror = MirrorMode::Vertical) {
    return Mapper304(header(chr_size, mirror), prg_rom(prg_size), chr_rom(chr_size));
}

std::vector<Byte> state(const Mapper304& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 304 validates every documented PRG and CHR population", "[mapper304]") {
    for (const auto prg_size : kPrgSizes) {
        for (const std::size_t chr_size : {0UZ, kChrBytes}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                REQUIRE(Mapper304::validate_geometry(header(chr_size, mirror), prg_size, chr_size));
            }
        }
    }

    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper304::validate_geometry(h, 49152U, kChrBytes));
    h = header();
    h.mapper_number = 43U;
    REQUIRE_FALSE(Mapper304::validate_geometry(h, 49152U, kChrBytes));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper304::validate_geometry(h, 49152U, kChrBytes));
    }

    for (const std::size_t size :
         {0UZ, 8192UZ, 16384UZ, 32767UZ, 32769UZ, 36864UZ, 49151UZ, 49153UZ, 65536UZ}) {
        REQUIRE_FALSE(Mapper304::validate_geometry(header(), size, kChrBytes));
    }
    for (const std::size_t size : {1UZ, 4096UZ, 8191UZ, 8193UZ, 16384UZ}) {
        REQUIRE_FALSE(Mapper304::validate_geometry(header(), 49152U, size));
    }

    const auto reject_memory = [](INesHeader candidate, std::size_t chr_size = kChrBytes) {
        REQUIRE_FALSE(Mapper304::validate_geometry(candidate, 49152U, chr_size));
    };
    h = header();
    h.has_battery = true;
    reject_memory(h);
    h = header();
    h.prg_ram_size = 8192U;
    reject_memory(h);
    h = header();
    h.prg_nvram_size = 8192U;
    reject_memory(h);
    h = header();
    h.chr_ram_size = 8192U;
    reject_memory(h);
    h = header(0U);
    h.chr_ram_size = 0U;
    reject_memory(h, 0U);
    h = header();
    h.chr_nvram_size = 8192U;
    reject_memory(h);
    h = header(0U);
    h.chr_ram_size = 0U;
    h.chr_nvram_size = 8192U;
    reject_memory(h, 0U);

    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper304::validate_geometry(header(kChrBytes, mirror), 49152U, kChrBytes));
    }
}

TEST_CASE("Mapper 304 maps every fixed and switchable PRG byte with physical wrapping",
          "[mapper304]") {
    for (const auto prg_size : kPrgSizes) {
        auto mapper = board(prg_size);
        const auto expected = prg_rom(prg_size);
        for (Byte bank = 0U; bank < 2U; ++bank) {
            mapper.write_expansion(0x4027U, bank);
            for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
                const std::size_t offset =
                    ((4U + static_cast<std::size_t>(bank)) * 8192U + (addr & 0x1FFFU)) % prg_size;
                INFO("prg_size=" << prg_size << " bank=" << bank << " addr=" << addr);
                REQUIRE(mapper.read_prg(static_cast<Address>(addr)) == expected[offset]);
            }
        }
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            REQUIRE(mapper.read_prg(static_cast<Address>(addr)) == expected[addr - 0x8000U]);
        }
    }
}

TEST_CASE("Mapper 304 bank latch uses only D0 at the exact documented port", "[mapper304]") {
    auto mapper = board();
    const auto expected = prg_rom(49152U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper.write_expansion(0x4027U, static_cast<Byte>(value));
        const std::size_t bank = 4U + (value & 1U);
        for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
            const std::size_t offset = bank * 8192U + (addr & 0x1FFFU);
            REQUIRE(mapper.read_prg(addr) == expected[offset]);
        }
    }

    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper.write_expansion(0x4027U, 1U);
        mapper.write_expansion(static_cast<Address>(addr), 0U);
        INFO("write_addr=" << addr);
        REQUIRE(mapper.read_prg(0x6000U) == expected[(addr == 0x4027U ? 4UZ : 5UZ) * 8192U]);
    }

    mapper.write_expansion(0x4027U, 1U);
    const auto before = state(mapper);
    for (const Address addr : {0x4027U, 0x6000U, 0x8027U, 0xC027U, 0xFFFFU}) {
        mapper.write_prg(addr, 0U);
        REQUIRE(state(mapper) == before);
    }
}

TEST_CASE("Mapper 304 drives only the required expansion reads and all PRG windows",
          "[mapper304][bus]") {
    auto mapper = board();
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const bool driven = addr >= 0x4042U && addr <= 0x4055U;
        INFO("addr=" << addr);
        REQUIRE(mapper.maps_expansion(static_cast<Address>(addr)) == driven);
        REQUIRE(mapper.expansion_read_mask(static_cast<Address>(addr)) == (driven ? 0xFFU : 0U));
        REQUIRE(mapper.read_expansion(static_cast<Address>(addr)) == (driven ? 0xFFU : 0U));
    }
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        REQUIRE(mapper.maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE(mapper.prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
        if (addr < 0x6000U) {
            REQUIRE(mapper.read_prg(static_cast<Address>(addr)) == 0U);
        }
    }
    REQUIRE(mapper.cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper.battery_ram().empty());
    REQUIRE_FALSE(mapper.has_expansion_audio());
    REQUIRE(mapper.audio_output() == 0.0F);
}

TEST_CASE("Mapper 304 preserves CHR ROM and provides unbanked volatile CHR RAM",
          "[mapper304][chr]") {
    auto rom = board(49152U, kChrBytes);
    const auto expected = chr_rom();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        rom.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xFFU));
        REQUIRE(rom.read_chr(static_cast<Address>(addr)) == expected[addr]);
    }

    auto ram = board(49152U, 0U);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        ram.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + (addr >> 8U)));
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(ram.read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr * 17U + (addr >> 8U)));
    }
    REQUIRE(ram.battery_ram().empty());
    ram.set_battery_ram(std::array<Byte, 2>{0xFFU, 0xFFU});
    REQUIRE(ram.read_chr(0U) == 0U);
    REQUIRE(ram.read_chr(1U) == 17U);
}

TEST_CASE("Mapper 304 retains fixed mirroring and uses non-emulated hardware IRQ policy",
          "[mapper304][reset][irq]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(49152U, 0U, mirror);
        mapper.write_expansion(0x4027U, 1U);
        mapper.write_chr(0x123U, 0xA7U);
        mapper.write_expansion(0x4068U, 1U);
        for (unsigned cycle = 0U; cycle < 20000U; ++cycle) {
            mapper.clock_cpu();
            mapper.clock_irq_counter();
            mapper.clock_audio();
            REQUIRE_FALSE(mapper.irq_pending());
            REQUIRE(mapper.audio_output() == 0.0F);
        }
        mapper.acknowledge_irq();
        REQUIRE_FALSE(mapper.irq_pending());
        mapper.write_expansion(0x4068U, 0U);
        REQUIRE_FALSE(mapper.irq_pending());
        REQUIRE(mapper.mirror_mode() == mirror);

        const auto before_reset = state(mapper);
        mapper.reset();
        REQUIRE(state(mapper) == before_reset);
        REQUIRE(mapper.read_prg(0x6000U) == prg_rom(49152U)[5U * 8192U]);
        REQUIRE(mapper.read_chr(0x123U) == 0xA7U);
        REQUIRE(mapper.mirror_mode() == mirror);

        mapper.power_on();
        REQUIRE(mapper.read_prg(0x6000U) == prg_rom(49152U)[4U * 8192U]);
        REQUIRE(mapper.read_chr(0x123U) == 0U);
        REQUIRE(mapper.mirror_mode() == mirror);
        REQUIRE_FALSE(mapper.irq_pending());
    }
}

TEST_CASE("Mapper 304 state restores all mutable bytes without serializing immutable ROM",
          "[mapper304][state]") {
    for (const std::size_t chr_size : {0UZ, kChrBytes}) {
        auto source = board(49152U, chr_size);
        source.write_expansion(0x4027U, 1U);
        if (chr_size == 0U) {
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                source.write_chr(static_cast<Address>(addr),
                                 static_cast<Byte>(addr * 23U + (addr >> 7U)));
            }
        }
        const auto saved = state(source);
        REQUIRE(saved.size() == (chr_size == 0U ? 8199UZ : 7UZ));

        auto target = board(49152U, chr_size);
        StateReader reader(saved);
        target.load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(target) == saved);
        for (const Address addr : {0U, 0x123U, 0x1FFFU}) {
            REQUIRE(target.read_chr(addr) == source.read_chr(addr));
        }

        auto followed = saved;
        followed.push_back(0xA7U);
        StateReader trailing(followed);
        target.load_state(trailing);
        REQUIRE(trailing.ok());
        REQUIRE(trailing.remaining() == 1U);
        REQUIRE(state(target) == saved);
    }

    auto source = board();
    source.write_expansion(0x4027U, 1U);
    const auto saved = state(source);
    auto different_rom = prg_rom(49152U);
    for (auto& value : different_rom) {
        value ^= 0xFFU;
    }
    const auto expected = different_rom;
    Mapper304 target(header(), std::move(different_rom), chr_rom());
    StateReader reader(saved);
    target.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(target.read_prg(0x6000U) == expected[5U * 8192U]);
    REQUIRE(target.read_prg(0x8000U) == expected[0U]);
    REQUIRE(target.read_chr(0x123U) == chr_rom()[0x123U]);
}

TEST_CASE("Mapper 304 rejects truncated and invalid states without any mutation",
          "[mapper304][state][atomic]") {
    auto mapper = board(49152U, 0U);
    mapper.write_expansion(0x4027U, 1U);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 31U + 7U));
    }
    const auto saved = state(mapper);
    REQUIRE(saved.size() == 8199U);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 48U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader reader(corrupt);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (unsigned bank = 2U; bank < 256U; ++bank) {
        auto corrupt = saved;
        corrupt[6U] = static_cast<Byte>(bank);
        StateReader reader(corrupt);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }

    const auto reject_cross_geometry = [&saved](Mapper304& target) {
        const auto before = state(target);
        StateReader reader(saved);
        target.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(target) == before);
    };
    auto other_prg = board(32768U, 0U);
    reject_cross_geometry(other_prg);
    auto other_mirror = board(49152U, 0U, MirrorMode::Horizontal);
    reject_cross_geometry(other_mirror);
    auto rom = board();
    reject_cross_geometry(rom);

    auto rom_saved = state(board());
    auto ram_target = board(49152U, 0U);
    const auto before = state(ram_target);
    StateReader wrong_population(rom_saved);
    ram_target.load_state(wrong_population);
    REQUIRE_FALSE(wrong_population.ok());
    REQUIRE(state(ram_target) == before);
}

} // namespace
