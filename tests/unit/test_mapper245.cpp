#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper245_rom.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 245U;
    h.is_nes2 = !legacy;
    h.has_battery = true;
    h.prg_nvram_size = legacy ? 0U : 8192U;
    h.chr_ram_size = 8192U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}
std::unique_ptr<Mapper> board(unsigned size = 1048576U, bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    std::vector<Byte> rom(size);
    for (unsigned page = 0U; page < size / 8192U; ++page)
        std::fill_n(rom.begin() + page * 8192U, 8192U, static_cast<Byte>(page));
    auto m = MapperRegistry::instance().create(header(legacy, vertical), std::move(rom), {});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void reg(Mapper& m, Byte index, Byte data, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | mode));
    m.write_prg(0x8001U, data);
}
TEST_CASE("Mapper 245 routes every live CHR selector to PRG A19 in both PRG modes", "[mapper245]") {
    for (unsigned size : {32768U, 65536U, 131072U, 262144U, 524288U, 1048576U}) {
        auto m = board(size);
        for (unsigned mode = 0U; mode < 4U; ++mode) {
            const auto control = static_cast<Byte>(mode << 6U);
            reg(*m, 6U, 3U, control);
            reg(*m, 7U, 5U, control);
            for (unsigned value = 0U; value < 256U; ++value) {
                for (Byte r = 0U; r < 6U; ++r)
                    reg(*m, r, static_cast<Byte>(value ^ ((r & 1U) * 2U)), control);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const unsigned selector = mode >= 2U ? slot + 2U : slot / 2U;
                    const unsigned outer = ((value ^ ((selector & 1U) * 2U)) & 2U) * 32U;
                    for (unsigned high : {0U, 0x1000U, 0x2000U, 0x3000U}) {
                        m->observe_ppu_address(static_cast<Address>(high + slot * 1024U));
                        const unsigned banks[4] = {
                            (mode & 1U) != 0U ? 62U : 3U, 5U, (mode & 1U) != 0U ? 3U : 62U, 63U};
                        for (unsigned window = 0U; window < 4U; ++window)
                            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + window * 8192U)) ==
                                    (outer + banks[window]) % (size / 8192U));
                    }
                }
            }
        }
    }
}
TEST_CASE("Mapper 245 retains physical pins across cached CHR accesses and register writes",
          "[mapper245]") {
    auto m = board();
    REQUIRE(m->observes_ppu_address());
    m->observe_ppu_address(0xC00U);
    reg(*m, 0U, 2U); // Inactive R0: A11 selects R1.
    REQUIRE(m->read_prg(0xE000U) == 63U);
    reg(*m, 1U, 2U);
    REQUIRE(m->read_prg(0xE000U) == 127U);
    m->read_chr(0U);
    m->write_chr(0U, 0xA7U);
    REQUIRE(m->read_prg(0xE000U) == 127U);
    m->write_prg(0x8000U, 0x80U); // Same pins now select R5.
    REQUIRE(m->read_prg(0xE000U) == 63U);
    reg(*m, 5U, 2U, 0x80U);
    REQUIRE(m->read_prg(0xE000U) == 127U);
    for (unsigned v = 0U; v < 256U; ++v) {
        reg(*m, 6U, static_cast<Byte>(v), 0x80U);
        reg(*m, 7U, static_cast<Byte>(v), 0x80U);
        REQUIRE(m->read_prg(0x8000U) == (64U | (v & 63U)));
        REQUIRE(m->read_prg(0xA000U) == (64U | (v & 63U)));
        REQUIRE(m->read_prg(0xC000U) == 126U);
    }
}
TEST_CASE("Mapper 245 physical PPUDATA and sprite fetch pins select PRG independently of A12",
          "[mapper245]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper245_rom());
    REQUIRE(c);
    c->write_prg(0x8000U, 1U);
    c->write_prg(0x8001U, 2U);
    Ppu ppu;
    ppu.connect_cartridge(&*c);
    for (unsigned byte = 0U; byte < 256U; ++byte)
        ppu.write_oam_dma(0xFFU); // Empty sprite slots drive the dummy tile $FF.
    const auto address = [&ppu](Address addr) {
        ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
        ppu.write_register(6U, static_cast<Byte>(addr));
    };
    address(0x800U);
    REQUIRE(c->read_prg(0xFA00U) == 127U);
    static_cast<void>(ppu.read_register(7U));
    REQUIRE(c->read_prg(0xFA00U) == 127U);
    address(0x1000U);
    REQUIRE(c->read_prg(0xFA00U) == 63U);
    address(0x2800U);
    REQUIRE(c->read_prg(0xFA00U) == 127U);
    address(0x2000U);
    ppu.write_register(0U, 8U);
    ppu.write_register(1U, 0x18U);
    ppu.step_dots(259U);
    REQUIRE(c->read_prg(0xFA00U) == 63U);
    ppu.step_dots(1U);
    REQUIRE(c->read_prg(0xFA00U) == 127U);
    c->write_prg(0x8000U, 0x80U);
    REQUIRE(c->read_prg(0xFA00U) == 63U);
}
TEST_CASE("Mapper 245 pattern RAM stays flat while WRAM obeys MMC3 protection", "[mapper245]") {
    auto m = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        m->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr >> 5U));
    }
    REQUIRE(m->battery_ram().size() == 8192U);
    for (Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte r = 0U; r < 6U; ++r)
            reg(*m, r, 0xFFU, mode);
        m->write_prg(0xA001U, 0xC0U);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            m->write_prg(static_cast<Address>(0x6000U + addr), 0U);
            REQUIRE(m->read_prg(static_cast<Address>(0x6000U + addr)) ==
                    static_cast<Byte>(addr >> 5U));
            REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr ^ (addr >> 8U)));
        }
    }
    m->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    m->write_chr(0x1234U, 0x59U);
    REQUIRE(m->read_chr(0x1234U) == 0x59U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6000U, 0xA7U);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
}
TEST_CASE("Mapper 245 reset policies retain battery data and clear cold pattern RAM",
          "[mapper245]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(1048576U, legacy, vertical);
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->configure_board_profile("unknown"));
            REQUIRE_FALSE(m->configure_board_jumpers(0U));
            m->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
            m->write_chr(0U, 0x5AU);
            reg(*m, 0U, 2U);
            m->write_prg(0xA000U, vertical ? 1U : 0U);
            m->reset();
            REQUIRE(m->read_prg(0xE000U) == 63U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            REQUIRE(m->read_chr(0U) == 0x5AU);
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            m->power_on();
            REQUIRE(m->read_chr(0U) == 0U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        }
}
TEST_CASE("Mapper 245 preserves scanline IRQ compatibility and partial counter snapshots",
          "[mapper245]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    const auto saved = state(*m);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->reset();
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending()); // Existing MMC3 repeating-zero policy; clone detail unverified.
}
TEST_CASE("Mapper 245 validates geometry and native RAM declarations", "[mapper245]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, unsigned size = 1048576U, unsigned chr = 0U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(size), std::vector<Byte>(chr)));
    };
    for (auto size : {0U, 16384U, 32769U, 98304U, 1048575U, 2097152U})
        reject(header(), size);
    reject(header(), 1048576U, 8192U);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h);
    }
    for (unsigned which = 0U; which < 7U; ++which) {
        auto h = header();
        switch (which) {
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
            h.chr_ram_size = 16384U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        case 5U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        reject(h);
    }
}
TEST_CASE("Mapper 245 snapshots bind geometry and reset mirroring and reject corruption atomically",
          "[mapper245]") {
    auto m = board();
    reg(*m, 5U, 2U, 0x80U);
    m->observe_ppu_address(0xC00U);
    m->write_chr(0x1FFFU, 0x5AU);
    m->write_prg(0x6000U, 0xA7U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 16402U);
    for (unsigned length : {0U, 8191U, 16384U, 16399U, 16401U}) {
        StateReader r{std::span(saved).first(length)};
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto [offset, bad] : std::array<std::pair<unsigned, Byte>, 10>{{{16384U, 8U},
                                                                               {16385U, 1U},
                                                                               {16392U, 64U},
                                                                               {16393U, 64U},
                                                                               {16394U, 9U},
                                                                               {16397U, 8U},
                                                                               {16398U, 1U},
                                                                               {16399U, 0U},
                                                                               {16400U, 0U},
                                                                               {16401U, 4U}}}) {
        auto corrupt = saved;
        corrupt[offset] = bad;
        StateReader r(corrupt);
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (auto other : {32768U, 524288U, 1048576U}) {
        auto target = board(other, false, other != 1048576U);
        const auto before = state(*target);
        StateReader r(saved);
        target->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*target) == before);
    }
    m->power_on();
    StateReader r(saved);
    m->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_prg(0xE000U) == 127U);
}
TEST_CASE("Mapper 245 original firmware exercises physical PPU pins IRQ RAM and machine replay",
          "[mapper245]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (unsigned size : {32768U, 524288U, 1048576U}) {
                auto c = Cartridge::from_data(
                    mapperbus::tests::make_mapper245_rom(legacy, vertical, size));
                REQUIRE(c);
                Emulator e;
                REQUIRE(e.load_cartridge(std::move(*c), ""));
                e.power_on();
                e.step_frame();
                INFO("size=" << size << " legacy=" << legacy << " vertical=" << vertical
                             << " pc=" << e.cpu().pc() << " irq=" << +e.read_cpu(0x11U));
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x11U) >= 3U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                REQUIRE(e.read_cpu(0x15U) == 0U);
                const auto saved = e.save_state();
                e.step_frame();
                const auto expected = e.save_state();
                REQUIRE(e.load_state(saved));
                e.step_frame();
                REQUIRE(e.save_state() == expected);
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0xA7U);
                REQUIRE(e.read_cpu(0x15U) == 0x5AU);
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0xA7U);
                REQUIRE(e.read_cpu(0x15U) == 0U);
                const auto before = e.save_state();
                auto bad = saved;
                bad.resize(bad.size() - 10U);
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
}
} // namespace
