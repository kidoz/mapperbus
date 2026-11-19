#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper052.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 0U, bool battery = false, bool chr_ram = true) {
    INesHeader h{};
    h.mapper_number = 52U;
    h.is_nes2 = true;
    h.submapper = submapper;
    h.has_battery = battery;
    h.prg_ram_size = battery ? 0U : 0x2000U;
    h.prg_nvram_size = battery ? 0x2000U : 0U;
    h.chr_ram_size = submapper == 13U || (submapper == 14U && chr_ram) ? 0x2000U : 0U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 3U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 5U + offset + (offset >> 8U));
}

std::size_t rom_size(Byte submapper) {
    return submapper == 13U ? 0x80000U : 0x100000U;
}

std::unique_ptr<Realtec8213> board(INesHeader h = header()) {
    REQUIRE(Realtec8213::validate_geometry(h, rom_size(h.submapper), rom_size(h.submapper)));
    std::vector<Byte> prg(rom_size(h.submapper));
    std::vector<Byte> chr(rom_size(h.submapper));
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i / 0x2000U, i % 0x2000U);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i / 0x400U, i % 0x400U);
    return std::make_unique<Realtec8213>(h, std::move(prg), std::move(chr));
}

void bank(Realtec8213& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(Byte outer, unsigned inner) {
    const unsigned a17 = (outer & 8U) != 0U ? outer & 1U : (inner >> 4U) & 1U;
    return (inner & 15U) | (a17 << 4U) | ((outer & 2U) << 4U) | ((outer & 4U) << 4U);
}

unsigned expected_chr_bank(Byte outer, unsigned inner, Byte submapper = 0U) {
    const unsigned a17 = (outer & 0x40U) != 0U ? (outer >> 4U) & 1U : (inner >> 4U) & 1U;
    const unsigned a18 = submapper == 14U ? (outer >> 1U) & 1U : (outer >> 5U) & 1U;
    return (inner & 15U) | (a17 << 4U) | (a18 << 5U) | ((outer & 4U) << 4U);
}

std::vector<Byte> state(const Realtec8213& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 052 outer register aliases lock and obey MMC3 WRAM protection", "[mapper052]") {
    auto mapper = board();
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xA000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
    REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));

    bank(*mapper, 6U, 1U);
    for (Address alias : {Address{0x6000U}, Address{0x6800U}, Address{0x7FFFU}}) {
        mapper->reset();
        bank(*mapper, 6U, 1U);
        mapper->write_prg(alias, 0x0BU);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(expected_prg_bank(0x0BU, 1U), 0U));
        mapper->write_prg(alias, 0x8BU);
        mapper->write_prg(0x6000U, 0U);
        mapper->write_prg(0x7FFFU, 0x7FU);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(expected_prg_bank(0x0BU, 1U), 0U));
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    }

    bank(*mapper, 6U, 1U);
    mapper->write_prg(0x6000U, 0x0BU);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x7FFFU, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(expected_prg_bank(0x0BU, 1U), 0U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(1U, 0U));
}

TEST_CASE("Mapper 052 applies outer PRG pins to switchable and fixed banks", "[mapper052]") {
    auto mapper = board();
    for (Byte mode : {Byte{0U}, Byte{0x40U}}) {
        bank(*mapper, 6U, 21U, mode);
        bank(*mapper, 7U, 7U, mode);
        for (unsigned value = 0U; value < 128U; ++value) {
            const auto outer = static_cast<Byte>(value);
            mapper->write_prg(0x6800U, outer);
            const std::array<unsigned, 4> inner = {
                mode == 0U ? 21U : 30U, 7U, mode == 0U ? 30U : 21U, 31U};
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const Address addr = static_cast<Address>(0x8000U + slot * 0x2000U + 0x123U);
                REQUIRE(mapper->read_prg(addr) ==
                        prg_cell(expected_prg_bank(outer, inner[slot]), 0x123U));
            }
        }
    }
}

