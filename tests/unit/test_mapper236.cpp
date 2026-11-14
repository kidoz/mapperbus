#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper236_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::array<std::size_t, 3> kPrg{131072U, 262144U, 524288U};
constexpr std::array<std::size_t, 3> kChr{65536U, 131072U, 0U};
INesHeader header(unsigned population, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 236U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.chr_ram_size = population == 2U ? 8192U : 0U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
Byte pattern(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 7U + offset / 16U * 3U + offset % 16U);
}
std::vector<Byte> prg(unsigned population) {
    std::vector<Byte> bytes(kPrg[population]);
    for (std::size_t a = 0U; a < bytes.size(); ++a)
        bytes[a] = pattern(static_cast<unsigned>(a / 16384U), static_cast<unsigned>(a % 16384U));
    return bytes;
}
std::vector<Byte> chr(unsigned population) {
    std::vector<Byte> bytes(kChr[population]);
    for (std::size_t a = 0U; a < bytes.size(); ++a)
        bytes[a] = static_cast<Byte>(a / 8192U);
    return bytes;
}
std::unique_ptr<Mapper> board(unsigned population = 0U, Byte pads = 0U, bool legacy = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(
        header(population, legacy), prg(population), chr(population));
    REQUIRE(m);
    REQUIRE((*m)->configure_board_jumpers(pads));
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}
// Independent truth table in 16 KiB page numbers, preserving each 128 KiB block.
unsigned page(unsigned population, unsigned lower, unsigned upper, bool high) {
    const unsigned block = population == 2U ? (lower % 8U) * 8U : upper / 8U % 2U * 8U;
    const unsigned inner = upper % 8U;
    unsigned selected = 0U;
    switch (upper / 16U % 4U) {
    case 0U:
    case 1U:
        selected = high ? 7U : inner;
        break;
    case 2U:
        selected = inner / 2U * 2U + static_cast<unsigned>(high);
        break;
    default:
        selected = inner;
        break;
    }
    return (block + selected) % static_cast<unsigned>(kPrg[population] / 16384U);
}
TEST_CASE("Mapper 236 registers every documented Realtec ROM and RAM population", "[mapper236]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (bool legacy : {false, true}) {
            auto m = board(population, 0U, legacy);
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(m->read_prg(0x8000U) == pattern(0U, 0U));
            REQUIRE(m->read_prg(0xC000U) == pattern(7U, 0U));
            REQUIRE(state(*m).size() == (population == 2U ? 8196U : 4U));
            REQUIRE(m->battery_ram().empty());
            REQUIRE_FALSE(m->has_expansion_audio());
            m->clock_cpu();
            m->clock_irq_counter();
            m->clock_audio();
            REQUIRE_FALSE(m->irq_pending());
        }
}
TEST_CASE("Mapper 236 combines both address latches in all PRG modes and physical ROM sizes",
          "[mapper236]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(population, 11U);
        for (unsigned lower = 0U; lower < 64U; ++lower)
            for (unsigned upper = 0U; upper < 64U; ++upper) {
                m->write_prg(static_cast<Address>(0x8000U + lower), 0xFFU);
                m->write_prg(static_cast<Address>(0xC000U + upper), 0U);
                for (Address a : {0x8000U, 0x9A6FU, 0xBFFFU, 0xC000U, 0xFA6FU, 0xFFFFU}) {
                    const unsigned offset =
                        upper / 16U % 4U == 1U ? (a % 16384U / 16U * 16U + 11U) : a % 16384U;
                    REQUIRE(m->read_prg(a) ==
                            pattern(page(population, lower, upper, a >= 0xC000U), offset));
                    REQUIRE(m->prg_read_mask(a) == 0xFFU);
                }
                REQUIRE(m->mirror_mode() ==
                        (lower / 32U % 2U != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
                if (population != 2U)
                    REQUIRE(m->read_chr(0x123U) ==
                            lower % static_cast<unsigned>(kChr[population] / 8192U));
            }
    }
}
TEST_CASE("Mapper 236 solder pads replace ROM address pins rather than returned data",
          "[mapper236]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(population);
        m->write_prg(0x8007U, 0U);
        for (unsigned pads = 0U; pads < 16U; ++pads) {
            REQUIRE(m->configure_board_profile("menu-" + std::to_string(pads)));
            for (unsigned mode = 0U; mode < 4U; ++mode)
                for (unsigned bank = 0U; bank < 16U; ++bank) {
                    const auto upper = mode * 16U + bank;
                    m->write_prg(static_cast<Address>(0xC000U + upper), 0U);
                    for (unsigned block : {0U, 0x1A60U, 0x3FF0U})
                        for (unsigned nibble = 0U; nibble < 16U; ++nibble)
                            for (bool high : {false, true}) {
                                const auto addr = static_cast<Address>((high ? 0xC000U : 0x8000U) +
                                                                       block + nibble);
                                REQUIRE(m->read_prg(addr) ==
                                        pattern(page(population, 7U, upper, high),
                                                block + (mode == 1U ? pads : nibble)));
                            }
                }
        }
        const auto saved = state(*m);
        for (unsigned invalid = 16U; invalid < 256U; ++invalid) {
            REQUIRE_FALSE(m->configure_board_jumpers(static_cast<Byte>(invalid)));
            REQUIRE(state(*m) == saved);
        }
        for (std::string_view bad : {"",
                                     "unknown",
                                     "menu-",
                                     "menu--1",
                                     "menu-+1",
                                     "menu-16",
                                     "menu-1x",
                                     "menu-42949672960"}) {
            REQUIRE_FALSE(m->configure_board_profile(bad));
            REQUIRE(state(*m) == saved);
        }
    }
}
TEST_CASE("Mapper 236 decodes the full register space and ignores data and unconnected pins",
          "[mapper236]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(population, 6U);
        unsigned lower = 0U, upper = 0U;
        for (unsigned a = 0U; a <= 0xFFFFU; ++a) {
            m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
            if (a >= 0xC000U)
                upper = a % 64U;
            else if (a >= 0x8000U)
                lower = a % 64U;
            const unsigned offset = upper / 16U % 4U == 1U ? 0x1A66U : 0x1A6FU;
            REQUIRE(m->read_prg(0x9A6FU) == pattern(page(population, lower, upper, false), offset));
            REQUIRE(m->read_prg(0xDA6FU) == pattern(page(population, lower, upper, true), offset));
            REQUIRE(m->mirror_mode() ==
                    (lower / 32U % 2U != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        for (Address a : {0x8000U, 0x802FU, 0xC000U, 0xC03FU}) {
            m->write_prg(a, 0U);
            const auto saved = state(*m);
            const auto low = m->read_prg(0x9A6FU), high = m->read_prg(0xDA6FU);
            const auto mirror = m->mirror_mode();
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(a, static_cast<Byte>(value));
                REQUIRE(m->read_prg(0x9A6FU) == low);
                REQUIRE(m->read_prg(0xDA6FU) == high);
                REQUIRE(m->mirror_mode() == mirror);
            }
            REQUIRE(state(*m) == saved);
        }
    }
}
TEST_CASE(
    "Mapper 236 CHR ROM is banked read-only and CHR RAM is unbanked retained and cold-cleared",
    "[mapper236]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(population);
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            m->write_prg(static_cast<Address>(0x8000U + bank), 0U);
            for (unsigned a = 0U; a < 8192U; ++a) {
                const auto expected = population == 2U
                                          ? static_cast<Byte>(bank)
                                          : static_cast<Byte>(bank % (kChr[population] / 8192U));
                REQUIRE(
                    m->read_chr(static_cast<Address>(a)) ==
                    (population == 2U ? static_cast<Byte>(bank == 0U ? 0U : bank - 1U) : expected));
                m->write_chr(static_cast<Address>(a),
                             population == 2U ? expected : static_cast<Byte>(expected ^ 0xFFU));
                REQUIRE(m->read_chr(static_cast<Address>(a)) == expected);
            }
        }
        m->reset();
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(m->read_chr(0x123U) == (population == 2U ? 15U : 0U));
        m->power_on();
        REQUIRE(m->read_chr(0x123U) == 0U);
    }
}
TEST_CASE("Mapper 236 states bind geometry and pads and reject malformed latches before commit",
          "[mapper236][state]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (Byte pads : {0U, 7U, 15U}) {
            auto m = board(population, pads);
            m->write_prg(0x802FU, 0U);
            m->write_prg(0xC03FU, 0U);
            m->write_chr(0x123U, 0xA7U);
            const auto saved = state(*m);
            auto copy = board(population, pads);
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*copy) == saved);
            for (std::size_t n : {0UZ, 1UZ, 2UZ, 3UZ, saved.size() - 1U}) {
                StateReader truncated(std::span(saved).first(n));
                m->load_state(truncated);
                REQUIRE_FALSE(truncated.ok());
                REQUIRE(state(*m) == saved);
            }
            for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                     {{0U, 0xFFU}, {1U, 0U}, {2U, 0xFFU}, {3U, 0xFFU}}}) {
                auto bad = saved;
                bad[offset] = value;
                StateReader invalid(bad);
                m->load_state(invalid);
                REQUIRE_FALSE(invalid.ok());
                REQUIRE(state(*m) == saved);
            }
            auto other_pad = board(population, static_cast<Byte>(pads ^ 1U));
            StateReader wrong_pad(saved);
            other_pad->load_state(wrong_pad);
            REQUIRE_FALSE(wrong_pad.ok());
            auto other_geometry = board((population + 1U) % 3U, pads);
            const auto before = state(*other_geometry);
            StateReader wrong_geometry(saved);
            other_geometry->load_state(wrong_geometry);
            REQUIRE_FALSE(wrong_geometry.ok());
            REQUIRE(state(*other_geometry) == before);
            m->reset();
            REQUIRE(state(*m)[0U] == saved[0U]);
        }
}
TEST_CASE("Mapper 236 rejects unsupported geometry memory and submappers", "[mapper236]") {
    register_builtin_mappers();
    for (unsigned population = 0U; population < 3U; ++population)
        for (unsigned kind = 0U; kind < 18U; ++kind) {
            auto h = header(population);
            auto p = kPrg[population], c = kChr[population];
            switch (kind) {
            case 0U:
                h.submapper = 1U;
                break;
            case 1U:
                h.submapper = 15U;
                break;
            case 2U:
                h.has_battery = true;
                break;
            case 3U:
                h.prg_ram_size = 8192U;
                break;
            case 4U:
                h.prg_nvram_size = 8192U;
                break;
            case 5U:
                h.chr_nvram_size = 8192U;
                break;
            case 6U:
                h.chr_ram_size = population == 2U ? 0U : 8192U;
                break;
            case 7U:
                h.chr_ram_size = 4096U;
                break;
            case 8U:
                h.chr_ram_size = 16384U;
                break;
            case 9U:
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            case 10U:
                h.mirror_mode = MirrorMode::SingleLower;
                break;
            case 11U:
                p = 0U;
                break;
            case 12U:
                p += 16384U;
                break;
            case 13U:
                p = 1048576U;
                break;
            case 14U:
                c = population == 2U ? 8192U : 0U;
                break;
            case 15U:
                c = population == 2U ? 65536U : kChr[population] / 2U;
                break;
            case 16U:
                h.is_nes2 = false;
                h.has_battery = true;
                break;
            case 17U:
                h.is_nes2 = false;
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            default:
                break;
            }
            REQUIRE_FALSE(
                MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
        }
}

Cartridge cartridge(unsigned population, Byte pads = 0U, bool legacy = false) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper236_program(population, pads).size() < 1536U);
    REQUIRE(mapperbus::tests::mapper236_boot(population, pads).size() < 0x1800U);
    auto c = Cartridge::from_data(
        mapperbus::tests::make_mapper236_rom(population, pads, legacy, (pads & 1U) != 0U));
    REQUIRE(c);
    REQUIRE(c->configure_board_profile("menu-" + std::to_string(pads)));
    return std::move(*c);
}
std::size_t payload_start(const std::vector<Byte>& bytes, unsigned population) {
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    return bytes.size() - tail.data().size() - (population == 2U ? 8196U : 4U);
}
TEST_CASE("Mapper 236 RAM firmware verifies pad address substitution banks PPU reset and replay",
          "[mapper236][state]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (unsigned pads = 0U; pads < 16U; ++pads)
            for (bool legacy : {false, true}) {
                CAPTURE(population, pads, legacy);
                Emulator e;
                REQUIRE(
                    e.load_cartridge(cartridge(population, static_cast<Byte>(pads), legacy), ""));
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                const auto early = e.save_state();
                const auto start = payload_start(early, population);
                REQUIRE(early[start] == (pads * 16U + population));
                REQUIRE(early[start + 1U] == 0xECU);
                REQUIRE(early[start + 2U] == (population == 2U ? 0x27U : 0x2FU));
                REQUIRE(early[start + 3U] ==
                        ((population == 2U ? 7U : 15U) + ((pads & 1U) != 0U ? 0x10U : 0x30U)));
                if (population == 2U)
                    REQUIRE(early[start + 4U + 0x123U] == 0x5AU);
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                         {{0U, 0xFFU}, {1U, 0U}, {2U, 0xFFU}, {3U, 0xFFU}}}) {
                    auto bad = early;
                    bad[start + offset] = value;
                    if (population == 2U)
                        bad[start + 4U + 0x123U] = 0U;
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == future);
                }
                auto truncated = early;
                truncated.resize(start + (population == 2U ? 8195U : 3U));
                REQUIRE_FALSE(e.load_state(truncated));
                REQUIRE(e.save_state() == future);
                e.reset();
                REQUIRE(e.read_cpu(0x9A80U) == pattern(0U, 0x1A80U));
                REQUIRE(e.read_cpu(0xFA80U) == pattern(7U, 0x3A80U));
                const auto reset = e.save_state();
                REQUIRE(reset[start] == early[start]);
                REQUIRE(reset[start + 2U] == 0U);
                REQUIRE(reset[start + 3U] == 0U);
                if (population == 2U)
                    REQUIRE(reset[start + 4U + 0x123U] == 0x5AU);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == (population == 2U ? 0x5AU : 0U));
                e.power_on();
                if (population == 2U)
                    REQUIRE(e.save_state()[start + 4U + 0x123U] == 0U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
            }
}
void paddr(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void pwrite(Ppu& p, Address a, Byte v) {
    paddr(p, a);
    p.write_register(7U, v);
}
Byte pread(Ppu& p, Address a) {
    paddr(p, a);
    (void)p.read_register(7U);
    paddr(p, a);
    return p.read_register(7U);
}
TEST_CASE("Mapper 236 bus and PPU route pad ROM reads open CPU space and both CIRAM wirings",
          "[mapper236][bus][ppu]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto c = cartridge(population, 11U);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        Ppu p;
        p.connect_cartridge(&c);
        p.reset();
        for (unsigned value = 0U; value < 256U; ++value)
            for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
                REQUIRE_FALSE(c.maps_prg(a));
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(a) == value);
            }
        bus.write(0x8000U, 0U);
        pwrite(p, 0x2000U, 0x19U);
        pwrite(p, 0x2400U, 0x27U);
        bus.write(0x8020U, 0U);
        REQUIRE(pread(p, 0x2400U) == 0x19U);
        REQUIRE(pread(p, 0x2800U) == 0x27U);
        bus.write(0x8000U, 0U);
        REQUIRE(pread(p, 0x2400U) == 0x27U);
        REQUIRE(pread(p, 0x2800U) == 0x19U);
        bus.write(0xC013U, 0U);
        for (unsigned nibble = 0U; nibble < 16U; ++nibble) {
            REQUIRE(bus.read(static_cast<Address>(0x9A80U + nibble)) == pattern(3U, 0x1A8BU));
            REQUIRE(bus.read(static_cast<Address>(0xFA80U + nibble)) == pattern(7U, 0x3A8BU));
        }
        pwrite(p, 0x123U, 0xA7U);
        REQUIRE(pread(p, 0x123U) == (population == 2U ? 0xA7U : 0U));
        bus.write(0x800FU, 0U);
        REQUIRE(pread(p, 0x123U) == (population == 2U ? 0xA7U : population == 0U ? 7U : 15U));
    }
}
TEST_CASE("Mapper 236 machine snapshots reject different physical pads with identical ROM bytes",
          "[mapper236][state]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        const auto bytes = mapperbus::tests::make_mapper236_rom(population);
        auto ca = Cartridge::from_data(bytes), cb = Cartridge::from_data(bytes);
        REQUIRE(ca);
        REQUIRE(cb);
        REQUIRE(cb->configure_board_jumpers(1U));
        Emulator a, b;
        REQUIRE(a.load_cartridge(std::move(*ca), ""));
        REQUIRE(b.load_cartridge(std::move(*cb), ""));
        a.power_on();
        b.power_on();
        a.step_frame();
        REQUIRE(a.content_hash() == b.content_hash());
        const auto before = b.save_state();
        REQUIRE_FALSE(b.load_state(a.save_state()));
        REQUIRE(b.save_state() == before);
    }
}
} // namespace
