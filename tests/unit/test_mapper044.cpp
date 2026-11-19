#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper044.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x100000U;

INesHeader header(bool legacy = false,
                  bool battery = false,
                  bool ram = true,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 44U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.mirror_mode = mirror;
    if (legacy) {
        h.prg_ram_size = 0x2000U;
    } else if (battery) {
        h.prg_nvram_size = ram ? 0x2000U : 0U;
    } else {
        h.prg_ram_size = ram ? 0x2000U : 0U;
    }
    return h;
}

std::vector<Byte> rom(std::size_t bank_size) {
    std::vector<Byte> data(kRomSize);
    for (std::size_t bank = 0U; bank < kRomSize / bank_size; ++bank) {
        data[bank * bank_size] = static_cast<Byte>(bank);
        data[bank * bank_size + 1U] = static_cast<Byte>(bank >> 8U);
    }
    return data;
}

std::unique_ptr<Mapper044> board(bool legacy = false,
                                 bool battery = false,
                                 bool ram = true,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(legacy, battery, ram, mirror);
    REQUIRE(Mapper044::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper044>(h, rom(0x2000U), rom(0x400U));
}

void select_bank(Mapper& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    mapper.write_prg(0x8001U, value);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 044 applies block pins to switchable and fixed PRG banks", "[mapper044]") {
    constexpr std::array<unsigned, 8> kAnd = {
        0x0FU, 0x0FU, 0x0FU, 0x0FU, 0x0FU, 0x0FU, 0x1FU, 0x1FU};
    constexpr std::array<unsigned, 8> kOr = {
        0x00U, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x60U};
    for (bool legacy : {false, true}) {
        auto mapper = board(legacy);
        for (unsigned block = 0U; block < 8U; ++block) {
            mapper->write_prg(0xA001U, static_cast<Byte>(0x80U | block));
            for (Byte mode : {0U, 0x40U}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    const auto r6 = static_cast<Byte>(value);
                    const auto r7 = static_cast<Byte>(value ^ 0xA5U);
                    select_bank(*mapper, 6U, r6, mode);
                    select_bank(*mapper, 7U, r7, mode);
                    const unsigned fixed2 = block < 6U ? 14U : 30U;
                    const unsigned fixed1 = block < 6U ? 15U : 31U;
                    const std::array<unsigned, 4> inner = {
                        mode == 0U ? r6 & 0x3FU : fixed2,
                        r7 & 0x3FU,
                        mode == 0U ? fixed2 : r6 & 0x3FU,
                        fixed1,
                    };
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto expected = (inner[slot] & kAnd[block]) | kOr[block];
                        const auto addr = static_cast<Address>(0x8000U + slot * 0x2000U);
                        REQUIRE(mapper->read_prg(addr) == expected);
                        REQUIRE(mapper->read_prg(addr + 1U) == 0U);
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 044 applies block pins after CHR pairing and inversion", "[mapper044]") {
    constexpr std::array<unsigned, 8> kAnd = {
        0x7FU, 0x7FU, 0x7FU, 0x7FU, 0x7FU, 0x7FU, 0xFFU, 0xFFU};
    constexpr std::array<unsigned, 8> kOr = {
        0x000U, 0x080U, 0x100U, 0x180U, 0x200U, 0x280U, 0x300U, 0x300U};
    auto mapper = board();
    for (unsigned block = 0U; block < 8U; ++block) {
        mapper->write_prg(0xA001U, static_cast<Byte>(0x80U | block));
        for (Byte mode : {0U, 0x80U}) {
            for (Byte reg = 0U; reg < 6U; ++reg) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    select_bank(*mapper, reg, static_cast<Byte>(value), mode);
                    const unsigned first = reg < 2U ? reg * 2U + (mode == 0U ? 0U : 4U)
                                                    : reg - 2U + (mode == 0U ? 4U : 0U);
                    for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                        const unsigned inner = reg < 2U ? (value & 0xFEU) | half : value;
                        const unsigned expected = (inner & kAnd[block]) | kOr[block];
                        const auto addr = static_cast<Address>((first + half) * 0x400U);
                        mapper->write_chr(addr, 0xA7U);
                        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(expected));
                        REQUIRE(mapper->read_chr(addr + 1U) == static_cast<Byte>(expected >> 8U));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 044 decodes every MMC3 register alias", "[mapper044]") {
    auto actual = board(false, false, false);
    auto expected = board(false, false, false);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto canonical = static_cast<Address>(0x8000U | (addr & 0xE001U));
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        actual->power_on();
        expected->power_on();
        actual->write_prg(static_cast<Address>(addr), value);
        expected->write_prg(canonical, value);
        INFO("addr=" << addr);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 044 implements MMC3 WRAM gates and bounded battery persistence", "[mapper044]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(legacy, true);
        REQUIRE(mapper->battery_ram().size() == 0x2000U);
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr ^ (addr >> 8U)));
        }

        mapper->write_prg(0xA001U, 0xC6U);
        mapper->write_prg(0x6000U, 0xA7U);
        REQUIRE(mapper->read_prg(0x6000U) == 0x60U);
        mapper->write_prg(0xA001U, 0x46U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->read_prg(0x6000U) == 0U);
        mapper->write_prg(0x6000U, 0x39U);
        mapper->write_prg(0xA001U, 0x86U);
        REQUIRE(mapper->read_prg(0x6000U) == 0x60U);

        std::vector<Byte> initial(0x2000U, 0x5AU);
        mapper->set_battery_ram(initial);
        REQUIRE(
            std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0x5AU; }));
        const auto before = state(*mapper);
        mapper->set_battery_ram(std::vector<Byte>(0x2001U, 0xA7U));
        REQUIRE(state(*mapper) == before);
        mapper->set_battery_ram(std::vector<Byte>(0x1FFFU, 0xA7U));
        REQUIRE(state(*mapper) == before);

        mapper->reset();
        REQUIRE(
            std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0x5AU; }));
        mapper->power_on();
        REQUIRE(
            std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0x5AU; }));
    }

    auto volatile_mapper = board();
    REQUIRE(volatile_mapper->battery_ram().empty());
    volatile_mapper->write_prg(0x6123U, 0xA7U);
    volatile_mapper->reset();
    REQUIRE(volatile_mapper->read_prg(0x6123U) == 0xA7U);
    volatile_mapper->power_on();
    REQUIRE(volatile_mapper->read_prg(0x6123U) == 0U);

    auto no_ram = board(false, false, false);
    REQUIRE(state(*no_ram).size() == 25U);
    REQUIRE(no_ram->battery_ram().empty());
    for (Address addr : {0x6000U, 0x7000U, 0x7FFFU}) {
        REQUIRE_FALSE(no_ram->maps_prg(addr));
        no_ram->write_prg(addr, 0xA7U);
        REQUIRE(no_ram->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 044 leaves disabled and absent WRAM on the CPU open bus", "[mapper044]") {
    auto no_ram = board(false, false, false);
    const auto initial = state(*no_ram);
    for (unsigned addr = 0x4020U; addr < 0x8000U; ++addr) {
        const auto address = static_cast<Address>(addr);
        REQUIRE_FALSE(no_ram->maps_prg(address));
        REQUIRE_FALSE(no_ram->maps_expansion(address));
        REQUIRE(no_ram->prg_read_mask(address) == 0xFFU);
        no_ram->write_prg(address, 0xA7U);
        no_ram->write_expansion(address, 0xA7U);
    }
    REQUIRE(state(*no_ram) == initial);

    auto mapper = board();
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    for (Byte gate : {0x00U, 0x40U}) {
        mapper->write_prg(0xA001U, gate);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    }
}

TEST_CASE("Mapper 044 implements MMC3 IRQ reload enable acknowledge and zero repetition",
          "[mapper044]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xA001U, 0x86U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned clock = 0U; clock < 8U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 044 reset restores block zero and header CIRAM while retaining WRAM",
          "[mapper044]") {
    for (bool vertical : {false, true}) {
        auto mapper =
            board(false, true, true, vertical ? MirrorMode::Vertical : MirrorMode::Horizontal);
        const auto initial = state(*mapper);
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
        REQUIRE(mapper->read_prg(0xC000U) == 14U);
        REQUIRE(mapper->read_prg(0xE000U) == 15U);
        mapper->write_prg(0x6000U, 0xA7U);
        select_bank(*mapper, 6U, 0x23U, 0xC0U);
        mapper->write_prg(0xA000U, static_cast<Byte>(vertical ? 0U : 1U));
        mapper->write_prg(0xA001U, 0xC7U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->reset();
        REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
        const auto after_reset = state(*mapper);
        REQUIRE(std::equal(initial.begin() + 8202, initial.end(), after_reset.begin() + 8202));
        mapper->write_prg(0x6000U, 0x39U);
        mapper->write_prg(0xA001U, 0x85U);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6000U) == 0x39U);
        const auto after_power = state(*mapper);
        REQUIRE(std::equal(initial.begin() + 8202, initial.end(), after_power.begin() + 8202));
    }
}

