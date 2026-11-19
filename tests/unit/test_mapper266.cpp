#include <array>
#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <vector>

#include "core/mappers/city_fighter266.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 266U;
    h.is_nes2 = true;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> prg_image(std::size_t size = 131072U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<Byte>((i / 32768U) * 61U + (i & 0x7FFFU));
    }
    return data;
}

std::vector<Byte> chr_image(std::size_t size = 524288U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<Byte>((i / 1024U) * 37U + (i & 0x03FFU));
    }
    return data;
}

std::unique_ptr<CityFighter266> board(std::size_t prg_size = 131072U,
                                      std::size_t chr_size = 524288U,
                                      bool vertical = true) {
    return std::make_unique<CityFighter266>(
        header(vertical), prg_image(prg_size), chr_image(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

template <typename T> T field(const std::vector<Byte>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <typename T> void set_field(std::vector<Byte>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::array<uint16_t, 8> chr_banks(const Mapper& mapper) {
    return field<std::array<uint16_t, 8>>(state(mapper), 3U);
}

Byte expected_prg(std::size_t size, unsigned bank, Address addr) {
    const auto page = bank % (size / 32768U);
    return static_cast<Byte>(page * 61U + (addr & 0x7FFFU));
}

Byte expected_chr(std::size_t size, unsigned bank, Address addr) {
    const auto physical = bank % (size / 1024U);
    return static_cast<Byte>(physical * 37U + (addr & 0x03FFU));
}

void select_chr(Mapper& mapper, unsigned slot, uint16_t bank) {
    constexpr std::array<Address, 8> addresses = {
        0xD000U, 0xD008U, 0xA000U, 0xA008U, 0xB000U, 0xB008U, 0xE000U, 0xE008U};
    mapper.write_prg(addresses[slot], static_cast<Byte>(bank));
    mapper.write_prg(static_cast<Address>(addresses[slot] + 4U), static_cast<Byte>(bank >> 4U));
}

void require_unchanged_on_load(CityFighter266& mapper,
                               const std::vector<Byte>& before,
                               const std::vector<Byte>& input) {
    StateReader reader(input);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
}

TEST_CASE("Mapper 266 accepts pin-bounded ROM-only NES 2.0 geometry", "[mapper266]") {
    for (std::size_t prg_size = 32768U; prg_size <= 131072U; prg_size *= 2U) {
        for (std::size_t chr_size = 8192U; chr_size <= 524288U; chr_size *= 2U) {
            for (bool vertical : {false, true}) {
                REQUIRE(CityFighter266::validate_geometry(header(vertical), prg_size, chr_size));
                auto mapper = board(prg_size, chr_size, vertical);
                REQUIRE(mapper->read_prg(0x8000U) == expected_prg(prg_size, 0U, 0x8000U));
                REQUIRE(mapper->read_chr(0U) == expected_chr(chr_size, 0U, 0U));
                REQUIRE(mapper->mirror_mode() ==
                        (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                REQUIRE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE(state(*mapper).size() == 28U);
            }
        }
    }
}

TEST_CASE("Mapper 266 rejects unsupported formats sizes memory and CIRAM metadata", "[mapper266]") {
    const auto reject = [](const INesHeader& h, std::size_t prg_size, std::size_t chr_size) {
        REQUIRE_FALSE(CityFighter266::validate_geometry(h, prg_size, chr_size));
    };
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, 131072U, 524288U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, 131072U, 524288U);
    }
    for (std::size_t size : {0U, 16384U, 32767U, 32769U, 98304U, 262144U, 524288U}) {
        reject(header(), size, 524288U);
    }
    for (std::size_t size : {0U, 4096U, 8191U, 8193U, 24576U, 1048576U}) {
        reject(header(), 131072U, size);
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        reject(h, 131072U, 524288U);
    }
    for (unsigned field_index = 0U; field_index < 5U; ++field_index) {
        auto h = header();
        switch (field_index) {
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
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        reject(h, 131072U, 524288U);
    }
}

TEST_CASE("Mapper 266 PRG select uses only D3-D2 and wraps fitted ROMs", "[mapper266]") {
    for (std::size_t size = 32768U; size <= 131072U; size *= 2U) {
        auto mapper = board(size);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x900CU, static_cast<Byte>(value));
            const unsigned bank = (value >> 2U) & 3U;
            REQUIRE(mapper->read_prg(0x8123U) == expected_prg(size, bank, 0x8123U));
            REQUIRE(mapper->read_prg(0xFEDCU) == expected_prg(size, bank, 0xFEDCU));
        }
        const auto pages = size / 32768U;
        for (unsigned bank = 0U; bank < pages; ++bank) {
            mapper->write_prg(0x900CU, static_cast<Byte>(bank << 2U));
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
                const auto address = static_cast<Address>(addr);
                REQUIRE(mapper->read_prg(address) == expected_prg(size, bank, address));
            }
        }
    }
}

TEST_CASE("Mapper 266 PRG and DAC masks cover all aliases without bus conflicts", "[mapper266]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0x0CU);
        const bool selected = addr >= 0x8000U && (addr & 0xF80CU) == 0x900CU;
        INFO("write address: " << addr);
        REQUIRE(mapper->read_prg(0x8000U) == expected_prg(131072U, selected ? 3U : 0U, 0x8000U));
    }
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0xAFU);
        const bool selected = addr >= 0x8000U && (addr & 0xF80CU) == 0x980CU;
        REQUIRE(mapper->audio_output() == Catch::Approx(selected ? 1.0F : 0.0F));
    }
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE_FALSE(mapper->maps_expansion(0x980CU));
}

