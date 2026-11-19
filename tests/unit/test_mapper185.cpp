#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/cnrom185.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 4U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 185U;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> patterned_rom(std::size_t size, unsigned seed) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i * 37U + i / 251U + seed) & 0xFFU);
    return data;
}

std::unique_ptr<Cnrom185> board(Byte submapper = 4U,
                                std::size_t prg_size = 0x8000U,
                                bool legacy = false) {
    return std::make_unique<Cnrom185>(
        header(submapper, legacy), patterned_rom(prg_size, 0x53U), patterned_rom(0x2000U, 0xA7U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 185 accepts only the documented fixed ROM populations and submappers",
          "[mappers][mapper185]") {
    for (Byte submapper : {0U, 4U, 5U, 6U, 7U})
        for (std::size_t prg_size : {0x4000UZ, 0x8000UZ})
            for (MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                auto h = header(submapper);
                h.mirror_mode = mirror;
                REQUIRE(Cnrom185::validate_geometry(h, prg_size, 0x2000U));
            }
    auto legacy = header(0U, true);
    legacy.prg_ram_size = 0x2000U;
    REQUIRE(Cnrom185::validate_geometry(legacy, 0x4000U, 0x2000U));

    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        std::size_t prg_size = 0x8000U;
        std::size_t chr_size = 0x2000U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.submapper = 3U;
            break;
        case 2U:
            h.submapper = 8U;
            break;
        case 3U:
            prg_size = 0U;
            break;
        case 4U:
            prg_size = 0x2000U;
            break;
        case 5U:
            prg_size = 0xC000U;
            break;
        case 6U:
            prg_size = 0x10000U;
            break;
        case 7U:
            chr_size = 0U;
            break;
        case 8U:
            chr_size = 0x4000U;
            break;
        case 9U:
            h.has_battery = true;
            break;
        case 10U:
            h.prg_ram_size = 0x2000U;
            break;
        case 11U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 12U:
            h.chr_ram_size = 0x2000U;
            break;
        case 13U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 14U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 15U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Cnrom185::validate_geometry(h, prg_size, chr_size));
    }
}

TEST_CASE("Mapper 185 applies AND bus conflicts and compares only CS1 and CS2",
          "[mappers][mapper185]") {
    for (Byte submapper : {4U, 5U, 6U, 7U}) {
        Cnrom185 mapper(
            header(submapper), std::vector<Byte>(0x8000U, 0xFFU), patterned_rom(0x2000U, 0xA7U));
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper.write_prg(0x8000U, static_cast<Byte>(value));
            const bool enabled = (value & 3U) == submapper - 4U;
            REQUIRE(state(mapper)[2] == static_cast<Byte>(value & 0xC3U));
            REQUIRE(mapper.read_chr(0U) == (enabled ? 0xA7U : 0x00U));
            REQUIRE(mapper.read_chr(0x1FFFU) ==
                    (enabled ? patterned_rom(0x2000U, 0xA7U)[0x1FFFU] : 0xFFU));
        }
    }
}

TEST_CASE("Mapper 185 decodes every PRG write alias against the fixed PRG mapping",
          "[mappers][mapper185]") {
    for (std::size_t prg_size : {0x4000UZ, 0x8000UZ}) {
        auto mapper = board(6U, prg_size);
        const auto prg = patterned_rom(prg_size, 0x53U);
        const auto chr = patterned_rom(0x2000U, 0xA7U);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto value = static_cast<Byte>(addr * 29U + addr / 257U);
            const auto offset = (addr - 0x8000U) % prg_size;
            mapper->write_prg(static_cast<Address>(addr), value);
            const auto latch = static_cast<Byte>(value & prg[offset] & 0xC3U);
            const bool enabled = (latch & 3U) == 2U;
            const auto chr_addr = static_cast<Address>(addr & 0x1FFFU);
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == prg[offset]);
            REQUIRE(mapper->read_chr(chr_addr) ==
                    (enabled ? chr[chr_addr] : static_cast<Byte>(chr_addr & 0xFFU)));
        }
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    prg[(addr - 0x8000U) % prg_size]);
    }
}

