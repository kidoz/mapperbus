#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper121.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool nes2 = true, bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 121U;
    result.is_nes2 = nes2;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    result.prg_ram_size = nes2 ? 0U : 8192U;
    return result;
}

std::vector<Byte> rom(std::size_t size, std::size_t page_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / page_size) >> (i % page_size == 1U ? 8U : 0U));
    return data;
}

std::unique_ptr<Mapper121> board(std::size_t prg_size = 0x40000U,
                                 std::size_t chr_size = 0x40000U,
                                 INesHeader h = header()) {
    return std::make_unique<Mapper121>(h, rom(prg_size, 0x2000U), rom(chr_size, 0x400U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned chr_marker(Mapper& mapper, Address addr) {
    return mapper.read_chr(addr) | (static_cast<unsigned>(mapper.read_chr(addr + 1U)) << 8U);
}

Byte reversed(Byte value) {
    return static_cast<Byte>(((value & 1U) << 5U) | ((value & 2U) << 3U) | ((value & 4U) << 1U) |
                             ((value & 8U) >> 1U) | ((value & 0x10U) >> 3U) |
                             ((value & 0x20U) >> 5U));
}

TEST_CASE("Mapper 121 accepts documented board geometries and rejects unsupported memory",
          "[mapper121]") {
    for (bool nes2 : {false, true})
        for (bool vertical : {false, true}) {
            auto h = header(nes2, vertical);
            for (std::size_t chr_size : {0x2000U, 0x20000U, 0x40000U, 0x80000U}) {
                REQUIRE(Mapper121::validate_geometry(h, 0x40000U, chr_size));
                auto mapper = board(0x40000U, chr_size, h);
                REQUIRE(mapper->mirror_mode() ==
                        (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                REQUIRE(mapper->read_prg(0x8000U) == 0U);
                REQUIRE(mapper->read_prg(0xC000U) == 30U);
                REQUIRE(mapper->read_prg(0xE000U) == 31U);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(state(*mapper).size() == 23U);
            }
            REQUIRE(Mapper121::validate_geometry(h, 0x80000U, 0x80000U));
            REQUIRE(board(0x80000U, 0x80000U, h)->read_prg(0xE000U) == 31U);
        }

    for (unsigned kind = 0U; kind < 20U; ++kind) {
        auto h = header();
        std::size_t prg_size = 0x40000U;
        std::size_t chr_size = 0x40000U;
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
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            prg_size = 0U;
            break;
        case 9U:
            prg_size = 0x20000U;
            break;
        case 10U:
            prg_size = 0x80000U;
            break;
        case 11U:
            prg_size += 1U;
            break;
        case 12U:
            chr_size = 0U;
            break;
        case 13U:
            chr_size = 0x400U;
            break;
        case 14U:
            chr_size = 0x40001U;
            break;
        case 15U:
            chr_size = 0x60000U;
            break;
        case 16U:
            chr_size = 0x100000U;
            break;
        case 17U:
            prg_size = 0x80000U;
            chr_size = 0x40000U;
            break;
        case 18U:
            h = header(false);
            h.has_battery = true;
            break;
        case 19U:
            h = header(false);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(Mapper121::validate_geometry(h, prg_size, chr_size));
    }
}

TEST_CASE("Mapper 121 preserves normal MMC3 PRG and CHR modes", "[mapper121]") {
    auto mapper = board();
    for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U})
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            set_bank(*mapper, 6U, static_cast<Byte>(raw), mode);
            set_bank(*mapper, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
            const std::array<unsigned, 4> expected{
                (mode & 0x40U) != 0U ? 30U : raw % 32U,
                (raw ^ 0x35U) % 32U,
                (mode & 0x40U) != 0U ? raw % 32U : 30U,
                31U,
            };
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        expected[slot]);
        }

    constexpr std::array<Byte, 6> initial{8U, 12U, 47U, 63U, 100U, 191U};
    for (Byte inversion : {0U, 0x80U})
        for (unsigned reg = 0U; reg < 6U; ++reg)
            for (unsigned value = 0U; value < 256U; ++value) {
                auto regs = initial;
                regs[reg] = static_cast<Byte>(value);
                for (unsigned r = 0U; r < 6U; ++r)
                    set_bank(*mapper, static_cast<Byte>(r), regs[r], inversion);
                const std::array<unsigned, 8> expected{regs[0] & 0xFEU,
                                                       (regs[0] & 0xFEU) + 1U,
                                                       regs[1] & 0xFEU,
                                                       (regs[1] & 0xFEU) + 1U,
                                                       regs[2],
                                                       regs[3],
                                                       regs[4],
                                                       regs[5]};
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 0x400U);
                    const auto selected = expected[slot ^ (inversion != 0U ? 4U : 0U)];
                    REQUIRE(chr_marker(*mapper, addr) == selected);
                    mapper->write_chr(addr, static_cast<Byte>(selected ^ 0xFFU));
                    REQUIRE(chr_marker(*mapper, addr) == selected);
                }
            }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 121 protection arrays use the write index and read address A8", "[mapper121]") {
    constexpr std::array<Byte, 4> first{0x83U, 0x83U, 0x42U, 0x00U};
    constexpr std::array<Byte, 4> second{0x00U, 0x02U, 0x02U, 0x03U};
    auto mapper = board();
    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
        const auto value = static_cast<Byte>(addr >> 2U);
        mapper->write_expansion(static_cast<Address>(addr), value);
        REQUIRE(mapper->read_expansion(0x5000U) == first[value & 3U]);
        REQUIRE(mapper->read_expansion(0x5100U) == second[value & 3U]);
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) ==
                ((addr & 0x100U) != 0U ? second[value & 3U] : first[value & 3U]));
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)));
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == 0xFFU);
    }
    for (Address addr : {0x4020U, 0x4FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 3U);
        REQUIRE(mapper->read_expansion(0x5000U) == first[3U]);
    }
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
}

