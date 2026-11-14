#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper230_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
TEST_CASE("Mapper 230 registers the reset-driven 22-in-1 board", "[mappers][mapper230]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 230U;
    h.is_nes2 = true;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Horizontal;
    auto m = MapperRegistry::instance().create(h, std::vector<Byte>(655360U), {});
    REQUIRE(m);
}

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 230U;
    h.is_nes2 = !legacy;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}
std::unique_ptr<Mapper> board(bool legacy = false) {
    register_builtin_mappers();
    std::vector<Byte> p(655360U);
    for (std::size_t a = 0U; a < p.size(); ++a)
        p[a] = static_cast<Byte>(a / 16384U);
    auto m = MapperRegistry::instance().create(header(legacy), std::move(p), {});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
std::pair<unsigned, unsigned> banks(unsigned value, bool multicart) {
    if (!multicart)
        return {value % 8U, 7U};
    const unsigned page = value % 32U;
    if ((value / 32U % 2U) != 0U)
        return {8U + page, 8U + page};
    return {8U + page / 2U * 2U, 9U + page / 2U * 2U};
}
TEST_CASE("Mapper 230 power-on selects Contra and every soft reset alternates the two chips",
          "[mappers][mapper230]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0xC000U) == 7U);
        for (unsigned reset = 0U; reset < 16U; ++reset) {
            m->reset();
            const bool multicart = reset % 2U == 0U;
            REQUIRE(m->read_prg(0x8000U) == (multicart ? 8U : 0U));
            REQUIRE(m->read_prg(0xC000U) == (multicart ? 9U : 7U));
            REQUIRE(m->mirror_mode() ==
                    (multicart ? MirrorMode::Horizontal : MirrorMode::Vertical));
            m->write_prg(0xFFFFU, 0xFFU);
        }
        m->power_on();
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0xC000U) == 7U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        m->reset();
        REQUIRE(m->read_prg(0x8000U) == 8U);
    }
}
TEST_CASE("Mapper 230 decodes every data byte without crossing physical chip boundaries",
          "[mappers][mapper230]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (bool multicart : {false, true}) {
            if (multicart)
                m->reset();
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(0x8000U, static_cast<Byte>(value));
                const auto [low, high] = banks(value, multicart);
                REQUIRE(m->read_prg(0x8000U) == low);
                REQUIRE(m->read_prg(0xBFFFU) == low);
                REQUIRE(m->read_prg(0xC000U) == high);
                REQUIRE(m->read_prg(0xFFFFU) == high);
                REQUIRE(m->mirror_mode() == (!multicart || value / 64U % 2U != 0U
                                                 ? MirrorMode::Vertical
                                                 : MirrorMode::Horizontal));
                const auto saved = state(*m);
                const Byte ignored = multicart ? 0x80U : 0xF8U;
                m->write_prg(0xFFFFU, static_cast<Byte>(value ^ ignored));
                REQUIRE(state(*m) == saved);
            }
        }
    }
}
TEST_CASE("Mapper 230 mirrors the data latch at every CPU register address",
          "[mappers][mapper230]") {
    auto m = board();
    for (bool multicart : {false, true}) {
        if (multicart)
            m->reset();
        for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
            const auto value = static_cast<Byte>(a ^ (a / 256U));
            m->write_prg(static_cast<Address>(a), value);
            const auto [low, high] = banks(value, multicart);
            REQUIRE(m->read_prg(0x9A00U) == low);
            REQUIRE(m->read_prg(0xFA00U) == high);
            REQUIRE(m->mirror_mode() == (!multicart || value / 64U % 2U != 0U
                                             ? MirrorMode::Vertical
                                             : MirrorMode::Horizontal));
        }
    }
}
TEST_CASE("Mapper 230 CHR RAM is unbanked and survives soft resets but cold power clears it",
          "[mappers][mapper230]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
            m->write_chr(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
        }
        for (unsigned resets = 0U; resets < 4U; ++resets) {
            m->reset();
            m->write_prg(0x8000U, static_cast<Byte>(resets * 32U));
            for (unsigned a = 0U; a < 8192U; ++a) {
                const auto addr = static_cast<Address>(a);
                const auto expected = static_cast<Byte>((a ^ (a / 256U)) + resets);
                REQUIRE(m->read_chr(addr) == expected);
                m->write_chr(addr, static_cast<Byte>(expected + 1U));
                REQUIRE(m->read_chr(addr) == static_cast<Byte>(expected + 1U));
            }
        }
        m->power_on();
        for (unsigned a = 0U; a < 8192U; ++a)
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
    }
}
TEST_CASE("Mapper 230 snapshots retain mode and RAM without turning rejected loads into resets",
          "[mappers][mapper230][state]") {
    for (bool multicart : {false, true}) {
        auto m = board();
        if (multicart)
            m->reset();
        m->write_prg(0x8000U, 0xFFU);
        m->write_chr(0x123U, 0xA7U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 8196U);
        auto copy = board();
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        for (std::size_t n : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 4096UZ, 8195UZ}) {
            StateReader truncated(std::span(saved).first(n));
            m->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*m) == saved);
        }
        for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                 {{0U, 1U}, {1U, 0U}, {2U, 2U}, {3U, static_cast<Byte>(multicart ? 128U : 8U)}}}) {
            auto bad = saved;
            bad[offset] = value;
            bad[4U + 0x123U] = 0U;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        m->reset();
        copy->reset();
        REQUIRE(state(*m) == state(*copy));
        REQUIRE(m->read_prg(0x8000U) == (multicart ? 0U : 8U));
        REQUIRE(m->read_chr(0x123U) == 0xA7U);
    }
}
TEST_CASE("Mapper 230 rejects unsupported geometry and memory populations",
          "[mappers][mapper230]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t p = 655360U, c = 0U;
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
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 16384U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 9U:
            p = 0U;
            break;
        case 10U:
            p = 131072U;
            break;
        case 11U:
            p = 524288U;
            break;
        case 12U:
            --p;
            break;
        case 13U:
            p += 16384U;
            break;
        case 14U:
            c = 8192U;
            break;
        case 15U:
            h = header(true);
            h.has_battery = true;
            break;
        case 16U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    auto m = board();
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    m->clock_cpu();
    m->clock_irq_counter();
    m->clock_audio();
    REQUIRE_FALSE(m->irq_pending());
}
Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper230_program(false).size() < 0x1800U);
    REQUIRE(mapperbus::tests::mapper230_program(true).size() < 0x1800U);
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper230_rom(legacy, vertical));
    REQUIRE(c);
    return std::move(*c);
}
TEST_CASE("Mapper 230 CPU and snapshots preserve reset phase independently from power-on",
          "[mappers][mapper230][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(legacy, vertical), ""));
            e.power_on();
            for (unsigned phase = 0U; phase < 4U; ++phase) {
                const bool multicart = phase % 2U != 0U;
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == (multicart ? 0xD7U : 0xC7U));
                REQUIRE(e.read_cpu(0x14U) == (phase == 0U ? 0U : multicart ? 0x39U : 0xA7U));
                const auto early = e.save_state();
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                const auto start = early.size() - tail.data().size() - 8196U;
                for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                         {{0U, 1U},
                          {1U, 0U},
                          {2U, 2U},
                          {3U, static_cast<Byte>(multicart ? 128U : 8U)}}}) {
                    auto bad = early;
                    bad[start + offset] = value;
                    bad[start + 4U + 0x123U] = 0U;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
                auto truncated = early;
                truncated.resize(start + 8195U);
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(truncated));
                REQUIRE(e.save_state() == before);
                e.reset();
                REQUIRE(e.read_cpu(0x9A00U) == (multicart ? 0U : 8U));
                REQUIRE(e.read_cpu(0xFA00U) == (multicart ? 7U : 9U));
            }
            e.power_on();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 0U);
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xD7U);
            e.power_on();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        }
}
TEST_CASE("Mapper 230 leaves low CPU memory and expansion pins floating", "[mappers][mapper230]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            bus.write(a, 0xFFU);
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(a) == value);
        }
    auto m = board();
    m->reset();
    m->write_prg(0x8000U, 0x7FU);
    const auto saved = state(*m);
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*m) == saved);
}
} // namespace
