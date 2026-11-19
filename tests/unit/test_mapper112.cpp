#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper112.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = false) {
    INesHeader h{};
    h.mapper_number = 112U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

std::vector<Byte> rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Mapper112> board(const INesHeader& h = header(),
                                 std::size_t prg_size = 0x20000U,
                                 std::size_t chr_size = 0x10000U) {
    REQUIRE(Mapper112::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper112>(h, rom(prg_size, 0x2000U), rom(chr_size, 0x400U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper& mapper, Byte index, Byte value) {
    mapper.write_prg(0x8000U, index);
    mapper.write_prg(0xA000U, value);
}

TEST_CASE("Mapper 112 maps two PRG registers and six CHR registers", "[mapper112]") {
    auto m = board();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_prg(0xA000U) == 0U);
    REQUIRE(m->read_prg(0xC000U) == 14U);
    REQUIRE(m->read_prg(0xE000U) == 15U);

    constexpr std::array<Byte, 8> banks{0x03U, 0x05U, 0x3EU, 0x21U, 0x23U, 0x1AU, 0x27U, 0x16U};
    for (Byte index = 0U; index < banks.size(); ++index)
        select_bank(*m, index, banks[index]);

    REQUIRE(m->read_prg(0x8000U) == 0x03U);
    REQUIRE(m->read_prg(0x9FFFU) == 0x03U);
    REQUIRE(m->read_prg(0xA000U) == 0x05U);
    REQUIRE(m->read_prg(0xBFFFU) == 0x05U);
    REQUIRE(m->read_prg(0xC000U) == 14U);
    REQUIRE(m->read_prg(0xDFFFU) == 14U);
    REQUIRE(m->read_prg(0xE000U) == 15U);
    REQUIRE(m->read_prg(0xFFFFU) == 15U);

    REQUIRE(m->read_chr(0x0000U) == 0x3EU);
    REQUIRE(m->read_chr(0x03FFU) == 0x3EU);
    REQUIRE(m->read_chr(0x0400U) == 0x3FU);
    REQUIRE(m->read_chr(0x07FFU) == 0x3FU);
    REQUIRE(m->read_chr(0x0800U) == 0x20U);
    REQUIRE(m->read_chr(0x0FFFU) == 0x21U);
    REQUIRE(m->read_chr(0x1000U) == 0x23U);
    REQUIRE(m->read_chr(0x13FFU) == 0x23U);
    REQUIRE(m->read_chr(0x1400U) == 0x1AU);
    REQUIRE(m->read_chr(0x1800U) == 0x27U);
    REQUIRE(m->read_chr(0x1C00U) == 0x16U);
    REQUIRE(m->read_chr(0x1FFFU) == 0x16U);
}

TEST_CASE("Mapper 112 retains full bank bytes before fitted-ROM wrapping", "[mapper112]") {
    auto m = board(header(), 5U * 0x2000U, 12U * 0x400U);
    for (Byte index = 0U; index < 8U; ++index)
        select_bank(*m, index, 0xFFU);

    REQUIRE(m->read_prg(0x8123U) == 0U);
    REQUIRE(m->read_prg(0xA123U) == 0U);
    REQUIRE(m->read_prg(0xC123U) == 3U);
    REQUIRE(m->read_prg(0xE123U) == 4U);
    REQUIRE(m->read_chr(0x0000U) == 2U);
    REQUIRE(m->read_chr(0x0400U) == 3U);
    REQUIRE(m->read_chr(0x0800U) == 2U);
    REQUIRE(m->read_chr(0x0C00U) == 3U);
    for (Address addr : {0x1000U, 0x1400U, 0x1800U, 0x1C00U})
        REQUIRE(m->read_chr(addr) == 3U);
}

TEST_CASE("Mapper 112 decodes every register alias and ignores unlisted MMC3 ports",
          "[mapper112]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw < 0x8000U; raw += 0x107U) {
        actual->write_prg(static_cast<Address>(raw), 0xFFU);
        expected->write_prg(static_cast<Address>(raw), 0xFFU);
    }
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 37U + raw / 251U);
        Address canonical = 0xC000U;
        switch (addr & 0xE001U) {
        case 0x8000U:
            canonical = 0x8000U;
            break;
        case 0xA000U:
            canonical = 0xA000U;
            break;
        case 0xE000U:
            canonical = 0xE000U;
            break;
        default:
            break;
        }
        actual->write_prg(addr, value);
        expected->write_prg(canonical, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->read_prg(0x8000U) == expected->read_prg(0x8000U));
        REQUIRE(actual->read_prg(0xA000U) == expected->read_prg(0xA000U));
        REQUIRE(actual->read_prg(0xC000U) == expected->read_prg(0xC000U));
        REQUIRE(actual->read_prg(0xE000U) == expected->read_prg(0xE000U));
        for (Address chr : {0x0000U, 0x0800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U})
            REQUIRE(actual->read_chr(chr) == expected->read_chr(chr));
    }
}