TEST_CASE("Mapper 121 implements every protection index and bit-reversed bank", "[mapper121]") {
    auto mapper = board();
    for (unsigned index = 0U; index < 64U; ++index) {
        CAPTURE(index);
        mapper->reset();
        set_bank(*mapper, 6U, 1U);
        set_bank(*mapper, 7U, 2U);
        mapper->write_prg(0x8001U, 0x15U);
        mapper->write_prg(0x8003U, static_cast<Byte>(index));
        unsigned a = 21U, c = 30U, e = 31U;
        if (index == 0x2AU)
            a = 10U;
        if (index == 0x28U)
            c = 10U;
        if (index == 0x26U || index == 0x2CU || index == 0x20U || index == 0x29U ||
            index == 0x2BU || index == 0x3CU || index == 0x3FU)
            e = 10U;
        REQUIRE(mapper->read_prg(0xA000U) == a);
        REQUIRE(mapper->read_prg(0xC000U) == c);
        REQUIRE(mapper->read_prg(0xE000U) == e);
    }

    for (unsigned raw = 0U; raw < 64U; ++raw) {
        mapper->reset();
        mapper->write_prg(0x8001U, static_cast<Byte>(raw));
        mapper->write_prg(0x8003U, 0x26U);
        REQUIRE(mapper->read_prg(0xE000U) == reversed(static_cast<Byte>(raw)) % 32U);
        mapper->write_prg(0x8001U, static_cast<Byte>(raw ^ 0x3FU));
        REQUIRE(mapper->read_prg(0xE000U) == reversed(static_cast<Byte>(raw ^ 0x3FU)) % 32U);
    }

    mapper->reset();
    mapper->write_prg(0x8001U, 0x20U);
    mapper->write_prg(0x8003U, 0x2AU);
    mapper->write_prg(0x8001U, 0x10U);
    mapper->write_prg(0x8003U, 0x28U);
    mapper->write_prg(0x8001U, 0x08U);
    mapper->write_prg(0x8003U, 0x26U);
    REQUIRE(mapper->read_prg(0xA000U) == 2U);
    REQUIRE(mapper->read_prg(0xC000U) == 4U);
    REQUIRE(mapper->read_prg(0xE000U) == 4U);
    mapper->write_prg(0x8001U, 0x04U);
    mapper->write_prg(0x8003U, 0x2FU);
    REQUIRE(mapper->read_prg(0xA000U) == 2U);
    REQUIRE(mapper->read_prg(0xC000U) == 4U);
    REQUIRE(mapper->read_prg(0xE000U) == 8U);
    mapper->write_prg(0x8001U, 0U);
    mapper->write_prg(0x8003U, 0x2CU);
    REQUIRE(mapper->read_prg(0xE000U) == 8U);
    mapper->write_prg(0x8003U, 0U);
    REQUIRE(mapper->read_prg(0xA000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 30U);
    REQUIRE(mapper->read_prg(0xE000U) == 31U);
    mapper->write_prg(0x8003U, 0x2CU);
    REQUIRE(mapper->read_prg(0xE000U) == 31U);
}

TEST_CASE("Mapper 121 exhaustively decodes E003 aliases and MMC3 forwarding", "[mapper121]") {
    auto actual = board();
    auto expected = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(addr * 29U);
        const auto port = addr & 0xE003U;
        const auto canonical = port == 0x8003U ? (addr & 0xE000U) | 3U : addr & 0xE001U;
        actual->write_prg(static_cast<Address>(addr), value);
        expected->write_prg(static_cast<Address>(canonical), value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->read_prg(0x8000U) == expected->read_prg(0x8000U));
        REQUIRE(actual->read_prg(0xA000U) == expected->read_prg(0xA000U));
        REQUIRE(actual->read_prg(0xC000U) == expected->read_prg(0xC000U));
        REQUIRE(actual->read_prg(0xE000U) == expected->read_prg(0xE000U));
        REQUIRE(chr_marker(*actual, 0U) == chr_marker(*expected, 0U));
    }

    actual->reset();
    set_bank(*actual, 6U, 3U);
    actual->write_prg(0x8003U, 0xE6U);
    actual->write_prg(0x8001U, 5U);
    REQUIRE(actual->read_prg(0x8000U) == 30U);
    REQUIRE(actual->read_prg(0xC000U) == 5U);
    REQUIRE(actual->read_prg(0xE000U) == 8U);
    REQUIRE(chr_marker(*actual, 0U) == 0U);
}

