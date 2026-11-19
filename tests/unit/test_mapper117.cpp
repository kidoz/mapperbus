#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper117.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = false, Region region = Region::NTSC) {
    INesHeader h{};
    h.mapper_number = 117U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.region = region;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 41U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 23U + offset + (offset >> 7U));
}

std::vector<Byte> prg_image(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = prg_cell(i / 8192U, i % 8192U);
    return data;
}

std::vector<Byte> chr_image(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = chr_cell(i / 1024U, i % 1024U);
    return data;
}

std::unique_ptr<Mapper117> board(const INesHeader& h = header(),
                                 std::size_t prg_size = 0x20000U,
                                 std::size_t chr_size = 0x10000U) {
    REQUIRE(Mapper117::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper117>(h, prg_image(prg_size), chr_image(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void clock_lines(Mapper117& mapper, unsigned count) {
    for (unsigned i = 0U; i < count; ++i)
        mapper.on_ppu_hblank();
}

TEST_CASE("Mapper 117 maps three PRG registers, fixed last bank and eight CHR registers",
          "[mapper117]") {
    auto m = board();
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_prg(0xA000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_prg(0xC000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_prg(0xE000U) == prg_cell(15U, 0U));
    REQUIRE(m->read_prg(0xFFFFU) == prg_cell(15U, 8191U));

    constexpr std::array<Byte, 3> prg_banks{3U, 9U, 14U};
    for (unsigned slot = 0U; slot < prg_banks.size(); ++slot) {
        m->write_prg(static_cast<Address>(0x8000U + slot), prg_banks[slot]);
        for (unsigned offset : {0U, 1U, 0x123U, 0x1FFFU}) {
            const auto addr = static_cast<Address>(0x8000U + slot * 8192U + offset);
            REQUIRE(m->read_prg(addr) == prg_cell(prg_banks[slot], offset));
        }
    }
    REQUIRE(m->read_prg(0xE123U) == prg_cell(15U, 0x123U));

    constexpr std::array<Byte, 8> chr_banks{2U, 7U, 11U, 19U, 31U, 43U, 52U, 63U};
    for (unsigned slot = 0U; slot < chr_banks.size(); ++slot) {
        m->write_prg(static_cast<Address>(0xA000U + slot), chr_banks[slot]);
        for (unsigned offset : {0U, 1U, 0x155U, 0x3FFU}) {
            const auto addr = static_cast<Address>(slot * 1024U + offset);
            REQUIRE(m->read_chr(addr) == chr_cell(chr_banks[slot], offset));
        }
    }
}

TEST_CASE("Mapper 117 retains full bank bytes before fitted-ROM wrapping", "[mapper117]") {
    auto m = board(header(), 5U * 8192U, 11U * 1024U);
    for (unsigned slot = 0U; slot < 3U; ++slot)
        m->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(0xFFU - slot));
    for (unsigned slot = 0U; slot < 8U; ++slot)
        m->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(0xF8U + slot));

    REQUIRE(m->read_prg(0x8123U) == prg_cell(0U, 0x123U));
    REQUIRE(m->read_prg(0xA123U) == prg_cell(4U, 0x123U));
    REQUIRE(m->read_prg(0xC123U) == prg_cell(3U, 0x123U));
    REQUIRE(m->read_prg(0xE123U) == prg_cell(4U, 0x123U));
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U + 0x123U)) ==
                chr_cell((0xF8U + slot) % 11U, 0x123U));
}

TEST_CASE("Mapper 117 decodes only the exact published register addresses", "[mapper117]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 37U + raw / 251U);
        Address canonical = 0x7000U;
        if (addr <= 0x8002U || (addr >= 0xA000U && addr <= 0xA007U) ||
            (addr >= 0xC000U && addr <= 0xC002U) || addr == 0xE000U)
            canonical = addr;
        actual->write_prg(addr, value);
        expected->write_prg(canonical, value);
        REQUIRE(state(*actual) == state(*expected));
        for (Address prg : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
            REQUIRE(actual->read_prg(prg) == expected->read_prg(prg));
        for (Address chr : {0x0000U, 0x0400U, 0x0800U, 0x0C00U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U})
            REQUIRE(actual->read_chr(chr) == expected->read_chr(chr));
    }
}

