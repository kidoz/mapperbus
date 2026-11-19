#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper288.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 288U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / 8192U * 7U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper288> board(std::size_t prg_size = 131072U,
                                 std::size_t chr_size = 65536U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper288::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper288>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void require_mapping(Mapper288& mapper, Byte latch) {
    const std::size_t prg_bank = ((latch >> 3U) & 3U) % (131072U / 32768U);
    const std::size_t chr_bank = (latch & 7U) % (65536U / 8192U);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xE567U, 0xFFFFU}) {
        REQUIRE(mapper.read_prg(addr) == cell(prg_bank * 32768U + (addr & 0x7FFFU)));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU}) {
        REQUIRE(mapper.read_chr(addr) == cell(chr_bank * 8192U + (addr & 0x1FFFU)));
    }
    REQUIRE(mapper.mirror_mode() ==
            ((latch & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
}

TEST_CASE("Mapper 288 validates its NES 2.0 ROM-only geometry", "[mapper288]") {
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ}) {
        for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ}) {
            REQUIRE(Mapper288::validate_geometry(header(), prg_size, chr_size));
            REQUIRE(
                Mapper288::validate_geometry(header(MirrorMode::Horizontal), prg_size, chr_size));
        }
    }

    auto invalid = header();
    invalid.mapper_number = 287U;
    REQUIRE_FALSE(Mapper288::validate_geometry(invalid, 131072U, 65536U));
    invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper288::validate_geometry(invalid, 131072U, 65536U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper288::validate_geometry(invalid, 131072U, 65536U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 262144UZ, 16777216UZ}) {
        REQUIRE_FALSE(Mapper288::validate_geometry(header(), size, 65536U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 131072UZ, 16777216UZ}) {
        REQUIRE_FALSE(Mapper288::validate_geometry(header(), 131072U, size));
    }

    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 8192U;
            break;
        case 2U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3U:
            invalid.chr_ram_size = 8192U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper288::validate_geometry(invalid, 131072U, 65536U));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper288::validate_geometry(invalid, 131072U, 65536U));
    }
}

TEST_CASE("Mapper 288 latches A5 through A0 at every upper CPU address", "[mapper288]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto data = static_cast<Byte>(addr * 37U + addr / 256U);
        mapper->write_prg(static_cast<Address>(addr), data);
        const auto latch = static_cast<Byte>(addr & 0x3FU);
        INFO("address=" << addr << " data=" << data);
        REQUIRE(state(*mapper)[10U] == latch);
        require_mapping(*mapper, latch);
    }

    mapper->write_prg(0x802AU, 0U);
    const auto before = state(*mapper);
    for (unsigned data = 0U; data < 256U; ++data) {
        mapper->write_prg(0x802AU, static_cast<Byte>(data));
        REQUIRE(state(*mapper) == before);
    }
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 288 decodes bank pins before wrapping smaller fitted ROMs", "[mapper288]") {
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ}) {
        for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ}) {
            auto mapper = board(prg_size, chr_size);
            for (unsigned latch = 0U; latch < 64U; ++latch) {
                mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
                const std::size_t prg_bank = ((latch >> 3U) & 3U) % (prg_size / 32768U);
                const std::size_t chr_bank = (latch & 7U) % (chr_size / 8192U);
                CAPTURE(prg_size, chr_size, latch);
                for (const Address addr : {0x8000U, 0xABCDU, 0xFFFFU}) {
                    REQUIRE(mapper->read_prg(addr) == cell(prg_bank * 32768U + (addr & 0x7FFFU)));
                }
                for (const Address addr : {0x0000U, 0x0555U, 0x1FFFU}) {
                    REQUIRE(mapper->read_chr(addr) == cell(chr_bank * 8192U + (addr & 0x1FFFU)));
                }
            }
        }
    }
}

TEST_CASE("Mapper 288 exposes immutable CHR ROM and no unrelated hardware", "[mapper288]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | bank), 0U);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            const auto expected = cell(bank * 8192U + addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(~expected));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_audio();
        mapper->clock_irq_counter();
        mapper->on_ppu_hblank();
    }
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 288 reset and power-on select deterministic bank zero", "[mapper288]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(131072U, 65536U, initial);
        require_mapping(*mapper, 0U);
        for (const bool cold : {false, true}) {
            mapper->write_prg(0xFFFFU, 0xA5U);
            require_mapping(*mapper, 0x3FU);
            if (cold) {
                mapper->power_on();
            } else {
                mapper->reset();
            }
            require_mapping(*mapper, 0U);
            REQUIRE(state(*mapper)[10U] == 0U);
        }
    }
}

TEST_CASE("Mapper 288 snapshots are bounded, replayable, and atomic on rejection",
          "[mapper288][state]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 11U);
    REQUIRE(saved[10U] == 0x3FU);

    auto replay = board();
    StateReader valid(saved);
    replay->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(state(*replay) == saved);
    require_mapping(*replay, 0x3FU);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }
    for (std::size_t offset = 0U; offset < 10U; ++offset) {
        auto damaged = saved;
        damaged[offset] ^= 1U;
        StateReader reader(damaged);
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }
    for (const Byte latch : {0x40U, 0x80U, 0xFFU}) {
        auto damaged = saved;
        damaged[10U] = latch;
        StateReader reader(damaged);
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }

    for (const std::size_t prg_size : {32768UZ, 65536UZ}) {
        auto other = board(prg_size);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }
    for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ}) {
        auto other = board(131072U, chr_size);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {0x12U, 0x34U});
    StateReader tail(with_tail);
    replay->load_state(tail);
    REQUIRE(tail.ok());
    REQUIRE(tail.remaining() == 2U);
    REQUIRE(state(*replay) == saved);
}

} // namespace
