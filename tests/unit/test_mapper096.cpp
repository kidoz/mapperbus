#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/oeka_kids.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 96U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.nametable_bits = 1U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = legacy ? 8192U : 32768U;
    return h;
}

Byte prg_value(std::size_t bank, std::size_t offset) {
    const auto index = bank * 0x8000U + offset;
    return static_cast<Byte>((index * 37U + bank * 83U + index / 256U) & 0xFFU);
}

std::vector<Byte> make_prg() {
    std::vector<Byte> data(0x20000U);
    for (std::size_t bank = 0U; bank < 4U; ++bank)
        for (std::size_t offset = 0U; offset < 0x8000U; ++offset)
            data[bank * 0x8000U + offset] = prg_value(bank, offset);
    return data;
}

std::unique_ptr<OekaKids> board(INesHeader h = header()) {
    return std::make_unique<OekaKids>(h, make_prg(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_outer(OekaKids& mapper, Byte outer) {
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto address = static_cast<Address>(addr);
        if ((mapper.read_prg(address) & 7U) == 7U) {
            mapper.write_prg(address, outer);
            return;
        }
    }
    FAIL("Synthetic PRG has no conflict-free outer-bank write");
}

void select_inner(OekaKids& mapper, Byte inner) {
    mapper.observe_ppu_address(0x0000U);
    mapper.observe_ppu_address(static_cast<Address>(0x2000U | (inner << 8U)));
}

Byte chr_value(std::size_t bank, std::size_t offset) {
    return static_cast<Byte>((bank * 29U + offset * 7U + offset / 256U) & 0xFFU);
}

TEST_CASE("Mapper 096 validates its exact ROM RAM and CIRAM population", "[mapper096]") {
    REQUIRE(OekaKids::validate_geometry(header(), 0x20000U, 0U));
    REQUIRE(OekaKids::validate_geometry(header(true), 0x20000U, 0U));

    for (std::size_t size : {0UZ, 0x8000UZ, 0x1FFFFUZ, 0x20001UZ, 0x40000UZ}) {
        INFO(size);
        REQUIRE_FALSE(OekaKids::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 0x2000UZ, 0x8000UZ})
        REQUIRE_FALSE(OekaKids::validate_geometry(header(), 0x20000U, size));

    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(OekaKids::validate_geometry(h, 0x20000U, 0U));
    }
    for (auto mode : {MirrorMode::Horizontal,
                      MirrorMode::FourScreen,
                      MirrorMode::SingleLower,
                      MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        REQUIRE_FALSE(OekaKids::validate_geometry(h, 0x20000U, 0U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 32768U;
            break;
        default:
            break;
        }
        INFO(field);
        REQUIRE_FALSE(OekaKids::validate_geometry(h, 0x20000U, 0U));
    }
    auto h = header(true);
    h.prg_nvram_size = 8192U;
    REQUIRE_FALSE(OekaKids::validate_geometry(h, 0x20000U, 0U));
    h = header(true);
    h.chr_nvram_size = 8192U;
    REQUIRE_FALSE(OekaKids::validate_geometry(h, 0x20000U, 0U));
}

TEST_CASE("Mapper 096 decodes every CPU alias with old-bank bus conflicts", "[mapper096]") {
    auto mapper = board();
    Byte outer = 0U;
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const auto address = static_cast<Address>(addr);
        if (addr >= 0x8000U)
            outer = static_cast<Byte>(0xA7U & prg_value(outer & 3U, addr & 0x7FFFU) & 7U);
        mapper->write_prg(address, 0xA7U);
        REQUIRE(mapper->read_prg(0x8000U) == prg_value(outer & 3U, 0U));
        REQUIRE(mapper->maps_prg(address) == (addr >= 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(address));
    }

    for (Byte initial = 0U; initial < 8U; ++initial)
        for (unsigned data = 0U; data < 256U; ++data) {
            INFO(initial);
            INFO(data);
            select_outer(*mapper, initial);
            const auto old = mapper->read_prg(0x8123U);
            mapper->write_prg(0x8123U, static_cast<Byte>(data));
            const auto expected = static_cast<Byte>(old & data & 7U);
            REQUIRE(mapper->read_prg(0x9000U) == prg_value(expected & 3U, 0x1000U));
        }
}

TEST_CASE("Mapper 096 maps and writes every CHR RAM bank", "[mapper096]") {
    auto mapper = board();
    for (Byte outer : {0U, 4U}) {
        select_outer(*mapper, outer);
        for (Byte inner = 0U; inner < 4U; ++inner) {
            select_inner(*mapper, inner);
            const std::size_t bank = outer + inner;
            for (std::size_t offset = 0U; offset < 0x1000U; ++offset)
                mapper->write_chr(static_cast<Address>(offset), chr_value(bank, offset));
        }
        for (std::size_t offset = 0U; offset < 0x1000U; ++offset)
            mapper->write_chr(static_cast<Address>(0x1000U + offset),
                              chr_value(outer + 3U, offset));
    }

    for (Byte outer : {0U, 4U}) {
        select_outer(*mapper, outer);
        for (Byte inner = 0U; inner < 4U; ++inner) {
            select_inner(*mapper, inner);
            for (std::size_t offset = 0U; offset < 0x1000U; ++offset) {
                INFO(outer);
                INFO(inner);
                INFO(offset);
                REQUIRE(mapper->read_chr(static_cast<Address>(offset)) ==
                        chr_value(outer + inner, offset));
                REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) ==
                        chr_value(outer + 3U, offset));
            }
        }
    }
}

TEST_CASE("Mapper 096 latches inner CHR only on entry to PPU region two", "[mapper096]") {
    auto mapper = board();
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    for (Byte previous = 0U; previous < 4U; ++previous)
        for (Byte next = 0U; next < 4U; ++next)
            for (Byte inner = 0U; inner < 4U; ++inner) {
                mapper->power_on();
                mapper->observe_ppu_address(static_cast<Address>(previous << 12U));
                mapper->observe_ppu_address(
                    static_cast<Address>((next << 12U) | (inner << 8U) | 0xA5U));
                const auto bytes = state(*mapper);
                REQUIRE(bytes[3] == (next == 2U && previous != 2U ? inner : 0U));
                REQUIRE(bytes[4] == next);
            }

    mapper->power_on();
    mapper->observe_ppu_address(0x2200U);
    const auto before = state(*mapper);
    for (Address addr : {0x23C0U, 0x23FFU, 0x2FFFU, 0x2100U, 0x2000U}) {
        mapper->read_chr(addr & 0x1FFFU);
        mapper->observe_ppu_address(addr);
        REQUIRE(state(*mapper)[3] == 2U);
    }
    REQUIRE(before[3] == 2U);
}

TEST_CASE("Mapper 096 reset retains latches and RAM while cold power clears them", "[mapper096]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    REQUIRE(initial.size() == 32773U);
    select_outer(*mapper, 6U);
    select_inner(*mapper, 2U);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_chr(0x1123U, 0x5CU);
    const auto active = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == active);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    REQUIRE(mapper->read_chr(0x1123U) == 0x5CU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x8000U) == prg_value(0U, 0U));
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    REQUIRE(mapper->read_chr(0x1123U) == 0U);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 096 snapshots replay RAM and PPU phase without ROM", "[mapper096][state]") {
    auto mapper = board();
    select_outer(*mapper, 6U);
    select_inner(*mapper, 2U);
    for (std::size_t offset = 0U; offset < 0x8000U; ++offset)
        mapper->write_chr(static_cast<Address>(offset & 0x0FFFU), static_cast<Byte>(offset));
    mapper->observe_ppu_address(0x1000U);
    mapper->observe_ppu_address(0x2200U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 32773U);

    auto restored = board(header(true));
    StateReader reader(saved);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0xFFFFU) == mapper->read_prg(0xFFFFU));
    REQUIRE(restored->read_chr(0x0123U) == mapper->read_chr(0x0123U));
    REQUIRE(restored->read_chr(0x1123U) == mapper->read_chr(0x1123U));

    restored->observe_ppu_address(0x2300U);
    REQUIRE(state(*restored)[3] == 2U);
    restored->observe_ppu_address(0x0000U);
    restored->observe_ppu_address(0x2100U);
    REQUIRE(state(*restored)[3] == 1U);
}