TEST_CASE("Mapper 185 submapper zero counts only completed PPUDATA reads", "[mappers][mapper185]") {
    auto mapper = board(0U);
    const auto initial = state(*mapper);
    REQUIRE(mapper->read_chr(0U) == 0x01U);
    for (unsigned addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>((addr & 0xFFU) | 1U));
    REQUIRE(state(*mapper) == initial);

    for (Address addr : {0x0007U, 0x2006U, 0x4007U, 0x8000U, 0xFFFFU})
        mapper->observe_cpu_read(addr, 0U);
    REQUIRE(state(*mapper) == initial);
    mapper->observe_cpu_read(0x2007U, 0U);
    REQUIRE(mapper->read_chr(0U) == 0x01U);
    mapper->observe_cpu_read(0x200FU, 0U);
    REQUIRE(mapper->read_chr(0U) == 0xA7U);
    const auto enabled = state(*mapper);
    mapper->observe_cpu_read(0x3FFFU, 0U);
    REQUIRE(state(*mapper) == enabled);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    mapper->observe_cpu_read(0x3FFFU, 0U);
    REQUIRE(mapper->read_chr(0U) == 0x01U);
    mapper->observe_cpu_read(0x2007U, 0U);
    REQUIRE(mapper->read_chr(0U) == 0xA7U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 185 reset selects latch zero without changing fixed CIRAM",
          "[mappers][mapper185]") {
    for (Byte submapper : {4U, 5U, 6U, 7U}) {
        auto mapper = board(submapper);
        mapper->write_prg(0x8000U, 0x0FU);
        mapper->observe_cpu_read(0x2007U, 0U);
        mapper->write_chr(0x123U, 0x5CU);
        mapper->reset();
        REQUIRE(mapper->read_chr(0U) == (submapper == 4U ? 0xA7U : 0x00U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*mapper).size() == 4U);
    }
}

TEST_CASE("Mapper 185 exposes no RAM, low registers, IRQ or audio", "[mappers][mapper185]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (unsigned addr = 0x4020U; addr < 0x8000U; ++addr) {
        const auto address = static_cast<Address>(addr);
        REQUIRE_FALSE(mapper->maps_prg(address));
        REQUIRE_FALSE(mapper->maps_expansion(address));
        REQUIRE(mapper->read_prg(address) == 0U);
        mapper->write_prg(address, 0xFFU);
        mapper->write_expansion(address, 0xFFU);
    }
    REQUIRE(state(*mapper) == saved);
    mapper->set_battery_ram(std::array<Byte, 2>{1U, 2U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    mapper->clock_cpu();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 185 snapshots validate profile and commit all fields atomically",
          "[mappers][mapper185][state]") {
    for (Byte submapper : {0U, 4U, 5U, 6U, 7U}) {
        Cnrom185 mapper(
            header(submapper), std::vector<Byte>(0x8000U, 0xFFU), patterned_rom(0x2000U, 0xA7U));
        mapper.write_prg(0xFFFFU, 0xADU);
        if (submapper == 0U)
            mapper.observe_cpu_read(0x2007U, 0U);
        const auto saved = state(mapper);
        REQUIRE(saved.size() == 4U);

        Cnrom185 restored(
            header(submapper), std::vector<Byte>(0x8000U, 0xFFU), patterned_rom(0x2000U, 0xA7U));
        StateReader reader(saved);
        restored.load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(restored) == saved);

        for (unsigned kind = 0U; kind < 8U; ++kind) {
            auto bad = saved;
            if (kind < 4U)
                bad.resize(kind);
            else if (kind == 4U)
                bad[0U] ^= 0x80U;
            else if (kind == 5U)
                bad[1U] ^= 0x80U;
            else if (kind == 6U)
                bad[2U] = 0x10U;
            else
                bad[3U] = submapper == 0U ? 3U : 1U;
            mapper.write_prg(0x8000U, 0x0FU);
            const auto before = state(mapper);
            StateReader invalid(bad);
            mapper.load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(mapper) == before);
        }

        auto other = board(submapper == 7U ? 6U : 7U);
        const auto before = state(*other);
        StateReader wrong_profile(saved);
        other->load_state(wrong_profile);
        REQUIRE_FALSE(wrong_profile.ok());
        REQUIRE(state(*other) == before);
    }
}

Cartridge bus_cartridge(Byte submapper, bool legacy) {
    std::vector<Byte> image(16U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 2U;
    image[5U] = 1U;
    image[6U] = 0x90U;
    image[7U] = legacy ? 0xB0U : 0xB8U;
    image[8U] = static_cast<Byte>(submapper << 4U);
    const auto prg = patterned_rom(0x8000U, 0x53U);
    const auto chr = patterned_rom(0x2000U, 0xA7U);
    image.insert(image.end(), prg.begin(), prg.end());
    image.insert(image.end(), chr.begin(), chr.end());

    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        185U, [](const INesHeader& h, std::vector<Byte> p, std::vector<Byte> c) {
            return std::make_unique<Cnrom185>(h, std::move(p), std::move(c));
        });
    auto result = Cartridge::from_data(image);
    register_builtin_mappers();
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("Mapper 185 legacy heuristic follows actual PPUDATA bus reads", "[mappers][mapper185]") {
    auto cartridge = bus_cartridge(0U, true);
    Ppu ppu;
    ppu.connect_cartridge(&cartridge);
    ppu.reset();
    MemoryBus bus;
    bus.connect_cartridge(&cartridge);
    bus.connect_ppu(&ppu);

    REQUIRE(cartridge.read_chr(0U) == 0x01U);
    bus.write(0x2006U, 0U);
    bus.write(0x2006U, 0U);
    bus.read(0x2007U);
    REQUIRE(cartridge.read_chr(0U) == 0x01U);
    bus.read(0x2007U);
    REQUIRE(cartridge.read_chr(0U) == 0xA7U);

    bus.write(0x2006U, 0U);
    bus.write(0x2006U, 0U);
    bus.read(0x2007U);
    REQUIRE(bus.read(0x2007U) == 0xA7U);
}

} // namespace