TEST_CASE("Mapper 266 mirroring register has every alias and all VRC4 modes", "[mapper266]") {
    constexpr std::array modes = {MirrorMode::Vertical,
                                  MirrorMode::Horizontal,
                                  MirrorMode::SingleLower,
                                  MirrorMode::SingleUpper};
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
    }
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 2U);
        const bool selected = addr >= 0x8000U && (addr & 0xF00CU) == 0x9000U;
        REQUIRE(mapper->mirror_mode() ==
                (selected ? MirrorMode::SingleLower : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 266 rewired CHR registers cover every alias and preserve nibbles",
          "[mapper266]") {
    constexpr std::array<Address, 8> addresses = {
        0xD000U, 0xD008U, 0xA000U, 0xA008U, 0xB000U, 0xB008U, 0xE000U, 0xE008U};
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned high = 0U; high < 2U; ++high) {
            const auto canonical = static_cast<Address>(addresses[slot] + high * 4U);
            unsigned aliases = 0U;
            for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
                if (addr < 0x8000U || (addr & 0xF00CU) != canonical) {
                    continue;
                }
                ++aliases;
                mapper->reset();
                mapper->write_prg(static_cast<Address>(addr), high ? 0xF2U : 0xFBU);
                const auto banks = chr_banks(*mapper);
                REQUIRE(banks[slot] == (high ? 0x120U : 0x0BU));
                for (unsigned other = 0U; other < 8U; ++other) {
                    if (other != slot) {
                        REQUIRE(banks[other] == 0U);
                    }
                }
            }
            REQUIRE(aliases == (1U << (16U - std::popcount(0xF00CU))));
        }
        mapper->reset();
        mapper->write_prg(addresses[slot], 0xFFU);
        mapper->write_prg(static_cast<Address>(addresses[slot] + 4U), 0xFFU);
        REQUIRE(chr_banks(*mapper)[slot] == 511U);
        mapper->write_prg(addresses[slot], 0xA6U);
        REQUIRE(chr_banks(*mapper)[slot] == 0x1F6U);
        mapper->write_prg(static_cast<Address>(addresses[slot] + 4U), 0x12U);
        REQUIRE(chr_banks(*mapper)[slot] == 0x126U);
    }
}

