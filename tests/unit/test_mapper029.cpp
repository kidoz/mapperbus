#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/ret_cufrom.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 131072U;
constexpr std::size_t kStateSize = 2U + 1U + 8192U + 32768U;

INesHeader header(bool nes2 = true) {
    INesHeader h{};
    h.mapper_number = 29U;
    h.is_nes2 = nes2;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = 8192U;
    h.chr_ram_size = nes2 ? 32768U : 8192U;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

std::vector<Byte> prg_image() {
    std::vector<Byte> prg(kPrgSize);
    for (unsigned i = 0U; i < prg.size(); ++i)
        prg[i] = cell(i / 16384U, i % 16384U);
    return prg;
}

RetCufrom board() {
    return RetCufrom(header(), prg_image(), {});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 029 validates the exact RET-CUFROM board geometry", "[mapper029]") {
    for (bool nes2 : {false, true}) {
        INFO("NES 2.0: " << nes2);
        REQUIRE(RetCufrom::validate_geometry(header(nes2), kPrgSize, 0U));
    }

    for (std::size_t size : {0UZ, 16384UZ, 65536UZ, 98304UZ, 262144UZ}) {
        INFO("PRG bytes: " << size);
        REQUIRE_FALSE(RetCufrom::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 8192UZ, 32768UZ}) {
        INFO("CHR ROM bytes: " << size);
        REQUIRE_FALSE(RetCufrom::validate_geometry(header(), kPrgSize, size));
    }
    for (MirrorMode mirror : {MirrorMode::Horizontal,
                              MirrorMode::FourScreen,
                              MirrorMode::SingleLower,
                              MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    }

    auto h = header();
    h.submapper = 1U;
    REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    h = header(false);
    h.submapper = 1U;
    REQUIRE(RetCufrom::validate_geometry(h, kPrgSize, 0U));

    h = header();
    h.has_battery = true;
    REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    for (std::size_t size : {0UZ, 4096UZ, 16384UZ}) {
        h = header();
        h.prg_ram_size = size;
        REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    }
    for (std::size_t size : {0UZ, 8192UZ, 16384UZ, 65536UZ}) {
        h = header();
        h.chr_ram_size = size;
        REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    }
    h = header();
    h.prg_nvram_size = 8192U;
    REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
    h = header();
    h.chr_nvram_size = 32768U;
    REQUIRE_FALSE(RetCufrom::validate_geometry(h, kPrgSize, 0U));
}

TEST_CASE("Mapper 029 decodes every register alias without bus conflicts", "[mapper029]") {
    auto mapper = board();
    constexpr std::array<Address, 8> kAliases = {
        0x8000U, 0x8001U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xE000U, 0xFFFFU};
    constexpr std::array<Address, 5> kChrOffsets = {0U, 1U, 0x1234U, 0x1FFEU, 0x1FFFU};

    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, static_cast<Byte>(bank));
        for (Address addr : kChrOffsets)
            mapper.write_chr(addr, cell(bank + 20U, addr));
    }

    for (unsigned value = 0U; value < 256U; ++value)
        for (Address alias : kAliases) {
            mapper.write_prg(alias, static_cast<Byte>(value));
            const unsigned prg_bank = (value >> 2U) & 7U;
            const unsigned chr_bank = value & 3U;
            INFO("value: " << value << ", alias: " << alias);
            REQUIRE(mapper.read_prg(0x8000U) == cell(prg_bank, 0U));
            REQUIRE(mapper.read_prg(0xBFFFU) == cell(prg_bank, 16383U));
            REQUIRE(mapper.read_prg(0xC000U) == cell(7U, 0U));
            REQUIRE(mapper.read_prg(0xFFFFU) == cell(7U, 16383U));
            for (Address addr : kChrOffsets)
                REQUIRE(mapper.read_chr(addr) == cell(chr_bank + 20U, addr));
        }

    mapper.write_prg(0xFFFFU, 0xFFU);
    const auto saved = state(mapper);
    REQUIRE(saved[2] == 0xFFU);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper.prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper.cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 029 provides four independent 8 KiB CHR RAM banks", "[mapper029]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < 8192U; ++offset)
            mapper.write_chr(static_cast<Address>(offset), cell(bank, offset));
    }
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0xFFFFU, static_cast<Byte>(bank | 0x1CU));
        for (unsigned offset = 0U; offset < 8192U; ++offset) {
            INFO("bank: " << bank << ", offset: " << offset);
            REQUIRE(mapper.read_chr(static_cast<Address>(offset)) == cell(bank, offset));
        }
    }
}

