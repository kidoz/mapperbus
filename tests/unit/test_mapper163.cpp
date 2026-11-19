#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/nanjing_fc001.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 163U;
    h.is_nes2 = !legacy;
    h.has_battery = true;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.prg_nvram_size = legacy ? 0U : 8192U;
    h.chr_ram_size = 8192U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 32768U);
    return data;
}

std::unique_ptr<NanjingFc001> board(bool legacy = false,
                                    bool vertical = true,
                                    std::size_t prg_size = 2097152U) {
    const auto h = header(legacy, vertical);
    REQUIRE(NanjingFc001::validate_geometry(h, prg_size, 0U));
    return std::make_unique<NanjingFc001>(h, rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const NanjingFc001& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(NanjingFc001& mapper, std::span<const Byte> data) {
    StateReader reader(data);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

Byte swapped(Byte value, Byte mode) {
    if ((mode & 1U) == 0U)
        return value;
    return static_cast<Byte>((value & 0xFCU) | ((value & 1U) << 1U) | ((value & 2U) >> 1U));
}

unsigned expected_bank(std::size_t size, Byte low, Byte high, Byte mode) {
    const auto canonical_mode = static_cast<Byte>(mode & 5U);
    const auto canonical_low = static_cast<Byte>(swapped(low, canonical_mode) & 0x8FU);
    const auto canonical_high = static_cast<Byte>(swapped(high, canonical_mode) & 3U);
    unsigned bank = canonical_low & 15U;
    if ((canonical_mode & 4U) == 0U)
        bank |= 3U;
    if (size == 1048576U)
        bank |= canonical_high != 0U ? 16U : 0U;
    else
        bank |= static_cast<unsigned>(canonical_high) << 4U;
    return bank;
}

TEST_CASE("Mapper 163 validates FC-001 ROM, RAM, battery and mirroring geometry", "[mapper163]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (std::size_t size = 32768U; size <= 2097152U; size *= 2U)
                REQUIRE(NanjingFc001::validate_geometry(header(legacy, vertical), size, 0U));

    for (std::size_t size : {0UZ, 1UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 2097153UZ, 4194304UZ}) {
        REQUIRE_FALSE(NanjingFc001::validate_geometry(header(), size, 0U));
    }
    REQUIRE_FALSE(NanjingFc001::validate_geometry(header(), 32768U, 8192U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        const auto result = NanjingFc001::validate_geometry(h, 32768U, 0U);
        REQUIRE_FALSE(result);
        if (submapper == 1U)
            REQUIRE(result.error().find("$4016/$4017") != std::string::npos);
    }
    for (unsigned kind = 0U; kind < 8U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.has_battery = false;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 0U;
            break;
        case 3U:
            h.prg_nvram_size = 4096U;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 4096U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(NanjingFc001::validate_geometry(h, 32768U, 0U));
    }
    auto legacy = header(true);
    legacy.has_battery = false;
    REQUIRE_FALSE(NanjingFc001::validate_geometry(legacy, 32768U, 0U));
}

TEST_CASE("Mapper 163 decodes the full PRG matrix and wraps only physical ROM", "[mapper163]") {
    constexpr std::array<Byte, 7> modes{0U, 1U, 2U, 4U, 5U, 7U, 0xFBU};
    constexpr std::array<Byte, 7> highs{0U, 1U, 2U, 3U, 4U, 0xFBU, 0xFFU};
    for (std::size_t size = 32768U; size <= 2097152U; size *= 2U) {
        auto m = board(false, true, size);
        REQUIRE(m->read_prg(0x8000U) == 3U % (size / 32768U));
        for (const Byte mode : modes)
            for (const Byte high : highs)
                for (unsigned low = 0U; low < 256U; ++low) {
                    m->write_expansion(0x5300U, mode);
                    m->write_expansion(0x5200U, high);
                    m->write_expansion(0x5000U, static_cast<Byte>(low));
                    const auto expected = static_cast<Byte>(
                        expected_bank(size, static_cast<Byte>(low), high, mode) % (size / 32768U));
                    INFO(size);
                    INFO(mode);
                    INFO(high);
                    INFO(low);
                    REQUIRE(m->read_prg(0x8000U) == expected);
                    REQUIRE(m->read_prg(0xBFFFU) == expected);
                    REQUIRE(m->read_prg(0xC000U) == expected);
                    REQUIRE(m->read_prg(0xFFFFU) == expected);
                }
    }
}

TEST_CASE("Mapper 163 register masks aliases and write-only pages are exact", "[mapper163]") {
    auto actual = board();
    auto expected = board();
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        actual->power_on();
        expected->power_on();
        const auto page = addr & 0xFF00U;
        const bool ordinary = page >= 0x5000U && page <= 0x5300U;
        const bool feedback = (addr & 0xF300U) == 0x5100U;
        REQUIRE(actual->maps_expansion(static_cast<Address>(addr)) == (ordinary || feedback));
        REQUIRE(actual->expansion_read_mask(static_cast<Address>(addr)) == (feedback ? 0x08U : 0U));

        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        actual->write_expansion(static_cast<Address>(addr), value);
        if (ordinary && (page != 0x5100U || (addr & 0xFFU) <= 1U))
            expected->write_expansion(static_cast<Address>(addr), value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->read_expansion(static_cast<Address>(addr)) ==
                expected->read_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 163 feedback latch follows A0/A1 and swapped E without driving other bits",
          "[mapper163]") {
    auto m = board();
    for (Byte mode : {0U, 1U, 4U, 5U})
        for (bool initial : {false, true})
            for (unsigned data = 0U; data < 256U; ++data) {
                m->reset();
                m->write_expansion(0x5300U, mode);
                m->write_expansion(0x5100U, initial ? 4U : 0U);
                m->write_expansion(0x5100U, static_cast<Byte>(data));
                const auto value = swapped(static_cast<Byte>(data), mode);
                const bool latched = (value & 4U) != 0U;
                for (Address page : {0x5100U, 0x5500U, 0x5900U, 0x5D00U})
                    for (Address offset = 0U; offset <= 0xFFU; ++offset) {
                        const auto addr = static_cast<Address>(page | offset);
                        REQUIRE(m->read_expansion(addr) == (!latched ? 0x08U : 0U));
                        REQUIRE(m->expansion_read_mask(addr) == 0x08U);
                    }

                m->reset();
                m->write_expansion(0x5300U, mode);
                m->write_expansion(0x5100U, initial ? 4U : 0U);
                m->write_expansion(0x5101U, static_cast<Byte>(data));
                const bool flipped = (swapped(static_cast<Byte>(data), mode) & 1U) != 0U;
                REQUIRE(m->read_expansion(0x5500U) == ((initial != flipped) ? 0U : 0x08U));
                m->write_expansion(0x5102U, 0xFFU);
                REQUIRE(m->read_expansion(0x5501U) == ((initial != flipped) ? 0U : 0x08U));
            }
}

TEST_CASE("Mapper 163 automatic CHR switch uses A9 from the last A13 rising edge", "[mapper163]") {
    auto m = board();
    for (Address addr = 0U; addr < 8192U; ++addr)
        m->write_chr(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
    m->write_expansion(0x50FFU, 0x80U);
    m->observe_ppu_address(0x2200U);
    for (Address addr = 0U; addr < 8192U; ++addr) {
        const auto mapped = static_cast<Address>((addr & 0x0FFFU) | 0x1000U);
        REQUIRE(m->read_chr(addr) == static_cast<Byte>(mapped ^ (mapped >> 8U)));
    }
    m->observe_ppu_address(0x2000U);
    m->observe_ppu_address(0x2200U);
    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2000U);
    for (Address addr = 0U; addr < 8192U; ++addr) {
        const auto mapped = static_cast<Address>(addr & 0x0FFFU);
        REQUIRE(m->read_chr(addr) == static_cast<Byte>(mapped ^ (mapped >> 8U)));
    }

    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2200U);
    m->write_chr(0x0123U, 0xA7U);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);
    m->write_expansion(0x5000U, 0U);
    REQUIRE(m->read_chr(0x0123U) != 0xA7U);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);
    m->write_chr(0x0123U, 0x39U);
    REQUIRE(m->read_chr(0x0123U) == 0x39U);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);
    m->observe_ppu_address(0U);
    m->observe_ppu_address(0x2200U);
    m->write_expansion(0x5000U, 0x80U);
    REQUIRE(m->read_chr(0x0123U) == 0xA7U);
}

TEST_CASE("Mapper 163 reset clears registers but retains RAM and cold power clears only CHR",
          "[mapper163]") {
    auto m = board();
    REQUIRE(m->battery_ram().size() == 8192U);
    std::array<Byte, 8192> battery{};
    for (std::size_t i = 0U; i < battery.size(); ++i)
        battery[i] = static_cast<Byte>(i * 37U + 11U);
    m->set_battery_ram(battery);
    m->set_battery_ram(std::span(battery).first(8191U));
    REQUIRE(std::ranges::equal(m->battery_ram(), battery));
    m->set_battery_ram(std::vector<Byte>(8193U, 0xA7U));
    REQUIRE(std::ranges::equal(m->battery_ram(), battery));

    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(m->maps_prg(addr));
        REQUIRE(m->read_prg(addr) == battery[addr & 0x1FFFU]);
        m->write_prg(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    REQUIRE_FALSE(m->maps_prg(0x5FFFU));
    m->write_chr(0x1234U, 0xA7U);
    m->write_expansion(0x5000U, 0x8FU);
    m->write_expansion(0x5200U, 3U);
    m->write_expansion(0x5300U, 5U);
    m->write_expansion(0x5100U, 4U);
    m->observe_ppu_address(0x2200U);
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 3U);
    REQUIRE(m->read_expansion(0x5500U) == 0x08U);
    REQUIRE(m->read_chr(0x1234U) == 0xA7U);
    REQUIRE(m->read_prg(0x6123U) == static_cast<Byte>(0x6123U ^ 0x61U));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->power_on();
    REQUIRE(m->read_chr(0x1234U) == 0U);
    REQUIRE(m->read_prg(0x6123U) == static_cast<Byte>(0x6123U ^ 0x61U));
    REQUIRE_FALSE(m->has_expansion_audio());
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 163 snapshots replay bank feedback pin and RAM state", "[mapper163][state]") {
    auto m = board();
    m->write_expansion(0x5300U, 5U);
    m->write_expansion(0x5000U, 0x8AU);
    m->write_expansion(0x5200U, 2U);
    m->write_expansion(0x5100U, 4U);
    m->observe_ppu_address(0x2200U);
    m->write_prg(0x6123U, 0x59U);
    m->write_chr(0x0123U, 0xA7U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 16396U);

    auto copy = board();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8123U) == m->read_prg(0x8123U));
    REQUIRE(copy->read_expansion(0x5500U) == 0U);
    REQUIRE(copy->read_prg(0x6123U) == 0x59U);
    REQUIRE(copy->read_chr(0x0123U) == 0xA7U);
    copy->observe_ppu_address(0x2000U);
    copy->observe_ppu_address(0x2200U);
    REQUIRE(state(*copy) == saved);

    auto horizontal = board(false, false);
    const auto before_horizontal = state(*horizontal);
    StateReader wrong_mirror(saved);
    horizontal->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*horizontal) == before_horizontal);
    auto small = board(false, true, 1048576U);
    const auto before_small = state(*small);
    StateReader wrong_size(saved);
    small->load_state(wrong_size);
    REQUIRE_FALSE(wrong_size.ok());
    REQUIRE(state(*small) == before_small);

    const auto h = header();
    auto different_rom =
        std::make_unique<NanjingFc001>(h, std::vector<Byte>(2097152U, 0xEEU), std::vector<Byte>{});
    load(*different_rom, saved);
    REQUIRE(different_rom->read_prg(0x8123U) == 0xEEU);
}

