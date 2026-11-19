#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/caltron_041.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool nes2 = true) {
    INesHeader h{};
    h.mapper_number = 41U;
    h.is_nes2 = nes2;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> data(0x40000U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i / 0x8000U) * 0x21U + i);
    return data;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> data(0x20000U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i / 0x2000U) * 0x11U + i);
    return data;
}

std::unique_ptr<Caltron041> board() {
    return std::make_unique<Caltron041>(header(), prg_rom(), chr_rom());
}

Byte expected_prg(Byte outer, Address addr) {
    return static_cast<Byte>((outer & 3U) * 0x21U + addr);
}

Byte expected_chr(Byte outer, Byte inner, Address addr) {
    const auto bank = ((outer >> 3U) & 3U) * 4U + inner;
    return static_cast<Byte>(bank * 0x11U + addr);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 041 decodes every outer-bank alias and data bit", "[mapper041]") {
    auto mapper = board();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0U, 0x8000U));
    REQUIRE(mapper->read_chr(0x0000U) == expected_chr(0U, 0U, 0x0000U));

    for (unsigned addr = 0x6000U; addr < 0x6800U; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U + addr / 0x100U);
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto outer = static_cast<Byte>(value & 0x3FU);
        for (Address read_addr : {0x8000U, 0x9A55U, 0xBFFFU, 0xC000U, 0xE5AAU, 0xFFFFU})
            REQUIRE(mapper->read_prg(read_addr) == expected_prg(outer, read_addr));
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (Address read_addr : {0x0000U, 0x0123U, 0x1FFFU})
            REQUIRE(mapper->read_chr(read_addr) == expected_chr(outer, 0U, read_addr));
    }

    mapper->write_prg(0x6000U, 0x2BU);
    const auto saved = state(*mapper);
    for (Address addr : {0x0000U, 0x5FFFU, 0x6800U, 0x6FFFU, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 041 inner CHR latch honors enable and AND bus conflicts", "[mapper041]") {
    auto mapper = board();
    mapper->write_prg(0x67FFU, 0x06U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto address = static_cast<Address>(addr);
        const auto value = static_cast<Byte>(addr * 13U + addr / 0x100U);
        const auto expected = static_cast<Byte>(value & mapper->read_prg(address) & 3U);
        mapper->write_prg(address, value);
        INFO("inner write address " << addr);
        REQUIRE(mapper->read_chr(0x1FFFU) == expected_chr(6U, expected, 0x1FFFU));
        REQUIRE(mapper->read_prg(address) == expected_prg(6U, address));
    }

    const auto enabled = state(*mapper);
    for (Byte outer : {0U, 1U, 2U, 3U}) {
        mapper->write_prg(0x6000U, outer);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        mapper->write_prg(0x6000U, 0x06U);
        REQUIRE(state(*mapper) == enabled);
    }

    mapper->write_prg(0x6000U, 0x07U);
    for (unsigned value = 0U; value < 256U; ++value) {
        constexpr Address addr = 0xD5A3U;
        const auto expected = static_cast<Byte>(value & mapper->read_prg(addr) & 3U);
        mapper->write_prg(addr, static_cast<Byte>(value));
        REQUIRE(mapper->read_chr(0x1234U) == expected_chr(7U, expected, 0x1234U));
    }
}

TEST_CASE("Mapper 041 maps all CHR banks and keeps pattern ROM immutable", "[mapper041]") {
    auto mapper = board();
    for (Byte outer : {0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U}) {
        const auto selected_outer = static_cast<Byte>(outer | 4U);
        mapper->write_prg(0x6000U, selected_outer);
        const auto select_addr =
            static_cast<Address>(0x8000U + ((3U - (selected_outer & 3U)) & 3U));
        for (Byte inner = 0U; inner < 4U; ++inner) {
            mapper->write_prg(select_addr, static_cast<Byte>(inner | 0xFCU));
            REQUIRE(mapper->read_chr(0x0000U) == expected_chr(outer, inner, 0x0000U));
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                const auto address = static_cast<Address>(addr);
                const auto expected = mapper->read_chr(address);
                mapper->write_chr(address, static_cast<Byte>(~expected));
                REQUIRE(mapper->read_chr(address) == expected_chr(outer, inner, address));
            }
        }
    }
}

TEST_CASE("Mapper 041 reset and power-on clear both latches", "[mapper041]") {
    auto mapper = board();
    const auto reset_state = state(*board());
    mapper->write_prg(0x6000U, 0x3FU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->reset();
    REQUIRE(state(*mapper) == reset_state);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0U, 0x8000U));
    REQUIRE(mapper->read_chr(0x0000U) == expected_chr(0U, 0U, 0x0000U));

    mapper->write_prg(0x6000U, 0x3FU);
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->power_on();
    REQUIRE(state(*mapper) == reset_state);
}

TEST_CASE("Mapper 041 state replay is bounded and rejection is atomic", "[mapper041][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x3EU);
    mapper->write_prg(0xD5A3U, 0xA7U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 4U);

    auto restored = board();
    StateReader valid(saved);
    restored->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0xC123U) == mapper->read_prg(0xC123U));
    REQUIRE(restored->read_chr(0x1234U) == mapper->read_chr(0x1234U));
    REQUIRE(restored->mirror_mode() == mapper->mirror_mode());

    const auto before = state(*restored);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        restored->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*restored) == before);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
             {{0U, 1U}, {1U, 0U}, {2U, 0x40U}, {2U, 0x80U}, {3U, 4U}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        StateReader reader(invalid);
        restored->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*restored) == before);
    }
}

TEST_CASE("Mapper 041 accepts only its documented geometry", "[mapper041]") {
    REQUIRE(Caltron041::validate_geometry(header(), 0x40000U, 0x20000U));
    auto legacy = header(false);
    legacy.prg_ram_size = 0x2000U;
    REQUIRE(Caltron041::validate_geometry(legacy, 0x40000U, 0x20000U));
    legacy.prg_nvram_size = 0x2000U;
    REQUIRE_FALSE(Caltron041::validate_geometry(legacy, 0x40000U, 0x20000U));

    for (std::size_t prg_size : {0UZ, 1UZ, 0x20000UZ, 0x3FFFFUZ, 0x40001UZ, 0x80000UZ})
        REQUIRE_FALSE(Caltron041::validate_geometry(header(), prg_size, 0x20000U));
    for (std::size_t chr_size : {0UZ, 1UZ, 0x2000UZ, 0x10000UZ, 0x1FFFFUZ, 0x20001UZ, 0x40000UZ})
        REQUIRE_FALSE(Caltron041::validate_geometry(header(), 0x40000U, chr_size));

    auto invalid = header();
    invalid.submapper = 1U;
    REQUIRE_FALSE(Caltron041::validate_geometry(invalid, 0x40000U, 0x20000U));
    invalid = header();
    invalid.has_battery = true;
    REQUIRE_FALSE(Caltron041::validate_geometry(invalid, 0x40000U, 0x20000U));
    for (std::size_t INesHeader::* field : {&INesHeader::prg_ram_size,
                                            &INesHeader::prg_nvram_size,
                                            &INesHeader::chr_ram_size,
                                            &INesHeader::chr_nvram_size}) {
        invalid = header();
        invalid.*field = 0x2000U;
        REQUIRE_FALSE(Caltron041::validate_geometry(invalid, 0x40000U, 0x20000U));
    }
    for (MirrorMode mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Caltron041::validate_geometry(invalid, 0x40000U, 0x20000U));
    }

    auto mapper = board();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

} // namespace
