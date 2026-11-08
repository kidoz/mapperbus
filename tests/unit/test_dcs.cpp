#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "../support/dcs_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
constexpr unsigned kDcsCycles = 107520U;

Cartridge dcs_cart(std::span<const Byte> option = {}) {
    register_builtin_mappers();
    auto result = Cartridge::from_data(mapperbus::tests::make_dcs_base_rom(), {}, option);
    REQUIRE(result);
    return std::move(*result);
}
} // namespace

TEST_CASE("DCS option ROM expires on the documented M2 boundary and strobes reactivate it",
          "[mappers][dcs][bus]") {
    const std::vector<Byte> option(0x4000U, 0x5AU);
    auto cart = dcs_cart(option);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0xF000U, 0U);
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x8000U) == 0xA5U); // Not activated yet.
    bus.write(0x7FFFU, 0xFFU);
    REQUIRE(bus.read(0xBFFFU) == 0x5AU);
    bus.clock_cpu_bus(0U, kDcsCycles - 1U);
    REQUIRE(bus.read(0x8000U) == 0x5AU);
    bus.clock_cpu_bus(0U);
    bus.write(0U, 0x42U);
    REQUIRE(bus.read(0x8000U) == 0x42U);
    bus.write(0x6000U, 0U);
    REQUIRE(bus.read(0x8000U) == 0x5AU);
    REQUIRE(cart.read_prg(0xC000U) == 7U);
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U); // Licensing port is write-only.
    Ppu ppu;
    bus.connect_ppu(&ppu);
    bus.write(0x6000U, 0U);
    bus.clock_cpu_bus(0U, kDcsCycles - 514U);
    REQUIRE(bus.run_oam_dma(0U, 0xC000U, false) == 513U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0xFFU);
    bus.clock_cpu_bus(0U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0U);
}

TEST_CASE("DCS timer runs independently of bank selection and WRAM gating", "[mappers][dcs]") {
    const std::vector<Byte> option(0x4000U, 0x5AU);
    auto cart = dcs_cart(option);
    cart.write_prg(0x6000U, 0U);    // Internal ROM selected, WRAM disabled.
    cart.write_prg(0xF000U, 0x18U); // WRAM + internal ROM.
    for (unsigned i = 0U; i < kDcsCycles; ++i) {
        cart.write_prg(0x6000U, 0x11U); // WRAM writes must NOT retrigger.
        cart.clock_cpu();
    }
    cart.write_prg(0xF000U, 0U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0U);
    cart.write_prg(0x6000U, 0U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0xFFU);
    cart.write_prg(0xF000U, 0x10U);
    REQUIRE(cart.read_prg(0x6000U) == 0x11U);
    for (unsigned i = 0U; i < kDcsCycles; ++i)
        cart.clock_cpu();
    REQUIRE(cart.prg_read_mask(0x8000U) == 0U);
    REQUIRE(cart.prg_read_mask(0xC000U) == 0xFFU);
    cart.reset();
    REQUIRE(cart.read_prg(0x8000U) == 0U);
    cart.write_prg(0xF000U, 0U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0U);
}

TEST_CASE("DCS option chips mirror smaller dumps and select all eight external banks",
          "[mappers][dcs]") {
    for (const std::size_t size : {0x4000U, 0x8000U, 0x10000U, 0x20000U}) {
        std::vector<Byte> option(size);
        for (std::size_t i = 0U; i < size; ++i)
            option[i] = static_cast<Byte>(0x40U + i / 0x4000U);
        auto cart = dcs_cart(option);
        cart.write_prg(0x6000U, 0U);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            cart.write_prg(0xF000U, bank);
            REQUIRE(cart.read_prg(0x8000U) == 0x40U + bank % (size / 0x4000U));
            REQUIRE(cart.read_prg(0xBFFFU) == 0x40U + bank % (size / 0x4000U));
            REQUIRE(cart.read_prg(0xC000U) == 7U);
            cart.write_prg(0xF000U, static_cast<Byte>(bank | 8U));
            REQUIRE(cart.read_prg(0x8000U) == bank);
        }
    }
}

