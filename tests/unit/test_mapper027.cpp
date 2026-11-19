#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper027.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgBytes = 128 * 1024;
constexpr std::size_t kChrBytes = 512 * 1024;

INesHeader header(std::size_t ram = 8192U,
                  bool battery = false,
                  bool chr_ram = false,
                  bool legacy = false) {
    INesHeader result{};
    result.mapper_number = 27U;
    result.is_nes2 = !legacy;
    result.has_battery = battery;
    result.mirror_mode = MirrorMode::Vertical;
    if (!legacy) {
        result.prg_ram_size = battery ? 0U : ram;
        result.prg_nvram_size = battery ? ram : 0U;
        result.chr_ram_size = chr_ram ? 8192U : 0U;
    }
    return result;
}

Byte cell(std::size_t bank, std::size_t offset = 0U) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

std::vector<Byte> patterned_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0; offset < size; ++offset)
        rom[offset] = cell(offset / bank_size, offset % bank_size);
    return rom;
}

std::unique_ptr<Mapper027> board(const INesHeader& h = header(),
                                 std::size_t prg_size = kPrgBytes,
                                 std::size_t chr_size = kChrBytes) {
    REQUIRE(Mapper027::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper027>(
        h, patterned_rom(prg_size, 8192U), patterned_rom(chr_size, 1024U));
}

std::unique_ptr<Mapper027> chr_ram_board(const INesHeader& h = header(8192U, false, true)) {
    return board(h, kPrgBytes, 0U);
}

std::vector<Byte> state(const Mapper027& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address chr_register(unsigned slot, bool high = false) {
    return static_cast<Address>(0xB000U + slot / 2U * 0x1000U + (slot & 1U) * 2U +
                                (high ? 1U : 0U));
}

void set_chr_bank(Mapper027& mapper, unsigned slot, unsigned bank) {
    mapper.write_prg(chr_register(slot), static_cast<Byte>(bank));
    mapper.write_prg(chr_register(slot, true), static_cast<Byte>(bank >> 4U));
}

TEST_CASE("Mapper 027 PRG banking implements VRC4f masks and swap modes", "[mapper027]") {
    auto mapper = board();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xA000U) == cell(1U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(14U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(15U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(15U, 8191U));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());

    mapper->write_prg(0x8000U, 5U);
    mapper->write_prg(0xA000U, 7U);
    for (Byte control : {0U, 2U}) {
        mapper->write_prg(0x9002U, control);
        const std::array<unsigned, 4> pages{
            control == 0U ? 5U : 14U, 7U, control == 0U ? 14U : 5U, 15U};
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const unsigned slot = (addr - 0x8000U) / 8192U;
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    cell(pages[slot], addr & 0x1FFFU));
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9002U, 0U);
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0x8000U) == cell((value & 31U) % 16U));
        REQUIRE(mapper->read_prg(0xA000U) == cell((value & 31U) % 16U));
    }
}

TEST_CASE("Mapper 027 address decoding is exactly the documented F00F mask", "[mapper027]") {
    auto actual = board();
    auto expected = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        INFO("Address " << addr);
        const auto value = static_cast<Byte>(addr * 29U + (addr >> 8U));
        actual->write_prg(static_cast<Address>(addr), value);
        expected->write_prg(static_cast<Address>(addr & 0xF00FU), value);
        REQUIRE(state(*actual) == state(*expected));
    }

    actual->reset();
    actual->write_prg(0x9080U, 3U);
    REQUIRE(actual->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(actual->read_prg(0x8000U) == cell(0U));
    REQUIRE(actual->read_prg(0xC000U) == cell(14U));
}

TEST_CASE("Mapper 027 provides all four CIRAM modes with two data bits", "[mapper027]") {
    auto mapper = board();
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    }
}