TEST_CASE("Mapper 096 rejects malformed snapshots atomically", "[mapper096][state]") {
    auto mapper = board();
    select_outer(*mapper, 6U);
    select_inner(*mapper, 2U);
    mapper->write_chr(0x0123U, 0xA7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (std::size_t size : {0UZ, 1UZ, 2UZ, 4UZ, 5UZ, 100UZ, saved.size() - 1UZ})
        reject(std::span(saved).first(size));
    for (std::size_t offset : {0UZ, 1UZ}) {
        auto bad = saved;
        bad[offset] ^= 0xFFU;
        reject(bad);
    }
    for (unsigned field = 2U; field < 5U; ++field)
        for (unsigned value = 0U; value < 256U; ++value) {
            auto bytes = saved;
            bytes[field] = static_cast<Byte>(value);
            StateReader reader(bytes);
            mapper->load_state(reader);
            const bool valid = value < (field == 2U ? 8U : 4U);
            REQUIRE(reader.ok() == valid);
            if (valid) {
                REQUIRE(state(*mapper) == bytes);
                StateReader restore(saved);
                mapper->load_state(restore);
                REQUIRE(restore.ok());
            } else {
                REQUIRE(state(*mapper) == saved);
            }
        }
}

std::vector<Byte> make_image() {
    std::vector<Byte> image(16U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 8U;
    image[6] = 1U;
    image[7] = 0x68U;
    image[11] = 0x09U;
    const auto prg = make_prg();
    image.insert(image.end(), prg.begin(), prg.end());
    return image;
}

void ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

void ppu_write(Ppu& ppu, Address addr, Byte value) {
    ppu_address(ppu, addr);
    ppu.write_register(7U, value);
}

Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}

TEST_CASE("Mapper 096 PPU register accesses drive CHR latching and vertical CIRAM", "[mapper096]") {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        96U, [](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<OekaKids>(h, std::move(prg), std::move(chr));
        });
    auto cartridge = Cartridge::from_data(make_image());
    REQUIRE(cartridge);
    Ppu ppu;
    ppu.connect_cartridge(&*cartridge);
    ppu.reset();

    for (Byte inner = 0U; inner < 4U; ++inner) {
        ppu_address(ppu, 0x0000U);
        ppu_address(ppu, static_cast<Address>(0x2000U | (inner << 8U)));
        ppu_write(ppu, static_cast<Address>(0x0100U + inner), static_cast<Byte>(0x40U + inner));
    }
    for (Byte inner = 0U; inner < 4U; ++inner) {
        ppu_address(ppu, 0x0000U);
        ppu_address(ppu, static_cast<Address>(0x2000U | (inner << 8U)));
        REQUIRE(ppu_read(ppu, static_cast<Address>(0x0100U + inner)) == 0x40U + inner);
    }

    ppu_address(ppu, 0x0000U);
    ppu_address(ppu, 0x2200U);
    ppu_address(ppu, 0x23C0U);
    ppu_write(ppu, 0x0123U, 0x99U);
    ppu_address(ppu, 0x0000U);
    ppu_address(ppu, 0x2100U);
    REQUIRE(ppu_read(ppu, 0x0123U) == 0U);
    ppu_address(ppu, 0x0000U);
    ppu_address(ppu, 0x2200U);
    REQUIRE(ppu_read(ppu, 0x0123U) == 0x99U);

    ppu_write(ppu, 0x1100U, 0xA7U);
    ppu_address(ppu, 0x0000U);
    ppu_address(ppu, 0x2100U);
    REQUIRE(ppu_read(ppu, 0x1100U) == 0xA7U);

    ppu_write(ppu, 0x2000U, 0x19U);
    ppu_write(ppu, 0x2400U, 0x27U);
    REQUIRE(ppu_read(ppu, 0x2800U) == 0x19U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x27U);
}

} // namespace
