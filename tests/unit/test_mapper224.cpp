#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper224.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 0x2000000U;
constexpr std::size_t kChrSize = 0x40000U;

INesHeader header(bool legacy = false, bool battery = false, bool wram = true) {
    INesHeader h{};
    h.mapper_number = 224U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.mirror_mode = MirrorMode::Vertical;
    h.chr_ram_size = legacy ? 0U : kChrSize;
    h.prg_ram_size = !legacy && wram && !battery ? 8192U : 0U;
    h.prg_nvram_size = !legacy && battery ? 8192U : 0U;
    return h;
}

std::vector<Byte> prg_rom(std::size_t size = kPrgSize) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; i += 8192U) {
        const auto page = i / 8192U;
        data[i] = static_cast<Byte>(page);
        data[i + 1U] = static_cast<Byte>(page >> 8U);
    }
    return data;
}

std::unique_ptr<Kt008> board(bool legacy = false,
                             bool battery = false,
                             bool wram = true,
                             std::size_t prg_size = kPrgSize,
                             std::size_t chr_size = kChrSize) {
    auto h = header(legacy, battery, wram);
    if (!legacy)
        h.chr_ram_size = chr_size;
    return std::make_unique<Kt008>(h, prg_rom(prg_size), std::vector<Byte>{});
}

void bank(Kt008& m, Byte index, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | mode));
    m.write_prg(0x8001U, value);
}

unsigned prg_page(Kt008& m, Address addr) {
    return m.read_prg(addr) | (static_cast<unsigned>(m.read_prg(addr + 1U)) << 8U);
}

unsigned chr_bank(Kt008& m, Address addr) {
    return m.read_chr(addr) | (static_cast<unsigned>(m.read_chr(addr + 1U)) << 8U);
}

void set_pin(unsigned& page, unsigned pin, bool high) {
    const auto mask = 1U << (pin - 13U);
    page = (page & ~mask) | (high ? mask : 0U);
}

Byte oracle_mmc3_chr(const std::array<Byte, 8>& regs, Byte select, Byte mode, unsigned slot) {
    const auto mapped = slot ^ ((select & 0x80U) != 0U ? 4U : 0U);
    if (mapped < 4U) {
        if ((mode & 0x40U) != 0U)
            return (mapped & 1U) != 0U ? 0U : regs[mapped / 2U];
        return (regs[mapped / 2U] & 0xFEU) | (mapped & 1U);
    }
    return regs[mapped - 2U];
}

Byte oracle_chr(const std::array<Byte, 8>& regs,
                Byte select,
                const std::array<Byte, 6>& outer,
                unsigned slot) {
    Byte result = oracle_mmc3_chr(regs, select, outer[3U], slot);
    if ((outer[3U] & 0x10U) != 0U) {
        const auto control = static_cast<unsigned>(outer[2U]);
        const auto masked = static_cast<Byte>(control & ~((control >> 4U) & 7U) & 15U);
        result = static_cast<Byte>((static_cast<unsigned>(masked) << 3U) | (slot & 7U) |
                                   (static_cast<unsigned>(result) & 0x80U));
    }
    if ((outer[0U] & 0x80U) != 0U)
        result = static_cast<Byte>((result & 0x7FU) | ((outer[0U] & 0x10U) << 3U));
    return result;
}

