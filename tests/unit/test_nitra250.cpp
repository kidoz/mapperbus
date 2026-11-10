#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "core/binary_file.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(uint16_t id = 250U) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x20000U,
                              std::size_t c = 0x20000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void write(Mapper& m, Address port, Byte logical) {
    const auto native = static_cast<Address>((port & 0xE000U) | ((port & 1U) << 10U) | logical);
    m.write_prg(native, static_cast<Byte>(logical ^ 0xFFU));
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    write(m, 0x8000U, static_cast<Byte>(reg | mode));
    write(m, 0x8001U, value);
}
constexpr std::array<Address, 8> kNative = {
    0x8000U, 0x8400U, 0xA000U, 0xA400U, 0xC000U, 0xC400U, 0xE000U, 0xE400U};
constexpr std::array<Address, 8> kMmc3 = {
    0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
TEST_CASE("Nitra 250 uses all address-data values on all MMC3 ports regardless of CPU data",
          "[mappers][nitra250]") {
    auto m = board();
    auto reference = board(header(119U));
    // TQROM has the same compact register/IRQ payload; chip selection does
    // not enter this test because it never writes CHR memory.
    for (unsigned port = 0U; port < 8U; ++port) {
        for (unsigned value = 0U; value < 256U; ++value) {
            for (Byte ignored : {0U, 0x5AU, 0xFFU}) {
                m->reset();
                reference->reset();
                bank(*m, 2U, 9U);
                reference->write_prg(0x8000U, 2U);
                reference->write_prg(0x8001U, 9U);
                m->write_prg(static_cast<Address>(kNative[port] | value), ignored);
                reference->write_prg(kMmc3[port], static_cast<Byte>(value));
                auto expected = state(*reference);
                expected.push_back(0U);
                REQUIRE((state(*m) == expected));
            }
        }
    }
}
TEST_CASE("Nitra 250 ignores unused address bits and all CPU data pins", "[mappers][nitra250]") {
    auto m = board();
    auto reference = board(header(119U));
    for (unsigned port = 0U; port < 8U; ++port) {
        for (unsigned combination = 0U; combination < 16U; ++combination) {
            const unsigned aliases = ((combination & 3U) << 8U) | ((combination & 12U) << 9U);
            m->reset();
            reference->reset();
            m->write_prg(static_cast<Address>(kNative[port] | aliases | 0xC7U), 0U);
            reference->write_prg(kMmc3[port], 0xC7U);
            auto expected = state(*reference);
            expected.push_back(0U);
            REQUIRE((state(*m) == expected));
        }
        for (unsigned ignored = 0U; ignored < 256U; ++ignored) {
            m->reset();
            reference->reset();
            m->write_prg(static_cast<Address>(kNative[port] | 0x39U), static_cast<Byte>(ignored));
            reference->write_prg(kMmc3[port], 0x39U);
            auto expected = state(*reference);
            expected.push_back(0U);
            REQUIRE((state(*m) == expected));
        }
    }
}
std::vector<Byte> image(bool queen = false, bool legacy = false) {
    auto p = banks(0x20000U, 8192U);
    const auto c = banks(0x20000U, 1024U);
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 8U;
    rom[5] = 16U;
    rom[6] = 0xA1U;
    rom[7] = legacy ? 0xF0U : 0xF8U;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto mapper_write = [&store](Address port, Byte v) {
        const auto a = static_cast<Address>((port & 0xE000U) | ((port & 1U) << 10U) | v);
        store(a, 0xA5U); // Deliberately unrelated CPU data.
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&mapper_write](Byte r, Byte v) {
        mapper_write(0x8000U, r);
        mapper_write(0x8001U, v);
    };
    const auto address = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&address, &store](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &address, &expect](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U); // Isolate mapper IRQ from the APU frame IRQ.
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    expect(0x8000U, 3U);
    expect(0xA000U, 5U);
    expect(0xC000U, 14U);
    mapper_write(0x8000U, 0x46U);
    expect(0x8000U, 14U);
    expect(0xC000U, queen ? 2U : 3U);
    mapper_write(0x8001U, 7U); // R6=7 makes the mux difference observable again.
    expect(0xC000U, queen ? 6U : 7U);
    constexpr std::array<Byte, 6> kRegs = {0x83U, 5U, 0xE5U, 0xA6U, 0x37U, 0x6BU};
    for (Byte r = 0U; r < 6U; ++r)
        set_bank(r, kRegs[r]);
    pread(0U, 2U);
    pread(0x400U, 3U);
    pread(0x1000U, 0x65U);
    pwrite(0x1000U, 0x11U);
    pread(0x1000U, 0x65U);
    mapper_write(0x8000U, 0x80U);
    pread(0U, 0x65U);
    pread(0x1000U, 2U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    mapper_write(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    mapper_write(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    mapper_write(0xC000U, 2U);
    mapper_write(0xC001U, 0U);
    mapper_write(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xADU, 0x11U, 0U, 0xC9U, 0xD3U, 0xD0U, 0xF9U});
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x800U);
    std::ranges::copy(code, p.begin() + 0x1E000U);
    // Native $E000 acknowledges the mapper regardless of the CPU data byte.
    constexpr std::array<Byte, 12> kIrq = {
        0x48U, 0xA9U, 0xFFU, 0x8DU, 0U, 0xE0U, 0xA9U, 0xD3U, 0x85U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(kIrq, p.begin() + 0x1E800U);
    for (std::size_t v = 0x1FFFAU; v < 0x20000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x1FFFCU ? 0xE0U : 0xE8U;
    }
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool queen = false, bool legacy = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(queen, legacy));
    REQUIRE(r);
    if (queen)
        REQUIRE(r->configure_board_profile("nitra-queen-bee-v"));
    return std::move(*r);
}
TEST_CASE("Nitra 250 Queen Bee mux substitutes CPU A13 only in the upper PRG window",
          "[mappers][nitra250]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("nitra-queen-bee-v"));
    for (Byte r6 : {2U, 3U, 6U, 7U}) {
        bank(*m, 7U, 5U);
        for (Byte mode : {0U, 0x40U}) {
            bank(*m, 6U, r6, mode);
            REQUIRE(m->read_prg(0x8000U) == (mode != 0U ? 14U : r6));
            REQUIRE(m->read_prg(0xA000U) == 5U);
            REQUIRE(m->read_prg(0xC000U) == (mode != 0U ? (r6 & 0xFEU) : 14U));
            REQUIRE(m->read_prg(0xE000U) == 15U);
        }
    }
    bank(*m, 6U, 7U, 0x40U);
    REQUIRE_FALSE(m->configure_board_profile("invented"));
    REQUIRE(m->read_prg(0xC000U) == 6U);
    m->reset();
    bank(*m, 6U, 3U, 0x40U);
    REQUIRE(m->read_prg(0xC000U) == 2U);
    REQUIRE(m->configure_board_profile("nitra-l401"));
    REQUIRE(m->read_prg(0xC000U) == 3U);
    auto other = board(header(119U));
    REQUIRE_FALSE(other->configure_board_profile("nitra-queen-bee-v"));
}
TEST_CASE("Nitra 250 retains normal CHR wiring, IRQ progress and no phantom WRAM",
          "[mappers][nitra250]") {
    auto m = board();
    bank(*m, 0U, 0x83U);
    bank(*m, 2U, 0xE5U);
    REQUIRE(m->read_chr(0U) == 2U);
    REQUIRE(m->read_chr(0x400U) == 3U);
    write(*m, 0x8000U, 0x80U);
    REQUIRE(m->read_chr(0U) == 0x65U);
    REQUIRE(m->read_chr(0x1000U) == 2U);
    m->write_chr(0U, 0U);
    REQUIRE(m->read_chr(0U) == 0x65U);
    write(*m, 0xA000U, 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    write(*m, 0xA001U, 0x80U);
    m->write_prg(0x6000U, 0xFFU);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->maps_expansion(0x4100U));
    write(*m, 0xC000U, 2U);
    write(*m, 0xC001U, 0U);
    write(*m, 0xE001U, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    write(*m, 0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
}
TEST_CASE("Nitra 250 snapshots verify immutable PCB wiring and normalized register state",
          "[mappers][nitra250]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("nitra-queen-bee-v"));
    bank(*m, 6U, 3U, 0x40U);
    write(*m, 0xC000U, 2U);
    write(*m, 0xC001U, 0U);
    write(*m, 0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    REQUIRE(copy->configure_board_profile("nitra-queen-bee-v"));
    StateReader r(saved);
    copy->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(copy->read_prg(0xC000U) == 2U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    auto standard = board();
    StateReader mismatch(saved);
    standard->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    bank(*standard, 6U, 3U, 0x40U);
    REQUIRE(standard->read_prg(0xC000U) == 3U);
    for (std::size_t offset : {16384UZ, 16385UZ, 16392UZ, 16394UZ, 16397UZ, saved.size() - 1U}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
    StateReader truncated(std::span{saved}.first(saved.size() - 1U));
    copy->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
}
TEST_CASE("Nitra 250 geometry accepts legacy and NES2 ROM-only boards without invented submappers",
          "[mappers][nitra250]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        std::size_t p = 0x20000U, c = 0x20000U;
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
            p = 16384U;
            break;
        case 7U:
            p = 0x22000U;
            break;
        case 8U:
            c = 0U;
            break;
        case 9U:
            c = 0x20400U;
            break;
        case 10U:
            c = 8193U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    auto h = header();
    h.is_nes2 = false;
    h.prg_ram_size = 8192U;
    auto m = board(h);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    auto small = board(header(), 0x18000U, 0x18000U);
    bank(*small, 6U, 11U);
    bank(*small, 2U, 255U);
    REQUIRE(small->read_prg(0x8000U) == 11U);
    REQUIRE(small->read_chr(0x1000U) == 63U);
}
TEST_CASE("Nitra 250 native CPU programs verify both PCBs, PPUDATA and IRQ before replay",
          "[mappers][nitra250]") {
    for (bool queen : {false, true})
        for (bool legacy : {false, true}) {
            Emulator emulator;
            REQUIRE(emulator.load_cartridge(cartridge(queen, legacy), ""));
            emulator.reset();
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
            REQUIRE(emulator.read_cpu(0x11U) == 0xD3U);
            const auto early = emulator.save_state();
            emulator.step_frame();
            const auto future = emulator.save_state();
            REQUIRE(emulator.load_state(early));
            emulator.step_frame();
            REQUIRE((emulator.save_state() == future));
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = early;
            bad[bad.size() - tail.data().size() - 1U] = queen ? 0U : 1U;
            const auto before = emulator.save_state();
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE((emulator.save_state() == before));
        }
}
} // namespace
