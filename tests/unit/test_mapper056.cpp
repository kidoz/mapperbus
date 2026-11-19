#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper056_ks202.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool legacy = false, MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader header{};
    header.mapper_number = 56U;
    header.is_nes2 = !legacy;
    header.mirror_mode = mirror;
    header.prg_ram_size = 8192U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> rom(262144U);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>(i / 8192U);
    return rom;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> rom(131072U);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>(i / 1024U);
    return rom;
}

std::unique_ptr<Mapper056Ks202> make_board(bool legacy = false,
                                           MirrorMode mirror = MirrorMode::Horizontal) {
    return std::make_unique<Mapper056Ks202>(make_header(legacy, mirror), make_prg(), make_chr());
}

std::vector<Byte> save(const Mapper056Ks202& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper056Ks202& mapper, Byte selector) {
    mapper.write_prg(static_cast<Address>(0xE000U + selector * 257U), selector);
}

TEST_CASE("Mapper 056 maps all PRG bank bits and the semi-fixed window", "[mapper056]") {
    auto mapper = make_board();
    REQUIRE(mapper->read_prg(0x8000U) == 16U);
    REQUIRE(mapper->read_prg(0xA000U) == 16U);
    REQUIRE(mapper->read_prg(0xC000U) == 16U);
    REQUIRE(mapper->read_prg(0xE000U) == 31U);
    REQUIRE(mapper->read_prg(0xFFFFU) == 31U);

    for (Byte selector = 1U; selector <= 3U; ++selector) {
        select_bank(*mapper, selector);
        const auto slot = static_cast<std::size_t>(selector - 1U);
        const auto cpu = static_cast<Address>(0x8000U + slot * 0x2000U);
        for (Byte upper = 0U; upper <= 1U; ++upper) {
            for (Byte bank = 0U; bank <= 15U; ++bank) {
                mapper->write_prg(static_cast<Address>(0xF000U + slot),
                                  static_cast<Byte>((upper << 4U) | bank));
                INFO("selector=" << selector << " upper=" << upper << " bank=" << bank);
                REQUIRE(mapper->read_prg(cpu) == bank + upper * 16U);
                REQUIRE(mapper->read_prg(static_cast<Address>(cpu + 0x1FFFU)) ==
                        bank + upper * 16U);
            }
        }
    }

    select_bank(*mapper, 1U);
    mapper->write_prg(0xF003U, 0x0AU);
    REQUIRE(mapper->read_prg(0xE123U) == 15U);
    REQUIRE(mapper->read_prg(0x8123U) == 26U);
    mapper->write_prg(0xF003U, 0x1BU);
    REQUIRE(mapper->read_prg(0xE123U) == 31U);
    REQUIRE(mapper->read_prg(0x8123U) == 27U);
}

TEST_CASE("Mapper 056 decodes every superimposed register alias", "[mapper056]") {
    auto mapper = make_board();

    Byte fixed_bank = 31U;
    for (unsigned addr = 0xF000U; addr < 0xF400U; ++addr) {
        const auto slot = addr & 3U;
        select_bank(*mapper, static_cast<Byte>(slot + 1U));
        const auto value = static_cast<Byte>(0xA0U | ((addr & 1U) << 4U) | slot);
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto low_bank = slot < 3U ? slot : 15U;
        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                low_bank + ((addr & 1U) != 0U ? 16U : 0U));
        if (slot == 3U)
            fixed_bank = (addr & 1U) != 0U ? 31U : 15U;
        REQUIRE(mapper->read_prg(0xE000U) == fixed_bank);
    }

    select_bank(*mapper, 3U);
    for (unsigned addr = 0xF800U; addr < 0xFC00U; ++addr) {
        const auto value = static_cast<Byte>(0xA0U | (addr & 1U));
        mapper->write_prg(static_cast<Address>(addr), value);
        REQUIRE(mapper->mirror_mode() ==
                ((addr & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(mapper->read_prg(0xC000U) == (addr & 1U));
    }

    select_bank(*mapper, 2U);
    for (unsigned addr = 0xFC00U; addr <= 0xFFFFU; ++addr) {
        const auto slot = addr & 7U;
        const auto value = static_cast<Byte>(0x80U | (addr & 0x7FU));
        mapper->write_prg(static_cast<Address>(addr), value);
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x400U)) == (addr & 0x7FU));
        REQUIRE(mapper->read_prg(0xA000U) == 16U + (addr & 15U));
    }

    for (Byte selector = 0U; selector <= 7U; ++selector) {
        select_bank(*mapper, selector);
        mapper->write_prg(0xF400U, 0x0DU);
        if (selector >= 1U && selector <= 3U) {
            const auto cpu = static_cast<Address>(0x8000U + (selector - 1U) * 0x2000U);
            const auto expected = static_cast<Byte>((mapper->read_prg(cpu) & 0xF0U) | 13U);
            REQUIRE(mapper->read_prg(cpu) == expected);
        }
    }
}