unsigned oracle_prg(const std::array<Byte, 8>& regs,
                    Byte select,
                    const std::array<Byte, 6>& outer,
                    Byte ppu_slot,
                    Address addr) {
    const auto address = static_cast<unsigned>(addr);
    const auto slot = (addr - 0x8000U) >> 13U;
    const bool swapped = (select & 0x40U) != 0U;
    const bool weird = (outer[3U] & 0x40U) != 0U;
    unsigned page = 0U;
    if ((outer[3U] & 0x10U) != 0U) {
        page = (outer[3U] & 6U) << 2U;
        const auto cpu_a14 = ((address >> 14U) & 1U);
        const auto offset_a14 = (static_cast<unsigned>(outer[3U]) >> 3U) & 1U;
        page |= (((outer[1U] & 2U) != 0U ? cpu_a14 : offset_a14) << 1U);
        page |= (address >> 13U) & 1U;
    } else {
        const std::array<unsigned, 4> pages = {swapped ? 0xFEU : regs[6U],
                                               regs[7U],
                                               swapped ? regs[6U]
                                               : weird ? 0U
                                                       : 0xFEU,
                                               weird && !swapped ? 0U : 0xFFU};
        page = pages[slot];
    }
    set_pin(page, 17U, (outer[0U] & 0x40U) != 0U ? (outer[0U] & 1U) != 0U : (page & 0x10U) != 0U);
    set_pin(page, 18U, (outer[0U] & 0x80U) != 0U ? (outer[0U] & 2U) != 0U : (page & 0x20U) != 0U);
    set_pin(page, 19U, (outer[1U] & 0x40U) == 0U ? (outer[0U] & 4U) != 0U : (page & 0x40U) != 0U);
    set_pin(
        page, 20U, (outer[1U] & 0x80U) == 0U ? (outer[1U] & 0x10U) != 0U : (page & 0x80U) != 0U);
    set_pin(page, 21U, (outer[1U] & 4U) != 0U);
    set_pin(page, 22U, (outer[1U] & 8U) != 0U);
    set_pin(page, 23U, (outer[0U] & 0x20U) != 0U);
    set_pin(page, 24U, (outer[0U] & 0x40U) != 0U);
    if ((outer[5U] & 0x10U) != 0U) {
        const auto chr = oracle_chr(regs, select, outer, ppu_slot);
        set_pin(page, 18U, (chr & 0x40U) != 0U);
        set_pin(page, 19U, (chr & 0x80U) != 0U);
    }
    return page;
}

std::vector<Byte> state(const Kt008& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

void initialize_chr(Kt008& m) {
    for (unsigned value = 0U; value < 256U; ++value) {
        bank(m, 2U, static_cast<Byte>(value));
        m.write_chr(0x1000U, static_cast<Byte>(value));
        m.write_chr(0x1001U, static_cast<Byte>(value >> 8U));
    }
}

TEST_CASE("Mapper 224 decodes every supervisor alias as write-only registers", "[mapper224]") {
    auto m = board();
    for (unsigned raw = 0x5000U; raw < 0x6000U; ++raw) {
        m->reset();
        const auto before = state(*m);
        const auto addr = static_cast<Address>(raw);
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_expansion(addr, 0x25U);
        const auto saved = state(*m);
        if ((raw & 7U) < 6U) {
            for (unsigned index = 0U; index < 6U; ++index)
                REQUIRE(saved[11U + index] == (index == (raw & 7U) ? 0x25U : 0U));
        } else {
            REQUIRE(saved == before);
        }
    }
    m->reset();
    REQUIRE(prg_page(*m, 0x8000U) == 0U);
    REQUIRE(prg_page(*m, 0xA000U) == 0U);
    REQUIRE(prg_page(*m, 0xC000U) == 0x3EU);
    REQUIRE(prg_page(*m, 0xE000U) == 0x3FU);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(m->has_expansion_audio());
}

TEST_CASE("Mapper 224 applies all normal MMC3 and outer PRG pin combinations", "[mapper224]") {
    auto m = board();
    std::array<Byte, 8> regs{};
    regs[6U] = 0x35U;
    regs[7U] = 0x2BU;
    for (Byte select : {0U, 0x40U}) {
        bank(*m, 6U, regs[6U], select);
        bank(*m, 7U, regs[7U], select);
        for (unsigned first = 0U; first < 256U; ++first) {
            m->write_expansion(0x5000U, static_cast<Byte>(first));
            for (unsigned second = 0U; second < 256U; ++second) {
                m->write_expansion(0x5001U, static_cast<Byte>(second));
                const std::array<Byte, 6> outer = {
                    static_cast<Byte>(first), static_cast<Byte>(second), 0U, 0U, 0U, 0U};
                for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
                    REQUIRE(prg_page(*m, addr) == oracle_prg(regs, select, outer, 0U, addr));
            }
        }
    }
}

TEST_CASE("Mapper 224 normal CHR pairing inversion and outer A17 cover every bank", "[mapper224]") {
    auto m = board();
    initialize_chr(*m);
    std::array<Byte, 8> regs{};
    for (unsigned data = 0U; data < 256U; ++data) {
        for (Byte select : {0U, 0x80U}) {
            for (Byte index = 0U; index < 6U; ++index) {
                bank(*m, index, static_cast<Byte>(data ^ (index * 37U)), select);
                regs[index] = static_cast<Byte>(data ^ (index * 37U));
            }
            for (Byte control : {0U, 0x10U, 0x80U, 0x90U}) {
                m->write_expansion(0x5000U, control);
                const std::array<Byte, 6> outer = {control, 0U, 0U, 0U, 0U, 0U};
                for (unsigned slot = 0U; slot < 8U; ++slot)
                    REQUIRE(chr_bank(*m, static_cast<Address>(slot * 1024U)) ==
                            oracle_chr(regs, select, outer, slot));
            }
        }
    }
}

TEST_CASE("Mapper 224 GNROM and weird modes obey independent PRG and CHR wiring", "[mapper224]") {
    auto m = board();
    initialize_chr(*m);
    std::array<Byte, 8> regs = {0xA7U, 0x35U, 0xC9U, 0x72U, 0xEDU, 0x51U, 0x35U, 0x2BU};
    for (Byte index = 0U; index < 8U; ++index)
        bank(*m, index, regs[index]);
    for (unsigned control = 0U; control < 256U; ++control) {
        m->reset();
        for (Byte index = 0U; index < 8U; ++index)
            bank(*m, index, regs[index]);
        m->write_expansion(0x5000U, static_cast<Byte>(control ^ 0xA5U));
        m->write_expansion(0x5001U, static_cast<Byte>(control));
        m->write_expansion(0x5002U, static_cast<Byte>(control ^ 0x5AU));
        m->write_expansion(0x5003U, static_cast<Byte>(control));
        const std::array<Byte, 6> outer = {static_cast<Byte>(control ^ 0xA5U),
                                           static_cast<Byte>(control),
                                           static_cast<Byte>(control ^ 0x5AU),
                                           static_cast<Byte>(control),
                                           0U,
                                           0U};
        for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
            INFO("control=" << control << " addr=" << addr);
            REQUIRE(prg_page(*m, addr) == oracle_prg(regs, 0U, outer, 0U, addr));
        }
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(chr_bank(*m, static_cast<Address>(slot * 1024U)) ==
                    oracle_chr(regs, 0U, outer, slot));
    }
    m->reset();
    for (Byte index = 0U; index < 8U; ++index)
        bank(*m, index, regs[index]);
    m->write_expansion(0x5003U, 0x50U);
    for (Byte select : {0U, 0x80U}) {
        bank(*m, 6U, regs[6U], select);
        for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
            REQUIRE(prg_page(*m, addr) ==
                    oracle_prg(regs, select, {0U, 0U, 0U, 0x50U, 0U, 0U}, 0U, addr));
    }
}

