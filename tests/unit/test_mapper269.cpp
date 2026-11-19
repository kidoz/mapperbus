#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper269.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateBytes = 8222U;

INesHeader header(bool battery = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 269U;
    h.is_nes2 = true;
    h.has_battery = battery;
    h.prg_ram_size = battery ? 0U : 8192U;
    h.prg_nvram_size = battery ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

std::vector<Byte> rom(unsigned size, unsigned granularity) {
    std::vector<Byte> data(size);
    for (unsigned i = 0U; i < size; ++i)
        data[i] = cell(i / granularity, i % granularity);
    return data;
}

std::unique_ptr<Mapper269> board(bool battery = false,
                                 bool vertical = true,
                                 unsigned prg = 524288U,
                                 unsigned chr = 262144U) {
    const auto h = header(battery, vertical);
    REQUIRE(Mapper269::validate_geometry(h, prg, chr));
    return std::make_unique<Mapper269>(h, rom(prg, 8192U), rom(chr, 1024U));
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

void outer(Mapper& m, Byte chr, Byte prg, Byte mask, Byte prg_mask) {
    m.write_expansion(0x5000U, chr);
    m.write_expansion(0x5000U, prg);
    m.write_expansion(0x5000U, mask);
    m.write_expansion(0x5000U, prg_mask);
}

unsigned chr_mask(unsigned bits) {
    if (bits == 0U)
        return 0U;
    return bits >= 8U ? 255U : (255U << (8U - bits)) & 255U;
}

Byte decoded(Byte value) {
    const unsigned data = value;
    return static_cast<Byte>(((data & 1U) << 6U) | ((data & 2U) << 3U) | (data & 4U) |
                             ((data & 8U) >> 3U) | ((data & 16U) >> 3U) | ((data & 32U) >> 2U) |
                             ((data & 64U) >> 1U) | (data & 128U));
}

TEST_CASE("Mapper 269 validates native geometry and memory populations", "[mapper269]") {
    for (bool battery : {false, true})
        for (bool vertical : {false, true})
            for (unsigned prg : {32768U, 65536U, 131072U, 262144U, 524288U})
                for (unsigned chr : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U}) {
                    const auto h = header(battery, vertical);
                    REQUIRE(Mapper269::validate_geometry(h, prg, chr));
                }
    auto m = board();
    REQUIRE(m->battery_ram().empty());
    REQUIRE(m->read_prg(0xC123U) == cell(62U, 0x123U));
    REQUIRE(m->read_prg(0xE123U) == cell(63U, 0x123U));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(state(*m).size() == kStateBytes);
    REQUIRE_FALSE(m->has_expansion_audio());

    for (unsigned size : {0U, 16384U, 49152U, 1048576U})
        REQUIRE_FALSE(Mapper269::validate_geometry(header(), size, 8192U));
    for (unsigned size : {0U, 4096U, 12288U, 524288U})
        REQUIRE_FALSE(Mapper269::validate_geometry(header(), 32768U, size));
    for (unsigned variant = 0U; variant < 10U; ++variant) {
        auto h = header();
        switch (variant) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            h.prg_ram_size = 0U;
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
            h.has_battery = true;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            h.mapper_number = 268U;
            break;
        }
        REQUIRE_FALSE(Mapper269::validate_geometry(h, 32768U, 8192U));
    }
}

TEST_CASE("Mapper 269 outer registers use the F008 decoder and four-write sequence",
          "[mapper269]") {
    auto m = board();
    const auto initial = state(*m);
    REQUIRE(initial[8202U] == 0U);
    REQUIRE(initial[8203U] == 0U);
    REQUIRE(initial[8204U] == 0x0FU);
    REQUIRE(initial[8205U] == 0U);
    REQUIRE(initial[8206U] == 0U);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        m->reset();
        auto expected = initial;
        const bool selected = (addr & 0xF008U) == 0x5000U;
        if (selected) {
            expected[8202U] = 0xA5U;
            expected[8206U] = 1U;
        }
        m->write_expansion(static_cast<Address>(addr), 0xA5U);
        REQUIRE(m->maps_expansion(static_cast<Address>(addr)) == selected);
        REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) == 0U);
        REQUIRE(m->read_expansion(static_cast<Address>(addr)) == 0U);
        REQUIRE(state(*m) == expected);
    }
    m->reset();
    for (unsigned n = 0U; n < 12U; ++n)
        m->write_expansion(static_cast<Address>(0x5000U + (n & 7U)), static_cast<Byte>(n + 1U));
    const auto sequenced = state(*m);
    REQUIRE(sequenced[8202U] == 9U);
    REQUIRE(sequenced[8203U] == 10U);
    REQUIRE(sequenced[8204U] == 11U);
    REQUIRE(sequenced[8205U] == 12U);
    REQUIRE(sequenced[8206U] == 0U);
    REQUIRE(m->maps_prg(0x6000U));
    REQUIRE_FALSE(m->maps_prg(0x5000U));
}

