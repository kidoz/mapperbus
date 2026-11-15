#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper254_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false, bool battery = true, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 254U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.prg_ram_size = battery && !legacy ? 0U : 8192U;
    h.prg_nvram_size = battery && !legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}
Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}
std::unique_ptr<Mapper> board(bool legacy = false,
                              bool battery = true,
                              bool vertical = true,
                              bool patterned = false) {
    register_builtin_mappers();
    auto prg = mapperbus::tests::mapper254_prg(), chr = mapperbus::tests::mapper254_chr();
    if (patterned) {
        for (unsigned i = 0U; i < prg.size(); ++i)
            prg[i] = cell(i / 8192U, i % 8192U);
        for (unsigned i = 0U; i < chr.size(); ++i)
            chr[i] = cell(i / 1024U, i % 1024U);
    }
    auto m = MapperRegistry::instance().create(
        header(legacy, battery, vertical), std::move(prg), std::move(chr));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void reg(Mapper& m, Byte index, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | mode));
    m.write_prg(0x8001U, value);
}
TEST_CASE("Mapper 254 registers native and legacy WRAM populations", "[mapper254]") {
    for (bool legacy : {false, true})
        for (bool battery : {false, true})
            for (bool vertical : {false, true}) {
                auto m = board(legacy, battery, vertical);
                REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
                REQUIRE(state(*m).size() == 8211U);
                REQUIRE(m->read_prg(0x8000U) == 0U);
                REQUIRE(m->read_prg(0xA000U) == 0U);
                REQUIRE(m->read_prg(0xC000U) == 14U);
                REQUIRE(m->read_prg(0xE000U) == 15U);
                REQUIRE(m->mirror_mode() ==
                        (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                REQUIRE(m->maps_prg(0x6000U));
                REQUIRE_FALSE(m->has_expansion_audio());
                REQUIRE_FALSE(m->configure_board_profile("unknown"));
                REQUIRE_FALSE(m->configure_board_jumpers(0U));
            }
}
TEST_CASE("Mapper 254 XORs every raw byte and mask while respecting MMC3 RAM enable",
          "[mapper254]") {
    auto m = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, static_cast<Byte>(raw));
        for (unsigned mask = 0U; mask < 256U; ++mask) {
            m->write_prg(0xA001U, static_cast<Byte>(mask));
            REQUIRE(m->maps_prg(0x6000U) == (mask >= 128U));
            REQUIRE(m->read_prg(0x6000U) == (mask >= 128U ? (raw ^ mask) : 0U));
            REQUIRE(m->battery_ram()[0] == raw);
        }
    }
}
TEST_CASE("Mapper 254 overlays all MMC3 CPU aliases and never unlocks from WRAM writes",
          "[mapper254]") {
    auto m = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 0xA5U);
        m->write_prg(static_cast<Address>(addr), 0xA3U);
        const bool unlock = (addr & 0xE001U) == 0x8000U;
        const unsigned key = (addr & 0xE001U) == 0xA001U ? 0xA3U : 0x80U;
        const unsigned raw = addr == 0x6000U ? 0xA3U : 0xA5U;
        REQUIRE(m->read_prg(0x6000U) == (unlock ? raw : raw ^ key));
        if (unlock) {
            const auto saved = state(*m);
            REQUIRE(saved[8192U] == 3U);
            REQUIRE(saved[8193U] == 0x80U);
            REQUIRE(saved[8209U] == 1U);
            m->write_prg(0xA001U, 0xFFU);
            REQUIRE(m->read_prg(0x6000U) == raw);
        }
    }
}
TEST_CASE("Mapper 254 masks PRG pins and maps patterned cells in both modes", "[mapper254]") {
    auto m = board(false, true, true, true);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Byte mode : {0U, 0x40U}) {
            reg(*m, 6U, static_cast<Byte>(value), mode);
            reg(*m, 7U, static_cast<Byte>(value), mode);
            REQUIRE(m->read_prg(0x8000U) == cell(mode == 0U ? value & 15U : 14U, 0U));
            REQUIRE(m->read_prg(0xA000U) == cell(value & 15U, 0U));
        }
    for (unsigned bank = 0U; bank < 16U; ++bank)
        for (Byte mode : {0U, 0x40U}) {
            reg(*m, 6U, static_cast<Byte>(bank), mode);
            reg(*m, 7U, 5U, mode);
            const unsigned pages[4] = {mode == 0U ? bank : 14U, 5U, mode == 0U ? 14U : bank, 15U};
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        cell(pages[(addr - 0x8000U) / 8192U], addr & 0x1FFFU));
        }
}
TEST_CASE("Mapper 254 maps all CHR data bytes in both inversions and ignores ROM writes",
          "[mapper254]") {
    auto m = board(false, true, true, true);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Byte mode : {0U, 0x80U}) {
            std::array<unsigned, 6> banks{};
            for (Byte r = 0U; r < 6U; ++r) {
                banks[r] = (value + r * 17U) & 255U;
                reg(*m, r, static_cast<Byte>(banks[r]), mode);
            }
            const unsigned pages[8] = {banks[0] & 126U,
                                       (banks[0] & 126U) + 1U,
                                       banks[1] & 126U,
                                       (banks[1] & 126U) + 1U,
                                       banks[2] & 127U,
                                       banks[3] & 127U,
                                       banks[4] & 127U,
                                       banks[5] & 127U};
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                m->write_chr(static_cast<Address>(addr), 0xFFU);
                REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                        cell(pages[(addr / 1024U) ^ (mode == 0U ? 0U : 4U)], addr & 1023U));
            }
        }
}
TEST_CASE("Mapper 254 exposes open bus for disabled WRAM and persists raw protected bytes",
          "[mapper254]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper254_rom(false, true));
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    bus.write(0xA001U, 0x95U);
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        bus.write(static_cast<Address>(0x6000U + offset), cell(0U, offset));
    bus.write(0xA001U, 0xFFU);
    for (unsigned offset = 0U; offset < 8192U; ++offset) {
        bus.write(static_cast<Address>(0x6000U + offset), 0U);
        REQUIRE(bus.read(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(cell(0U, offset) ^ 255U));
    }
    bus.write(0xA001U, 0x15U);
    for (Address addr : {0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0xA001U, 0x80U);
    bus.write(0x9FFEU, 0U);
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        REQUIRE(bus.read(static_cast<Address>(0x6000U + offset)) == cell(0U, offset));
}
TEST_CASE("Mapper 254 reset rearms protection and cold power clears only volatile WRAM",
          "[mapper254]") {
    for (bool battery : {false, true}) {
        auto m = board(false, battery, false);
        m->write_prg(0x6000U, 0xA7U);
        reg(*m, 6U, 9U, 0xC0U);
        m->write_prg(0xA001U, 0x95U);
        m->write_prg(0xA000U, 0U);
        m->reset();
        REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(m->read_prg(0xC000U) == 14U);
        m->write_prg(0xBFFFU, 0x95U);
        REQUIRE(m->read_prg(0x6000U) == (0xA7U ^ 0x95U));
        m->power_on();
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0xA7U : 0U));
        m->write_prg(0xA001U, 0x80U);
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0x27U : 0x80U));
    }
}
TEST_CASE("Mapper 254 retains the MMC3 IRQ counter acknowledgement and zero-latch policy",
          "[mapper254]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xA001U, 0x95U);
    m->write_prg(0x9FFEU, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    StateReader r(partial);
    m->load_state(r);
    REQUIRE(r.ok());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (unsigned n = 0U; n < 3U; ++n) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        m->write_prg(0xE001U, 0U);
    }
}
TEST_CASE("Mapper 254 bounded states reject malformed fields and population mismatches atomically",
          "[mapper254]") {
    auto m = board();
    m->write_prg(0x6000U, 0xA7U);
    m->write_prg(0xA001U, 0x95U);
    for (bool unlocked : {false, true}) {
        if (unlocked)
            reg(*m, 6U, 9U, 0xC0U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 8211U);
        REQUIRE(saved[8209U] == static_cast<Byte>(unlocked));
        for (unsigned length : {0U, 1U, 8191U, 8192U, 8207U, 8210U}) {
            StateReader r(std::span(saved).first(length));
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        constexpr std::array<std::pair<unsigned, Byte>, 10> kBad{
            {{8192U, 8U},
             {8193U, 1U},
             {8200U, 64U},
             {8201U, 64U},
             {8202U, static_cast<Byte>(MirrorMode::FourScreen)},
             {8205U, 8U},
             {8206U, 1U},
             {8207U, 0xFFU},
             {8208U, 0U},
             {8209U, 2U}}};
        for (const auto [offset, value] : kBad) {
            auto bad = saved;
            bad[0] ^= 0xFFU;
            bad[offset] = value;
            StateReader r(bad);
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        m->power_on();
        StateReader r(saved);
        m->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE(state(*m) == saved);
        REQUIRE(m->read_prg(0x6000U) == (unlocked ? 0xA7U : 0x32U));
        for (const auto [battery, vertical] : {std::pair{false, true}, std::pair{true, false}}) {
            auto other = board(false, battery, vertical);
            const auto before = state(*other);
            StateReader mismatch(saved);
            other->load_state(mismatch);
            REQUIRE_FALSE(mismatch.ok());
            REQUIRE(state(*other) == before);
        }
    }
}
TEST_CASE("Mapper 254 accepts only the documented geometry and explicit native RAM metadata",
          "[mapper254]") {
    register_builtin_mappers();
    const auto reject = [](INesHeader h, unsigned p = 131072U, unsigned c = 131072U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (unsigned size : {0U, 32768U, 65536U, 262144U}) {
        reject(header(), size);
        reject(header(), 131072U, size);
    }
    for (unsigned variant = 0U; variant < 8U; ++variant) {
        auto h = header();
        switch (variant) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.chr_ram_size = 8192U;
            break;
        case 2U:
            h.chr_nvram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 0U;
            break;
        case 4U:
            h.prg_ram_size = 8192U;
            break;
        case 5U:
            h.prg_nvram_size = 16384U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        reject(h);
    }
}
TEST_CASE("Mapper 254 original firmware verifies protection banking CIRAM IRQ reset and replay",
          "[mapper254]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool battery : {false, true})
            for (bool vertical : {false, true}) {
                auto c = Cartridge::from_data(
                    mapperbus::tests::make_mapper254_rom(legacy, battery, vertical));
                REQUIRE(c);
                Emulator e;
                REQUIRE(e.load_cartridge(std::move(*c), ""));
                e.power_on();
                e.step_frame();
                INFO("pc=" << e.cpu().pc() << " irq=" << +e.read_cpu(0x11U));
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x11U) >= 3U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                REQUIRE(e.read_cpu(0x15U) == 0x80U);
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
                REQUIRE(e.read_cpu(0x15U) == 0x27U);
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == (battery ? 0xA7U : 0U));
                REQUIRE(e.read_cpu(0x15U) == (battery ? 0x27U : 0x80U));
                const auto before = e.save_state();
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                auto bad = saved;
                bad[bad.size() - tail.data().size() - 8211U + 8208U] ^= 1U;
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
                bad.resize(bad.size() - 10U);
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
}
} // namespace
