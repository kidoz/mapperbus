#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
std::vector<Byte> image(unsigned id, unsigned prg_banks, unsigned chr_banks, Byte submapper = 0U) {
    std::vector<Byte> data(16U + prg_banks * 16384U + chr_banks * 8192U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = static_cast<Byte>(prg_banks);
    data[5] = static_cast<Byte>(chr_banks);
    data[6] = static_cast<Byte>(id << 4U);
    data[7] = static_cast<Byte>((id & 0xF0U) | 8U);
    data[8] = static_cast<Byte>(submapper << 4U);
    data[11] = chr_banks == 0U ? 7U : 0U;
    for (unsigned n = 0; n < prg_banks * 2U + chr_banks; ++n)
        std::fill_n(data.begin() + 16 + n * 8192U, 8192U, static_cast<Byte>(n));
    return data;
}
void verify_state(Cartridge& cart) {
    StateWriter saved;
    cart.save_state(saved);
    cart.reset();
    StateWriter reset;
    cart.save_state(reset);
    StateReader bad(std::span<const Byte>(saved.data()).first(saved.data().size() - 1U));
    cart.load_state(bad);
    CHECK_FALSE(bad.ok());
    StateWriter after;
    cart.save_state(after);
    CHECK(after.data() == reset.data());
    StateReader reader(saved.data());
    cart.load_state(reader);
    REQUIRE(reader.ok());
    StateWriter restored;
    cart.save_state(restored);
    CHECK(restored.data() == saved.data());
}
} // namespace
TEST_CASE("Ball Games selects 8/16/32 KiB windows for both board wirings", "[ball-supervision]") {
    register_builtin_mappers();
    for (Byte sub : {0U, 1U}) {
        auto cart = Cartridge::from_data(image(51, 32, 0, sub));
        REQUIRE(cart);
        CHECK(cart->read_prg(0x6000) == 35U);
        CHECK(cart->read_prg(0x8000) == 0U);
        cart->write_prg(0x8000, 0x55U);
        CHECK(cart->read_prg(0x6000) == (sub ? 43U : 55U));
        CHECK(cart->read_prg(0x8000) == (sub ? 40U : 20U));
        cart->write_prg(0x7FFF, 0x10U);
        CHECK(cart->mirror_mode() == MirrorMode::Horizontal);
        CHECK(cart->read_prg(0x6000) == (sub ? 43U : 63U));
        CHECK(cart->read_prg(0x8000) == (sub ? 58U : 22U));
        CHECK(cart->read_prg(0xC000) == (sub ? 62U : 30U));
        cart->write_chr(0x1234, 0x89);
        verify_state(*cart);
        CHECK(cart->read_chr(0x1234) == 0x89U);
        CHECK_FALSE(cart->maps_prg(0x5FFF));
    }
}
TEST_CASE("Supervision accepts menu-first or separate EPROM and locks the game selection",
          "[ball-supervision]") {
    register_builtin_mappers();
    auto combined = image(53, 130, 0);
    auto separate = combined;
    separate[4] = 128;
    separate[14] = 1;
    std::rotate(separate.begin() + 16, separate.begin() + 16 + 32768, separate.end());
    for (const auto& data : {combined, separate}) {
        auto cart = Cartridge::from_data(data);
        REQUIRE(cart);
        CHECK(cart->read_prg(0x8000) == 0);
        CHECK(cart->read_prg(0xFFFF) == 3);
        CHECK(cart->read_prg(0x6000) == 19);
        cart->write_prg(0x6000, 0x32);
        cart->write_prg(0x7FFF, 0);
        cart->write_prg(0x8000, 3);
        CHECK(cart->read_prg(0x8000) == 42U);
        CHECK(cart->read_prg(0xC000) == 50U);
        CHECK(cart->read_prg(0x6000) == 51U);
        REQUIRE(cart->mirror_mode() == MirrorMode::Horizontal);
        verify_state(*cart);
        cart->reset();
        CHECK(cart->read_prg(0xFFFF) == 3);
    }
    separate.pop_back();
    CHECK_FALSE(Cartridge::from_data(separate));
}