TEST_CASE("Mapper 269 forwards every MMC3 CPU register alias", "[mapper269]") {
    auto m = board();
    const auto initial = state(*m);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        auto expected = initial;
        switch (addr & 0xE001U) {
        case 0x8000U:
            expected[8207U] = 3U;
            expected[8208U] = 0x80U;
            break;
        case 0x8001U:
            expected[8209U] = 0xA3U;
            break;
        case 0xA000U:
            expected[8217U] = static_cast<Byte>(MirrorMode::Horizontal);
            break;
        case 0xA001U:
            expected[8221U] = 0x80U;
            break;
        case 0xC000U:
            expected[8219U] = 0xA3U;
            break;
        case 0xC001U:
            expected[8220U] = 1U;
            break;
        case 0xE001U:
            expected[8220U] = 2U;
            break;
        default:
            break;
        }
        m->write_prg(static_cast<Address>(addr), 0xA3U);
        REQUIRE(state(*m) == expected);
    }
    m->write_expansion(0x5008U, 0xFFU);
    REQUIRE(state(*m)[8206U] == 0U);
}

TEST_CASE("Mapper 269 applies the six-bit outer PRG bit mask in both MMC3 modes", "[mapper269]") {
    auto m = board();
    for (unsigned mask = 0U; mask < 64U; ++mask)
        for (unsigned value = 0U; value < 64U; ++value) {
            outer(*m, 0U, 0xA5U, 0x0FU, static_cast<Byte>(mask | 0xC0U));
            for (Byte mode : {0U, 0x40U}) {
                reg(*m, 6U, static_cast<Byte>(value | 0xC0U), mode);
                reg(*m, 7U, 0x15U, mode);
                const unsigned inner[4] = {
                    mode == 0U ? value : 62U, 0x15U, mode == 0U ? 62U : value, 63U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const unsigned bank = ((inner[slot] & (63U ^ mask)) | (0xA5U & mask)) % 64U;
                    const auto addr = static_cast<Address>(0x8123U + slot * 8192U);
                    REQUIRE(m->read_prg(addr) == cell(bank, 0x123U));
                }
            }
        }
}

TEST_CASE("Mapper 269 applies every CHR mask count with pairing and inversion", "[mapper269]") {
    auto m = board();
    for (unsigned count = 0U; count < 16U; ++count)
        for (unsigned value = 0U; value < 256U; ++value) {
            outer(*m, 0xA5U, 0U, static_cast<Byte>(count | 0xC0U), 0xC0U);
            std::array<unsigned, 6> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = (value + index * 17U) & 255U;
                reg(*m, index, static_cast<Byte>(banks[index]));
            }
            const unsigned pages[8] = {banks[0] & 254U,
                                       banks[0] | 1U,
                                       banks[1] & 254U,
                                       banks[1] | 1U,
                                       banks[2],
                                       banks[3],
                                       banks[4],
                                       banks[5]};
            for (Byte mode : {0U, 0x80U}) {
                m->write_prg(0x8000U, mode);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto inner = pages[slot ^ (mode == 0U ? 0U : 4U)];
                    const auto mask = chr_mask(count);
                    const unsigned bank = ((inner & (255U ^ mask)) | (0xA5U & mask)) % 256U;
                    const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                    m->write_chr(addr, 0xFFU);
                    REQUIRE(m->read_chr(addr) == decoded(cell(bank, 0x123U)));
                }
            }
        }
}

