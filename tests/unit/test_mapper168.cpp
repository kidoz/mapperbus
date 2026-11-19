#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/mappers/racermate168.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool all_battery = false) {
    INesHeader h{};
    h.mapper_number = 168U;
    h.is_nes2 = !legacy;
    h.has_battery = true;
    h.mirror_mode = MirrorMode::Vertical;
    if (legacy) {
        h.prg_nvram_size = 8192U;
        h.chr_ram_size = 8192U;
    } else {
        h.chr_ram_size = all_battery ? 0U : 32768U;
        h.chr_nvram_size = all_battery ? 65536U : 32768U;
    }
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 53U + offset + (offset >> 8U));
}

std::unique_ptr<Racermate168> board(bool legacy = false, bool all_battery = false) {
    const auto h = header(legacy, all_battery);
    REQUIRE(Racermate168::validate_geometry(h, 65536U, 0U));
    std::vector<Byte> prg(65536U);
    for (unsigned offset = 0U; offset < prg.size(); ++offset)
        prg[offset] = prg_cell(offset / 16384U, offset % 16384U);
    return std::make_unique<Racermate168>(h, std::move(prg), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void unlock(Racermate168& mapper) {
    mapper.write_prg(0xF080U, 0xFFU);
    mapper.write_prg(0xF000U, 0U);
}

unsigned physical_bank(unsigned logical) {
    return (0U - logical) & 15U;
}

TEST_CASE("Mapper 168 accepts only the documented PRG CHR RAM and vertical populations",
          "[mapper168]") {
    for (bool legacy : {false, true})
        for (bool all : {false, true}) {
            auto mapper = board(legacy, all);
            REQUIRE(mapper->battery_ram().size() == (all && !legacy ? 65536U : 32768U));
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*mapper).size() == 65542U);
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
            REQUIRE(mapper->audio_output() == 0.0F);
        }

    for (unsigned size : {0U, 16384U, 32768U, 65535U, 65537U, 131072U})
        REQUIRE_FALSE(Racermate168::validate_geometry(header(), size, 0U));
    for (unsigned size : {1U, 8192U, 65535U, 65537U, 131072U})
        REQUIRE_FALSE(Racermate168::validate_geometry(header(), 65536U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Racermate168::validate_geometry(h, 65536U, 0U));
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = false;
        REQUIRE_FALSE(Racermate168::validate_geometry(h, 65536U, 0U));
        for (auto mode : {MirrorMode::Horizontal,
                          MirrorMode::FourScreen,
                          MirrorMode::SingleLower,
                          MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mode;
            REQUIRE_FALSE(Racermate168::validate_geometry(h, 65536U, 0U));
        }
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 8192U;
            break;
        case 2U:
            h.chr_ram_size = 0U;
            break;
        case 3U:
            h.chr_nvram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 65536U;
            break;
        case 5U:
            h.chr_ram_size = 16384U;
            h.chr_nvram_size = 49152U;
            break;
        default:
            h.chr_ram_size = 32768U;
            h.chr_nvram_size = 65536U;
            break;
        }
        REQUIRE_FALSE(Racermate168::validate_geometry(h, 65536U, 0U));
    }
}

TEST_CASE("Mapper 168 maps four PRG banks without bus conflicts and fixes the last bank",
          "[mapper168]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xBFFFU, static_cast<Byte>(value));
        for (unsigned offset : {0U, 1U, 0x1234U, 0x3FFFU}) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(value >> 6U, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                    prg_cell(3U, offset));
        }
    }
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(3U, 0U));
    }
}

TEST_CASE("Mapper 168 decodes every CPU register alias and ignores address and unused data bits",
          "[mapper168]") {
    auto mapper = board();
    unlock(*mapper);
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_chr(0x1000U, 0x0FU);
    mapper->write_prg(0x8000U, 0U);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(0x8000U, 0U);
        mapper->write_prg(static_cast<Address>(addr), 0xDFU);
        const bool bank_write = addr >= 0x8000U && addr < 0xC000U;
        INFO("address " << addr);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(bank_write ? 3U : 0U, 0U));
        REQUIRE(mapper->read_chr(0x1000U) == (bank_write ? 0x0FU : 0U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
    }
}