TEST_CASE("Mapper 052 applies outer CHR pins to every MMC3 slot and inversion mode",
          "[mapper052]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0x22U, 0x0BU, 0x13U, 0x2CU, 0x31U, 0x07U};
    for (Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            bank(*mapper, reg, values[reg], mode);
        for (unsigned value = 0U; value < 128U; ++value) {
            const auto outer = static_cast<Byte>(value);
            mapper->write_prg(0x6800U, outer);
            for (unsigned physical = 0U; physical < 8U; ++physical) {
                const unsigned slot = physical ^ (mode == 0U ? 0U : 4U);
                const unsigned inner =
                    slot < 4U ? (values[slot / 2U] & 0xFEU) | (slot & 1U) : values[slot - 2U];
                const Address addr = static_cast<Address>(physical * 0x400U + 0x155U);
                REQUIRE(mapper->read_chr(addr) ==
                        chr_cell(expected_chr_bank(outer, inner), 0x155U));
            }
        }
    }
}

TEST_CASE("Mapper 052 submapper 13 selects flat CHR RAM from outer PRG A17 and A18",
          "[mapper052]") {
    auto mapper = board(header(13U));
    mapper->write_prg(0x6000U, 3U);
    for (unsigned addr = 0U; addr < 0x2000U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    bank(*mapper, 2U, 0x35U);
    for (unsigned value = 0U; value < 128U; ++value) {
        const auto outer = static_cast<Byte>(value);
        mapper->write_prg(0x6800U, outer);
        const bool ram = (outer & 3U) == 3U;
        REQUIRE(mapper->read_chr(0x1234U) ==
                (ram ? Byte{0x34U ^ 0x12U} : chr_cell(expected_chr_bank(outer, 0x35U), 0x234U)));
        if (!ram) {
            mapper->write_chr(0x1234U, 0U);
            REQUIRE(mapper->read_chr(0x1234U) == chr_cell(expected_chr_bank(outer, 0x35U), 0x234U));
        }
    }
    mapper->write_prg(0x6000U, 3U);
    REQUIRE(mapper->read_chr(0x1234U) == Byte{0x34U ^ 0x12U});
}

TEST_CASE("Mapper 052 AB892 combines A18 and gates optional flat CHR RAM", "[mapper052]") {
    for (bool populated : {false, true}) {
        auto mapper = board(header(14U, false, populated));
        bank(*mapper, 5U, 0x27U);
        for (unsigned value = 0U; value < 128U; ++value) {
            const auto outer = static_cast<Byte>(value & ~0x20U);
            mapper->write_prg(0x6800U, outer);
            REQUIRE(mapper->read_chr(0x1C00U) ==
                    chr_cell(expected_chr_bank(outer, 0x27U, 14U), 0U));
            mapper->write_prg(0x6800U, static_cast<Byte>(outer | 0x20U));
            REQUIRE(mapper->read_chr(0x1C00U) ==
                    (populated ? 0U : chr_cell(expected_chr_bank(outer, 0x27U, 14U), 0U)));
        }
        if (populated) {
            mapper->write_prg(0x6800U, 0x20U);
            mapper->write_chr(0x1FFFU, 0xA7U);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                bank(*mapper, static_cast<Byte>(slot), 0xFFU);
                REQUIRE(mapper->read_chr(0x1FFFU) == 0xA7U);
            }
        }
    }
}