TEST_CASE("Mapper 121 A9711 drives CHR A18 from physical PPU A12", "[mapper121]") {
    auto mapper = board(0x40000U, 0x80000U);
    constexpr std::array<Byte, 6> regs{2U, 4U, 6U, 8U, 10U, 12U};
    for (Byte inversion : {0U, 0x80U}) {
        for (unsigned reg = 0U; reg < regs.size(); ++reg)
            set_bank(*mapper, static_cast<Byte>(reg), regs[reg], inversion);
        const std::array<unsigned, 8> banks{2U, 3U, 4U, 5U, 6U, 8U, 10U, 12U};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto logical = slot ^ (inversion != 0U ? 4U : 0U);
            REQUIRE(chr_marker(*mapper, static_cast<Address>(slot * 0x400U)) ==
                    banks[logical] + (slot >= 4U ? 256U : 0U));
        }
    }
}

TEST_CASE("Mapper 121 A9713 outer aliases switch complete PRG and CHR halves", "[mapper121]") {
    auto mapper = board(0x80000U, 0x80000U);
    set_bank(*mapper, 6U, 3U);
    set_bank(*mapper, 7U, 4U);
    set_bank(*mapper, 2U, 130U);
    mapper->write_prg(0xC001U, 0x14U);
    mapper->write_prg(0x8003U, 0x26U);
    mapper->write_expansion(0x5000U, 2U);
    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
        CAPTURE(addr);
        mapper->write_expansion(0x5180U, 0U);
        mapper->write_expansion(0x5000U, 2U);
        mapper->write_expansion(static_cast<Address>(addr), 0U);
        const bool outer = (addr & 0xF180U) == 0x5180U;
        mapper->write_expansion(static_cast<Address>(addr), 0x80U);
        const auto expected = outer ? 32U : 0U;
        REQUIRE(mapper->read_prg(0x8000U) == 3U + expected);
        REQUIRE(mapper->read_prg(0xA000U) == 4U + expected);
        REQUIRE(mapper->read_prg(0xC000U) == 30U + expected);
        REQUIRE(mapper->read_prg(0xE000U) == 16U + expected);
        REQUIRE(chr_marker(*mapper, 0x1000U) == 130U + expected * 8U);
        REQUIRE(mapper->read_expansion(0x5000U) == (outer ? 0x42U : 0x83U));
    }

    auto a9711 = board(0x40000U, 0x40000U);
    set_bank(*a9711, 6U, 3U);
    a9711->write_expansion(0x5180U, 0x80U);
    REQUIRE(a9711->read_prg(0x8000U) == 3U);
    REQUIRE(a9711->read_expansion(0x5000U) == 0x83U);
}