TEST_CASE("Mapper 168 NAND CHR wiring reaches all sixteen logical banks and both RAM chips",
          "[mapper168]") {
    for (bool all : {false, true}) {
        auto mapper = board(false, all);
        unlock(*mapper);
        constexpr unsigned offset = 0x0123U;
        for (unsigned logical = 0U; logical < 16U; ++logical) {
            mapper->write_prg(0x8000U, static_cast<Byte>(logical));
            mapper->write_chr(0x1000U + offset, static_cast<Byte>(0x80U + logical));
        }
        for (unsigned logical = 0U; logical < 16U; ++logical) {
            mapper->write_prg(0x8000U, static_cast<Byte>(logical));
            REQUIRE(mapper->read_chr(0x1000U + offset) == 0x80U + logical);
            REQUIRE(mapper->read_chr(offset) == 0x80U);
            const auto physical = physical_bank(logical);
            if (all || physical >= 8U)
                REQUIRE(mapper->battery_ram()[(physical - (all ? 0U : 8U)) * 4096U + offset] ==
                        0x80U + logical);
        }
        REQUIRE(mapper->battery_ram().size() == (all ? 65536U : 32768U));
    }
}

TEST_CASE("Mapper 168 protects only battery-backed CHR and uses D2 falling edges to unlock",
          "[mapper168]") {
    auto mapper = board();
    std::vector<Byte> persistent(32768U);
    for (unsigned index = 0U; index < persistent.size(); ++index)
        persistent[index] = static_cast<Byte>(index * 29U + (index >> 8U));
    mapper->set_battery_ram(persistent);
    mapper->power_on();

    mapper->write_prg(0x8000U, 1U);
    REQUIRE(mapper->read_chr(0x10ABU) == 0xABU);
    mapper->write_chr(0x10ABU, 0x55U);
    unlock(*mapper);
    REQUIRE(mapper->read_chr(0x10ABU) == persistent[7U * 4096U + 0xABU]);
    mapper->write_chr(0x10ABU, 0x55U);
    REQUIRE(mapper->read_chr(0x10ABU) == 0x55U);

    mapper->power_on();
    mapper->write_prg(0x8000U, 9U);
    mapper->write_chr(0x1000U, 0x67U);
    REQUIRE(mapper->read_chr(0x1000U) == 0x67U);
    mapper->write_chr(0x0000U, 0x38U);
    REQUIRE(mapper->read_chr(0U) == 0x38U);

    mapper->write_prg(0xC000U, 0x80U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->write_prg(0x8000U, 1U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
    mapper->write_prg(0xC000U, 4U);
    mapper->write_prg(0xC000U, 4U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
    mapper->write_prg(0xC000U, 0U);
    REQUIRE(mapper->read_chr(0x1000U) == persistent[7U * 4096U]);

    auto modified = board(false, true);
    std::vector<Byte> all_persistent(65536U, 0xA6U);
    modified->set_battery_ram(all_persistent);
    modified->power_on();
    REQUIRE(modified->read_chr(0x00ABU) == 0xABU);
    modified->write_chr(0x00ABU, 0x11U);
    unlock(*modified);
    REQUIRE(modified->read_chr(0x00ABU) == 0xA6U);
    modified->write_chr(0x00ABU, 0x11U);
    REQUIRE(modified->read_chr(0x00ABU) == 0x11U);
}

TEST_CASE("Mapper 168 counter asserts for alternating 1024-cycle intervals and D2 freezes zero",
          "[mapper168]") {
    auto mapper = board();
    for (unsigned cycle = 0U; cycle < 1023U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 1023U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 1024U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xF080U, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF000U, 0U);
    for (unsigned cycle = 0U; cycle < 1024U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 3071U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 168 soft reset retains state and cold power clears only volatile CHR",
          "[mapper168]") {
    auto mapper = board();
    std::vector<Byte> persistent(32768U, 0xB4U);
    mapper->set_battery_ram(persistent);
    unlock(*mapper);
    mapper->write_prg(0x8000U, 0x89U);
    mapper->write_chr(0x0000U, 0x27U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_chr(0x1000U, 0x39U);
    for (unsigned cycle = 0U; cycle < 777U; ++cycle)
        mapper->clock_cpu();
    const auto before_reset = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == before_reset);

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_chr(0U) == 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x8000U, 1U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
    unlock(*mapper);
    REQUIRE(mapper->read_chr(0x1000U) == 0x39U);
    REQUIRE(mapper->read_chr(0x1001U) == 0xB4U);

    auto modified = board(false, true);
    std::vector<Byte> all_persistent(65536U, 0x6DU);
    modified->set_battery_ram(all_persistent);
    modified->power_on();
    unlock(*modified);
    REQUIRE(modified->read_chr(0U) == 0x6DU);
}

TEST_CASE("Mapper 168 battery imports and jumper selection are size checked and atomic",
          "[mapper168]") {
    for (bool all : {false, true}) {
        auto mapper = board(false, all);
        const auto expected_size = all ? 65536U : 32768U;
        std::vector<Byte> data(expected_size, 0x91U);
        mapper->set_battery_ram(data);
        const auto imported = state(*mapper);
        for (unsigned size : {0U, 1U, expected_size - 1U, expected_size + 1U, 65536U}) {
            if (size == expected_size)
                continue;
            mapper->set_battery_ram(std::vector<Byte>(size, 0xEEU));
            REQUIRE(state(*mapper) == imported);
        }
        REQUIRE(
            std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0x91U; }));
    }

    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE(mapper->battery_ram().size() == 65536U);
    const auto modified = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    REQUIRE(state(*mapper) == modified);
    REQUIRE(mapper->configure_board_jumpers(0U));
    REQUIRE(mapper->battery_ram().size() == 32768U);
    REQUIRE_FALSE(mapper->configure_board_profile("all-battery"));
}

TEST_CASE("Mapper 168 state replay restores banks counter protection and all CHR bytes",
          "[mapper168]") {
    auto mapper = board();
    unlock(*mapper);
    mapper->write_prg(0x8000U, 0x8DU);
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + (addr >> 8U)));
    for (unsigned cycle = 0U; cycle < 1301U; ++cycle)
        mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 65542U);

    mapper->power_on();
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(2U, 0U));
    REQUIRE(mapper->irq_pending());
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr * 17U + (addr >> 8U)));
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 168 malformed states and wrong battery populations reject without mutation",
          "[mapper168]") {
    auto mapper = board();
    unlock(*mapper);
    mapper->write_prg(0x8000U, 0xC7U);
    for (unsigned cycle = 0U; cycle < 777U; ++cycle)
        mapper->clock_cpu();
    mapper->write_chr(0x1000U, 0x58U);
    const auto saved = state(*mapper);

    for (std::size_t size : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 100U, 65541U}) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte reserved : {0x10U, 0x20U, 0x30U, 0xF0U}) {
        auto bad = saved;
        bad[2] |= reserved;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned flags = 4U; flags < 256U; ++flags) {
        auto bad = saved;
        bad[3] = static_cast<Byte>(flags);
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    {
        auto bad = saved;
        bad[4] = 0xFFU;
        bad[5] = 0xFFU;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    {
        auto bad = saved;
        bad[3] |= 2U;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto modified = board(false, true);
    const auto modified_saved = state(*modified);
    StateReader wrong_population(saved);
    modified->load_state(wrong_population);
    REQUIRE_FALSE(wrong_population.ok());
    REQUIRE(state(*modified) == modified_saved);

    StateReader valid(saved);
    mapper->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