TEST_CASE("Mapper 117 has fixed CIRAM and immutable ROM without extra bus hardware",
          "[mapper117]") {
    for (bool vertical : {false, true}) {
        auto m = board(header(false, vertical));
        const auto initial = state(*m);
        REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(m->battery_ram().empty());
        REQUIRE(m->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(m->has_expansion_audio());
        REQUIRE(m->audio_output() == 0.0F);
        for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(m->maps_prg(addr));
            REQUIRE_FALSE(m->maps_expansion(addr));
            m->write_prg(addr, 0xFFU);
            m->write_expansion(addr, 0xFFU);
        }
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto before = m->read_chr(addr);
            m->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
            REQUIRE(m->read_chr(addr) == before);
            REQUIRE(m->prg_read_mask(addr) == 0xFFU);
        }
        m->set_battery_ram(std::array<Byte, 2>{1U, 2U});
        REQUIRE(m->battery_ram().empty());
        m->write_prg(0x8000U, 7U);
        m->write_prg(0xA000U, 9U);
        m->write_prg(0xC000U, 3U);
        m->write_prg(0xE000U, 1U);
        m->acknowledge_irq();
        for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
            m->clock_cpu();
            m->clock_irq_counter();
            m->clock_audio();
        }
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(state(*m) != initial);
    }
}

TEST_CASE("Mapper 117 scanline IRQ applies the published bias and latches until C writes",
          "[mapper117]") {
    for (Address reg : {0xC000U, 0xC001U, 0xC002U}) {
        auto m = board();
        m->write_prg(reg, 1U);
        m->write_prg(0xE000U, 0xFFU);
        clock_lines(*m, 21U);
        REQUIRE_FALSE(m->irq_pending());
        m->on_ppu_hblank();
        REQUIRE(m->irq_pending());
        REQUIRE(state(*m)[21U] == 0U);
        clock_lines(*m, 241U);
        REQUIRE(m->irq_pending());

        m->write_prg(0xE000U, 0U);
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        REQUIRE(m->irq_pending());
        m->write_prg(reg, 2U);
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE(state(*m)[21U] == 2U);
        REQUIRE(state(*m)[22U] == 2U);
        m->write_prg(0xE000U, 1U);
        clock_lines(*m, static_cast<unsigned>(timing_for_region(Region::NTSC).scanlines_per_frame));
        REQUIRE(m->irq_pending());
    }
}

TEST_CASE("Mapper 117 IRQ obeys zero latch, enable timing and visible-line limits", "[mapper117]") {
    auto zero = board();
    zero->write_prg(0xC000U, 0U);
    zero->write_prg(0xE000U, 1U);
    clock_lines(*zero, 2U * 262U);
    REQUIRE_FALSE(zero->irq_pending());

    auto disabled = board();
    disabled->write_prg(0xC001U, 1U);
    clock_lines(*disabled, 22U);
    REQUIRE_FALSE(disabled->irq_pending());
    disabled->write_prg(0xE000U, 1U);
    clock_lines(*disabled, 262U);
    REQUIRE(disabled->irq_pending());

    for (Byte counter : {220U, 221U, 239U, 255U}) {
        auto outside = board();
        outside->write_prg(0xC002U, counter);
        outside->write_prg(0xE000U, 1U);
        clock_lines(*outside, 3U * 262U);
        INFO("counter=" << unsigned(counter));
        REQUIRE_FALSE(outside->irq_pending());
    }

    for (Region region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        auto m = board(header(false, false, region));
        m->write_prg(0xC000U, 225U);
        m->write_prg(0xE000U, 1U);
        const auto lines = static_cast<unsigned>(timing_for_region(region).scanlines_per_frame);
        clock_lines(*m, 2U * lines);
        REQUIRE_FALSE(m->irq_pending());
        m->write_prg(0xC000U, 1U);
        clock_lines(*m, lines + 22U);
        REQUIRE(m->irq_pending());
    }
}

TEST_CASE("Mapper 117 reset restores deterministic registers and preserves region and CIRAM",
          "[mapper117]") {
    for (bool vertical : {false, true}) {
        auto m = board(header(false, vertical, Region::PAL));
        const auto initial = state(*m);
        REQUIRE(initial.size() == 27U);
        for (unsigned slot = 0U; slot < 3U; ++slot)
            m->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(0x80U + slot));
        for (unsigned slot = 0U; slot < 8U; ++slot)
            m->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(0xF0U + slot));
        m->write_prg(0xC000U, 1U);
        m->write_prg(0xE000U, 1U);
        clock_lines(*m, 22U);
        REQUIRE(m->irq_pending());
        REQUIRE(state(*m) != initial);
        m->reset();
        REQUIRE(state(*m) == initial);
        REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));

        m->write_prg(0x8002U, 0xFFU);
        m->write_prg(0xC001U, 0xAAU);
        clock_lines(*m, 17U);
        m->power_on();
        REQUIRE(state(*m) == initial);
    }
}