TEST_CASE("Mapper 121 retains MMC3 IRQ behavior through protection writes and reset",
          "[mapper121][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8003U, 0x26U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 1U);
    mapper->write_prg(0x8001U, 2U);
    mapper->write_prg(0xE001U, 0U);
    REQUIRE(mapper->read_prg(0xE000U) == 16U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    const auto fresh = state(*board());
    mapper->reset();
    REQUIRE(state(*mapper) == fresh);
    REQUIRE(mapper->read_expansion(0x5000U) == 0x83U);
    REQUIRE(mapper->read_expansion(0x5100U) == 0U);
}

TEST_CASE("Mapper 121 snapshots replay all mutable state and reject atomically",
          "[mapper121][state]") {
    auto mapper = board(0x80000U, 0x80000U);
    auto copy = board(0x80000U, 0x80000U);
    set_bank(*mapper, 6U, 3U, 0xC0U);
    set_bank(*mapper, 7U, 4U, 0xC0U);
    set_bank(*mapper, 2U, 130U, 0xC0U);
    mapper->write_expansion(0x5180U, 0x80U);
    mapper->write_expansion(0x5000U, 2U);
    mapper->write_prg(0x8001U, 0x14U);
    mapper->write_prg(0x8003U, 0x26U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 23U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0xE000U) == mapper->read_prg(0xE000U));
    REQUIRE(copy->read_expansion(0x5000U) == mapper->read_expansion(0x5000U));
    REQUIRE(chr_marker(*copy, 0U) == chr_marker(*mapper, 0U));
    REQUIRE(copy->irq_pending());

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated{std::span(saved).first(size)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 13>{{{0U, 0U},
                                                                                    {1U, 0U},
                                                                                    {2U, 8U},
                                                                                    {3U, 1U},
                                                                                    {10U, 64U},
                                                                                    {11U, 64U},
                                                                                    {12U, 2U},
                                                                                    {15U, 8U},
                                                                                    {15U, 4U},
                                                                                    {17U, 64U},
                                                                                    {18U, 64U},
                                                                                    {21U, 2U},
                                                                                    {22U, 4U}}}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto a9711 = board();
    const auto before = state(*a9711);
    StateReader wrong_geometry(saved);
    a9711->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*a9711) == before);

    auto smaller = board(0x40000U, 0x20000U);
    const auto smaller_state = state(*smaller);
    StateReader wrong_chr_size(smaller_state);
    a9711->load_state(wrong_chr_size);
    REQUIRE_FALSE(wrong_chr_size.ok());
    REQUIRE(state(*a9711) == before);

    auto bad = state(*a9711);
    bad[21U] = 1U;
    StateReader wrong_outer(bad);
    a9711->load_state(wrong_outer);
    REQUIRE_FALSE(wrong_outer.ok());
    REQUIRE(state(*a9711) == before);
}

} // namespace
