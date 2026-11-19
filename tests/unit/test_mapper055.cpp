#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <vector>

#include "core/mappers/mapper055.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.prg_rom_banks = 3U;
    h.chr_rom_banks = 1U;
    h.mapper_number = 55U;
    h.mirror_mode = mirror;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 2048U;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) + 0x37U);
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 17U + (offset >> 7U) + 0xA5U);
}

Mario1Malee2 board(bool legacy = false, MirrorMode mirror = MirrorMode::Vertical) {
    std::vector<Byte> prg(49152U);
    std::vector<Byte> chr(8192U);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i);
    return Mario1Malee2(header(legacy, mirror), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 055 decodes both expansion windows and the complete fixed ROM", "[mapper055]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(legacy);
        for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
            const auto cpu = static_cast<Address>(addr);
            REQUIRE(mapper.maps_prg(cpu) == (addr >= 0x6000U));
            REQUIRE(mapper.prg_read_mask(cpu) == 0xFFU);
            if (addr >= 0x8000U)
                REQUIRE(mapper.read_prg(cpu) == prg_cell(addr - 0x8000U));
            else if (addr >= 0x7000U)
                REQUIRE(mapper.read_prg(cpu) == 0U);
            else if (addr >= 0x6000U)
                REQUIRE(mapper.read_prg(cpu) == prg_cell(0x8000U + (addr & 0x07FFU)));
            else
                REQUIRE(mapper.read_prg(cpu) == 0U);
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == chr_cell(addr));
            mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(~addr));
            REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == chr_cell(addr));
        }
        REQUIRE_FALSE(mapper.has_expansion_audio());
        REQUIRE_FALSE(mapper.irq_pending());
        REQUIRE(mapper.cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper.battery_ram().empty());
    }
}

TEST_CASE("Mapper 055 mirrors RAM once and ignores every ROM write", "[mapper055]") {
    auto mapper = board();
    for (unsigned addr = 0x7000U; addr < 0x8000U; ++addr) {
        const auto cpu = static_cast<Address>(addr);
        const auto value = static_cast<Byte>(addr * 11U);
        mapper.write_prg(cpu, value);
        REQUIRE(mapper.read_prg(cpu) == value);
        REQUIRE(mapper.read_prg(static_cast<Address>(addr ^ 0x0800U)) == value);
    }
    for (unsigned addr = 0x6000U; addr <= 0xFFFFU; ++addr) {
        if (addr >= 0x7000U && addr < 0x8000U)
            continue;
        const auto cpu = static_cast<Address>(addr);
        const auto before = mapper.read_prg(cpu);
        mapper.write_prg(cpu, static_cast<Byte>(~before));
        REQUIRE(mapper.read_prg(cpu) == before);
    }
    const std::array<Byte, 2048> imported = [] {
        std::array<Byte, 2048> bytes{};
        bytes.fill(0xFFU);
        return bytes;
    }();
    mapper.set_battery_ram(imported);
    REQUIRE(mapper.read_prg(0x7000U) == 0U);
}

TEST_CASE("Mapper 055 retains RAM on reset and clears it only on cold power", "[mapper055]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(false, mirror);
        REQUIRE(mapper.mirror_mode() == mirror);
        for (unsigned addr = 0U; addr < 2048U; ++addr)
            mapper.write_prg(static_cast<Address>(0x7000U + addr), static_cast<Byte>(addr));
        mapper.reset();
        for (unsigned addr = 0U; addr < 2048U; ++addr)
            REQUIRE(mapper.read_prg(static_cast<Address>(0x7000U + addr)) ==
                    static_cast<Byte>(addr));
        REQUIRE(mapper.mirror_mode() == mirror);
        mapper.power_on();
        for (unsigned addr = 0U; addr < 2048U; ++addr)
            REQUIRE(mapper.read_prg(static_cast<Address>(0x7000U + addr)) == 0U);
        REQUIRE(mapper.read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper.read_chr(0x1234U) == chr_cell(0x1234U));
    }
}

TEST_CASE("Mapper 055 rejects incompatible ROM, RAM, battery, and mirroring metadata",
          "[mapper055]") {
    for (bool legacy : {false, true}) {
        REQUIRE(Mario1Malee2::validate_geometry(header(legacy), 49152U, 8192U));
        for (std::size_t prg_size : {0U, 1U, 16384U, 32768U, 49151U, 49153U, 65536U, 131072U})
            REQUIRE_FALSE(Mario1Malee2::validate_geometry(header(legacy), prg_size, 8192U));
        for (std::size_t chr_size : {0U, 1U, 4096U, 8191U, 8193U, 16384U})
            REQUIRE_FALSE(Mario1Malee2::validate_geometry(header(legacy), 49152U, chr_size));
        auto battery = header(legacy);
        battery.has_battery = true;
        REQUIRE_FALSE(Mario1Malee2::validate_geometry(battery, 49152U, 8192U));
        for (auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper})
            REQUIRE_FALSE(Mario1Malee2::validate_geometry(header(legacy, mirror), 49152U, 8192U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mario1Malee2::validate_geometry(h, 49152U, 8192U));
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0:
            h.prg_ram_size = 0U;
            break;
        case 1:
            h.prg_nvram_size = 2048U;
            break;
        case 2:
            h.chr_ram_size = 8192U;
            break;
        case 3:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mario1Malee2::validate_geometry(h, 49152U, 8192U));
    }
}

TEST_CASE("Mapper 055 state replay is bounded, atomic, and excludes immutable ROM", "[mapper055]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < 2048U; ++addr)
        mapper.write_prg(static_cast<Address>(0x7800U + addr),
                         static_cast<Byte>(addr * 7U + (addr >> 5U)));
    const auto saved = state(mapper);
    REQUIRE(saved.size() == 2050U);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (unsigned byte = 0U; byte < 2U; ++byte) {
        auto invalid = saved;
        invalid[byte] ^= 0xFFU;
        StateReader reader(invalid);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }

    mapper.power_on();
    StateReader reader(saved);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(mapper) == saved);
    for (unsigned addr = 0U; addr < 2048U; ++addr)
        REQUIRE(mapper.read_prg(static_cast<Address>(0x7000U + addr)) ==
                static_cast<Byte>(addr * 7U + (addr >> 5U)));
    REQUIRE(mapper.read_prg(0xFFFFU) == prg_cell(0x7FFFU));
    REQUIRE(mapper.read_chr(0x1FFFU) == chr_cell(0x1FFFU));
}

} // namespace