TEST_CASE("Mapper 224 mega-UNROM follows the live CHR pins", "[mapper224]") {
    auto m = board();
    std::array<Byte, 8> regs{};
    regs[6U] = 0x35U;
    bank(*m, 6U, regs[6U]);
    m->write_expansion(0x5005U, 0x10U);
    for (unsigned data = 0U; data < 256U; ++data) {
        bank(*m, 2U, static_cast<Byte>(data));
        regs[2U] = static_cast<Byte>(data);
        m->observe_ppu_address(0x1000U);
        const std::array<Byte, 6> outer = {0U, 0U, 0U, 0U, 0U, 0x10U};
        REQUIRE(prg_page(*m, 0x8000U) == oracle_prg(regs, 0U, outer, 4U, 0x8000U));
        m->observe_ppu_address(0U);
        REQUIRE(prg_page(*m, 0x8000U) == oracle_prg(regs, 0U, outer, 0U, 0x8000U));
    }
    m->write_expansion(0x5000U, 0x90U);
    m->write_expansion(0x5002U, 0x0FU);
    m->write_expansion(0x5003U, 0x10U);
    const std::array<Byte, 6> outer = {0x90U, 0U, 0x0FU, 0x10U, 0U, 0x10U};
    REQUIRE(prg_page(*m, 0x8000U) == oracle_prg(regs, 0U, outer, 0U, 0x8000U));
}

TEST_CASE("Mapper 224 supervisor and GNROM mask locks retain documented fields", "[mapper224]") {
    auto m = board();
    m->write_expansion(0x5000U, 0x35U);
    m->write_expansion(0x5001U, 0x62U);
    m->write_expansion(0x5003U, 0x80U);
    const auto locked = state(*m);
    for (Address addr : {0x5000U, 0x5001U, 0x5003U, 0x5004U, 0x5005U})
        m->write_expansion(addr, 0xFFU);
    REQUIRE(state(*m) == locked);
    m->write_expansion(0x5002U, 0xF5U);
    m->write_expansion(0x5002U, 0x0AU);
    REQUIRE(state(*m)[13U] == 0xFAU);
    m->reset();
    m->write_expansion(0x5003U, 0x90U);
    m->write_expansion(0x5000U, 0xFFU);
    REQUIRE(state(*m)[11U] == 0xFFU);
    m->write_expansion(0x5003U, 0x80U);
    m->write_expansion(0x5000U, 0U);
    REQUIRE(state(*m)[11U] == 0xFFU);
    m->reset();
    REQUIRE(state(*m)[13U] == 0U);
    REQUIRE(state(*m)[14U] == 0U);
}

