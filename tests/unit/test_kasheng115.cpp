#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header() {
    INesHeader h{};
    h.mapper_number = 115U;
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
std::vector<Byte> chr(std::size_t size = 0x80000U) {
    auto data = banks(size, 1024U);
    for (std::size_t page = 0U; page < size / 1024U; ++page)
        data[page * 1024U + 1U] = static_cast<Byte>(0x40U + (page >> 8U));
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x80000U,
                              std::size_t c = 0x80000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), chr(c));
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte r, Byte value, Byte mode = 0U) {
    m.write_prg(0x9FFEU, static_cast<Byte>(r | mode));
    m.write_prg(0x9FFFU, value);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Kasheng 115 every override byte selects the documented PRG pins in both MMC3 modes",
          "[mappers][kasheng115]") {
    auto m = board();
    bank(*m, 7U, 0xE5U);
    for (Byte mmc3 : {0U, 0x40U}) {
        bank(*m, 6U, 0xE3U, mmc3);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            m->write_prg(0x6000U, static_cast<Byte>(raw));
            const unsigned outer = (raw & 0x40U) != 0U ? 32U : 0U;
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                unsigned expected = 0U;
                if ((raw & 0x80U) == 0U) {
                    const std::array<unsigned, 4> inner =
                        mmc3 != 0U ? std::array<unsigned, 4>{30U, 5U, 3U, 31U}
                                   : std::array<unsigned, 4>{3U, 5U, 30U, 31U};
                    expected = outer + inner[slot];
                } else if ((raw & 0x20U) == 0U)
                    expected = outer + 2U * (raw & 15U) + (slot & 1U);
                else
                    expected = outer + 2U * (raw & 14U) + slot;
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == expected);
            }
        }
    }
    m->write_prg(0x6000U, 0x8FU);
    bank(*m, 6U, 7U);
    bank(*m, 7U, 9U);
    REQUIRE(m->read_prg(0x8000U) == 30U);
    m->write_prg(0x6000U, 0U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
    REQUIRE(m->read_prg(0xA000U) == 9U);
}
TEST_CASE("Kasheng 115 outer CHR bit preserves paired banks and inversion",
          "[mappers][kasheng115]") {
    auto m = board();
    constexpr std::array<Byte, 6> kRegs = {0x83U, 0xC5U, 0xE5U, 0xA6U, 0x37U, 0x6BU};
    constexpr std::array<Byte, 8> kPages = {0x82U, 0x83U, 0xC4U, 0xC5U, 0xE5U, 0xA6U, 0x37U, 0x6BU};
    for (Byte r = 0U; r < 6U; ++r)
        bank(*m, r, kRegs[r]);
    for (Byte inversion : {0U, 0x80U}) {
        m->write_prg(0x8000U, inversion);
        for (Byte outer : {0U, 1U, 0xFEU, 0xFFU}) {
            m->write_prg(0x7FFDU, outer);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto a = static_cast<Address>(slot * 1024U);
                REQUIRE(m->read_chr(a) == kPages[slot ^ ((inversion & 0x80U) != 0U ? 4U : 0U)]);
                REQUIRE(m->read_chr(static_cast<Address>(a + 1U)) == 0x40U + (outer & 1U));
                m->write_chr(a, 0U);
                REQUIRE(m->read_chr(a) == kPages[slot ^ ((inversion & 0x80U) != 0U ? 4U : 0U)]);
            }
        }
    }
}
TEST_CASE("Kasheng 115 low registers bypass WRAM gates and zero-latch IRQs repeat",
          "[mappers][kasheng115]") {
    auto m = board();
    bank(*m, 6U, 3U);
    for (Byte gate : {0U, 0x80U, 0xC0U}) {
        m->write_prg(0xA001U, gate);
        m->write_prg(0x7FFCU, 0x40U);
        REQUIRE(m->read_prg(0x8000U) == 35U);
        m->write_prg(0x7FFDU, 1U);
        REQUIRE(m->read_chr(1U) == 0x41U);
    }
    REQUIRE(m->prg_read_mask(0x6002U) == 7U);
    REQUIRE(m->maps_prg(0x7FFEU));
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    REQUIRE(m->configure_board_jumpers(7U));
    REQUIRE_FALSE(m->configure_board_jumpers(8U));
    REQUIRE(m->read_prg(0x7FFEU) == 7U);
    m->write_prg(0x6002U, 0U);
    REQUIRE(m->read_prg(0x6002U) == 7U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (Byte mode : {0U, 0x80U, 0xE0U}) {
        m->write_prg(0x6000U, mode);
        for (unsigned scanline = 0U; scanline < 3U; ++scanline) {
            m->clock_irq_counter();
            REQUIRE(m->irq_pending());
            m->acknowledge_irq();
        }
    }
    m->write_prg(0xE000U, 0U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->reset();
    REQUIRE(m->read_prg(0x6002U) == 7U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_chr(1U) == 0x40U);
    REQUIRE(m->battery_ram().empty());
}
TEST_CASE("Kasheng 115 state restores override, CHR outer pins, solder pads and partial IRQ",
          "[mappers][kasheng115]") {
    auto m = board();
    REQUIRE(m->configure_board_jumpers(5U));
    bank(*m, 2U, 0x65U);
    m->write_prg(0x6000U, 0xCDU);
    m->write_prg(0x6001U, 1U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    StateReader r(saved);
    copy->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(copy->read_prg(0x8000U) == 58U);
    REQUIRE(copy->read_chr(0x1001U) == 0x41U);
    REQUIRE(copy->read_prg(0x6002U) == 5U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    for (std::size_t offset : {16384UZ,
                               16385UZ,
                               16392UZ,
                               16394UZ,
                               saved.size() - 3U,
                               saved.size() - 2U,
                               saved.size() - 1U}) {
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
std::vector<Byte> image(Byte pad = 0U, bool legacy = false) {
    auto p = banks(0x80000U, 8192U);
    const auto c = chr();
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 64U;
    rom[6] = 0x31U;
    rom[7] = legacy ? 0x70U : 0x78U;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&store](Byte r, Byte v) {
        store(0x8000U, r);
        store(0x8001U, v);
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
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    expect(0x9A00U, 3U);
    expect(0xBA00U, 5U);
    expect(0xDA00U, 30U);
    expect(0xFA00U, 31U);
    store(0x6000U, 0x40U);
    expect(0x9A00U, 35U);
    expect(0xDA00U, 62U);
    expect(0xFA00U, 63U);
    store(0x8000U, 0x46U);
    expect(0x9A00U, 62U);
    expect(0xDA00U, 35U);
    store(0x6000U, 0x80U);
    expect(0x9A00U, 0U);
    expect(0xBA00U, 1U);
    expect(0xDA00U, 0U);
    expect(0xFA00U, 1U);
    store(0xA001U, 0U);
    store(0x7FFCU, 0xCFU);
    expect(0x9A00U, 62U);
    expect(0xBA00U, 63U);
    store(0x6000U, 0xA1U);
    expect(0x9A00U, 0U);
    expect(0xBA00U, 1U);
    expect(0xDA00U, 2U);
    expect(0xFA00U, 3U);
    set_bank(6U, 7U);
    set_bank(7U, 9U);
    expect(0x9A00U, 0U);
    store(0x6000U, 0U);
    expect(0x9A00U, 7U);
    expect(0xBA00U, 9U);
    set_bank(0U, 3U);
    set_bank(2U, 0x65U);
    pread(0x1000U, 0x65U);
    pread(0x1001U, 0x40U);
    store(0x7FFDU, 0xFFU);
    pread(0x1001U, 0x41U);
    store(0x8000U, 0x80U);
    pread(0U, 0x65U);
    pread(1U, 0x41U);
    pread(0x1000U, 2U);
    store(0x6001U, 0U);
    pread(1U, 0x40U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    store(0x6002U, 0xFFU);
    code.insert(code.end(), {0xADU, 2U, 0x60U, 0x29U, 7U, 0x85U, 0x10U, 0xC9U, pad, 0xD0U, 0xFEU});
    store(0x6000U, 0xEFU); // Keep NROM override active during repeating IRQs.
    store(0xC000U, 0U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 2U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    // Identical original firmware in each 16 KiB page keeps execution stable
    // while switching all four PRG windows between NROM and MMC3 mappings.
    for (std::size_t page = 0U; page < p.size(); page += 0x4000U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(page + 0x2000U));
        std::ranges::copy(kIrq, p.begin() + static_cast<std::ptrdiff_t>(page + 0x2800U));
        for (std::size_t v = 0x3FFAU; v < 0x4000U; v += 2U) {
            p[page + v] = 0U;
            p[page + v + 1U] = v == 0x3FFCU ? 0xE0U : 0xE8U;
        }
    }
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(Byte pad = 0U, bool legacy = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(pad, legacy));
    REQUIRE(r);
    REQUIRE(r->configure_board_jumpers(pad));
    return std::move(*r);
}
TEST_CASE("Kasheng 115 pad aliases drive only low bits and other low reads stay open",
          "[mappers][kasheng115]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (Byte pad = 0U; pad < 8U; ++pad) {
        REQUIRE(c.configure_board_jumpers(pad));
        for (Address addr : {0x6002U, 0x6006U, 0x7FFEU}) {
            bus.write(0x10U, 0xA8U);
            REQUIRE(bus.read(addr) == (0xA8U | pad));
        }
    }
    for (Address addr : {0x6000U, 0x6001U, 0x6003U, 0x7FFFU, 0x5002U}) {
        bus.write(0x10U, 0x5AU);
        REQUIRE(bus.read(addr) == 0x5AU);
    }
    REQUIRE_FALSE(c.has_battery());
    REQUIRE(c.battery_ram().empty());
}
TEST_CASE("Kasheng 115 ROM geometry rejects unsupported populations and obsolete mapper 248",
          "[mappers][kasheng115]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        std::size_t p = 0x80000U, c = 0x80000U;
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
            p = 0x82000U;
            break;
        case 8U:
            c = 0U;
            break;
        case 9U:
            c = 0x80400U;
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
    auto legacy = board(h);
    REQUIRE_FALSE(legacy->maps_prg(0x6000U));
    auto minimum = board(header(), 32768U, 8192U);
    minimum->write_prg(0x6000U, 0xCFU);
    REQUIRE(minimum->read_prg(0x8000U) == 2U);
    h = header();
    h.mapper_number = 248U;
    auto obsolete = MapperRegistry::instance().create(h, banks(0x80000U, 8192U), chr());
    REQUIRE_FALSE(obsolete);
    REQUIRE(obsolete.error().find("Unsupported mapper") != std::string::npos);
}
TEST_CASE("Kasheng 115 native CPU checks modes and repeated zero IRQs before state replay",
          "[mappers][kasheng115]") {
    for (Byte pad : {0U, 7U})
        for (bool legacy : {false, true}) {
            Emulator emulator;
            REQUIRE(emulator.load_cartridge(cartridge(pad, legacy), ""));
            emulator.reset();
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
            REQUIRE(emulator.read_cpu(0x11U) >= 2U);
            REQUIRE(emulator.read_cpu(0x10U) == pad);
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
            bad[bad.size() - tail.data().size() - 2U] = 2U;
            const auto before = emulator.save_state();
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE((emulator.save_state() == before));
        }
}
} // namespace
