#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper235_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
TEST_CASE("Mapper 235 registers the Golden Game board", "[mappers][mapper235]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 235U;
    h.is_nes2 = true;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Vertical;
    auto m = MapperRegistry::instance().create(h, std::vector<Byte>(2097152U), {});
    REQUIRE(m);
}

constexpr std::array<Byte, 8> kMasks{1U, 3U, 5U, 7U, 9U, 11U, 13U, 15U};
constexpr std::array<std::string_view, 8> kProfiles{"slots-0",
                                                    "slots-0-1",
                                                    "slots-0-2",
                                                    "slots-0-1-2",
                                                    "slots-0-3",
                                                    "slots-0-1-3",
                                                    "slots-0-2-3",
                                                    "slots-0-1-2-3"};
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 235U;
    h.is_nes2 = !legacy;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> rom(unsigned chips) {
    std::vector<Byte> p(chips * 1048576U);
    for (std::size_t a = 0U; a < p.size(); ++a)
        p[a] = static_cast<Byte>(a / 16384U);
    return p;
}
std::unique_ptr<Mapper> board(unsigned profile = 2U, bool legacy = false) {
    register_builtin_mappers();
    auto m =
        MapperRegistry::instance().create(header(legacy), rom(std::popcount(kMasks[profile])), {});
    REQUIRE(m);
    REQUIRE((*m)->configure_board_profile(kProfiles[profile]));
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
Address command(unsigned socket,
                unsigned bank,
                bool small = false,
                bool upper = false,
                bool vertical = false,
                bool single = false) {
    return static_cast<Address>(0x8000U + socket * 256U + bank + (small ? 2048U : 0U) +
                                (upper ? 4096U : 0U) + (vertical ? 8192U : 0U) +
                                (single ? 1024U : 0U));
}
unsigned packed(unsigned mask, unsigned socket) {
    unsigned index = 0U;
    for (unsigned i = 0U; i < socket; ++i)
        if ((mask & (1U << i)) != 0U)
            ++index;
    return index;
}
TEST_CASE("Mapper 235 supports every socket population without wrapping absent sockets into ROM",
          "[mapper235]") {
    for (unsigned profile = 0U; profile < 8U; ++profile) {
        auto m = board(profile);
        for (unsigned socket = 0U; socket < 4U; ++socket)
            for (unsigned bank = 0U; bank < 32U; ++bank)
                for (bool small : {false, true})
                    for (bool upper : {false, true})
                        for (bool vertical : {false, true})
                            for (bool single : {false, true}) {
                                const bool present = (kMasks[profile] & (1U << socket)) != 0U;
                                m->write_prg(command(socket, bank, small, upper, vertical, single),
                                             0xFFU);
                                const unsigned base =
                                    packed(kMasks[profile], socket) * 64U + bank * 2U;
                                const unsigned low = base + (small && upper ? 1U : 0U);
                                const unsigned high = base + (small ? (upper ? 1U : 0U) : 1U);
                                REQUIRE(m->prg_read_mask(0x8000U) == (present ? 0xFFU : 0U));
                                REQUIRE(m->read_prg(0x8000U) == (present ? low : 0U));
                                REQUIRE(m->read_prg(0xBFFFU) == (present ? low : 0U));
                                REQUIRE(m->read_prg(0xC000U) == (present ? high : 0U));
                                REQUIRE(m->read_prg(0xFFFFU) == (present ? high : 0U));
                                REQUIRE(m->mirror_mode() == (single     ? MirrorMode::SingleLower
                                                             : vertical ? MirrorMode::Vertical
                                                                        : MirrorMode::Horizontal));
                            }
    }
}
TEST_CASE("Mapper 235 defaults follow the documented packed layouts and profiles validate ROM size",
          "[mapper235]") {
    register_builtin_mappers();
    constexpr std::array<Byte, 4> defaults{1U, 5U, 7U, 15U};
    for (unsigned chips = 1U; chips <= 4U; ++chips) {
        auto m = MapperRegistry::instance().create(header(), rom(chips), {});
        REQUIRE(m);
        StateWriter writer;
        (*m)->save_state(writer);
        StateReader reader(writer.data());
        REQUIRE(reader.read<uint16_t>() == 0xEB00U + defaults[chips - 1U]);
        (*m)->write_chr(0x123U, 0xA7U);
        (*m)->write_prg(0xBF1FU, 0U);
        const auto saved = state(**m);
        for (unsigned profile = 0U; profile < 8U; ++profile)
            if (static_cast<unsigned>(std::popcount(kMasks[profile])) != chips) {
                REQUIRE_FALSE((*m)->configure_board_profile(kProfiles[profile]));
                REQUIRE(state(**m) == saved);
            }
        for (std::string_view bad :
             {"", "unknown", "slots-2", "slots-0-0", "slots-02", "slots-0-4", "slots-1-0"}) {
            REQUIRE_FALSE((*m)->configure_board_profile(bad));
            REQUIRE(state(**m) == saved);
        }
    }
}
TEST_CASE("Mapper 235 ignores data and unused pins at every CPU register address", "[mapper235]") {
    auto m = board(5U); // Physical sockets 0, 1 and 3; socket 2 is absent.
    constexpr std::array<unsigned, 4> rank{0U, 1U, 0U, 2U};
    for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
        const unsigned socket = a / 256U % 4U;
        const unsigned bank = a % 32U;
        const bool small = a / 2048U % 2U != 0U, upper = a / 4096U % 2U != 0U;
        const unsigned base = rank[socket] * 64U + bank * 2U;
        const unsigned low = base + (small && upper ? 1U : 0U),
                       high = base + (small ? (upper ? 1U : 0U) : 1U);
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
        REQUIRE(m->prg_read_mask(0x8000U) == (socket == 2U ? 0U : 0xFFU));
        REQUIRE(m->read_prg(0x9A00U) == (socket == 2U ? 0U : low));
        REQUIRE(m->read_prg(0xFA00U) == (socket == 2U ? 0U : high));
        REQUIRE(m->mirror_mode() == (a / 1024U % 2U != 0U   ? MirrorMode::SingleLower
                                     : a / 8192U % 2U != 0U ? MirrorMode::Vertical
                                                            : MirrorMode::Horizontal));
    }
    for (Address a : {0x8000U, 0x831FU, 0xBC1FU, 0xBF1FU}) {
        m->write_prg(a, 0U);
        const auto saved = state(*m);
        const auto low = m->read_prg(0x8000U), high = m->read_prg(0xC000U);
        const auto mask = m->prg_read_mask(0x8000U);
        const auto mirror = m->mirror_mode();
        for (unsigned data = 0U; data < 256U; ++data) {
            m->write_prg(a, static_cast<Byte>(data));
            REQUIRE(m->read_prg(0x8000U) == low);
            REQUIRE(m->read_prg(0xC000U) == high);
            REQUIRE(m->prg_read_mask(0x8000U) == mask);
            REQUIRE(m->mirror_mode() == mirror);
        }
        REQUIRE(state(*m) == saved);
    }
}
TEST_CASE("Mapper 235 CHR RAM remains writable in every socket mode and survives soft reset",
          "[mapper235]") {
    for (unsigned profile = 0U; profile < 8U; ++profile) {
        auto m = board(profile);
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
            m->write_chr(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
        }
        for (unsigned socket = 0U; socket < 4U; ++socket) {
            m->write_prg(command(socket, 31U, true, true, true, true), 0U);
            for (unsigned a = 0U; a < 8192U; ++a) {
                const auto addr = static_cast<Address>(a);
                const auto expected = static_cast<Byte>((a ^ (a / 256U)) + socket);
                REQUIRE(m->read_chr(addr) == expected);
                m->write_chr(addr, static_cast<Byte>(expected + 1U));
            }
        }
        m->reset();
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0xC000U) == 1U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        for (unsigned a = 0U; a < 8192U; ++a)
            REQUIRE(m->read_chr(static_cast<Address>(a)) ==
                    static_cast<Byte>((a ^ (a / 256U)) + 4U));
        m->power_on();
        for (unsigned a = 0U; a < 8192U; ++a)
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
    }
}
TEST_CASE("Mapper 235 snapshots validate socket wiring canonical pins and RAM before commit",
          "[mapper235][state]") {
    for (unsigned profile = 0U; profile < 8U; ++profile) {
        auto m = board(profile);
        m->write_chr(0x123U, 0xA7U);
        m->write_prg(0xBF1FU, 0U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 8196U);
        auto copy = board(profile);
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
        for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
                 {{0U, static_cast<Byte>(kMasks[profile] ^ 2U)},
                  {1U, 0U},
                  {2U, 0x3FU},
                  {3U, 0x7FU},
                  {3U, 0xBFU}}}) {
            auto bad = saved;
            bad[offset] = value;
            bad[4U + 0x123U] = 0U;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        auto other = board((profile + 1U) % 8U);
        StateReader wrong(saved);
        other->load_state(wrong);
        REQUIRE_FALSE(wrong.ok());
        m->reset();
        copy->reset();
        REQUIRE(state(*m) == state(*copy));
        REQUIRE(m->read_chr(0x123U) == 0xA7U);
    }
}
TEST_CASE("Mapper 235 rejects partial chips CHR ROM unsupported memory and header profiles",
          "[mapper235]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 20U; ++kind) {
        auto h = header();
        std::size_t p = 2097152U, c = 0U;
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
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_ram_size = 16384U;
            break;
        case 8U:
            h.chr_nvram_size = 8192U;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 11U:
            p = 0U;
            break;
        case 12U:
            p = 524288U;
            break;
        case 13U:
            p = 1572864U;
            break;
        case 14U:
            p += 16384U;
            break;
        case 15U:
            p = 5242880U;
            break;
        case 16U:
            c = 8192U;
            break;
        case 17U:
            h = header(true);
            h.has_battery = true;
            break;
        case 18U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 19U:
            --p;
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

Cartridge cartridge(unsigned profile, bool legacy = false, bool floating = false) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper235_boot(kMasks[profile], floating).size() < 0x1800U);
    if (floating)
        REQUIRE(mapperbus::tests::mapper235_program(kMasks[profile], true).size() < 1024U);
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper235_rom(
        kMasks[profile], legacy, profile % 2U == 0U, floating));
    REQUIRE(c);
    REQUIRE(c->configure_board_profile(kProfiles[profile]));
    return std::move(*c);
}
std::size_t payload_start(const std::vector<Byte>& bytes) {
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    return bytes.size() - tail.data().size() - 8196U;
}
Address final_latch(unsigned profile, bool floating) {
    if (!floating)
        return 0x3C1FU;
    unsigned hole = 1U;
    while ((kMasks[profile] & (1U << hole)) != 0U)
        ++hole;
    return static_cast<Address>(hole * 256U);
}
void exercise_cpu(unsigned profile, bool legacy, bool floating) {
    CAPTURE(profile, legacy, floating);
    Emulator e;
    REQUIRE(e.load_cartridge(cartridge(profile, legacy, floating), ""));
    e.power_on();
    e.step_frame();
    const auto marker = floating ? 0xD7U : 0xC7U;
    REQUIRE(e.read_cpu(0x12U) == marker);
    REQUIRE(e.read_cpu(0x14U) == 0U);
    const auto early = e.save_state();
    const auto start = payload_start(early);
    StateReader reader(std::span(early).subspan(start, 8196U));
    REQUIRE(reader.read<uint16_t>() == (0xEB00U | kMasks[profile]));
    REQUIRE(reader.read<Address>() == final_latch(profile, floating));
    REQUIRE(early[start + 4U + 0x123U] == (floating ? 0xA7U : 0x5AU));
    e.step_frame();
    const auto future = e.save_state();
    REQUIRE(e.load_state(early));
    e.step_frame();
    REQUIRE(e.save_state() == future);
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
             {{0U, static_cast<Byte>(kMasks[profile] ^ 2U)}, {1U, 0U}, {2U, 0x3FU}, {3U, 0x7FU}}}) {
        auto bad = early;
        bad[start + offset] = value;
        bad[start + 4U + 0x123U] = 0U;
        REQUIRE_FALSE(e.load_state(bad));
        REQUIRE(e.save_state() == future);
    }
    auto truncated = early;
    truncated.resize(start + 8195U);
    REQUIRE_FALSE(e.load_state(truncated));
    REQUIRE(e.save_state() == future);
    e.reset();
    REQUIRE(e.read_cpu(0x9A00U) == 0U);
    REQUIRE(e.read_cpu(0xFA00U) == 1U);
    const auto reset = e.save_state();
    REQUIRE(reset[start + 2U] == 0U);
    REQUIRE(reset[start + 3U] == 0U);
    REQUIRE(reset[start + 4U + 0x123U] == (floating ? 0xA7U : 0x5AU));
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == marker);
    REQUIRE(e.read_cpu(0x14U) == (floating ? 0xA7U : 0x5AU));
    e.power_on();
    REQUIRE(e.save_state()[start + 4U + 0x123U] == 0U);
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == marker);
    REQUIRE(e.read_cpu(0x14U) == 0U);
}
TEST_CASE("Mapper 235 CPU firmware verifies all socket layouts banking CIRAM and machine replay",
          "[mapper235][state]") {
    for (unsigned profile = 0U; profile < 8U; ++profile)
        for (bool legacy : {false, true}) {
            if (legacy && profile == 7U)
                continue; // 4 MiB requires the NES 2.0 extended PRG field.
            exercise_cpu(profile, legacy, false);
            if (profile != 7U)
                exercise_cpu(profile, legacy, true);
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
TEST_CASE("Mapper 235 empty sockets float every CPU bit while CHR and CIRAM stay connected",
          "[mapper235][bus][ppu]") {
    for (unsigned profile = 0U; profile < 8U; ++profile) {
        auto c = cartridge(profile);
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
        for (unsigned socket = 0U; socket < 4U; ++socket) {
            const bool present = (kMasks[profile] & (1U << socket)) != 0U;
            bus.write(command(socket, 31U, true, true, true, true), 0U);
            for (unsigned value = 0U; value < 256U; ++value) {
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(0x9A00U) ==
                        (present ? packed(kMasks[profile], socket) * 64U + 63U : value));
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(0xFA00U) ==
                        (present ? packed(kMasks[profile], socket) * 64U + 63U : value));
            }
            pwrite(p, 0x123U, static_cast<Byte>(socket + 0xA0U));
            REQUIRE(pread(p, 0x123U) == socket + 0xA0U);
            pwrite(p, 0x2000U, 0x19U);
            pwrite(p, 0x2C00U, 0x27U);
            for (Address a : {0x2000U, 0x2400U, 0x2800U, 0x2C00U, 0x3000U, 0x3EFFU}) {
                if (a == 0x3EFFU)
                    pwrite(p, 0x2EFFU, 0x27U);
                REQUIRE(pread(p, a) == 0x27U);
            }
            bus.write(0x8000U, 0xFFU);
            REQUIRE(bus.read(0x9A00U) == 0U);
            REQUIRE(bus.read(0xFA00U) == 1U);
        }
    }
}
TEST_CASE("Mapper 235 machine snapshots reject a different socket wiring with identical ROM bytes",
          "[mapper235][state]") {
    register_builtin_mappers();
    const auto bytes = mapperbus::tests::make_mapper235_rom(5U);
    Emulator a, b;
    auto ca = Cartridge::from_data(bytes), cb = Cartridge::from_data(bytes);
    REQUIRE(ca);
    REQUIRE(cb);
    REQUIRE(cb->configure_board_profile("slots-0-1"));
    REQUIRE(a.load_cartridge(std::move(*ca), ""));
    REQUIRE(b.load_cartridge(std::move(*cb), ""));
    a.power_on();
    b.power_on();
    REQUIRE(a.content_hash() == b.content_hash());
    a.step_frame();
    const auto before = b.save_state();
    REQUIRE_FALSE(b.load_state(a.save_state()));
    REQUIRE(b.save_state() == before);
}
} // namespace