TEST_CASE("Mapper 224 MMC3 aliases control mirroring WRAM and NEC-style IRQs", "[mapper224]") {
    auto m = board();
    initialize_chr(*m);
    for (unsigned raw = 0x8000U; raw < 0xA000U; ++raw) {
        m->reset();
        INFO("raw=" << raw);
        if ((raw & 1U) == 0U) {
            m->write_prg(static_cast<Address>(raw), 0xC2U);
            m->write_prg(0x8001U, 0xD5U);
        } else {
            m->write_prg(0x8000U, 0xC2U);
            m->write_prg(static_cast<Address>(raw), 0xD5U);
        }
        REQUIRE(chr_bank(*m, 0U) == 0xD5U);
    }
    for (unsigned offset = 0U; offset < 0x2000U; offset += 2U) {
        m->write_prg(static_cast<Address>(0xA000U + offset), static_cast<Byte>(offset | 1U));
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        m->write_prg(static_cast<Address>(0xA000U + offset), 0U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        m->write_prg(static_cast<Address>(0xA001U + offset), 0x80U);
        REQUIRE(m->maps_prg(0x6000U));
        m->write_prg(static_cast<Address>(0xA001U + offset), 0U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
    }
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6000U, 0xA7U);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
    m->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    m->write_prg(0xA001U, 0xC0U);
    m->write_prg(0x6000U, 0x5AU);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
    m->write_expansion(0x5003U, 0x20U);
    REQUIRE(m->maps_expansion(0x5000U));
    REQUIRE(m->maps_expansion(0x5FFFU));
    m->write_expansion(0x5003U, 0U);
    REQUIRE_FALSE(m->maps_expansion(0x5000U));

    for (unsigned raw = 0xC000U; raw < 0xE000U; raw += 2U) {
        m->reset();
        m->write_prg(static_cast<Address>(raw), 2U);
        m->write_prg(static_cast<Address>(raw + 1U), 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
    }
    for (unsigned raw = 0xE000U; raw < 0x10000U; ++raw) {
        m->reset();
        m->write_prg(0xC000U, 1U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(static_cast<Address>(raw), 0U);
        if ((raw & 1U) == 0U) {
            REQUIRE_FALSE(m->irq_pending());
        } else {
            m->acknowledge_irq();
            m->clock_irq_counter();
            m->clock_irq_counter();
            REQUIRE(m->irq_pending());
        }
    }

    for (unsigned latch = 0U; latch < 256U; ++latch) {
        m->reset();
        m->write_prg(0xC000U, static_cast<Byte>(latch));
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        for (unsigned clock = 0U; clock < 300U; ++clock) {
            m->clock_irq_counter();
            if (latch == 0U)
                REQUIRE_FALSE(m->irq_pending());
            else if (clock + 1U == latch + 1U)
                REQUIRE(m->irq_pending());
        }
        if (latch != 0U) {
            m->acknowledge_irq();
            REQUIRE_FALSE(m->irq_pending());
            m->write_prg(0xE000U, 0U);
            REQUIRE_FALSE(m->irq_pending());
        }
    }
}

TEST_CASE("Mapper 224 low WRAM persistence and cold power follow population", "[mapper224]") {
    for (bool battery : {false, true}) {
        auto m = board(false, battery);
        REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 0xA7U);
        bank(*m, 2U, 7U);
        m->write_chr(0x1000U, 0x5AU);
        m->write_expansion(0x5003U, 0x20U);
        m->write_expansion(0x5000U, 0x39U);
        REQUIRE(m->read_expansion(0x5000U) == 0x39U);
        m->reset();
        REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        bank(*m, 2U, 7U);
        REQUIRE(m->read_chr(0x1000U) == 0x5AU);
        m->power_on();
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0xA7U : 0U));
        bank(*m, 2U, 7U);
        REQUIRE(m->read_chr(0x1000U) == 0U);
        if (battery) {
            std::vector<Byte> replacement(8192U, 0x6CU);
            m->set_battery_ram(replacement);
            REQUIRE(m->read_prg(0x6123U) == 0x6CU);
            m->set_battery_ram(std::span(replacement).first(8191U));
            REQUIRE(m->read_prg(0x6000U) == 0x6CU);
        }
    }
    auto no_ram = board(false, false, false);
    REQUIRE_FALSE(no_ram->maps_prg(0x6000U));
    no_ram->write_expansion(0x5003U, 0x20U);
    REQUIRE_FALSE(no_ram->maps_expansion(0x5000U));
}

