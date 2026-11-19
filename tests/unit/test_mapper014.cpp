#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "core/mappers/sl1632.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 14U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> pages(std::size_t size, std::size_t page_size) {
    std::vector<Byte> bytes(size);
    for (std::size_t i = 0U; i < size; ++i)
        bytes[i] = static_cast<Byte>(i / page_size);
    return bytes;
}

std::unique_ptr<Sl1632> board(bool legacy = false,
                              bool vertical = true,
                              std::size_t prg_size = 0x80000U,
                              std::size_t chr_size = 0x80000U) {
    return std::make_unique<Sl1632>(
        header(legacy, vertical), pages(prg_size, 8192U), pages(chr_size, 1024U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void mmc3_bank(Mapper& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 014 powers up in MMC3 mode with independent VRC2 defaults", "[mapper014]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(legacy, vertical);
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0xA000U) == 0U);
            REQUIRE(m->read_prg(0xC000U) == 62U);
            REQUIRE(m->read_prg(0xE000U) == 63U);
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(m->battery_ram().empty());
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
            REQUIRE(state(*m).size() == 38U);

            m->write_prg(0xA131U, 0U);
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0xA000U) == 0U);
            REQUIRE(m->read_prg(0xC000U) == 62U);
            REQUIRE(m->read_prg(0xE000U) == 63U);
            for (Address addr : {0x0000U, 0x0400U, 0x1000U, 0x1C00U})
                REQUIRE(m->read_chr(addr) == 0xFFU);

            const auto initial = state(*board(legacy, vertical));
            m->reset();
            REQUIRE(state(*m) == initial);
            m->write_prg(0xA131U, 0x70U);
            m->power_on();
            REQUIRE(state(*m) == initial);
        }
}

TEST_CASE("Mapper 014 supervisor is exact-addressed and drives physical CHR regions",
          "[mapper014]") {
    auto m = board();
    mmc3_bank(*m, 6U, 3U);
    m->write_prg(0xA131U, 0U);
    m->write_prg(0x8000U, 5U);

    for (unsigned data = 0U; data < 256U; ++data) {
        m->write_prg(0xA131U, static_cast<Byte>(data));
        const bool mmc3 = (data & 1U) != 0U;
        const auto bytes = state(*m);
        REQUIRE(bytes[11U] == (data & 0x71U));
        REQUIRE(m->read_prg(0x8000U) == (mmc3 ? 3U : 5U));
        for (unsigned region = 0U; region < 3U; ++region) {
            const unsigned outer = (data >> (4U + region)) & 1U;
            const unsigned inner = mmc3 ? 0U : 255U;
            const auto addr = static_cast<Address>(region * 0x1000U + 0x123U);
            REQUIRE(m->read_chr(addr) == static_cast<Byte>(inner + outer * 256U));
        }
    }

    m->write_prg(0xA131U, 0x71U);
    for (Address addr : {0xA130U, 0xA132U, 0xA031U, 0xB131U, 0x9131U})
        m->write_prg(addr, 0U);
    REQUIRE(state(*m)[11U] == 0x71U);

    m->write_prg(0xA131U, 0xC1U);
    REQUIRE(state(*m)[25U] == 0xC0U);
    m->write_prg(0xA131U, 0x1EU);
    REQUIRE(m->read_prg(0xA000U) == 30U);
    REQUIRE(state(*m)[11U] == 0x10U);
}

TEST_CASE("Mapper 014 MMC3 PRG banking covers all aliases values and both modes", "[mapper014]") {
    auto m = board();
    for (Byte mode : {0U, 0x40U})
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            m->write_prg(0x9FFEU, static_cast<Byte>(6U | mode));
            m->write_prg(0x9FFFU, static_cast<Byte>(raw));
            mmc3_bank(*m, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
            const std::array<unsigned, 4> expected{
                mode == 0U ? raw & 0x3FU : 62U,
                (raw ^ 0x35U) & 0x3FU,
                mode == 0U ? 62U : raw & 0x3FU,
                63U,
            };
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        expected[slot]);
        }
}

