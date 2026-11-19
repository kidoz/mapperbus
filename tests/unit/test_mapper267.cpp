#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper267.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 0x200000U;
constexpr std::size_t kChrSize = 0x100000U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 267U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 13U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper267> board(bool legacy = false,
                                 bool vertical = true,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i / 0x2000U, i % 0x2000U);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i / 0x400U, i % 0x400U);
    return std::make_unique<Mapper267>(header(legacy, vertical), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void reg(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

Byte outer_value(unsigned bank) {
    return static_cast<Byte>(((bank & 3U) << 1U) | ((bank & 4U) << 3U));
}

TEST_CASE("Mapper 267 reset geometry and fixed inner banks follow the multicart contract",
          "[mapper267]") {
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            auto m = board(legacy, vertical);
            REQUIRE(state(*m).size() == 16U);
            REQUIRE(m->battery_ram().empty());
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
            REQUIRE(m->read_prg(0xA000U) == prg_cell(0U, 0U));
            REQUIRE(m->read_prg(0xC000U) == prg_cell(30U, 0U));
            REQUIRE(m->read_prg(0xE000U) == prg_cell(31U, 0U));
            REQUIRE(m->read_chr(0U) == chr_cell(0U, 0U));
            REQUIRE_FALSE(m->irq_pending());
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE(m->audio_output() == 0.0F);
            REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE(m->cpu_open_bus_mask() == 0xFFU);
            REQUIRE(m->maps_prg(0x8000U));
            REQUIRE(m->maps_prg(0xFFFFU));
            for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x6001U, 0x7FFFU}) {
                REQUIRE_FALSE(m->maps_prg(addr));
                REQUIRE_FALSE(m->maps_expansion(addr));
            }
        }
    }
}

TEST_CASE("Mapper 267 claims only the documented outer register address below PRG ROM",
          "[mapper267]") {
    auto m = board();
    for (unsigned addr = 0U; addr < 0x8000U; ++addr) {
        m->reset();
        m->write_prg(0x6000U, 0xA6U);
        m->write_prg(static_cast<Address>(addr), 0x26U);
        REQUIRE(state(*m)[2] == (addr == 0x6000U ? 0x26U : 0xA6U));
    }
}

TEST_CASE("Mapper 267 decodes every MMC3 register alias", "[mapper267]") {
    auto m = board();
    const auto initial = state(*m);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto actual = state(*m);
        switch (addr & 0xE001U) {
        case 0x8000U:
            REQUIRE(actual[3] == 0x85U);
            break;
        case 0x8001U:
            REQUIRE(actual[4] == 0x25U);
            break;
        case 0xA000U:
            REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
            break;
        case 0xA001U:
            REQUIRE(actual == initial);
            break;
        case 0xC000U:
            REQUIRE(actual[14] == 0xA5U);
            break;
        case 0xC001U:
            REQUIRE(actual[13] == 0U);
            REQUIRE(actual[15] == 1U);
            break;
        case 0xE000U:
            REQUIRE(actual[15] == 0U);
            break;
        case 0xE001U:
            REQUIRE(actual[15] == 2U);
            break;
        default:
            FAIL("unreachable MMC3 decode");
        }
    }
}

TEST_CASE("Mapper 267 outer bits select shared banks and unknown bits have no effect",
          "[mapper267]") {
    auto m = board();
    reg(*m, 6U, 7U);
    reg(*m, 7U, 9U);
    reg(*m, 2U, 11U);
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0x6000U, static_cast<Byte>(value));
        const unsigned outer = ((value & 0x20U) >> 3U) | ((value & 6U) >> 1U);
        REQUIRE(m->read_prg(0x8000U) == prg_cell(outer * 32U + 7U, 0U));
        REQUIRE(m->read_prg(0xA000U) == prg_cell(outer * 32U + 9U, 0U));
        REQUIRE(m->read_prg(0xC000U) == prg_cell(outer * 32U + 30U, 0U));
        REQUIRE(m->read_prg(0xE000U) == prg_cell(outer * 32U + 31U, 0U));
        REQUIRE(m->read_chr(0x1000U) == chr_cell(outer * 128U + 11U, 0U));
    }
}

TEST_CASE("Mapper 267 masks PRG registers without bus conflicts and maps both PRG modes",
          "[mapper267]") {
    auto m = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Byte mode : {0U, 0x40U}) {
            reg(*m, 6U, static_cast<Byte>(value), mode);
            reg(*m, 7U, static_cast<Byte>(value ^ 0xFFU), mode);
            const unsigned selected = value & 31U;
            const unsigned other = (value ^ 0xFFU) & 31U;
            const std::array<unsigned, 4> banks{
                mode == 0U ? selected : 30U, other, mode == 0U ? 30U : selected, 31U};
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8123U + slot * 0x2000U);
                REQUIRE(m->read_prg(addr) == prg_cell(banks[slot], 0x123U));
            }
        }
    }
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        m->write_prg(0x6000U, outer_value(outer));
        reg(*m, 6U, 13U);
        reg(*m, 7U, 17U);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const unsigned slot = (addr - 0x8000U) / 0x2000U;
            const std::array<unsigned, 4> inner{13U, 17U, 30U, 31U};
            REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                    prg_cell(outer * 32U + inner[slot], addr & 0x1FFFU));
        }
    }
}

