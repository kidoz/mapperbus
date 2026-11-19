#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper144.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 65536U;
constexpr std::size_t kChrSize = 65536U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 144U;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    return h;
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i / 0x8000U) * 0x5BU + i * 17U + (i >> 8U));
    return data;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> data(kChrSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i / 0x2000U) * 0x1DU + i * 29U + (i >> 9U));
    return data;
}

std::vector<Byte> state(const Mapper144& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 144 decodes every latch value and wraps unconnected ROM address pins",
          "[mapper144]") {
    for (unsigned value = 0U; value < 256U; ++value) {
        INFO(value);
        auto prg = prg_rom();
        const auto chr = chr_rom();
        prg[0] = (value & 1U) != 0U ? 0xFFU : 0xFEU;
        Mapper144 mapper(header(), prg, chr);
        mapper.write_prg(0x8000U, static_cast<Byte>(value));
        REQUIRE(state(mapper).back() == value);

        const std::size_t prg_bank = (value & 3U) % (kPrgSize / 0x8000U);
        for (const Address addr : {0x8001U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            const std::size_t offset = prg_bank * 0x8000U + (addr & 0x7FFFU);
            REQUIRE(mapper.read_prg(addr) == prg[offset]);
        }
        const std::size_t chr_bank = (value >> 4U) % (kChrSize / 0x2000U);
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU}) {
            const std::size_t offset = chr_bank * 0x2000U + (addr & 0x1FFFU);
            REQUIRE(mapper.read_chr(addr) == chr[offset]);
        }
    }
}

TEST_CASE("Mapper 144 bank register aliases cover the complete upper CPU window", "[mapper144]") {
    std::vector<Byte> prg(kPrgSize, 0xFFU);
    Mapper144 mapper(header(), prg, chr_rom());
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper.power_on();
        mapper.write_prg(static_cast<Address>(addr), 0xA5U);
        REQUIRE(state(mapper).back() == 0xA5U);
    }

    mapper.power_on();
    const auto before = state(mapper);
    for (const Address addr : {0x0000U, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        mapper.write_prg(addr, 0xFFU);
        REQUIRE_FALSE(mapper.maps_prg(addr));
        REQUIRE(mapper.read_prg(addr) == 0U);
    }
    REQUIRE(state(mapper) == before);
}

TEST_CASE("Mapper 144 resolves all bus conflicts with ROM D0 winning", "[mapper144]") {
    for (unsigned rom_value = 0U; rom_value < 256U; ++rom_value) {
        INFO(rom_value);
        auto prg = prg_rom();
        prg[0x1234U] = static_cast<Byte>(rom_value);
        Mapper144 mapper(header(), prg, chr_rom());
        for (unsigned written = 0U; written < 256U; ++written) {
            mapper.power_on();
            mapper.write_prg(0x9234U, static_cast<Byte>(written));
            const auto expected = rom_value & (written | 1U);
            REQUIRE(state(mapper).back() == expected);
        }
    }
}

TEST_CASE("Mapper 144 bus conflicts use the old PRG bank at the write address", "[mapper144]") {
    auto prg = prg_rom();
    prg[0] = 0xFFU;
    prg[0x1000U] = 0x0FU;
    prg[0x9000U] = 0xF0U;
    Mapper144 mapper(header(), prg, chr_rom());
    mapper.write_prg(0x8000U, 0x01U);
    REQUIRE(state(mapper).back() == 0x01U);
    mapper.write_prg(0x9000U, 0x0FU);
    REQUIRE(state(mapper).back() == 0x00U);
    REQUIRE(mapper.read_prg(0x8001U) == prg[0x0001U]);
}

TEST_CASE("Mapper 144 preserves fixed mirroring and has no RAM IRQ or audio", "[mapper144]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto prg = prg_rom();
        prg[0] = 0xFFU;
        Mapper144 mapper(header(mirror), prg, chr_rom());
        mapper.write_prg(0x8000U, 0xD3U);
        const auto chr_before = mapper.read_chr(0x0100U);
        mapper.write_chr(0x0100U, static_cast<Byte>(chr_before ^ 0xFFU));
        REQUIRE(mapper.read_chr(0x0100U) == chr_before);
        REQUIRE(mapper.mirror_mode() == mirror);
        mapper.reset();
        REQUIRE(mapper.mirror_mode() == mirror);
        REQUIRE(mapper.battery_ram().empty());
        REQUIRE_FALSE(mapper.maps_expansion(0x5000U));
        REQUIRE_FALSE(mapper.has_expansion_audio());
        REQUIRE_FALSE(mapper.irq_pending());
    }
}

