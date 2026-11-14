#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper253_rom.hpp"
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
    h.mapper_number = 253U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.prg_ram_size = battery && !legacy ? 0U : 8192U;
    h.prg_nvram_size = battery && !legacy ? 8192U : 0U;
    h.chr_ram_size = legacy ? 0U : 2048U;
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
    auto prg = mapperbus::tests::mapper253_prg(), chr = mapperbus::tests::mapper253_chr();
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
void chr_bank(Mapper& m, unsigned slot, unsigned bank) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 8U);
    m.write_prg(reg, static_cast<Byte>(bank & 15U));
    m.write_prg(reg + 4U, static_cast<Byte>(bank >> 4U));
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 253 registers mixed CHR with native and legacy WRAM populations", "[mapper253]") {
    for (bool legacy : {false, true})
        for (bool battery : {false, true})
            for (bool vertical : {false, true}) {
                auto m = board(legacy, battery, vertical);
                REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
                REQUIRE(state(*m).size() == 10270U);
                REQUIRE(m->read_prg(0x8000U) == 0U);
                REQUIRE(m->read_prg(0xA000U) == 1U);
                REQUIRE(m->read_prg(0xC000U) == 30U);
                REQUIRE(m->read_prg(0xE000U) == 31U);
                REQUIRE(m->mirror_mode() ==
                        (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                REQUIRE_FALSE(m->maps_prg(0x6000U));
                REQUIRE_FALSE(m->has_expansion_audio());
                REQUIRE_FALSE(m->configure_board_profile("unknown"));
                REQUIRE_FALSE(m->configure_board_jumpers(0U));
            }
}
TEST_CASE("Mapper 253 decodes A2 A3 at every CPU register alias", "[mapper253]") {
    auto m = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0x1BU);
        const unsigned group = addr >> 12U, port = (addr >> 2U) & 3U;
        const bool swap = group == 9U && port == 2U;
        REQUIRE(m->read_prg(0x8000U) == (swap ? 30U : group == 8U ? 27U : 0U));
        REQUIRE(m->read_prg(0xA000U) == (group == 10U ? 27U : 1U));
        REQUIRE(m->read_prg(0xC000U) == (swap ? 0U : 30U));
        REQUIRE(m->read_prg(0xE000U) == 31U);
        REQUIRE(m->maps_prg(0x6000U) == swap);
        REQUIRE(m->mirror_mode() ==
                (group == 9U && port == 0U ? MirrorMode::SingleUpper : MirrorMode::Vertical));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const bool selected =
                group >= 11U && group <= 14U && slot == (group - 11U) * 2U + port / 2U;
            REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U)) ==
                    (selected ? (port & 1U) == 0U ? 11U : 176U : 0U));
        }
    }
}
TEST_CASE("Mapper 253 masks every PRG data byte and maps patterned cells in both swap modes",
          "[mapper253]") {
    auto m = board(false, true, true, true);
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0x8000U, static_cast<Byte>(value));
        m->write_prg(0xA000U, static_cast<Byte>(value));
        m->write_prg(0x9008U, 0U);
        REQUIRE(m->read_prg(0x8000U) == cell(value & 31U, 0U));
        REQUIRE(m->read_prg(0xA000U) == cell(value & 31U, 0U));
    }
    for (unsigned bank = 0U; bank < 32U; ++bank)
        for (Byte mode : {0U, 2U}) {
            m->write_prg(0x8000U, static_cast<Byte>(bank));
            m->write_prg(0xA000U, static_cast<Byte>(bank));
            m->write_prg(0x9008U, mode);
            const unsigned pages[4] = {mode == 0U ? bank : 30U, bank, mode == 0U ? 30U : bank, 31U};
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        cell(pages[(addr - 0x8000U) / 8192U], addr & 0x1FFFU));
        }
}
TEST_CASE("Mapper 253 selects exact raw CHR banks 4 and 5 before ROM wrapping", "[mapper253]") {
    auto m = board(false, true, true, true);
    for (unsigned bank = 0U; bank < 512U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot)
            chr_bank(*m, slot, bank);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            const auto data = static_cast<Byte>(addr ^ (addr >> 8U));
            m->write_chr(static_cast<Address>(addr), data);
            REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                    (bank == 4U || bank == 5U ? data : cell(bank % 256U, addr & 0x3FFU)));
        }
    }
    chr_bank(*m, 0U, 4U);
    chr_bank(*m, 1U, 5U);
    m->write_chr(0U, 0x5AU);
    m->write_chr(0x400U, 0xA6U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        chr_bank(*m, slot, 4U);
        REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U)) == 0x5AU);
        chr_bank(*m, slot, 5U);
        REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U)) == 0xA6U);
    }
    // All data bits above each VRC4 nibble are disconnected except high D4.
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0xB000U, static_cast<Byte>(value));
        m->write_prg(0xB004U, static_cast<Byte>(value));
        const auto saved = state(*m);
        StateReader r(std::span(saved).subspan(4U));
        REQUIRE(r.read<uint16_t>() == ((value & 15U) | ((value & 31U) << 4U)));
    }
}
TEST_CASE("Mapper 253 WRAM gates and low CPU open bus use the VRC4e control port", "[mapper253]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper253_rom());
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        bus.write(addr, 0xA7U);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x9008U, 1U);
    REQUIRE(bus.read(0x6000U) == 0U);
    for (unsigned addr = 0x6000U; addr <= 0x7FFFU; ++addr)
        bus.write(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    bus.write(0x9008U, 0U);
    bus.write(0x6000U, 0U);
    bus.write(0x9008U, 3U);
    for (unsigned addr = 0x6000U; addr <= 0x7FFFU; ++addr)
        REQUIRE(bus.read(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ (addr >> 8U)));
}
TEST_CASE("Mapper 253 reset restores registers while cold power clears only volatile memory",
          "[mapper253]") {
    for (bool battery : {false, true}) {
        auto m = board(false, battery, false);
        m->write_prg(0x9008U, 1U);
        m->write_prg(0x6000U, 0xA7U);
        chr_bank(*m, 0U, 4U);
        m->write_chr(0U, 0x5AU);
        m->write_prg(0x9000U, 3U);
        m->reset();
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        m->write_prg(0x9008U, 1U);
        chr_bank(*m, 0U, 4U);
        REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        REQUIRE(m->read_chr(0U) == 0x5AU);
        m->power_on();
        m->write_prg(0x9008U, 1U);
        chr_bank(*m, 0U, 4U);
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0xA7U : 0U));
        REQUIRE(m->read_chr(0U) == 0U);
    }
}
TEST_CASE("Mapper 253 CPU IRQ modes cover all latches acknowledgement and fractional scanlines",
          "[mapper253]") {
    auto m = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        m->reset();
        m->write_prg(0xF000U, static_cast<Byte>(latch & 15U));
        m->write_prg(0xF004U, static_cast<Byte>(latch >> 4U));
        m->write_prg(0xF008U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle)
            m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_cpu();
        REQUIRE(m->irq_pending());
        m->write_prg(0xF00CU, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle)
            m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
    }
    m->write_prg(0xF000U, 15U);
    m->write_prg(0xF004U, 15U);
    m->write_prg(0xF008U, 3U);
    for (unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle)
            m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_cpu();
        REQUIRE(m->irq_pending());
        m->write_prg(0xF00CU, 0U);
    }
    m->write_prg(0xF008U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle)
        m->clock_cpu();
    const auto partial = state(*m);
    m->write_prg(0xF008U, 0U); // Every control write resets the divider, including disable.
    const auto disabled = state(*m);
    StateReader divider(std::span(disabled).subspan(24U));
    REQUIRE(divider.read<uint16_t>() == 341U);
    REQUIRE(disabled[23U] == partial[23U]);
    m->reset();
    StateReader r(partial);
    m->load_state(r);
    REQUIRE(r.ok());
    for (unsigned cycle = 0U; cycle < 13U; ++cycle)
        m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_cpu();
    REQUIRE(m->irq_pending());
}
TEST_CASE("Mapper 253 rejects other populations and unknown board profiles", "[mapper253]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, unsigned p = 262144U, unsigned c = 262144U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (unsigned size : {0U, 16384U, 32769U, 98304U, 262143U, 262145U, 524288U})
        reject(header(), size);
    for (unsigned size : {0U, 4096U, 8193U, 98304U, 524287U, 524289U, 1048576U})
        reject(header(), 262144U, size);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h);
    }
    for (unsigned which = 0U; which < 6U; ++which) {
        auto h = header();
        switch (which) {
        case 0U:
            h.chr_ram_size = 0U;
            break;
        case 1U:
            h.chr_nvram_size = 2048U;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 0U;
            break;
        case 4U:
            h.has_battery = false;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        reject(h);
    }
    auto m = board();
    const auto before = state(*m);
    REQUIRE_FALSE(m->configure_board_profile("unknown"));
    REQUIRE(state(*m) == before);
}
TEST_CASE("Mapper 253 stages bounded snapshots and rejects malformed fields atomically",
          "[mapper253]") {
    auto m = board();
    m->write_prg(0x9008U, 1U);
    m->write_prg(0x6000U, 0xA7U);
    chr_bank(*m, 0U, 4U);
    m->write_chr(0U, 0x5AU);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 10270U);
    for (unsigned length : {0U, 1U, 4U, 20U, 29U, 8220U, 10269U}) {
        StateReader r(std::span(saved).first(length));
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    constexpr std::array<std::pair<unsigned, Byte>, 13> kBadFields{{{0U, 0U},
                                                                    {1U, 0U},
                                                                    {2U, 32U},
                                                                    {3U, 32U},
                                                                    {5U, 2U},
                                                                    {20U, 2U},
                                                                    {21U, 4U},
                                                                    {20U, 5U},
                                                                    {25U, 2U},
                                                                    {26U, 8U},
                                                                    {27U, 2U},
                                                                    {28U, 2U},
                                                                    {28U, 1U}}};
    for (const auto [offset, value] : kBadFields) {
        auto bad = saved;
        bad[offset] = value;
        StateReader r(bad);
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    auto zero_prescaler = saved;
    zero_prescaler[24U] = zero_prescaler[25U] = 0U;
    StateReader zero(zero_prescaler);
    m->load_state(zero);
    REQUIRE_FALSE(zero.ok());
    REQUIRE(state(*m) == saved);
    for (bool battery : {false, true}) {
        auto other = board(false, battery, !battery);
        const auto before = state(*other);
        StateReader r(saved);
        other->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*other) == before);
    }
    m->power_on();
    StateReader r(saved);
    m->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(state(*m) == saved);
}
TEST_CASE("Mapper 253 geometry envelopes alias unconnected ROM pins", "[mapper253]") {
    register_builtin_mappers();
    for (unsigned p : {32768U, 65536U, 131072U, 262144U})
        for (unsigned c : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U, 524288U}) {
            auto m = MapperRegistry::instance().create(
                header(), std::vector<Byte>(p, 0xA5U), std::vector<Byte>(c, 0x59U));
            REQUIRE(m);
            REQUIRE((*m)->read_prg(0xE000U) == 0xA5U);
            chr_bank(**m, 0U, 0x1FFU);
            REQUIRE((*m)->read_chr(0U) == 0x59U);
        }
}
TEST_CASE("Mapper 253 f009s commands require PPU writes at the exact selected bank",
          "[mapper253]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("f009s"));
    for (unsigned bank = 0U; bank < 512U; ++bank)
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            m->power_on();
            chr_bank(*m, slot, bank);
            m->read_chr(static_cast<Address>(slot * 1024U));
            const auto probe_slot = (slot + 1U) & 7U;
            chr_bank(*m, probe_slot, 0x4CU);
            REQUIRE(m->read_chr(static_cast<Address>(probe_slot * 1024U)) == 0x4CU);
            m->write_chr(static_cast<Address>(slot * 1024U + 0x3FFU), 0U);
            for (unsigned probe : {0x28U, 0x4CU, 0x7CU, 4U}) {
                chr_bank(*m, 0U, probe);
                const unsigned active = bank == 0x88U   ? 0x4CU
                                        : bank == 0xC2U ? 0x7CU
                                        : bank == 0xC8U ? 4U
                                                        : 0x28U;
                REQUIRE(m->read_chr(0U) == (probe == active ? 0U : probe));
            }
        }
}
TEST_CASE("Mapper 253 relocated windows and ninth-bit aliases share the same physical 2 KiB",
          "[mapper253]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("f009s"));
    m->power_on();
    chr_bank(*m, 0U, 0x28U);
    chr_bank(*m, 1U, 0x29U);
    for (unsigned offset = 0U; offset < 1024U; ++offset) {
        m->write_chr(static_cast<Address>(offset), 0x5AU);
        m->write_chr(static_cast<Address>(1024U + offset), 0xA6U);
    }
    for (unsigned command : {0x80U, 0x88U, 0xC2U, 0xC8U}) {
        if (command != 0x80U) {
            chr_bank(*m, 7U, command);
            m->write_chr(0x1C00U, 0xFFU);
        }
        const unsigned first = command == 0x80U   ? 0x28U
                               : command == 0x88U ? 0x4CU
                               : command == 0xC2U ? 0x7CU
                                                  : 4U;
        const unsigned count = command == 0x80U || command == 0x88U ? 4U : 2U;
        for (unsigned bank = 0U; bank < 512U; ++bank) {
            chr_bank(*m, 0U, bank);
            const bool ram = (bank & 255U) >= first && (bank & 255U) < first + count;
            for (Address offset : {0U, 0x3FFU})
                REQUIRE(m->read_chr(offset) ==
                        (ram ? (bank & 1U) == 0U ? 0x5AU : 0xA6U : bank & 255U));
        }
    }
    const auto before = state(*m);
    REQUIRE_FALSE(m->configure_board_profile("unknown"));
    REQUIRE(state(*m) == before);
    auto old_board = board();
    chr_bank(*old_board, 0U, 0x88U);
    old_board->write_chr(0U, 0U);
    chr_bank(*old_board, 0U, 4U);
    REQUIRE(old_board->read_chr(0U) == 0U);
    chr_bank(*old_board, 0U, 0x4CU);
    REQUIRE(old_board->read_chr(0U) == 0x4CU);
}
TEST_CASE("Mapper 253 decoder survives soft reset and snapshots bind profile geometry and mode",
          "[mapper253]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("f009s"));
    chr_bank(*m, 0U, 0x88U);
    m->write_chr(0U, 0U);
    chr_bank(*m, 0U, 0x4CU);
    m->write_chr(0U, 0x5AU);
    m->reset();
    chr_bank(*m, 0U, 0x4CU);
    REQUIRE(m->read_chr(0U) == 0x5AU);
    const auto saved = state(*m);
    REQUIRE(saved[29U] == 0x88U);
    auto fixed = board();
    const auto fixed_before = state(*fixed);
    StateReader different_profile(saved);
    fixed->load_state(different_profile);
    REQUIRE_FALSE(different_profile.ok());
    REQUIRE(state(*fixed) == fixed_before);
    auto corrupt = saved;
    corrupt[29U] = 0x82U; // Other GAL-family commands have no 253-specific definition here.
    StateReader bad_mode(corrupt);
    m->load_state(bad_mode);
    REQUIRE_FALSE(bad_mode.ok());
    REQUIRE(state(*m) == saved);
    for (unsigned p : {32768U, 262144U}) {
        auto target = MapperRegistry::instance().create(
            header(), std::vector<Byte>(p), std::vector<Byte>(8192U));
        REQUIRE(target);
        REQUIRE((*target)->configure_board_profile("f009s"));
        const auto before = state(**target);
        StateReader mismatch(saved);
        (*target)->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(**target) == before);
    }
    m->power_on();
    REQUIRE(state(*m)[29U] == 0x80U);
    chr_bank(*m, 0U, 0x28U);
    REQUIRE(m->read_chr(0U) == 0U);
    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_chr(0U) == 0x5AU);
}
TEST_CASE("Mapper 253 original CPU firmware checks mixed memory all CIRAM modes IRQs and replay",
          "[mapper253]") {
    register_builtin_mappers();
    for (bool dynamic : {false, true})
        for (bool legacy : {false, true})
            for (bool battery : {false, true})
                for (bool vertical : {false, true}) {
                    auto c = Cartridge::from_data(
                        mapperbus::tests::make_mapper253_rom(legacy, battery, vertical, dynamic));
                    REQUIRE(c);
                    REQUIRE(c->configure_board_profile(dynamic ? "f009s" : "fixed"));
                    Emulator e;
                    REQUIRE(e.load_cartridge(std::move(*c), ""));
                    e.power_on();
                    e.step_frame();
                    INFO("pc=" << e.cpu().pc() << " irq=" << +e.read_cpu(0x11U));
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x11U) >= 6U);
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
                    REQUIRE(e.read_cpu(0x14U) == (battery ? 0xA7U : 0U));
                    REQUIRE(e.read_cpu(0x15U) == 0U);
                    const auto before = e.save_state();
                    StateWriter tail;
                    Controller{}.save_state(tail);
                    Controller{}.save_state(tail);
                    auto bad = saved;
                    bad[bad.size() - tail.data().size() - 10270U] ^= 1U;
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                    bad.resize(bad.size() - 10U);
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
}
} // namespace