TEST_CASE("Mapper 269 CHR bit permutation matches all 256 stored byte values", "[mapper269]") {
    auto h = header();
    std::vector<Byte> prg(32768U, 0U), chr(8192U);
    for (unsigned i = 0U; i < chr.size(); ++i)
        chr[i] = static_cast<Byte>(i);
    Mapper269 m(h, std::move(prg), std::move(chr));
    outer(m, 0U, 0U, 0U, 0U);
    reg(m, 2U, 0U);
    for (unsigned value = 0U; value < 256U; ++value)
        REQUIRE(m.read_chr(static_cast<Address>(0x1000U + value)) ==
                decoded(static_cast<Byte>(value)));
}

TEST_CASE("Mapper 269 WRAM honors MMC3 gates persistence and reset population", "[mapper269]") {
    for (bool battery : {false, true}) {
        auto m = board(battery, false);
        REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
        for (unsigned i = 0U; i < 8192U; ++i)
            m->write_prg(static_cast<Address>(0x6000U + i), static_cast<Byte>(i));
        for (unsigned i = 0U; i < 8192U; ++i)
            REQUIRE(m->read_prg(static_cast<Address>(0x6000U + i)) == static_cast<Byte>(i));
        m->write_prg(0xA001U, 0xC0U);
        m->write_prg(0x6000U, 0x55U);
        REQUIRE(m->read_prg(0x6000U) == 0U);
        m->write_prg(0xBFFFU, 0U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        REQUIRE(m->read_prg(0x6000U) == 0U);
        m->write_prg(0xA001U, 0x80U);
        REQUIRE(m->read_prg(0x6000U) == 0U);
        if (battery) {
            std::vector<Byte> saved(8192U, 0xA7U);
            m->set_battery_ram(saved);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            m->set_battery_ram(std::vector<Byte>(8193U, 0x5CU));
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        }
        m->write_prg(0x6000U, 0x39U);
        m->write_prg(0xA000U, 1U);
        m->reset();
        REQUIRE(m->read_prg(0x6000U) == 0x39U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*m)[8204U] == 0x0FU);
        m->write_prg(0x6000U, 0x39U);
        m->power_on();
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0x39U : 0U));
    }
}

TEST_CASE("Mapper 269 MMC3 IRQ reload acknowledgement and zero-latch repetition", "[mapper269]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    StateReader restore(partial);
    m->load_state(restore);
    REQUIRE(restore.ok());
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

TEST_CASE("Mapper 269 snapshots replay and reject corruption without mutation", "[mapper269]") {
    auto m = board(true);
    outer(*m, 0x81U, 0x42U, 0xE3U, 0x2AU);
    m->write_expansion(0x5000U, 0x7EU);
    reg(*m, 6U, 0x2CU, 0xC0U);
    m->write_prg(0x6000U, 0xA7U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    REQUIRE(saved.size() == kStateBytes);
    for (unsigned length : {0U, 1U, 9U, 10U, 8191U, 8201U, 8221U}) {
        StateReader r{std::span(saved).first(length)};
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    const std::array<std::pair<unsigned, Byte>, 13> invalid{{{0U, 1U},
                                                             {2U, 1U},
                                                             {6U, 1U},
                                                             {8206U, 4U},
                                                             {8207U, 8U},
                                                             {8208U, 1U},
                                                             {8215U, 64U},
                                                             {8216U, 64U},
                                                             {8217U, 2U},
                                                             {8220U, 8U},
                                                             {8220U, 4U},
                                                             {8221U, 1U},
                                                             {1U, 0U}}};
    for (const auto& [offset, value] : invalid) {
        INFO("offset=" << offset << " value=" << +value);
        auto bad = saved;
        bad[10] ^= 0xFFU;
        bad[offset] = value;
        StateReader r(bad);
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto [battery, vertical, prg, chr] :
         {std::array<unsigned, 4>{0U, 1U, 524288U, 262144U},
          {1U, 0U, 524288U, 262144U},
          {1U, 1U, 262144U, 262144U},
          {1U, 1U, 524288U, 131072U}}) {
        auto other = board(battery != 0U, vertical != 0U, prg, chr);
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
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
}

} // namespace