TEST_CASE("Mapper 029 maps volatile WRAM only at 6000-7FFF", "[mapper029]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        mapper.write_prg(static_cast<Address>(0x6000U + offset), cell(11U, offset));
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        REQUIRE(mapper.read_prg(static_cast<Address>(0x6000U + offset)) == cell(11U, offset));

    for (Address addr : {0x4020U, 0x5FFFU})
        REQUIRE_FALSE(mapper.maps_prg(addr));
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU})
        REQUIRE(mapper.maps_prg(addr));
    REQUIRE_FALSE(mapper.maps_expansion(0x5000U));
    REQUIRE(mapper.battery_ram().empty());
    REQUIRE_FALSE(mapper.has_flash_storage());
    REQUIRE_FALSE(mapper.has_expansion_audio());
    REQUIRE_FALSE(mapper.irq_pending());

    const auto before = state(mapper);
    const std::array<Byte, 3> imported = {1U, 2U, 3U};
    mapper.set_battery_ram(imported);
    mapper.write_prg(0x4020U, 0xAAU);
    mapper.write_prg(0x5FFFU, 0x55U);
    REQUIRE(state(mapper) == before);
}

TEST_CASE("Mapper 029 soft reset retains state and cold power clears volatile memory",
          "[mapper029]") {
    auto mapper = board();
    mapper.write_prg(0x8000U, 0x1BU);
    mapper.write_prg(0x6123U, 0xA7U);
    mapper.write_chr(0x1234U, 0x5AU);
    const auto populated = state(mapper);

    mapper.reset();
    REQUIRE(state(mapper) == populated);
    REQUIRE(mapper.read_prg(0x8000U) == cell(6U, 0U));
    REQUIRE(mapper.read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper.read_chr(0x1234U) == 0x5AU);

    mapper.power_on();
    REQUIRE(state(mapper) == state(board()));
    REQUIRE(mapper.read_prg(0x8000U) == cell(0U, 0U));
    REQUIRE(mapper.read_prg(0xC000U) == cell(7U, 0U));
    REQUIRE(mapper.read_prg(0x6123U) == 0U);
    REQUIRE(mapper.read_chr(0x1234U) == 0U);
}

TEST_CASE("Mapper 029 snapshots replay banks and RAM without serializing PRG ROM",
          "[mapper029][state]") {
    auto mapper = board();
    mapper.write_prg(0x8000U, 0xA6U);
    mapper.write_prg(0x6789U, 0x39U);
    mapper.write_chr(0x0456U, 0xC7U);
    const auto saved = state(mapper);
    REQUIRE(saved.size() == kStateSize);

    mapper.power_on();
    mapper.write_prg(0xFFFFU, 0x55U);
    mapper.write_prg(0x6000U, 0xFFU);
    StateReader reader(saved);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(mapper) == saved);
    REQUIRE(mapper.read_prg(0x8000U) == cell(1U, 0U));
    REQUIRE(mapper.read_prg(0x6789U) == 0x39U);
    REQUIRE(mapper.read_chr(0x0456U) == 0xC7U);

    auto different_prg = prg_image();
    different_prg[0] ^= 0xFFU;
    RetCufrom other(header(), std::move(different_prg), {});
    REQUIRE(state(other) == state(board()));
    const auto initial_other_rom = other.read_prg(0x8000U);
    StateReader other_reader(saved);
    other.load_state(other_reader);
    REQUIRE(other_reader.ok());
    REQUIRE(state(other) == saved);
    REQUIRE(other.read_prg(0x8000U) != initial_other_rom);
    REQUIRE(other.read_prg(0x8000U) == cell(1U, 0U));

    auto framed = saved;
    framed.insert(framed.end(), {0x12U, 0x34U});
    StateReader framed_reader(framed);
    mapper.load_state(framed_reader);
    REQUIRE(framed_reader.ok());
    REQUIRE(framed_reader.remaining() == 2U);
    REQUIRE(state(mapper) == saved);
}

TEST_CASE("Mapper 029 rejects malformed snapshots atomically", "[mapper029][state]") {
    auto mapper = board();
    mapper.write_prg(0x8000U, 0x1EU);
    mapper.write_prg(0x7FFFU, 0xA5U);
    mapper.write_chr(0x1FFFU, 0x5AU);
    const auto saved = state(mapper);

    for (std::size_t length : {0UZ, 1UZ, 2UZ, 3UZ, 8194UZ, 8195UZ, 40962UZ}) {
        INFO("truncated bytes: " << length);
        StateReader reader{std::span(saved).first(length)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }

    for (std::size_t offset : {0UZ, 1UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 0xFFU;
        StateReader reader(invalid);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
}

} // namespace