TEST_CASE("Mapper 014 MMC3 CHR pairing inversion ROM protection and CIRAM follow MMC3",
          "[mapper014]") {
    auto m = board();
    constexpr std::array<Byte, 6> initial{8U, 12U, 47U, 63U, 100U, 191U};
    for (Byte inversion : {0U, 0x80U})
        for (unsigned reg = 0U; reg < 6U; ++reg)
            for (unsigned value = 0U; value < 256U; ++value) {
                auto regs = initial;
                regs[reg] = static_cast<Byte>(value);
                for (unsigned r = 0U; r < 6U; ++r)
                    mmc3_bank(*m, static_cast<Byte>(r), regs[r], inversion);
                const std::array<unsigned, 8> expected{
                    regs[0] & 0xFEU,
                    (regs[0] & 0xFEU) + 1U,
                    regs[1] & 0xFEU,
                    (regs[1] & 0xFEU) + 1U,
                    regs[2],
                    regs[3],
                    regs[4],
                    regs[5],
                };
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                    const auto expected_byte = expected[slot ^ (inversion != 0U ? 4U : 0U)];
                    REQUIRE(m->read_chr(addr) == expected_byte);
                    m->write_chr(addr, static_cast<Byte>(expected_byte ^ 0xFFU));
                    REQUIRE(m->read_chr(addr) == expected_byte);
                }
            }

    m->write_prg(0xBFFE, 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0xA000U, 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    for (Address addr : {0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU}) {
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE(m->prg_read_mask(addr) == 0xFFU);
        m->write_prg(addr, 1U);
        REQUIRE(m->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 014 VRC2b PRG registers accept address mirrors and mask bank data",
          "[mapper014]") {
    auto m = board();
    m->write_prg(0xA131U, 0U);
    for (unsigned port = 0U; port < 4U; ++port)
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            m->write_prg(static_cast<Address>(0x8F00U + port), static_cast<Byte>(raw));
            REQUIRE(m->read_prg(0x8000U) == (raw & 31U));
            m->write_prg(static_cast<Address>(0xAFFCU | port), static_cast<Byte>(raw ^ 0xA5U));
            REQUIRE(m->read_prg(0xA000U) == ((raw ^ 0xA5U) & 31U));
            REQUIRE(m->read_prg(0xC000U) == 62U);
            REQUIRE(m->read_prg(0xE000U) == 63U);
        }
}

TEST_CASE("Mapper 014 VRC2b CHR registers cover every port mirror and nibble", "[mapper014]") {
    auto m = board();
    m->write_prg(0xA131U, 0U);
    for (unsigned bank = 0U; bank < 8U; ++bank)
        for (unsigned low = 0U; low < 16U; ++low)
            for (unsigned high = 0U; high < 16U; ++high) {
                const Address base = static_cast<Address>(0xB000U + (bank / 2U) * 0x1000U);
                const unsigned low_port = (bank & 1U) * 2U;
                m->write_prg(static_cast<Address>(base + 0x0F00U + low_port),
                             static_cast<Byte>(0xA0U | low));
                m->write_prg(static_cast<Address>(base + 0x0FF0U + low_port + 1U),
                             static_cast<Byte>(0x50U | high));
                const auto addr = static_cast<Address>(bank * 1024U + 0x123U);
                const auto expected = static_cast<Byte>((high << 4U) | low);
                REQUIRE(m->read_chr(addr) == expected);
                m->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                REQUIRE(m->read_chr(addr) == expected);
            }

    for (Address addr : {0x9000U, 0x9001U, 0x9002U, 0x9003U, 0x9FFCU})
        for (Byte data : {0U, 1U, 2U, 3U}) {
            m->write_prg(addr, data);
            REQUIRE(m->mirror_mode() ==
                    ((data & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
}

TEST_CASE("Mapper 014 VRC2 microwire latch is the only low CPU mapping", "[mapper014]") {
    auto m = board();
    m->write_prg(0xA131U, 0U);
    for (Address addr : {0x6000U, 0x6100U, 0x6FFFU}) {
        REQUIRE(m->maps_prg(addr));
        REQUIRE(m->prg_read_mask(addr) == 1U);
        for (Byte data : {0U, 1U, 2U, 0xFFU}) {
            m->write_prg(addr, data);
            REQUIRE(m->read_prg(addr) == (data & 1U));
        }
    }
    for (Address addr : {0x7000U, 0x7FFFU}) {
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE(m->prg_read_mask(addr) == 0xFFU);
        m->write_prg(addr, 1U);
        REQUIRE(m->read_prg(addr) == 0U);
    }
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU})
        REQUIRE_FALSE(m->maps_expansion(addr));
}

TEST_CASE("Mapper 014 retains independent mode state and exposes only MMC3 IRQs",
          "[mapper014][irq]") {
    auto m = board();
    mmc3_bank(*m, 6U, 9U);
    mmc3_bank(*m, 2U, 0x44U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xDFFE, 2U);
    m->write_prg(0xDFFFU, 0U);
    m->write_prg(0xFFFFU, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());

    m->write_prg(0xA131U, 0U);
    m->write_prg(0x8000U, 7U);
    m->write_prg(0xA000U, 11U);
    m->write_prg(0xB000U, 3U);
    m->write_prg(0xB001U, 2U);
    m->write_prg(0x9000U, 1U);
    m->write_prg(0x6000U, 1U);
    REQUIRE_FALSE(m->irq_pending());
    m->acknowledge_irq();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());

    m->write_prg(0xA131U, 1U);
    REQUIRE(m->irq_pending());
    REQUIRE(m->read_prg(0x8000U) == 9U);
    REQUIRE(m->read_chr(0x1000U) == 0x44U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());

    m->write_prg(0xA131U, 0U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
    REQUIRE(m->read_prg(0xA000U) == 0U);
    REQUIRE(m->read_chr(0U) == 0x23U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(m->read_prg(0x6000U) == 1U);
    m->write_prg(0xF002U, 0xFFU);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());

    m->write_prg(0xA131U, 1U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xFFFEU, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 014 state replays both ASICs and rejects malformed input atomically",
          "[mapper014][state]") {
    auto m = board();
    auto copy = board();
    mmc3_bank(*m, 6U, 9U, 0xC0U);
    mmc3_bank(*m, 2U, 0x44U, 0xC0U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->write_prg(0xA131U, 0U);
    m->write_prg(0x8000U, 7U);
    m->write_prg(0xA000U, 11U);
    m->write_prg(0xB000U, 3U);
    m->write_prg(0xB001U, 2U);
    m->write_prg(0x9000U, 1U);
    m->write_prg(0x6000U, 1U);
    m->write_prg(0xA131U, 0x81U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 38U);
    REQUIRE(saved[0U] == 0U);
    REQUIRE(saved[1U] == 0x0EU);

    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(copy->irq_pending());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == 62U);
    REQUIRE(copy->read_chr(0x0000U) == 0x44U);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader short_state(std::span(saved).first(size));
        m->load_state(short_state);
        REQUIRE_FALSE(short_state.ok());
        REQUIRE(state(*m) == saved);
    }

    const std::array<std::pair<std::size_t, Byte>, 17> invalid{{
        {0U, 1U},
        {1U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 0U},
        {11U, 0x80U},
        {12U, 0x08U},
        {19U, 0x40U},
        {20U, 0xC0U},
        {21U, 2U},
        {24U, 8U},
        {24U, 4U},
        {25U, 1U},
        {26U, 32U},
        {27U, 0xFFU},
        {36U, 4U},
        {37U, 2U},
    }};
    for (const auto [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader bad_state(bad);
        m->load_state(bad_state);
        REQUIRE_FALSE(bad_state.ok());
        REQUIRE(state(*m) == saved);
    }

    auto other = board(false, true, 0x40000U);
    const auto before = state(*other);
    StateReader wrong_geometry(saved);
    other->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other) == before);
}

TEST_CASE("Mapper 014 validates ROM bounds and ROM-only metadata", "[mapper014]") {
    for (bool legacy : {false, true}) {
        const auto h = header(legacy);
        for (std::size_t prg : {0x8000UZ, 0x10000UZ, 0x20000UZ, 0x40000UZ, 0x80000UZ})
            for (std::size_t chr :
                 {0x2000UZ, 0x4000UZ, 0x8000UZ, 0x10000UZ, 0x20000UZ, 0x40000UZ, 0x80000UZ})
                REQUIRE(Sl1632::validate_geometry(h, prg, chr));
    }

    for (std::size_t prg : {0UZ, 0x4000UZ, 0x6000UZ, 0x18000UZ, 0x100000UZ})
        REQUIRE_FALSE(Sl1632::validate_geometry(header(), prg, 0x80000U));
    for (std::size_t chr : {0UZ, 0x400UZ, 0x3000UZ, 0x18000UZ, 0x100000UZ})
        REQUIRE_FALSE(Sl1632::validate_geometry(header(), 0x80000U, chr));

    auto wrong_id = header();
    wrong_id.mapper_number = 116U;
    REQUIRE_FALSE(Sl1632::validate_geometry(wrong_id, 0x80000U, 0x80000U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Sl1632::validate_geometry(h, 0x80000U, 0x80000U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        REQUIRE_FALSE(Sl1632::validate_geometry(h, 0x80000U, 0x80000U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(Sl1632::validate_geometry(h, 0x80000U, 0x80000U));
    }
    auto legacy_battery = header(true);
    legacy_battery.has_battery = true;
    REQUIRE_FALSE(Sl1632::validate_geometry(legacy_battery, 0x80000U, 0x80000U));
}

} // namespace
