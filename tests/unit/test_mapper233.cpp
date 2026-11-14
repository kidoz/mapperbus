#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper233_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
TEST_CASE("Mapper 233 registers the reset-switched 42 Games board", "[mappers][mapper233]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 233U;
    h.is_nes2 = true;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Horizontal;
    auto m = MapperRegistry::instance().create(h, std::vector<Byte>(1048576U), {});
    REQUIRE(m);
}

constexpr std::array<std::string_view, 3> kProfiles{"reset-42", "disch-233", "everynes-vector"};
constexpr std::array<std::array<unsigned, 4>, 4> kMirrors{
    {{0U, 0U, 0U, 1U}, {0U, 1U, 0U, 1U}, {0U, 0U, 1U, 1U}, {1U, 1U, 1U, 1U}}};
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 233U;
    h.is_nes2 = !legacy;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}
std::vector<Byte> rom(std::size_t pages) {
    std::vector<Byte> data(pages * 16384U);
    for (std::size_t a = 0U; a < data.size(); ++a)
        data[a] = static_cast<Byte>(a / 16384U);
    return data;
}
std::unique_ptr<Mapper> board(unsigned profile = 0U, bool legacy = false, std::size_t pages = 32U) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy), rom(pages), {});
    REQUIRE(m);
    REQUIRE((*m)->configure_board_profile(kProfiles[profile]));
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
std::pair<unsigned, unsigned> banks(unsigned value, unsigned profile) {
    const unsigned base = profile == 1U ? 0U : (value / 128U) * 32U;
    const unsigned page = value % 32U;
    if (value / 32U % 2U != 0U)
        return {base + page, base + page};
    return {base + page / 2U * 2U, base + page / 2U * 2U + 1U};
}
MirrorMode mirror(unsigned value, unsigned profile) {
    if (profile != 1U)
        return value / 64U % 2U != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    constexpr std::array<MirrorMode, 4> modes{MirrorMode::SingleLower,
                                              MirrorMode::Vertical,
                                              MirrorMode::Horizontal,
                                              MirrorMode::SingleUpper};
    return modes[value / 64U];
}
TEST_CASE("Mapper 233 profiles decode every data byte before smaller fitted-ROM wrapping",
          "[mappers][mapper233]") {
    for (unsigned profile = 0U; profile < 3U; ++profile)
        for (bool legacy : {false, true})
            for (std::size_t pages : {1UZ, 3UZ, 32UZ, 64UZ}) {
                if (profile == 1U && pages > 32U)
                    continue;
                auto m = board(profile, legacy, pages);
                for (unsigned value = 0U; value < 256U; ++value) {
                    m->write_prg(0x8000U, static_cast<Byte>(value));
                    const auto [low, high] = banks(value, profile);
                    REQUIRE(m->read_prg(0x8000U) == low % pages);
                    REQUIRE(m->read_prg(0xBFFFU) == low % pages);
                    REQUIRE(m->read_prg(0xC000U) == high % pages);
                    REQUIRE(m->read_prg(0xFFFFU) == high % pages);
                    REQUIRE(m->mirror_mode() == mirror(value, profile));
                }
            }
}
TEST_CASE("Mapper 233 register mirrors preserve each profile's distinct write protocol",
          "[mappers][mapper233]") {
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        auto m = board(profile, false, profile == 1U ? 32U : 64U);
        for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
            m->write_prg(0x8001U, 0U);
            m->write_prg(0x8000U, 0x55U);
            m->write_chr(0x123U, 0x39U);
            const auto value = static_cast<Byte>(a ^ (a / 256U));
            m->write_prg(static_cast<Address>(a), value);
            const unsigned control = profile == 1U || a % 2U == 0U ? value : 0x55U;
            const auto [low, high] = banks(control, profile);
            REQUIRE(m->read_prg(0x9A00U) == low);
            REQUIRE(m->read_prg(0xFA00U) == high);
            REQUIRE(m->mirror_mode() == mirror(control, profile));
            m->write_chr(0x123U, 0xA7U);
            const bool protected_ram = profile != 1U && a % 2U != 0U && (value & 2U) != 0U;
            REQUIRE(m->read_chr(0x123U) == (protected_ram ? 0x39U : 0xA7U));
        }
    }
}
TEST_CASE("Mapper 233 distinguishes cold power, A19 reset switching and the vector-read theory",
          "[mappers][mapper233]") {
    auto m = board(0U, false, 64U);
    for (unsigned n = 0U; n < 16U; ++n) {
        REQUIRE(m->read_prg(0x8000U) == (n % 2U != 0U ? 32U : 0U));
        m->write_chr(0x123U, 0xA7U);
        m->reset();
        REQUIRE(m->read_chr(0x123U) == 0xA7U);
    }
    m->write_prg(0x8000U, 0xFFU);
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 0x7FU);
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 32U);
    m->power_on();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_chr(0x123U) == 0U);
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        auto b = board(profile, false, profile == 1U ? 32U : 64U);
        REQUIRE(b->read_prg(0xFFFDU) == 1U);
        REQUIRE(b->read_prg(0x8000U) == 0U);
        for (unsigned bit = 0U; bit < 16U; ++bit) {
            b->write_prg(0x8000U, 0U);
            b->observe_cpu_read(static_cast<Address>(0xFFFDU ^ (1U << bit)), 0xFFU);
            REQUIRE(b->read_prg(0x8000U) == 0U);
        }
        b->observe_cpu_read(0xFFFDU, 0U);
        REQUIRE(b->read_prg(0x8000U) == (profile == 2U ? 63U : 0U));
        b->reset();
        REQUIRE(b->read_prg(0x8000U) == (profile == 0U ? 32U : 0U));
    }
}
TEST_CASE("Mapper 233 CHR protection and soft resets cover every fitted RAM cell",
          "[mappers][mapper233]") {
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        auto m = board(profile);
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
            m->write_chr(static_cast<Address>(a), 0x39U);
        }
        m->write_prg(0x8001U, 0xFFU);
        for (unsigned a = 0U; a < 8192U; ++a) {
            m->write_chr(static_cast<Address>(a), 0xA7U);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == (profile == 1U ? 0xA7U : 0x39U));
        }
        m->reset();
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == (profile == 1U ? 0xA7U : 0x39U));
            m->write_chr(static_cast<Address>(a), 0x5AU);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0x5AU);
        }
        m->power_on();
        for (unsigned a = 0U; a < 8192U; ++a)
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
    }
}
TEST_CASE("Mapper 233 rejects unknown populations and impossible Disch geometry",
          "[mappers][mapper233]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t p = 524288U, c = 0U;
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
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            p = 0U;
            break;
        case 10U:
            p = 8192U;
            break;
        case 11U:
            --p;
            break;
        case 12U:
            p = 1064960U;
            break;
        case 13U:
            c = 8192U;
            break;
        case 14U:
            h = header(true);
            h.has_battery = true;
            break;
        case 15U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 16U:
            h.chr_ram_size = 4096U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    auto m = board(0U, false, 64U);
    const auto before = state(*m);
    REQUIRE_FALSE(m->configure_board_profile("disch-233"));
    for (std::string_view bad : {"", "disch", "reset", "unknown"})
        REQUIRE_FALSE(m->configure_board_profile(bad));
    REQUIRE(state(*m) == before);
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    m->clock_cpu();
    m->clock_irq_counter();
    m->clock_audio();
    REQUIRE_FALSE(m->irq_pending());
}
TEST_CASE("Mapper 233 snapshots bind profiles and reject malformed input without changing RAM or "
          "reset phase",
          "[mappers][mapper233][state]") {
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        auto m = board(profile);
        m->write_chr(0x123U, 0xA7U);
        m->write_prg(0x8000U, 0xFFU);
        m->write_prg(0x8001U, 0xFFU);
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
        for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                 {{0U, static_cast<Byte>((profile + 1U) % 3U)}, {1U, 0U}, {3U, 1U}, {3U, 3U}}}) {
            auto bad = saved;
            bad[offset] = value;
            bad[4U + 0x123U] = 0U;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        if (profile == 1U) {
            auto bad = saved;
            bad[3U] = 2U;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        auto other = board((profile + 1U) % 3U);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        m->reset();
        copy->reset();
        REQUIRE(state(*m) == state(*copy));
        REQUIRE(m->read_chr(0x123U) == 0xA7U);
    }
}
void paddr(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void pwrite(Ppu& p, Address a, Byte value) {
    paddr(p, a);
    p.write_register(7U, value);
}
Byte pread(Ppu& p, Address a) {
    paddr(p, a);
    (void)p.read_register(7U);
    paddr(p, a);
    return p.read_register(7U);
}
TEST_CASE("Mapper 233 Disch CIRAM wiring includes the asymmetric AAAB layout and upper aliases",
          "[mappers][mapper233][ppu]") {
    auto m = board(1U);
    // Route the same board through the real cartridge/PPU interface.
    std::vector<Byte> image(16U + 524288U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 32U;
    image[6] = 0x90U;
    image[7] = 0xE8U;
    image[11] = 7U;
    auto c = Cartridge::from_data(image);
    REQUIRE(c);
    REQUIRE(c->configure_board_profile("disch-233"));
    Ppu p;
    p.connect_cartridge(&*c);
    p.reset();
    for (unsigned a = 0U; a < 1024U; ++a) {
        c->write_prg(0x8000U, 0x40U);
        pwrite(p, static_cast<Address>(0x2000U + a), static_cast<Byte>(0x19U ^ a));
        pwrite(p, static_cast<Address>(0x2400U + a), static_cast<Byte>(0xA7U ^ a));
    }
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        c->write_prg(0x8000U, static_cast<Byte>(mode * 64U));
        m->write_prg(0x8000U, static_cast<Byte>(mode * 64U));
        for (unsigned a = 0x2000U; a < 0x3F00U; ++a) {
            const unsigned slot = a / 1024U % 4U;
            const auto expected =
                static_cast<Byte>((kMirrors[mode][slot] == 0U ? 0x19U : 0xA7U) ^ (a % 1024U));
            REQUIRE(pread(p, static_cast<Address>(a)) == expected);
            const auto mapped = m->nametable_address(static_cast<Address>(a));
            if (mode == 0U) {
                REQUIRE(mapped);
                REQUIRE(*mapped == kMirrors[mode][slot] * 1024U + a % 1024U);
            } else
                REQUIRE_FALSE(mapped);
        }
    }
    c->write_prg(0x8000U, 0U);
    pwrite(p, 0x2400U, 0x5AU);
    REQUIRE(pread(p, 0x2000U) == 0x5AU);
    REQUIRE(pread(p, 0x2800U) == 0x5AU);
    REQUIRE(pread(p, 0x2C00U) == 0xA7U);
}

Cartridge cartridge(unsigned profile = 0U, bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper233_program(profile).size() < 0x1800U);
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper233_rom(profile, legacy, vertical));
    REQUIRE(c);
    REQUIRE(c->configure_board_profile(kProfiles[profile]));
    return std::move(*c);
}
TEST_CASE("Mapper 233 vector read observes the old byte and leaves other profiles unchanged",
          "[mappers][mapper233]") {
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        auto bytes = mapperbus::tests::make_mapper233_rom(1U);
        const auto p = rom(32U);
        std::ranges::copy(p, bytes.begin() + 16);
        auto c = Cartridge::from_data(bytes);
        REQUIRE(c);
        REQUIRE(c->configure_board_profile(kProfiles[profile]));
        MemoryBus bus;
        bus.connect_cartridge(&*c);
        REQUIRE(c->read_prg(0xFFFDU) == 1U);
        REQUIRE(c->read_prg(0x8000U) == 0U);
        REQUIRE(bus.read(0xFFFDU) == 1U);
        REQUIRE(c->read_prg(0x8000U) == (profile == 2U ? 31U : 0U));
        REQUIRE(c->read_prg(0xC000U) == (profile == 2U ? 31U : 1U));
        for (unsigned value = 0U; value < 256U; ++value)
            for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
                bus.write(a, 0xFFU);
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(a) == value);
            }
        auto m = board(profile);
        m->write_prg(0x8000U, 0xFFU);
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
}
TEST_CASE(
    "Mapper 233 CPU profiles exercise banked fetches CIRAM protection reset and atomic replay",
    "[mappers][mapper233][state]") {
    for (unsigned profile = 0U; profile < 3U; ++profile)
        for (bool legacy : {false, true})
            for (bool vertical : {false, true}) {
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(profile, legacy, vertical), ""));
                e.power_on();
                for (unsigned phase = 0U; phase < 4U; ++phase) {
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U + profile);
                    const Byte initial =
                        profile == 0U ? (phase % 2U != 0U ? 32U : 0U) : (profile == 2U ? 63U : 0U);
                    REQUIRE(e.read_cpu(0x14U) == initial);
                    REQUIRE(e.read_cpu(0x15U) == (phase == 0U ? 0U : 0x5AU));
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
                    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 3>{
                             {{0U, static_cast<Byte>((profile + 1U) % 3U)}, {1U, 0U}, {3U, 1U}}}) {
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
                }
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U + profile);
                REQUIRE(e.read_cpu(0x15U) == 0U);
            }
}
TEST_CASE("Mapper 233 machine snapshots reject a different profile even for identical ROM bytes",
          "[mappers][mapper233][state]") {
    const auto bytes = mapperbus::tests::make_mapper233_rom(1U);
    Emulator source;
    auto c = Cartridge::from_data(bytes);
    REQUIRE(c);
    REQUIRE(c->configure_board_profile("reset-42"));
    REQUIRE(source.load_cartridge(std::move(*c), ""));
    source.power_on();
    const auto saved = source.save_state();
    for (std::string_view profile : {"disch-233", "everynes-vector"}) {
        Emulator other;
        auto copy = Cartridge::from_data(bytes);
        REQUIRE(copy);
        REQUIRE(copy->configure_board_profile(profile));
        REQUIRE(other.load_cartridge(std::move(*copy), ""));
        other.power_on();
        REQUIRE(other.content_hash() == source.content_hash());
        const auto before = other.save_state();
        REQUIRE_FALSE(other.load_state(saved));
        REQUIRE(other.save_state() == before);
    }
}
} // namespace