TEST_CASE("Mapper 056 non-PRG bank selectors never alter the low bank latch", "[mapper056]") {
    auto mapper = make_board();
    select_bank(*mapper, 1U);
    mapper->write_prg(0xF000U, 0x15U);
    for (Byte selector : {0U, 4U, 5U, 6U, 7U}) {
        select_bank(*mapper, selector);
        for (Address addr : {0xF400U, 0xF800U, 0xFC00U, 0xFFFFU}) {
            mapper->write_prg(addr, 0x0FU);
            REQUIRE(mapper->read_prg(0x8000U) == 21U);
            REQUIRE(mapper->read_prg(0xA000U) == 16U);
            REQUIRE(mapper->read_prg(0xC000U) == 16U);
        }
        mapper->write_prg(0xF000U, 0x0FU);
        REQUIRE(mapper->read_prg(0x8000U) == 5U);
        mapper->write_prg(0xF000U, 0x1FU);
        REQUIRE(mapper->read_prg(0x8000U) == 21U);
    }
}

TEST_CASE("Mapper 056 maps every CHR bank in every slot and protects ROM", "[mapper056]") {
    auto mapper = make_board();
    for (Byte slot = 0U; slot < 8U; ++slot) {
        for (Byte bank = 0U; bank < 128U; ++bank) {
            mapper->write_prg(static_cast<Address>(0xFC00U + slot), bank);
            const auto ppu = static_cast<Address>(slot * 0x400U);
            INFO("slot=" << slot << " bank=" << bank);
            REQUIRE(mapper->read_chr(ppu) == bank);
            REQUIRE(mapper->read_chr(static_cast<Address>(ppu + 0x3FFU)) == bank);
            mapper->write_chr(ppu, static_cast<Byte>(bank ^ 0xFFU));
            REQUIRE(mapper->read_chr(ppu) == bank);
        }
    }
    mapper->write_prg(0xFC03U, 0xFFU);
    REQUIRE(mapper->read_chr(0x0C00U) == 127U);
}

TEST_CASE("Mapper 056 WRAM is volatile, reset-retained and not battery backed", "[mapper056]") {
    for (bool legacy : {false, true}) {
        auto mapper = make_board(legacy, MirrorMode::Vertical);
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        }
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr ^ (addr >> 8U)));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);

        mapper->write_prg(0xF001U, 0U);
        mapper->write_prg(0xFC00U, 123U);
        mapper->write_prg(0xF800U, 0U);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x6000U) == 0x60U);
        REQUIRE(mapper->read_prg(0x7FFFU) == 0x80U);
        REQUIRE(mapper->read_prg(0x8000U) == 16U);
        REQUIRE(mapper->read_prg(0xA000U) == 16U);
        REQUIRE(mapper->read_prg(0xC000U) == 16U);
        REQUIRE(mapper->read_prg(0xE000U) == 31U);
        REQUIRE(mapper->read_chr(0U) == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

        mapper->power_on();
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == 0U);
        for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            mapper->write_prg(addr, 0xFFU);
        }
    }
}