TEST_CASE("Mapper 266 maps all nine-bit CHR banks and ignores pattern writes", "[mapper266]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned bank = 0U; bank < 512U; ++bank) {
            select_chr(*mapper, slot, static_cast<uint16_t>(bank));
            const auto addr = static_cast<Address>(slot * 1024U + 0x155U);
            REQUIRE(mapper->read_chr(addr) == expected_chr(524288U, bank, addr));
        }
    }
    for (std::size_t size = 8192U; size <= 524288U; size *= 2U) {
        auto small = board(131072U, size);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            select_chr(*small, slot, static_cast<uint16_t>(slot * 73U + 3U));
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            const auto address = static_cast<Address>(addr);
            const auto bank = static_cast<unsigned>(addr / 1024U) * 73U + 3U;
            small->write_chr(address, static_cast<Byte>(addr ^ 0xFFU));
            REQUIRE(small->read_chr(address) == expected_chr(size, bank, address));
        }
    }
}

TEST_CASE("Mapper 266 DAC is unsigned immediate stable and resettable", "[mapper266]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x980CU, static_cast<Byte>(value));
        REQUIRE(mapper->audio_output() == Catch::Approx(static_cast<float>(value & 15U) / 15.0F));
        mapper->clock_audio();
        mapper->clock_cpu();
        REQUIRE(mapper->audio_output() == Catch::Approx(static_cast<float>(value & 15U) / 15.0F));
    }
    mapper->reset();
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 266 IRQ latch and control aliases implement cycle mode", "[mapper266]") {
    auto mapper = board();
    unsigned latch_aliases = 0U;
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        if (addr >= 0x8000U && (addr & 0xF00CU) == 0xF000U) {
            ++latch_aliases;
            mapper->reset();
            mapper->write_prg(static_cast<Address>(addr), 0xABU);
            REQUIRE(field<Byte>(state(*mapper), 21U) == 0x0BU);
        }
        if (addr >= 0x8000U && (addr & 0xF00CU) == 0xF004U) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(addr), 0xABU);
            REQUIRE(field<Byte>(state(*mapper), 21U) == 0xB0U);
        }
    }
    REQUIRE(latch_aliases == (1U << (16U - std::popcount(0xF00CU))));

    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        if (addr < 0x8000U || (addr & 0xF00CU) != 0xF008U) {
            continue;
        }
        mapper->reset();
        mapper->write_prg(0xF000U, 0x0DU);
        mapper->write_prg(0xF004U, 0x0FU);
        mapper->write_prg(static_cast<Address>(addr), 0xFEU);
        const auto saved = state(*mapper);
        REQUIRE(field<Byte>(saved, 22U) == 0xFDU);
        REQUIRE(field<Byte>(saved, 25U) == 6U);
        REQUIRE(field<Byte>(saved, 26U) == 1U);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        REQUIRE(field<Byte>(state(*mapper), 22U) == 0xFDU);
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 266 scanline IRQ uses 114 114 113 clocks and control resets phase",
          "[mapper266]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned cycle = 0U; cycle < 113U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    for (unsigned cycle = 0U; cycle < 113U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    for (unsigned cycle = 0U; cycle < 112U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xF008U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(field<uint16_t>(state(*mapper), 23U) == 341U);
    const auto disabled = state(*mapper);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(state(*mapper) == disabled);
}

TEST_CASE("Mapper 266 IRQ acknowledgement applies A without disturbing counter phase",
          "[mapper266]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        if (addr < 0x8000U || (addr & 0xF00CU) != 0xF00CU) {
            continue;
        }
        StateReader restore(before);
        mapper->load_state(restore);
        REQUIRE(restore.ok());
        mapper->write_prg(static_cast<Address>(addr), 0xFFU);
        const auto after = state(*mapper);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(field<Byte>(after, 22U) == field<Byte>(before, 22U));
        REQUIRE(field<uint16_t>(after, 23U) == field<uint16_t>(before, 23U));
        REQUIRE(field<Byte>(after, 26U) == 1U);
    }
    mapper->write_prg(0xF008U, 0U);
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE(field<Byte>(state(*mapper), 26U) == 0U);
}