TEST_CASE("Mapper 117 snapshots replay banking, IRQ and scanline phase atomically",
          "[mapper117][state]") {
    const auto h = header(false, true, Region::Dendy);
    auto m = board(h);
    for (unsigned slot = 0U; slot < 3U; ++slot)
        m->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(0xF1U + slot));
    for (unsigned slot = 0U; slot < 8U; ++slot)
        m->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(slot * 31U + 7U));
    m->write_prg(0xC001U, 40U);
    m->write_prg(0xE000U, 1U);
    clock_lines(*m, 61U);
    REQUIRE(m->irq_pending());
    const auto saved = state(*m);
    REQUIRE(saved.size() == 27U);

    auto copy = board(h);
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0xA123U, 0xC456U, 0xE789U})
        REQUIRE(copy->read_prg(addr) == m->read_prg(addr));
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U)
        REQUIRE(copy->read_chr(addr) == m->read_chr(addr));
    for (unsigned line = 0U; line < 600U; ++line) {
        m->on_ppu_hblank();
        copy->on_ppu_hblank();
        REQUIRE(copy->irq_pending() == m->irq_pending());
        REQUIRE(state(*copy) == state(*m));
    }

    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated{std::span(saved).first(size)};
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    for (Byte flags : {4U, 5U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[23U] = flags;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    for (uint16_t scanline : {312U, 313U, 0xFFFFU}) {
        auto bad = saved;
        bad[24U] = static_cast<Byte>(scanline);
        bad[25U] = static_cast<Byte>(scanline >> 8U);
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }

    for (unsigned kind = 0U; kind < 4U; ++kind) {
        auto other_h = h;
        std::size_t prg_size = 0x20000U;
        std::size_t chr_size = 0x10000U;
        if (kind == 0U)
            other_h.mirror_mode = MirrorMode::Horizontal;
        if (kind == 1U)
            prg_size += 8192U;
        if (kind == 2U)
            chr_size += 1024U;
        if (kind == 3U)
            other_h.region = Region::NTSC;
        auto other = board(other_h, prg_size, chr_size);
        const auto before = state(*other);
        StateReader mismatched(saved);
        other->load_state(mismatched);
        REQUIRE_FALSE(mismatched.ok());
        REQUIRE(state(*other) == before);
    }

    for (std::size_t index = 10U; index < 23U; ++index)
        for (unsigned value = 0U; value < 256U; ++value) {
            auto full_byte = saved;
            full_byte[index] = static_cast<Byte>(value);
            StateReader valid(full_byte);
            m->load_state(valid);
            REQUIRE(valid.ok());
            REQUIRE(state(*m) == full_byte);
        }
}

TEST_CASE("Mapper 117 accepts decoder-sized ROMs and rejects unsupported geometry", "[mapper117]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (Region region : {Region::NTSC, Region::PAL, Region::Dendy, Region::Multi}) {
                const auto h = header(legacy, vertical, region);
                for (const auto [p, c] :
                     std::array<std::pair<std::size_t, std::size_t>, 5>{{{0x8000U, 0x2000U},
                                                                         {0xA000U, 0x2C00U},
                                                                         {0x20000U, 0x10000U},
                                                                         {0x1FE000U, 0x3FC00U},
                                                                         {0x200000U, 0x40000U}}})
                    REQUIRE(Mapper117::validate_geometry(h, p, c));
            }

    for (auto p : {0U, 0x2000U, 0x4000U, 0x7FFFU, 0x8001U, 0x201000U})
        REQUIRE_FALSE(Mapper117::validate_geometry(header(), p, 0x2000U));
    for (auto c : {0U, 0x400U, 0x1000U, 0x1FFFU, 0x2001U, 0x40400U})
        REQUIRE_FALSE(Mapper117::validate_geometry(header(), 0x8000U, c));

    for (unsigned kind = 0U; kind < 11U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.mapper_number = 4U;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 10U:
            h = header(true);
            h.has_battery = true;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Mapper117::validate_geometry(h, 0x8000U, 0x2000U));
    }
    auto legacy = header(true);
    legacy.submapper = 15U;
    REQUIRE(Mapper117::validate_geometry(legacy, 0x8000U, 0x2000U));
}

} // namespace