TEST_CASE("Mapper 224 snapshots replay and reject corruption without mutation", "[mapper224]") {
    auto m = board(false, true);
    m->write_expansion(0x5000U, 0x35U);
    m->write_expansion(0x5001U, 0x62U);
    m->write_expansion(0x5002U, 0x35U);
    m->write_expansion(0x5003U, 0x10U);
    m->write_expansion(0x5005U, 0x10U);
    bank(*m, 0U, 0xA7U, 0xC0U);
    bank(*m, 6U, 0x35U, 0xC0U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6000U, 0x5AU);
    bank(*m, 2U, 0xC9U, 0xC0U);
    m->write_chr(0U, 0x39U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->observe_ppu_address(0x1400U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 270384U);
    m->power_on();
    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());

    const auto before = state(*m);
    for (std::size_t length : {0U, 1U, 10U, 31U, 32U, 39U, 40U, 8231U, 8232U, 8239U, 270383U}) {
        StateReader reader{std::span(saved).first(length)};
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == before);
    }
    const std::array<std::pair<std::size_t, Byte>, 11> bad_fields = {{{1U, 0U},
                                                                      {2U, 1U},
                                                                      {6U, 1U},
                                                                      {10U, 0U},
                                                                      {17U, 8U},
                                                                      {24U, 0x40U},
                                                                      {26U, 2U},
                                                                      {29U, 8U},
                                                                      {29U, 4U},
                                                                      {30U, 1U},
                                                                      {31U, 8U}}};
    for (const auto& [offset, value] : bad_fields) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        m->load_state(reader);
        INFO("offset=" << offset << " value=" << +value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == before);
    }
    for (std::size_t offset : {32U, 8232U}) {
        auto bad = saved;
        std::fill_n(bad.begin() + static_cast<std::ptrdiff_t>(offset), 8U, 0xFFU);
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == before);
    }
    for (bool other_chr : {false, true}) {
        auto other = board(false, false, true, kPrgSize, other_chr ? 8192U : kChrSize);
        const auto original = state(*other);
        StateReader wrong(saved);
        other->load_state(wrong);
        REQUIRE_FALSE(wrong.ok());
        REQUIRE(state(*other) == original);
    }
}

TEST_CASE("Mapper 224 validates KT-008 ROM and NES 2.0 memory geometry", "[mapper224]") {
    for (std::size_t size : {16384U, 32768U, 131072U, 2097152U, 16777216U, 33554432U})
        REQUIRE(Kt008::validate_geometry(header(), size, 0U));
    for (std::size_t size : {0U, 8192U, 24576U, 33570816U})
        REQUIRE_FALSE(Kt008::validate_geometry(header(), size, 0U));
    REQUIRE_FALSE(Kt008::validate_geometry(header(), kPrgSize, 8192U));
    for (std::size_t size : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U}) {
        auto h = header();
        h.chr_ram_size = size;
        REQUIRE(Kt008::validate_geometry(h, kPrgSize, 0U));
    }
    for (std::size_t size : {0U, 4096U, 24576U, 524288U}) {
        auto h = header();
        h.chr_ram_size = size;
        REQUIRE_FALSE(Kt008::validate_geometry(h, kPrgSize, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Kt008::validate_geometry(h, kPrgSize, 0U));
    }
    for (MirrorMode mirror :
         {MirrorMode::SingleLower, MirrorMode::SingleUpper, MirrorMode::FourScreen}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Kt008::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned kind = 0U; kind < 6U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.prg_ram_size = h.prg_nvram_size = 8192U;
            break;
        case 1U:
            h.prg_ram_size = 4096U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            h.mapper_number = 268U;
            break;
        }
        REQUIRE_FALSE(Kt008::validate_geometry(h, kPrgSize, 0U));
    }
    REQUIRE(Kt008::validate_geometry(header(true), kPrgSize, 0U));
    auto legacy = board(true);
    REQUIRE(legacy->battery_ram().empty());
    REQUIRE(state(*legacy).size() == 270384U);
}
} // namespace