TEST_CASE("Mapper 044 snapshots replay all mutable state and reject corruption atomically",
          "[mapper044][state]") {
    auto mapper = board(false, true);
    for (Byte reg = 0U; reg < 8U; ++reg)
        select_bank(*mapper, reg, static_cast<Byte>(reg * 29U + 7U));
    select_bank(*mapper, 6U, 0x23U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0xA001U, 0xC6U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 8217U);

    mapper->power_on();
    mapper->write_prg(0x6000U, 0U);
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0xC000U) == 0x63U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated{std::span(saved).first(length)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }

    constexpr std::array<std::size_t, 9> kOffsets = {
        0U, 2U, 8202U, 8203U, 8210U, 8211U, 8212U, 8213U, 8216U};
    for (const auto offset : kOffsets) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto bad = saved;
    bad[8216U] = 5U;
    StateReader inconsistent_irq(bad);
    mapper->load_state(inconsistent_irq);
    REQUIRE_FALSE(inconsistent_irq.ok());
    REQUIRE(state(*mapper) == saved);

    for (auto kind = 0U; kind < 3U; ++kind) {
        auto other = kind == 0U   ? board(false, true, true, MirrorMode::Horizontal)
                     : kind == 1U ? board(false, false)
                                  : board(false, false, false);
        const auto before = state(*other);
        StateReader wrong_population(saved);
        other->load_state(wrong_population);
        REQUIRE_FALSE(wrong_population.ok());
        REQUIRE(state(*other) == before);
    }
}

