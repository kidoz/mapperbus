#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper106.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 106U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = 0x2000U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 41U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

Mapper106 board(bool legacy = false, bool vertical = true) {
    std::vector<Byte> prg(0x40000U);
    std::vector<Byte> chr(0x20000U);
    for (unsigned offset = 0U; offset < prg.size(); ++offset)
        prg[offset] = prg_cell(offset / 0x2000U, offset % 0x2000U);
    for (unsigned offset = 0U; offset < chr.size(); ++offset)
        chr[offset] = chr_cell(offset / 0x400U, offset % 0x400U);
    return Mapper106(header(legacy, vertical), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper106& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void require_unchanged_after_bad_state(Mapper106& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
}

TEST_CASE("Mapper 106 cold power uses deterministic defaults and volatile WRAM", "[mapper106]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto mapper = board(legacy, vertical);
            REQUIRE(mapper.battery_ram().empty());
            REQUIRE(state(mapper).size() == 8210U);
            REQUIRE(mapper.mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(mapper.has_expansion_audio());
            REQUIRE_FALSE(mapper.irq_pending());
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        prg_cell(31U, 0U));
            const std::array<unsigned, 8> chr_defaults = {
                126U, 127U, 126U, 127U, 127U, 127U, 127U, 127U};
            for (unsigned slot = 0U; slot < 8U; ++slot)
                REQUIRE(mapper.read_chr(static_cast<Address>(slot * 0x400U)) ==
                        chr_cell(chr_defaults[slot], 0U));
            for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU})
                REQUIRE_FALSE(mapper.maps_prg(addr));
            for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU})
                REQUIRE(mapper.maps_prg(addr));
            for (Address addr : {0x4020U, 0x6000U, 0x8000U, 0xFFFFU}) {
                REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
                REQUIRE_FALSE(mapper.maps_expansion(addr));
            }
        }
}

TEST_CASE("Mapper 106 decodes every register alias throughout CPU ROM space", "[mapper106]") {
    auto mapper = board(false, false);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper.power_on();
        mapper.write_prg(static_cast<Address>(addr), 0U);
        const auto reg = addr & 0x0FU;
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const unsigned expected = reg == slot ? (slot < 4U ? slot & 1U : 0U)
                                      : slot < 4U ? 126U | (slot & 1U)
                                                  : 127U;
            INFO("addr=" << addr << " slot=" << slot);
            REQUIRE(mapper.read_chr(static_cast<Address>(slot * 0x400U + 0x155U)) ==
                    chr_cell(expected, 0x155U));
        }
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const unsigned expected =
                reg == slot + 8U ? (slot == 0U || slot == 3U ? 16U : 0U) : 31U;
            REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + slot * 0x2000U + 0x123U)) ==
                    prg_cell(expected, 0x123U));
        }
        REQUIRE(mapper.mirror_mode() ==
                (reg == 12U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
}

TEST_CASE("Mapper 106 bank registers mask every data byte without bus conflicts", "[mapper106]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 12U; ++reg)
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper.power_on();
            mapper.write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(value));
            if (reg < 8U) {
                const unsigned expected = reg < 4U ? (value & 0x7EU) | (reg & 1U) : value & 0x7FU;
                REQUIRE(mapper.read_chr(static_cast<Address>(reg * 0x400U + 0x2A5U)) ==
                        chr_cell(expected, 0x2A5U));
            } else {
                const unsigned expected =
                    reg == 8U || reg == 11U ? 16U | (value & 0x0FU) : value & 0x1FU;
                REQUIRE(mapper.read_prg(
                            static_cast<Address>(0x8000U + (reg - 8U) * 0x2000U + 0x345U)) ==
                        prg_cell(expected, 0x345U));
            }
        }
    mapper.write_prg(0x800CU, 0U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.write_prg(0x800CU, 1U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
    mapper.write_prg(0x800CU, 0xFEU);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.write_prg(0x800CU, 0xFFU);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 106 maps every PRG and CHR ROM cell and protects CHR ROM", "[mapper106]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 32U; ++bank) {
        for (unsigned slot = 0U; slot < 4U; ++slot)
            mapper.write_prg(static_cast<Address>(0x8008U + slot), static_cast<Byte>(bank));
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto slot = (addr >> 13U) & 3U;
            const unsigned selected = slot == 0U || slot == 3U ? 16U | (bank & 0x0FU) : bank;
            REQUIRE(mapper.read_prg(static_cast<Address>(addr)) ==
                    prg_cell(selected, addr & 0x1FFFU));
        }
    }
    for (unsigned bank = 0U; bank < 128U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot)
            mapper.write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
            const auto slot = (addr >> 10U) & 7U;
            const unsigned selected = slot < 4U ? (bank & 0x7EU) | (slot & 1U) : bank;
            mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(bank ^ 0xFFU));
            REQUIRE(mapper.read_chr(static_cast<Address>(addr)) ==
                    chr_cell(selected, addr & 0x3FFU));
        }
    }
}

TEST_CASE("Mapper 106 exposes exactly 8 KiB WRAM and clears it only on cold power", "[mapper106]") {
    auto mapper = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        mapper.write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper.read_prg(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
    const auto saved = state(mapper);
    mapper.reset();
    REQUIRE(state(mapper) == saved);
    mapper.power_on();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper.read_prg(static_cast<Address>(addr)) == 0U);
}