TEST_CASE("Mapper 052 MMC3 aliases control mirroring and scanline IRQ state", "[mapper052]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xBFFEU, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 052 validates ROM submapper memory and CIRAM geometry", "[mapper052]") {
    for (Byte submapper : {Byte{0U}, Byte{13U}, Byte{14U}})
        REQUIRE(Realtec8213::validate_geometry(
            header(submapper), rom_size(submapper), rom_size(submapper)));
    for (unsigned size : {0x8000U, 0x10000U, 0x20000U, 0x40000U, 0x80000U, 0x100000U})
        REQUIRE(Realtec8213::validate_geometry(header(), size, size));
    for (unsigned size : {0U, 0x2000U, 0x4000U, 0x30000U, 0x100001U, 0x200000U})
        REQUIRE_FALSE(Realtec8213::validate_geometry(header(), size, 0x100000U));
    for (unsigned size : {0U, 0x1000U, 0x3000U, 0x18000U, 0x100001U, 0x200000U})
        REQUIRE_FALSE(Realtec8213::validate_geometry(header(), 0x100000U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        if (submapper == 13U || submapper == 14U)
            continue;
        REQUIRE_FALSE(Realtec8213::validate_geometry(header(submapper), 0x100000U, 0x100000U));
    }
    REQUIRE_FALSE(Realtec8213::validate_geometry(header(13U), 0x100000U, 0x80000U));
    REQUIRE_FALSE(Realtec8213::validate_geometry(header(14U), 0x80000U, 0x80000U));
    for (unsigned which = 0U; which < 7U; ++which) {
        auto h = header();
        switch (which) {
        case 0U:
            h.prg_ram_size = 0x4000U;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 5U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        REQUIRE_FALSE(Realtec8213::validate_geometry(h, 0x100000U, 0x100000U));
    }
    auto h = header();
    h.is_nes2 = false;
    h.has_battery = false;
    h.prg_ram_size = h.prg_nvram_size = h.chr_ram_size = 0U;
    REQUIRE(Realtec8213::validate_geometry(h, 0x100000U, 0x100000U));
    h.is_nes2 = true;
    auto no_ram = board(h);
    REQUIRE_FALSE(no_ram->maps_prg(0x6000U));
    REQUIRE(no_ram->battery_ram().empty());
    no_ram->write_prg(0x6000U, 0x0BU);
    REQUIRE(no_ram->read_prg(0xE000U) == prg_cell(expected_prg_bank(0x0BU, 31U), 0U));
}

TEST_CASE("Mapper 052 save state replays RAM IRQ and latch state without ROM bytes",
          "[mapper052]") {
    auto mapper = board(header(14U, true));
    std::array<Byte, 0x2000> saved_ram{};
    for (unsigned i = 0U; i < saved_ram.size(); ++i)
        saved_ram[i] = static_cast<Byte>(i * 13U);
    mapper->set_battery_ram(saved_ram);
    mapper->write_prg(0x6000U, 0x6BU);
    mapper->write_chr(0x1ABCU, 0xA7U);
    bank(*mapper, 6U, 19U, 0xC0U);
    bank(*mapper, 0U, 0x22U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 16412U);
    REQUIRE(mapper->battery_ram().size() == 0x2000U);

    mapper->reset();
    mapper->write_prg(0x6000U, 0U);
    mapper->write_chr(0U, 0U);
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x6123U) == saved_ram[0x123U]);
    REQUIRE(mapper->read_chr(0x1ABCU) == 0xA7U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());

    for (std::size_t length : {std::size_t{0U},
                               std::size_t{1U},
                               std::size_t{11U},
                               std::size_t{27U},
                               std::size_t{100U},
                               saved.size() - 1U}) {
        StateReader truncated{std::span(saved).first(length)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 9> bad_fields = {{{0U, 0U},
                                                                         {4U, 0U},
                                                                         {8U, 0U},
                                                                         {10U, 0U},
                                                                         {13U, 8U},
                                                                         {14U, 1U},
                                                                         {21U, 0x40U},
                                                                         {23U, 2U},
                                                                         {27U, 1U}}};
    for (const auto& [offset, value] : bad_fields) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader rejected(corrupt);
        mapper->load_state(rejected);
        INFO("offset=" << offset << " value=" << +value);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto pending_without_enable = saved;
    pending_without_enable[26U] = 4U;
    StateReader rejected(pending_without_enable);
    mapper->load_state(rejected);
    REQUIRE_FALSE(rejected.ok());
    REQUIRE(state(*mapper) == saved);

    auto other = board();
    const auto before = state(*other);
    StateReader wrong_geometry(saved);
    other->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other) == before);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == saved_ram[0x123U]);
}

TEST_CASE("Mapper 052 reset unlocks outer bank and cold power clears volatile RAM", "[mapper052]") {
    auto mapper = board(header(13U));
    mapper->write_prg(0x6000U, 0x83U);
    mapper->write_chr(0x123U, 0xA7U);
    auto saved = state(*mapper);
    std::fill(saved.begin() + 28U, saved.begin() + 28U + 0x2000U, 0x5AU);
    StateReader restored(saved);
    mapper->load_state(restored);
    REQUIRE(restored.ok());
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    REQUIRE(state(*mapper)[12U] == 0U);
    mapper->write_prg(0x6000U, 3U);
    REQUIRE(mapper->read_chr(0x123U) == 0xA7U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6000U, 3U);
    REQUIRE(mapper->read_chr(0x123U) == 0U);
}

} // namespace