TEST_CASE("Mapper 044 validates the documented ROM and memory population", "[mapper044]") {
    for (const std::size_t size : {0UZ, 0x4000UZ, 0x20000UZ, 0xFFFFFUZ, 0x100001UZ, 0x200000UZ}) {
        REQUIRE_FALSE(Mapper044::validate_geometry(header(), size, kRomSize));
        REQUIRE_FALSE(Mapper044::validate_geometry(header(), kRomSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper044::validate_geometry(h, kRomSize, kRomSize));
    }
    auto legacy = header(true);
    legacy.submapper = 15U;
    REQUIRE(Mapper044::validate_geometry(legacy, kRomSize, kRomSize));

    for (MirrorMode mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            Mapper044::validate_geometry(header(false, false, true, mirror), kRomSize, kRomSize));
    }
    REQUIRE(Mapper044::validate_geometry(header(false, false, false), kRomSize, kRomSize));
    REQUIRE(Mapper044::validate_geometry(header(false, false, true), kRomSize, kRomSize));
    REQUIRE(Mapper044::validate_geometry(header(false, true, true), kRomSize, kRomSize));
    REQUIRE(Mapper044::validate_geometry(header(true, false), kRomSize, kRomSize));
    REQUIRE(Mapper044::validate_geometry(header(true, true), kRomSize, kRomSize));

    for (unsigned kind = 0U; kind < 7U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_ram_size = 0x4000U;
            break;
        case 4U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 5U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        INFO("kind=" << kind);
        REQUIRE_FALSE(Mapper044::validate_geometry(h, kRomSize, kRomSize));
    }
    auto battery_without_ram = header(false, true, false);
    REQUIRE_FALSE(Mapper044::validate_geometry(battery_without_ram, kRomSize, kRomSize));
}

} // namespace