TEST_CASE("Mapper 106 counter clocks only at M2 falling edges and saturates", "[mapper106]") {
    auto mapper = board();
    mapper.clock_cpu();
    mapper.clock_irq_counter();
    mapper.clock_audio();
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.write_prg(0x800DU, 0xA7U);
    mapper.write_prg(0x800FU, 0U);
    for (unsigned cycle = 0U; cycle < 65534U; ++cycle) {
        mapper.clock_cpu_falling_edge();
        REQUIRE_FALSE(mapper.irq_pending());
    }
    mapper.clock_cpu_falling_edge();
    REQUIRE(mapper.irq_pending());
    mapper.acknowledge_irq();
    REQUIRE(mapper.irq_pending());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle)
        mapper.clock_cpu_falling_edge();
    REQUIRE(mapper.irq_pending());
    const auto saved = state(mapper);
    mapper.reset();
    REQUIRE(state(mapper) == saved);
    mapper.write_prg(0x800DU, 0U);
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.write_prg(0x800EU, 0xFEU);
    mapper.write_prg(0x800FU, 0xFFU);
    mapper.clock_cpu_falling_edge();
    REQUIRE(mapper.irq_pending());
    mapper.write_prg(0x800EU, 0U);
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.write_prg(0x800FU, 0xFFU);
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.write_prg(0x800EU, 0xFFU);
    REQUIRE(mapper.irq_pending());
}

TEST_CASE("Mapper 106 soft reset retains every register and cold power restores defaults",
          "[mapper106]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 12U; ++reg)
        mapper.write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg * 17U + 3U));
    mapper.write_prg(0x800EU, 0x34U);
    mapper.write_prg(0x800FU, 0x12U);
    mapper.write_prg(0x6000U, 0xA7U);
    const auto saved = state(mapper);
    mapper.reset();
    REQUIRE(state(mapper) == saved);
    mapper.power_on();
    REQUIRE(mapper.read_prg(0x6000U) == 0U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper.irq_pending());
    for (unsigned slot = 0U; slot < 4U; ++slot)
        REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + slot * 0x2000U + 0x111U)) ==
                prg_cell(31U, 0x111U));
}

TEST_CASE("Mapper 106 rejects unsupported geometry and memory metadata", "[mapper106]") {
    for (bool legacy : {false, true}) {
        REQUIRE(Mapper106::validate_geometry(header(legacy), 0x40000U, 0x20000U));
        for (unsigned size : {0U, 0x2000U, 0x10000U, 0x3FFFFU, 0x40001U, 0x80000U}) {
            REQUIRE_FALSE(Mapper106::validate_geometry(header(legacy), size, 0x20000U));
            REQUIRE_FALSE(Mapper106::validate_geometry(header(legacy), 0x40000U, size));
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto h = header(legacy);
            h.mirror_mode = mode;
            REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
        }
        auto h = header(legacy);
        h.has_battery = true;
        REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
        h = header(legacy);
        h.chr_ram_size = 0x2000U;
        REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
        h = header(legacy);
        h.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
    }
    for (unsigned which = 0U; which < 4U; ++which) {
        auto h = header();
        if (which == 0U)
            h.prg_ram_size = 0U;
        else if (which == 1U)
            h.prg_ram_size = 0x1000U;
        else if (which == 2U)
            h.prg_ram_size = 0x4000U;
        else
            h.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(Mapper106::validate_geometry(h, 0x40000U, 0x20000U));
    }
    auto legacy = header(true);
    legacy.prg_ram_size = 0U;
    REQUIRE(Mapper106::validate_geometry(legacy, 0x40000U, 0x20000U));
}

TEST_CASE("Mapper 106 state replays banking WRAM mirroring and IRQ state", "[mapper106]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 12U; ++reg)
        mapper.write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg * 11U + 7U));
    mapper.write_prg(0x800EU, 0xFEU);
    mapper.write_prg(0x800FU, 0xFFU);
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        mapper.write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 3U));
    const auto saved = state(mapper);
    REQUIRE(saved.size() == 8210U);
    mapper.power_on();
    StateReader reader(saved);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(mapper) == saved);
    mapper.clock_cpu_falling_edge();
    REQUIRE(mapper.irq_pending());
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper.read_prg(static_cast<Address>(addr)) == static_cast<Byte>(addr * 3U));
}

TEST_CASE("Mapper 106 rejects truncated malformed and mismatched states atomically",
          "[mapper106]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 12U; ++reg)
        mapper.write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg * 13U + 5U));
    mapper.write_prg(0x800EU, 0xFEU);
    mapper.write_prg(0x800FU, 0xFFU);
    mapper.write_prg(0x6123U, 0xA7U);
    const auto saved = state(mapper);
    for (unsigned length = 0U; length < saved.size(); ++length)
        require_unchanged_after_bad_state(mapper, std::span(saved).first(length));
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        require_unchanged_after_bad_state(mapper, corrupt);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        auto corrupt = saved;
        corrupt[2U + slot] = 0x80U;
        require_unchanged_after_bad_state(mapper, corrupt);
    }
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        auto corrupt = saved;
        corrupt[2U + slot] = static_cast<Byte>((corrupt[2U + slot] & 0x7EU) | ((slot & 1U) ^ 1U));
        require_unchanged_after_bad_state(mapper, corrupt);
    }
    const std::array<Byte, 4> bad_prg = {0x0FU, 0x20U, 0xFFU, 0x20U};
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        auto corrupt = saved;
        corrupt[10U + slot] = bad_prg[slot];
        require_unchanged_after_bad_state(mapper, corrupt);
    }
    for (const auto& [offset, value] :
         std::array<std::pair<unsigned, Byte>, 2>{{{14U, 2U}, {17U, 2U}}}) {
        auto corrupt = saved;
        corrupt[offset] = value;
        require_unchanged_after_bad_state(mapper, corrupt);
    }
    auto other = board(false, false);
    require_unchanged_after_bad_state(other, saved);
}

} // namespace