TEST_CASE("Mapper 027 CHR registers retain nine bits across all eight slots", "[mapper027]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const unsigned bank = 0x120U + slot;
        set_chr_bank(*mapper, slot, bank);
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == cell(bank));
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 1023U)) == cell(bank, 1023U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == cell(0x120U + slot));
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(chr_register(0U), static_cast<Byte>(value));
        mapper->write_prg(chr_register(0U, true), static_cast<Byte>(value));
        const unsigned bank = (value & 15U) | ((value & 31U) << 4U);
        REQUIRE(mapper->read_chr(0U) == cell(bank));
    }
    const auto before = state(*mapper);
    mapper->write_chr(0U, 0xA7U);
    mapper->write_chr(0x1FFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 027 CHR RAM is banked writable and volatile", "[mapper027]") {
    auto mapper = chr_ram_board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_bank(*mapper, slot, slot);
        mapper->write_chr(static_cast<Address>(slot * 1024U + 17U),
                          static_cast<Byte>(0x40U + slot));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 17U)) == 0x40U + slot);
    set_chr_bank(*mapper, 0U, 8U);
    REQUIRE(mapper->read_chr(17U) == 0x40U);
    mapper->write_chr(17U, 0xA6U);
    set_chr_bank(*mapper, 0U, 0U);
    REQUIRE(mapper->read_chr(17U) == 0xA6U);
    mapper->write_prg(0x8000U, 5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF002U, 6U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_chr(17U) == 0xA6U);
    mapper->power_on();
    REQUIRE(mapper->read_chr(17U) == 0U);
}

TEST_CASE("Mapper 027 WRAM enable decode persistence and reset follow VRC4", "[mapper027]") {
    for (const auto& config : std::array{header(0U),
                                         header(2048U),
                                         header(8192U),
                                         header(2048U, true),
                                         header(8192U, true),
                                         header(8192U, false, false, true),
                                         header(8192U, true, false, true)}) {
        auto mapper = board(config);
        const std::size_t ram_size =
            config.is_nes2 ? config.prg_ram_size + config.prg_nvram_size : 8192U;
        REQUIRE(mapper->battery_ram().size() == (config.has_battery ? ram_size : 0U));
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        mapper->write_prg(0x6000U, 0xA7U);
        REQUIRE(mapper->read_prg(0x6000U) == 0U);
        if (ram_size == 0U) {
            mapper->write_prg(0x9002U, 1U);
            REQUIRE_FALSE(mapper->maps_prg(0x6000U));
            continue;
        }

        mapper->write_prg(0x9002U, 1U);
        REQUIRE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->maps_prg(0x7FFFU) == (ram_size == 8192U));
        for (unsigned addr = 0x6000U; addr < (ram_size == 8192U ? 0x8000U : 0x7000U); ++addr) {
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == static_cast<Byte>(addr));
        }
        if (ram_size == 2048U)
            REQUIRE(mapper->read_prg(0x6800U) == mapper->read_prg(0x6000U));
        mapper->write_prg(0x6000U, 0x5AU);
        mapper->reset();
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        mapper->write_prg(0x9002U, 1U);
        REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);

        const std::vector<Byte> saved(mapper->battery_ram().begin(), mapper->battery_ram().end());
        auto copy = board(config);
        copy->set_battery_ram(saved);
        copy->write_prg(0x9002U, 1U);
        REQUIRE(copy->read_prg(0x6000U) == (config.has_battery ? 0x5AU : 0U));
        mapper->power_on();
        mapper->write_prg(0x9002U, 1U);
        REQUIRE(mapper->read_prg(0x6000U) == (config.has_battery ? 0x5AU : 0U));
        if (config.has_battery) {
            const std::array<Byte, 2> partial{0xC3U, 0x39U};
            mapper->set_battery_ram(partial);
            REQUIRE(mapper->read_prg(0x6000U) == 0xC3U);
            REQUIRE(mapper->read_prg(0x6001U) == 0x39U);
            REQUIRE(mapper->read_prg(0x6002U) == saved[2U]);
            mapper->set_battery_ram(std::vector<Byte>(16384U, 0xA7U));
            REQUIRE(mapper->battery_ram().size() == ram_size);
            REQUIRE(mapper->read_prg(0x6000U + ram_size - 1U) == 0xA7U);
        }
    }
}