TEST_CASE("Mapper 266 reset restores deterministic defaults and retains no writable memory",
          "[mapper266]") {
    auto mapper = board(131072U, 524288U, false);
    mapper->write_prg(0x900CU, 0x0CU);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x980CU, 0x0FU);
    select_chr(*mapper, 7U, 511U);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    for (bool cold : {false, true}) {
        if (cold) {
            mapper->power_on();
        } else {
            mapper->reset();
        }
        REQUIRE(mapper->read_prg(0x8000U) == expected_prg(131072U, 0U, 0x8000U));
        REQUIRE(mapper->read_chr(0x1C00U) == expected_chr(524288U, 0U, 0x1C00U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper).size() == 28U);
    }
}

TEST_CASE("Mapper 266 fixed snapshots replay partial IRQ and DAC state", "[mapper266]") {
    auto mapper = board();
    mapper->write_prg(0x900CU, 8U);
    mapper->write_prg(0x9000U, 2U);
    mapper->write_prg(0x980CU, 0x0AU);
    select_chr(*mapper, 5U, 0x1ABU);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned cycle = 0U; cycle < 228U; ++cycle) {
        mapper->clock_cpu();
    }
    for (unsigned cycle = 0U; cycle < 51U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 28U);
    mapper->reset();
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->audio_output() == Catch::Approx(10.0F / 15.0F));
    REQUIRE(chr_banks(*mapper)[5U] == 0x1ABU);
    for (unsigned cycle = 0U; cycle < 500U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto expected = state(*mapper);
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    for (unsigned cycle = 0U; cycle < 500U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(state(*mapper) == expected);
}

TEST_CASE("Mapper 266 snapshot rejection is bounded and atomic", "[mapper266]") {
    auto mapper = board();
    mapper->write_prg(0x900CU, 0x0CU);
    mapper->write_prg(0x980CU, 0x0BU);
    select_chr(*mapper, 3U, 0x123U);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span<const Byte>(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        require_unchanged_on_load(*mapper, saved, bad);
    }
    const auto reject_byte = [&](std::size_t offset, Byte value) {
        auto bad = saved;
        bad[offset] = value;
        require_unchanged_on_load(*mapper, saved, bad);
    };
    reject_byte(2U, 4U);
    reject_byte(19U, static_cast<Byte>(MirrorMode::FourScreen));
    reject_byte(19U, 0xFFU);
    reject_byte(20U, 16U);
    reject_byte(25U, 8U);
    reject_byte(26U, 2U);
    reject_byte(27U, 2U);
    auto bad = saved;
    set_field<uint16_t>(bad, 3U, 512U);
    require_unchanged_on_load(*mapper, saved, bad);
    for (uint16_t prescaler : {0U, 342U, 65535U}) {
        bad = saved;
        set_field<uint16_t>(bad, 23U, prescaler);
        require_unchanged_on_load(*mapper, saved, bad);
    }
    bad = saved;
    bad[26U] = 0U;
    bad[27U] = 1U;
    require_unchanged_on_load(*mapper, saved, bad);

    for (bool different_prg : {false, true}) {
        for (bool different_chr : {false, true}) {
            for (bool different_mirror : {false, true}) {
                if (!different_prg && !different_chr && !different_mirror) {
                    continue;
                }
                auto other = board(different_prg ? 65536U : 131072U,
                                   different_chr ? 262144U : 524288U,
                                   !different_mirror);
                const auto before = state(*other);
                require_unchanged_on_load(*other, before, saved);
            }
        }
    }
}

} // namespace