TEST_CASE("Mapper 144 soft reset retains the latch and power-on selects bank zero", "[mapper144]") {
    auto prg = prg_rom();
    const auto chr = chr_rom();
    prg[0] = 0xFFU;
    Mapper144 mapper(header(), prg, chr);
    mapper.write_prg(0x8000U, 0xB3U);
    const auto selected = state(mapper);
    const auto prg_value = mapper.read_prg(0xFFFFU);
    const auto chr_value = mapper.read_chr(0x1FFFU);

    mapper.reset();
    REQUIRE(state(mapper) == selected);
    REQUIRE(mapper.read_prg(0xFFFFU) == prg_value);
    REQUIRE(mapper.read_chr(0x1FFFU) == chr_value);

    mapper.power_on();
    REQUIRE(state(mapper).back() == 0U);
    REQUIRE(mapper.read_prg(0xFFFFU) == prg[0x7FFFU]);
    REQUIRE(mapper.read_chr(0x1FFFU) == chr[0x1FFFU]);
}

TEST_CASE("Mapper 144 state replays the latch and rejects malformed input atomically",
          "[mapper144][state]") {
    auto prg = prg_rom();
    const auto chr = chr_rom();
    prg[0] = 0xFFU;
    Mapper144 source(header(), prg, chr);
    source.write_prg(0x8000U, 0xABU);
    const auto saved = state(source);
    REQUIRE(saved.size() == 3U);

    Mapper144 target(header(), prg, chr);
    StateReader reader(saved);
    target.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(target) == saved);
    REQUIRE(target.read_prg(0xFFFFU) == source.read_prg(0xFFFFU));
    REQUIRE(target.read_chr(0x1FFFU) == source.read_chr(0x1FFFU));

    for (const std::size_t length : {0UZ, 1UZ, 2UZ}) {
        const auto before = state(target);
        StateReader truncated{std::span(saved).first(length)};
        target.load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(target) == before);
    }
    for (const std::size_t offset : {0UZ, 1UZ}) {
        auto bad = saved;
        bad[offset] ^= 0xFFU;
        const auto before = state(target);
        StateReader invalid(bad);
        target.load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(target) == before);
    }

    Mapper144 other_mirror(header(MirrorMode::Horizontal), prg, chr);
    const auto before = state(other_mirror);
    StateReader wrong_geometry(saved);
    other_mirror.load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(other_mirror) == before);

    auto followed = saved;
    followed.push_back(0xA7U);
    StateReader trailing(followed);
    target.load_state(trailing);
    REQUIRE(trailing.ok());
    REQUIRE(trailing.remaining() == 1U);
    REQUIRE(state(target) == saved);
}

TEST_CASE("Mapper 144 state accepts every possible latch byte", "[mapper144][state]") {
    Mapper144 mapper(header(), prg_rom(), chr_rom());
    auto candidate = state(mapper);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        candidate.back() = static_cast<Byte>(latch);
        StateReader reader(candidate);
        mapper.load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(mapper) == candidate);
    }
}

TEST_CASE("Mapper 144 validates the documented ROM-only board geometry", "[mapper144]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto h = header(mirror);
        REQUIRE(Mapper144::validate_geometry(h, kPrgSize, kChrSize));
        h.is_nes2 = false;
        REQUIRE(Mapper144::validate_geometry(h, kPrgSize, kChrSize));
    }

    auto h = header();
    h.submapper = 1U;
    REQUIRE_FALSE(Mapper144::validate_geometry(h, kPrgSize, kChrSize));
    for (const std::size_t size : {0UZ, 32768UZ, 65535UZ, 65537UZ, 131072UZ}) {
        REQUIRE_FALSE(Mapper144::validate_geometry(header(), size, kChrSize));
        REQUIRE_FALSE(Mapper144::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper144::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }

    const auto reject_memory = [](const INesHeader& candidate) {
        REQUIRE_FALSE(Mapper144::validate_geometry(candidate, kPrgSize, kChrSize));
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
    h = header();
    h.chr_nvram_size = 8192U;
    reject_memory(h);
}

} // namespace