TEST_CASE("Double Dragon II bootleg independently banks PRG, CHR and CIRAM and times a CPU IRQ",
          "[ball-supervision]") {
    register_builtin_mappers();
    auto rom = image(127, 8, 16);
    for (unsigned page = 0U; page < 128U; ++page)
        std::fill_n(rom.begin() + 16 + 131072U + page * 1024U, 1024U, static_cast<Byte>(page));
    auto cart = Cartridge::from_data(rom);
    REQUIRE(cart);
    REQUIRE(cart->read_prg(0xFFFFU) == 15U);
    for (unsigned slot = 0; slot < 4; ++slot) {
        cart->write_prg(0xFF80U + slot, slot);
        REQUIRE(cart->read_prg(0x8000U + slot * 8192U) == (slot == 3U ? 15U : slot));
        cart->write_prg(0xFFD0U + slot, slot >> 1U);
        REQUIRE(cart->nametable_address(0x2005U + slot * 1024U) == 5U + (slot >> 1U) * 1024U);
    }
    for (unsigned slot = 0; slot < 8; ++slot) {
        cart->write_prg((slot < 4U ? 0xA010U : 0xBB20U) + (slot & 3U), 120U + slot);
        REQUIRE(cart->read_chr(slot * 1024U) == 120U + slot);
    }
    cart->write_prg(0xFAB1U, 0);
    for (unsigned i = 0; i < 255U; ++i)
        cart->clock_cpu();
    REQUIRE_FALSE(cart->irq_pending());
    StateWriter partial;
    cart->save_state(partial);
    cart->clock_cpu();
    REQUIRE(cart->irq_pending());
    cart->write_prg(0xAB40U, 0);
    REQUIRE_FALSE(cart->irq_pending());
    StateReader reader(partial.data());
    cart->load_state(reader);
    REQUIRE(reader.ok());
    cart->clock_cpu();
    REQUIRE(cart->irq_pending());
    verify_state(*cart);
}

TEST_CASE("Historical mapper IDs dispatch to their boards and retain image identity",
          "[legacy-alias]") {
    register_builtin_mappers();
    struct Population {
        uint16_t alias;
        uint16_t canonical;
        Byte prg16;
        Byte chr8;
    };
    for (const Population pop : {Population{102, 284, 4, 4},
                                 Population{109, 137, 4, 4},
                                 Population{110, 243, 4, 8},
                                 Population{129, 58, 8, 8},
                                 Population{130, 331, 32, 64},
                                 Population{131, 205, 32, 32},
                                 Population{161, 1, 8, 0}}) {
        CAPTURE(pop.alias);
        for (const bool legacy : {false, true}) {
            auto data = image(pop.alias, pop.prg16, pop.chr8);
            if (pop.alias == 102U)
                data[10] = 7U;
            if (pop.alias == 161U) {
                data[6] |= 2U;
                data[10] = 0x70U;
            }
            auto canonical = data;
            canonical[6] = static_cast<Byte>((data[6] & 15U) | (pop.canonical << 4U));
            canonical[7] = static_cast<Byte>((pop.canonical & 0xF0U) | 8U);
            canonical[8] = static_cast<Byte>(pop.canonical >> 8U);
            if (legacy) {
                data[7] &= 0xF0U;
                data[10] = data[11] = 0;
            }
            auto original = Cartridge::from_data(data);
            auto target = Cartridge::from_data(canonical);
            if (!original)
                FAIL(original.error());
            if (!target)
                FAIL(target.error());
            REQUIRE(original->header().mapper_number == pop.alias);
            REQUIRE(original->header().is_nes2 != legacy);
            for (auto* cart : {&*original, &*target}) {
                cart->write_expansion(0x4100, 5);
                cart->write_expansion(0x4101, 3);
                cart->write_prg(0x6800, 1);
                cart->write_prg(0x8000, 6);
                cart->write_prg(0x8001, 5);
                cart->write_prg(0xA000, 0x13);
                cart->write_prg(0xC000, 0x12);
                cart->write_prg(0xE000, 0x0D);
                for (unsigned bit = 0; bit < 5; ++bit)
                    cart->write_prg(0xE000, (5U >> bit) & 1U);
                cart->observe_ppu_address(0x1000);
            }
            for (Address addr : {0x6000, 0x8000, 0xA000, 0xC000, 0xE000})
                REQUIRE(original->read_prg(addr) == target->read_prg(addr));
            REQUIRE(original->read_chr(0x1000) == target->read_chr(0x1000));
            REQUIRE(original->mirror_mode() == target->mirror_mode());
            StateWriter old_state, canonical_state;
            original->save_state(old_state);
            target->save_state(canonical_state);
            REQUIRE(old_state.data() == canonical_state.data());
            Emulator old_machine, canonical_machine;
            REQUIRE(old_machine.load_cartridge(std::move(*original), ""));
            REQUIRE(canonical_machine.load_cartridge(std::move(*target), ""));
            old_machine.power_on();
            canonical_machine.power_on();
            auto saved = old_machine.save_state();
            REQUIRE(old_machine.mapper_number() == pop.alias);
            REQUIRE(old_machine.load_state(saved));
            REQUIRE_FALSE(canonical_machine.load_state(saved));
        }
        auto bad = image(pop.alias, pop.prg16, pop.chr8, 1U);
        REQUIRE_FALSE(Cartridge::from_data(bad));
    }
    auto invalid = image(130, 32, 64);
    invalid[7] &= 0xF0U;
    invalid[8] = 2U; // explicit 16 KiB iNES RAM is incompatible with NS03
    REQUIRE_FALSE(Cartridge::from_data(invalid));
}