TEST_CASE("Mapper 027 VRC IRQ covers every latch and both clock modes", "[mapper027]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch));
        mapper->write_prg(0xF001U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF002U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->write_prg(0xF000U, 14U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 0U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 3U);
    for (unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
    }
}

TEST_CASE("Mapper 027 geometry validation rejects unsupported board populations", "[mapper027]") {
    for (std::size_t size : {0U, 8192U, 16384U, 24576U, 32767U, 32769U, 262145U, 524288U})
        REQUIRE_FALSE(Mapper027::validate_geometry(header(), size, kChrBytes));
    for (std::size_t size : {1U, 1023U, 1025U, 524289U, 1048576U})
        REQUIRE_FALSE(Mapper027::validate_geometry(header(), kPrgBytes, size));

    for (unsigned kind = 0U; kind < 11U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.prg_ram_size = 4096U;
            break;
        case 2U:
            h.prg_nvram_size = 2048U;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            h = header(8192U, false, true);
            h.chr_ram_size = 0U;
            break;
        case 9U:
            h = header(8192U, false, true);
            h.chr_ram_size = 4096U;
            break;
        default:
            h = header(0U);
            h.has_battery = true;
            break;
        }
        const std::size_t chr_size = kind == 8U || kind == 9U ? 0U : kChrBytes;
        REQUIRE_FALSE(Mapper027::validate_geometry(h, kPrgBytes, chr_size));
    }
    for (std::size_t prg_size : {32768U, 40960U, 262144U})
        for (std::size_t chr_size : {1024U, 8192U, 524288U})
            REQUIRE(Mapper027::validate_geometry(header(), prg_size, chr_size));
    REQUIRE(Mapper027::validate_geometry(header(0U, false, true), 32768U, 0U));
    REQUIRE(Mapper027::validate_geometry(header(2048U, true, true), 32768U, 0U));
}

TEST_CASE("Mapper 027 snapshots replay atomically without ROM payloads", "[mapper027]") {
    auto mapper = chr_ram_board();
    mapper->write_prg(0x9002U, 1U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 11U);
    set_chr_bank(*mapper, 0U, 3U);
    mapper->write_chr(0xC17U, 0x5AU);
    mapper->write_prg(0x9000U, 2U);
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle)
        mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 16431U);

    auto copy = chr_ram_board();
    StateReader copy_reader(saved);
    copy->load_state(copy_reader);
    REQUIRE(copy_reader.ok());
    REQUIRE(copy_reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6000U) == 0xA7U);
    REQUIRE(copy->read_chr(0xC17U) == 0x5AU);

    for (unsigned cycle = 0U; cycle < 14U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x6000U, 0U);
    mapper->write_chr(0xC17U, 0U);
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    REQUIRE(mapper->read_chr(0xC17U) == 0x5AU);
    for (unsigned cycle = 0U; cycle < 13U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 027 rejects truncated corrupt and mismatched states atomically", "[mapper027]") {
    auto mapper = chr_ram_board();
    mapper->write_prg(0x9002U, 1U);
    mapper->write_prg(0x6000U, 0xA7U);
    set_chr_bank(*mapper, 0U, 0x123U);
    mapper->write_chr(0x123U, 0x5AU);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF002U, 6U);
    const auto saved = state(*mapper);

    for (std::size_t length : {0UZ, 1UZ, 4UZ, 12UZ, 31UZ, 39UZ, 47UZ, 1000UZ, saved.size() - 1U}) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 9>{{
             {0U, 0U},
             {4U, 1U},
             {8U, 1U},
             {12U, 0U},
             {13U, 32U},
             {16U, 0xFFU},
             {31U, 2U},
             {32U, 4U},
             {37U, 8U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (bool overflow : {false, true}) {
        auto bad = saved;
        bad[35U] = overflow ? 0xFFU : 0U;
        bad[36U] = overflow ? 0xFFU : 0U;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte pending : {1U, 2U}) {
        auto bad = saved;
        bad[37U] = 5U;
        bad[38U] = pending;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto bad_length = saved;
    std::fill_n(bad_length.begin() + 39, 8, 0xFFU);
    StateReader length_reader(bad_length);
    mapper->load_state(length_reader);
    REQUIRE_FALSE(length_reader.ok());
    REQUIRE(state(*mapper) == saved);

    auto other = board(header(0U), 32768U, 1024U);
    const auto before = state(*other);
    StateReader wrong_board(saved);
    other->load_state(wrong_board);
    REQUIRE_FALSE(wrong_board.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