TEST_CASE("Mapper 112 nametable control uses zero horizontal and one vertical", "[mapper112]") {
    for (bool vertical : {false, true}) {
        auto m = board(header(false, vertical));
        REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        for (unsigned value = 0U; value < 256U; ++value) {
            m->write_prg(static_cast<Address>(0xE000U + value * 2U), static_cast<Byte>(value));
            REQUIRE(m->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
    }
}

TEST_CASE("Mapper 112 reset clears bank latches and restores header mirroring", "[mapper112]") {
    for (bool vertical : {false, true}) {
        auto m = board(header(false, vertical));
        const auto initial = state(*m);
        REQUIRE(initial.size() == 12U);
        for (Byte index = 0U; index < 8U; ++index)
            select_bank(*m, index, static_cast<Byte>(0x80U + index));
        m->write_prg(0xE000U, vertical ? 0U : 1U);
        REQUIRE(state(*m) != initial);
        m->reset();
        REQUIRE(state(*m) == initial);
        select_bank(*m, 7U, 0xFFU);
        m->write_prg(0xE000U, vertical ? 0U : 1U);
        m->power_on();
        REQUIRE(state(*m) == initial);
    }
}

TEST_CASE("Mapper 112 exposes ROM only and ignores CHR writes and unused clocks", "[mapper112]") {
    auto m = board();
    select_bank(*m, 4U, 0x91U);
    const auto saved = state(*m);
    for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto before = m->read_chr(addr);
        m->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(m->read_chr(addr) == before);
        REQUIRE(m->prg_read_mask(addr) == 0xFFU);
    }
    m->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(m->battery_ram().empty());
    REQUIRE(m->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(m->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->audio_output() == 0.0F);
    REQUIRE(state(*m) == saved);
}

TEST_CASE("Mapper 112 snapshots replay all latches and reject damage atomically",
          "[mapper112][state]") {
    auto m = board(header(false, true));
    for (Byte index = 0U; index < 8U; ++index)
        select_bank(*m, index, static_cast<Byte>(index * 29U + 0xF0U));
    select_bank(*m, 5U, 0xA7U);
    m->write_prg(0xE000U, 0U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 12U);

    auto copy = board(header(false, true));
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == m->read_prg(0x8000U));
    REQUIRE(copy->read_prg(0xA000U) == m->read_prg(0xA000U));
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U)
        REQUIRE(copy->read_chr(addr) == m->read_chr(addr));

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    for (Byte invalid_select : {8U, 9U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[2] = invalid_select;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    for (Byte invalid_mirror : {2U, 3U, 0xFFU}) {
        auto bad = saved;
        bad.back() = invalid_mirror;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }

    auto other_header = board();
    const auto other_before = state(*other_header);
    StateReader mismatched(saved);
    other_header->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*other_header) == other_before);

    for (std::size_t index = 0U; index < 8U; ++index)
        for (unsigned value = 0U; value < 256U; ++value) {
            auto full_byte = saved;
            full_byte[3U + index] = static_cast<Byte>(value);
            StateReader valid(full_byte);
            m->load_state(valid);
            REQUIRE(valid.ok());
            REQUIRE(state(*m) == full_byte);
        }
}

TEST_CASE("Mapper 112 accepts decoder-sized ROMs and rejects unsupported geometry", "[mapper112]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            const auto h = header(legacy, vertical);
            for (const auto [p, c] :
                 std::array<std::pair<std::size_t, std::size_t>, 5>{{{0x8000U, 0x2000U},
                                                                     {0xA000U, 0x3000U},
                                                                     {0x20000U, 0x10000U},
                                                                     {0x1FE000U, 0x3FC00U},
                                                                     {0x200000U, 0x40000U}}})
                REQUIRE(Mapper112::validate_geometry(h, p, c));
        }

    for (auto p : {0U, 0x2000U, 0x4000U, 0x7FFFU, 0x8001U, 0x201000U})
        REQUIRE_FALSE(Mapper112::validate_geometry(header(), p, 0x2000U));
    for (auto c : {0U, 0x400U, 0x1000U, 0x1FFFU, 0x2001U, 0x40400U})
        REQUIRE_FALSE(Mapper112::validate_geometry(header(), 0x8000U, c));

    for (unsigned kind = 0U; kind < 10U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            h = header(true);
            h.has_battery = true;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Mapper112::validate_geometry(h, 0x8000U, 0x2000U));
    }
    auto legacy = header(true);
    legacy.submapper = 15U;
    REQUIRE(Mapper112::validate_geometry(legacy, 0x8000U, 0x2000U));
}

} // namespace