TEST_CASE("DCS save states bind both immutable ROMs and replay expiry without partial restore",
          "[mappers][dcs][state]") {
    const std::vector<Byte> option(0x4000U, 0x5AU);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(dcs_cart(option), {}));
    emulator.reset();
    for (unsigned i = 0U; i < 3U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0x8000U) == 0x5AU);
    const auto running = emulator.save_state();
    for (unsigned i = 0U; i < 35840U; ++i)
        emulator.step_instruction(); // 107520 M2 cycles.
    const auto expired = emulator.save_state();
    REQUIRE(emulator.read_cpu(0x8000U) != 0x5AU);
    REQUIRE(emulator.load_state(running));
    for (unsigned i = 0U; i < 35840U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.save_state() == expired);
    auto corrupt_timer = expired;
    const std::uint32_t out_of_range = kDcsCycles + 1U;
    std::memcpy(
        corrupt_timer.data() + corrupt_timer.size() - 14U, &out_of_range, sizeof(out_of_range));
    REQUIRE_FALSE(emulator.load_state(corrupt_timer));
    REQUIRE(emulator.save_state() == expired);
    auto changed = option;
    changed.back() ^= 1U;
    Emulator other;
    REQUIRE(other.load_cartridge(dcs_cart(changed), {}));
    other.reset();
    const auto before = other.save_state();
    REQUIRE(other.content_hash() != emulator.content_hash());
    REQUIRE_FALSE(other.load_state(running));
    REQUIRE(other.save_state() == before);
    Emulator empty;
    REQUIRE(empty.load_cartridge(dcs_cart(), {}));
    REQUIRE_FALSE(empty.load_state(running));
    auto renamed =
        Cartridge::from_data(mapperbus::tests::make_dcs_base_rom(), "other-name.nes", option);
    REQUIRE(renamed);
    REQUIRE(renamed->content_hash() == emulator.content_hash());
    auto changed_base = mapperbus::tests::make_dcs_base_rom();
    changed_base[16U] ^= 1U;
    auto another_base = Cartridge::from_data(changed_base, {}, option);
    REQUIRE(another_base);
    REQUIRE(another_base->content_hash() != emulator.content_hash());
}

TEST_CASE("DCS attachment rejects invalid payloads and unrelated cartridge boards",
          "[mappers][dcs]") {
    register_builtin_mappers();
    for (const std::size_t size : {1U, 0x2000U, 0x4001U, 0xC000U, 0x40000U})
        REQUIRE_FALSE(Cartridge::from_data(
            mapperbus::tests::make_dcs_base_rom(), {}, std::vector<Byte>(size)));
    auto regular = mapperbus::tests::make_dcs_base_rom();
    regular[8U] = 0U;
    REQUIRE_FALSE(Cartridge::from_data(regular, {}, std::vector<Byte>(0x4000U)));
}

TEST_CASE("DCS snapshots restore partial countdowns, reject corrupt counters and import empty v19",
          "[mappers][dcs][state]") {
    const std::vector<Byte> chip(0x4000U, 0x5AU);
    auto cart = dcs_cart(chip);
    cart.write_prg(0xF000U, 0U);
    cart.write_prg(0x6000U, 0U);
    for (unsigned i = 0U; i < kDcsCycles - 2U; ++i)
        cart.clock_cpu();
    StateWriter writer;
    cart.save_state(writer);
    for (unsigned replay = 0U; replay < 2U; ++replay) {
        StateReader reader(writer.data());
        cart.load_state(reader);
        REQUIRE(reader.ok());
        cart.clock_cpu();
        REQUIRE(cart.prg_read_mask(0x8000U) == 0xFFU);
        cart.clock_cpu();
        REQUIRE(cart.prg_read_mask(0x8000U) == 0U);
    }
    auto corrupt = writer.data();
    const std::uint32_t invalid = kDcsCycles + 1U;
    std::memcpy(corrupt.data() + corrupt.size() - sizeof(invalid), &invalid, sizeof(invalid));
    StateReader invalid_reader(corrupt);
    cart.load_state(invalid_reader);
    REQUIRE_FALSE(invalid_reader.ok());
    auto empty = dcs_cart();
    StateWriter current;
    empty.save_state(current);
    StateReader legacy(
        std::span(current.data()).first(current.data().size() - sizeof(std::uint32_t)));
    empty.load_state(legacy, 19U);
    REQUIRE(legacy.ok());
    REQUIRE(legacy.remaining() == 0U);
    StateReader attached_legacy(
        std::span(current.data()).first(current.data().size() - sizeof(std::uint32_t)));
    cart.load_state(attached_legacy, 19U);
    REQUIRE_FALSE(attached_legacy.ok());
    // Machine v19 with an empty option slot remains loadable under the base identity.
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(dcs_cart(), {}));
    emulator.reset();
    auto old = emulator.save_state();
    const std::uint32_t version = 19U;
    std::memcpy(old.data() + 4U, &version, sizeof(version));
    old.erase(old.end() - 14, old.end() - 10); // Before the two external controller tails.
    REQUIRE(emulator.load_state(old));
}