TEST_CASE("Mapper 056 KS202 IRQ counts CPU cycles in 16-bit and 8-bit modes", "[mapper056]") {
    auto mapper = make_board();
    mapper->write_prg(0x8FFFU, 0x0DU);
    mapper->write_prg(0x9FFFU, 0x0FU);
    mapper->write_prg(0xAFFFU, 0x0FU);
    mapper->write_prg(0xBFFFU, 0x0FU);
    mapper->write_prg(0xC123U, 0x02U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xDFFFU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 3U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 20U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0x06U);
    for (unsigned cycle = 0U; cycle < 3U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xD000U, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0x07U);
    mapper->write_prg(0xD000U, 0U);
    for (unsigned cycle = 0U; cycle < 3U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xD000U, 0U);
    for (unsigned cycle = 0U; cycle < 3U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 056 state replay is deterministic and rejection is atomic",
          "[mapper056][state]") {
    auto mapper = make_board();
    select_bank(*mapper, 1U);
    mapper->write_prg(0xF001U, 0x15U);
    select_bank(*mapper, 2U);
    mapper->write_prg(0xF001U, 0x07U);
    select_bank(*mapper, 3U);
    mapper->write_prg(0xF002U, 0x19U);
    for (Byte slot = 0U; slot < 8U; ++slot)
        mapper->write_prg(static_cast<Address>(0xFC00U + slot), static_cast<Byte>(20U + slot));
    mapper->write_prg(0xF800U, 1U);
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 3U));
    mapper->write_prg(0x8000U, 0xF0U);
    mapper->write_prg(0x9000U, 0xFFU);
    mapper->write_prg(0xA000U, 0xFFU);
    mapper->write_prg(0xB000U, 0x0FU);
    mapper->write_prg(0xC000U, 0x02U);
    mapper->clock_cpu();

    const auto saved = save(*mapper);
    REQUIRE(saved.size() == 8217U);
    auto copy = make_board(true, MirrorMode::Vertical);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(save(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == 21U);
    REQUIRE(copy->read_prg(0xA000U) == 7U);
    REQUIRE(copy->read_prg(0xC000U) == 17U);
    REQUIRE(copy->read_prg(0xE000U) == 31U);
    REQUIRE(copy->read_prg(0x6123U) == mapper->read_prg(0x6123U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (Byte slot = 0U; slot < 8U; ++slot)
        REQUIRE(copy->read_chr(static_cast<Address>(slot * 0x400U)) == 20U + slot);

    for (unsigned cycle = 0U; cycle < 40U; ++cycle) {
        mapper->clock_cpu();
        copy->clock_cpu();
    }
    REQUIRE(save(*copy) == save(*mapper));
    REQUIRE(copy->irq_pending() == mapper->irq_pending());

    const auto before = save(*mapper);
    for (std::size_t size : {0UZ, 1UZ, 24UZ, 25UZ, 4096UZ, 8216UZ}) {
        StateReader truncated{std::span(saved).first(size)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(save(*mapper) == before);
    }

    constexpr std::array<std::pair<std::size_t, Byte>, 11> invalid{{
        {0U, 0x39U},
        {2U, 16U},
        {5U, 2U},
        {9U, 128U},
        {17U, 8U},
        {18U, 2U},
        {23U, 8U},
        {24U, 2U},
        {3U, 0xFFU},
        {8U, 0xFFU},
        {16U, 0xFFU},
    }};
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[25U + 0x123U] ^= 0xFFU;
        bad[offset] = value;
        StateReader rejected(bad);
        mapper->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(save(*mapper) == before);
    }
}

TEST_CASE("Mapper 056 validates its recorded ROM and RAM population", "[mapper056]") {
    for (bool legacy : {false, true})
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical})
            REQUIRE(
                Mapper056Ks202::validate_geometry(make_header(legacy, mirror), 262144U, 131072U));

    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto header = make_header();
        std::size_t prg_size = 262144U;
        std::size_t chr_size = 131072U;
        switch (kind) {
        case 0U:
            header.submapper = 1U;
            break;
        case 1U:
            header.has_battery = true;
            break;
        case 2U:
            header.prg_ram_size = 0U;
            break;
        case 3U:
            header.prg_ram_size = 16384U;
            break;
        case 4U:
            header.prg_nvram_size = 8192U;
            break;
        case 5U:
            header.chr_ram_size = 8192U;
            break;
        case 6U:
            header.chr_nvram_size = 8192U;
            break;
        case 7U:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            header.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            prg_size = 0U;
            break;
        case 10U:
            prg_size = 131072U;
            break;
        case 11U:
            prg_size = 524288U;
            break;
        case 12U:
            --prg_size;
            break;
        case 13U:
            chr_size = 0U;
            break;
        case 14U:
            chr_size = 65536U;
            break;
        case 15U:
            chr_size = 262144U;
            break;
        case 16U:
            --chr_size;
            break;
        case 17U:
            header = make_header(true);
            header.has_battery = true;
            break;
        default:
            break;
        }
        INFO("invalid geometry kind=" << kind);
        REQUIRE_FALSE(Mapper056Ks202::validate_geometry(header, prg_size, chr_size));
    }
}

} // namespace