TEST_CASE("Mapper 163 rejects truncated and invalid states without mutation",
          "[mapper163][state]") {
    auto m = board();
    m->write_expansion(0x5300U, 5U);
    m->write_expansion(0x5000U, 0x8AU);
    m->write_expansion(0x5200U, 2U);
    m->write_expansion(0x5100U, 4U);
    m->observe_ppu_address(0x2200U);
    m->write_prg(0x6123U, 0x59U);
    m->write_chr(0x0123U, 0xA7U);
    const auto saved = state(*m);
    const auto reject = [&](std::span<const Byte> data) {
        StateReader reader(data);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    };
    for (std::size_t length = 0U; length < 12U; ++length)
        reject(std::span(saved).first(length));
    for (std::size_t length : {12UZ, 100UZ, 8203UZ, 16395UZ})
        reject(std::span(saved).first(length));
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(bad);
    }
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 12>{{{4U, 0U},
                                                       {4U, 1U},
                                                       {6U, 0x10U},
                                                       {6U, 0x70U},
                                                       {7U, 4U},
                                                       {7U, 0xFFU},
                                                       {8U, 2U},
                                                       {8U, 0xFAU},
                                                       {9U, 2U},
                                                       {10U, 2U},
                                                       {11U, 2U},
                                                       {11U, 0xFFU}}}) {
        auto bad = saved;
        bad[offset] = value;
        REQUIRE(bad != saved);
        reject(bad);
    }
    load(*m, saved);
    REQUIRE(state(*m) == saved);
}

} // namespace