TEST_CASE("Mapper 267 maps every CHR cell in both inversions and protects ROM", "[mapper267]") {
    auto m = board();
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        m->write_prg(0x6000U, outer_value(outer));
        for (Byte mode : {0U, 0x80U}) {
            constexpr std::array<Byte, 6> values{0xFEU, 0x7DU, 0x55U, 0x7FU, 0x80U, 0x01U};
            for (Byte index = 0U; index < 6U; ++index)
                reg(*m, index, values[index], mode);
            const std::array<unsigned, 8> inner{0x7EU, 0x7FU, 0x7CU, 0x7DU, 0x55U, 0x7FU, 0U, 1U};
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                const unsigned slot = (addr / 0x400U) ^ (mode == 0U ? 0U : 4U);
                m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xFFU));
                REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                        chr_cell(outer * 128U + inner[slot], addr & 0x3FFU));
            }
        }
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Byte index = 0U; index < 6U; ++index) {
            reg(*m, index, static_cast<Byte>(value));
            REQUIRE(state(*m)[4U + index] == (value & 0x7FU));
        }
    }
}

TEST_CASE("Mapper 267 wraps decoded addresses to fitted non-power-of-two ROMs", "[mapper267]") {
    constexpr std::size_t prg_size = 5U * 0x2000U;
    constexpr std::size_t chr_size = 11U * 0x400U;
    auto m = board(false, true, prg_size, chr_size);
    m->write_prg(0x6000U, outer_value(7U));
    reg(*m, 6U, 31U);
    reg(*m, 7U, 30U);
    reg(*m, 2U, 127U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const unsigned slot = (addr - 0x8000U) / 0x2000U;
        const std::array<unsigned, 4> banks{31U, 30U, 30U, 31U};
        const std::size_t offset = (7U * 32U + banks[slot]) * 0x2000U + (addr & 0x1FFFU);
        REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                prg_cell((offset % prg_size) / 0x2000U, (offset % prg_size) % 0x2000U));
    }
    for (unsigned addr = 0x1000U; addr < 0x1400U; ++addr) {
        const std::size_t offset = (7U * 128U + 127U) * 0x400U + (addr & 0x3FFU);
        REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                chr_cell((offset % chr_size) / 0x400U, (offset % chr_size) % 0x400U));
    }
}

TEST_CASE("Mapper 267 implements latch reload enable acknowledgement and zero repetition",
          "[mapper267]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
    REQUIRE(partial[13] == 2U);
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
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
    for (unsigned clock = 0U; clock < 4U; ++clock) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        m->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 267 reset restores menu outer bank registers mirroring and IRQ state",
          "[mapper267]") {
    auto m = board(false, false);
    m->write_prg(0x6000U, 0xE6U);
    reg(*m, 6U, 19U, 0xC0U);
    reg(*m, 2U, 77U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->reset();
    REQUIRE(state(*m) == state(*board(false, false)));
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_chr(0U) == chr_cell(0U, 0U));
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0x6000U, 0xA6U);
    m->power_on();
    REQUIRE(state(*m) == state(*board(false, false)));
}

TEST_CASE("Mapper 267 rejects unsupported geometry submappers RAM and CIRAM metadata",
          "[mapper267]") {
    const auto reject =
        [](INesHeader h, std::size_t prg_size = 0x40000U, std::size_t chr_size = 0x20000U) {
            REQUIRE_FALSE(Mapper267::validate_geometry(h, prg_size, chr_size));
        };
    for (std::size_t size : {0U, 0x4000U, 0x6000U, 0x202000U}) {
        reject(header(), size);
    }
    for (std::size_t size : {0U, 0x1000U, 0x1800U, 0x100400U}) {
        reject(header(), 0x40000U, size);
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h);
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        reject(h);
    }
    for (MirrorMode mode :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        reject(h);
    }
    REQUIRE(Mapper267::validate_geometry(header(), 0x8000U, 0x2000U));
    REQUIRE(Mapper267::validate_geometry(header(), kPrgSize, kChrSize));
    REQUIRE(Mapper267::validate_geometry(header(), 5U * 0x2000U, 11U * 0x400U));
    auto legacy = header(true);
    legacy.prg_ram_size = 0x2000U;
    REQUIRE(Mapper267::validate_geometry(legacy, 0x40000U, 0x20000U));
}

TEST_CASE("Mapper 267 state replay is bounded and all rejected imports are atomic", "[mapper267]") {
    auto m = board();
    m->write_prg(0x6000U, 0xE6U);
    reg(*m, 0U, 0xFEU, 0xC0U);
    reg(*m, 6U, 0xF3U);
    reg(*m, 7U, 0x8DU);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 3U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    REQUIRE(saved.size() == 16U);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    auto trailing = saved;
    trailing.push_back(0U);
    StateReader extra(trailing);
    m->load_state(extra);
    REQUIRE_FALSE(extra.ok());
    REQUIRE(state(*m) == saved);

    std::vector<std::pair<std::size_t, Byte>> invalid{{0U, 0U},
                                                      {1U, 0U},
                                                      {3U, 0x08U},
                                                      {3U, 0x10U},
                                                      {3U, 0x20U},
                                                      {10U, 0x20U},
                                                      {11U, 0x80U},
                                                      {12U, 2U},
                                                      {15U, 8U},
                                                      {15U, 4U}};
    for (unsigned index = 0U; index < 6U; ++index)
        invalid.emplace_back(4U + index, 0x80U);
    for (const auto [offset, value] : invalid) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader reader(corrupt);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }

    auto other = board(false, false);
    const auto other_saved = state(*other);
    StateReader wrong_mirror(saved);
    other->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other) == other_saved);

    m->reset();
    StateReader replay(saved);
    m->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(19U + 7U * 32U, 0U));
    REQUIRE(m->read_prg(0xC000U) == prg_cell(30U + 7U * 32U, 0U));
    REQUIRE(m->read_prg(0xA000U) == prg_cell(13U + 7U * 32U, 0U));
    m->clock_irq_counter();
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
}

} // namespace
