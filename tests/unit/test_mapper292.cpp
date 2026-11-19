#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper292.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kChrSize = 512U * 1024U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 292U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 18U) * 43U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = pattern(offset);
    return bytes;
}

std::unique_ptr<Mapper292> board(std::size_t prg_size = 512U * 1024U, bool vertical = true) {
    REQUIRE(Mapper292::validate_geometry(header(vertical), prg_size, kChrSize));
    return std::make_unique<Mapper292>(header(vertical), rom(prg_size), rom(kChrSize));
}

void write_bus(Mapper292& mapper, Address addr, Byte value) {
    mapper.observe_cpu_write(addr, value);
    if (addr >= 0x6000U)
        mapper.write_prg(addr, value);
}

void set_bank(Mapper292& mapper, unsigned index, Byte value, Byte control = 0U) {
    write_bus(mapper, 0x8000U, static_cast<Byte>(control | index));
    write_bus(mapper, 0x8001U, value);
}

void set_extra(Mapper292& mapper, unsigned index, Byte value, Byte index_bits = 0xC0U) {
    write_bus(mapper, 0x6000U, static_cast<Byte>(index_bits | index));
    mapper.observe_cpu_write(0U, value);
    REQUIRE(mapper.read_prg(0x6000U) == 0U);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 292 has deterministic MMC3 power state and no unrelated hardware",
          "[mapper292]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(32768U, vertical);
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(mapper->read_prg(0x8000U) == pattern(0U));
        REQUIRE(mapper->read_prg(0xA000U) == pattern(0U));
        REQUIRE(mapper->read_prg(0xC000U) == pattern(16384U));
        REQUIRE(mapper->read_prg(0xE000U) == pattern(24576U));
        REQUIRE(mapper->read_chr(0U) == pattern(0U));
        REQUIRE(mapper->read_chr(0x0800U) == pattern(0U));
        REQUIRE(mapper->read_chr(0x1000U) == pattern(0U));
        REQUIRE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->maps_prg(0x7FFFU));
        REQUIRE(mapper->maps_prg(0x8000U));
        REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
        REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
        REQUIRE(mapper->prg_read_mask(0x7FFFU) == 0U);
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper).size() == 28U);

        set_bank(*mapper, 6U, 3U);
        set_extra(*mapper, 1U, 0xFFU);
        write_bus(*mapper, 0xA000U, 1U);
        write_bus(*mapper, 0xE001U, 1U);
        mapper->clock_irq_counter();
        mapper->reset();
        auto fresh = board(32768U, vertical);
        REQUIRE(state(*mapper) == state(*fresh));
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
        mapper->set_battery_ram(std::array<Byte, 8192>{});
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Mapper 292 implements all MMC3 PRG windows, masks and aliases", "[mapper292]") {
    for (std::size_t size : {32768UZ, 40960UZ, 131072UZ, 524288UZ}) {
        CAPTURE(size);
        auto mapper = board(size);
        const std::size_t count = size / 8192U;
        for (Byte control : {0U, 0x40U}) {
            CAPTURE(control);
            set_bank(*mapper, 6U, 0xFFU, control);
            set_bank(*mapper, 7U, 0xC5U, control);
            const std::size_t r6 = 63U % count;
            const std::size_t r7 = 5U % count;
            const std::array<std::size_t, 4> banks = {
                control == 0U ? r6 : count - 2U,
                r7,
                control == 0U ? count - 2U : r6,
                count - 1U,
            };
            for (Address addr = 0x8000U; addr != 0U; ++addr) {
                const unsigned slot = (addr - 0x8000U) >> 13U;
                REQUIRE(mapper->read_prg(addr) == pattern(banks[slot] * 8192U + (addr & 0x1FFFU)));
            }
        }
        for (Byte value : {0U, 1U, 17U, 62U, 63U, 64U, 255U}) {
            set_bank(*mapper, 6U, value);
            const auto expected = static_cast<Byte>(value & 0x3FU) % count;
            REQUIRE(mapper->read_prg(0x8000U) == pattern(expected * 8192U));
        }
    }

    auto mapper = board();
    for (Address even = 0x8000U; even < 0xA000U; even += 2U) {
        write_bus(*mapper, even, 0x46U);
        write_bus(*mapper, static_cast<Address>(even + 1U), 0x0AU);
        REQUIRE(mapper->read_prg(0xC000U) == pattern(10U * 8192U));
        REQUIRE(mapper->read_prg(0x8000U) == pattern(62U * 8192U));
    }
    for (Address addr = 0x8000U; addr < 0xA000U; ++addr)
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 292 applies BMW8544 CHR wiring in normal mode", "[mapper292]") {
    auto mapper = board();
    set_bank(*mapper, 0U, 0xFFU);
    set_bank(*mapper, 1U, 0x7FU);
    for (unsigned index = 2U; index < 6U; ++index)
        set_bank(*mapper, index, static_cast<Byte>(index * 39U));
    set_extra(*mapper, 0U, 0xA5U);
    set_extra(*mapper, 1U, 0xD5U);

    const std::size_t first_bank = (0xFEU >> 1U) ^ 0xA5U;
    const std::size_t second_bank = (0x7EU >> 1U) ^ ((0xD5U << 1U) & 0x80U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        std::size_t offset = 0U;
        if (addr < 0x0800U)
            offset = first_bank * 2048U + (addr & 0x07FFU);
        else if (addr < 0x1000U)
            offset = second_bank * 2048U + (addr & 0x07FFU);
        else
            offset = (0xD5U & 0x3FU) * 4096U + (addr & 0x0FFFU);
        REQUIRE(mapper->read_chr(addr) == pattern(offset));
    }

    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto original = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(addr ^ 0x5AU));
        REQUIRE(mapper->read_chr(addr) == original);
    }
    REQUIRE(state(*mapper) == before);

    for (unsigned value = 0U; value < 256U; ++value) {
        set_extra(*mapper, 0U, static_cast<Byte>(value));
        REQUIRE(mapper->read_chr(0U) == pattern(((0xFEU >> 1U) ^ value) * 2048U));
        set_extra(*mapper, 1U, static_cast<Byte>(value));
        REQUIRE(mapper->read_chr(0x0800U) ==
                pattern((((0x7EU >> 1U) ^ ((value << 1U) & 0x80U))) * 2048U));
        REQUIRE(mapper->read_chr(0x1000U) == pattern((value & 0x3FU) * 4096U));
    }
}

TEST_CASE("Mapper 292 applies R2-R5 and fixed 4 KiB wiring in inverted mode", "[mapper292]") {
    auto mapper = board();
    set_extra(*mapper, 0U, 0x69U);
    set_extra(*mapper, 1U, 0x6DU);
    set_bank(*mapper, 0U, 0x02U, 0x80U);
    set_bank(*mapper, 1U, 0x04U, 0x80U);
    set_bank(*mapper, 2U, 0x12U, 0x80U);
    set_bank(*mapper, 3U, 0xA5U, 0x80U);
    set_bank(*mapper, 4U, 0x44U, 0x80U);
    set_bank(*mapper, 5U, 0xD7U, 0x80U);

    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        std::size_t offset = 0U;
        if (addr < 0x0400U)
            offset = (((0x12U >> 1U) ^ 0x69U) * 2048U) + (addr & 0x07FFU);
        else if (addr < 0x0800U)
            offset = (((0xA5U >> 1U) ^ 0x69U) * 2048U) + (addr & 0x07FFU);
        else if (addr < 0x0C00U)
            offset = (((0x44U >> 1U) ^ ((0x6DU << 1U) & 0x80U)) * 2048U) + (addr & 0x07FFU);
        else if (addr < 0x1000U)
            offset = (((0xD7U >> 1U) ^ ((0x6DU << 1U) & 0x80U)) * 2048U) + (addr & 0x07FFU);
        else
            offset = (0x6DU & 0x3FU) * 4096U + (addr & 0x0FFFU);
        REQUIRE(mapper->read_chr(addr) == pattern(offset));
    }

    std::vector<Byte> visible(8192U);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        visible[addr] = mapper->read_chr(addr);
    set_bank(*mapper, 0U, 0xFEU, 0x80U);
    set_bank(*mapper, 1U, 0xFEU, 0x80U);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(mapper->read_chr(addr) == visible[addr]);
}

TEST_CASE("Mapper 292 latches every CPU write and fills registers on enabled reads",
          "[mapper292]") {
    auto mapper = board();
    set_bank(*mapper, 0U, 0U);
    write_bus(*mapper, 0x6000U, 0xC0U);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U + addr / 256U);
        mapper->observe_cpu_write(static_cast<Address>(addr), value);
        REQUIRE(mapper->read_prg(0x6000U) == 0U);
        REQUIRE(mapper->read_chr(0U) == pattern(value * 2048U));
    }

    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto index = static_cast<Byte>(0xC0U | (addr & 1U));
        const auto value = static_cast<Byte>(addr * 11U);
        write_bus(*mapper, addr, index);
        mapper->observe_cpu_write(0x0023U, value);
        REQUIRE(mapper->read_prg(addr) == 0U);
        if ((index & 1U) == 0U)
            REQUIRE(mapper->read_chr(0U) == pattern(value * 2048U));
        else
            REQUIRE(mapper->read_chr(0x1000U) == pattern((value & 0x3FU) * 4096U));
    }

    for (Byte index : {0U, 1U, 0x40U, 0x41U, 0x80U, 0x81U, 0xC0U, 0xC1U}) {
        write_bus(*mapper, 0x6000U, index);
        const auto value = static_cast<Byte>(index ^ 0x5BU);
        mapper->observe_cpu_write(0U, value);
        mapper->read_prg(0x7FFFU);
        if ((index & 1U) == 0U)
            REQUIRE(mapper->read_chr(0U) == pattern(value * 2048U));
        else
            REQUIRE(mapper->read_chr(0x1000U) == pattern((value & 0x3FU) * 4096U));
    }
}

TEST_CASE("Mapper 292 uses A001 enable without creating RAM or driven read data", "[mapper292]") {
    auto mapper = board();
    set_extra(*mapper, 0U, 0x12U);
    const auto enabled_state = state(*mapper);

    write_bus(*mapper, 0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    write_bus(*mapper, 0x6000U, 0xC1U);
    mapper->observe_cpu_write(0U, 0xFFU);
    mapper->read_prg(0x6000U);
    REQUIRE(state(*mapper)[24U] == enabled_state[24U]);
    REQUIRE(state(*mapper)[25U] == enabled_state[25U]);
    REQUIRE(state(*mapper)[26U] == enabled_state[26U]);

    write_bus(*mapper, 0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6000U));
    write_bus(*mapper, 0x7FFFU, 0xC1U);
    mapper->observe_cpu_write(0U, 0xABU);
    mapper->read_prg(0x6000U);
    REQUIRE(mapper->read_chr(0x1000U) == pattern((0xABU & 0x3FU) * 4096U));
    REQUIRE((state(*mapper)[23U] & 0xC0U) == 0xC0U);
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 292 implements mirroring, RAM gates and scanline IRQ registers",
          "[mapper292][irq]") {
    auto mapper = board();
    for (Address even = 0xA000U; even < 0xC000U; even += 2U) {
        write_bus(*mapper, even, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        write_bus(*mapper, even, 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    for (Address odd = 0xA001U; odd < 0xC000U; odd += 2U) {
        write_bus(*mapper, odd, 0U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        write_bus(*mapper, odd, 0x80U);
        REQUIRE(mapper->maps_prg(0x6000U));
    }

    write_bus(*mapper, 0xC000U, 2U);
    write_bus(*mapper, 0xC001U, 0U);
    write_bus(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    write_bus(*mapper, 0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    write_bus(*mapper, 0xC000U, 0U);
    write_bus(*mapper, 0xC001U, 0U);
    write_bus(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    write_bus(*mapper, 0xFFFEU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 292 rejects undocumented geometry and memory populations", "[mapper292]") {
    for (std::size_t size : {32768UZ, 40960UZ, 65536UZ, 524288UZ})
        REQUIRE(Mapper292::validate_geometry(header(), size, kChrSize));
    for (std::size_t size : {0UZ, 8192UZ, 16384UZ, 24576UZ, 32767UZ, 32769UZ, 524289UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper292::validate_geometry(header(), size, kChrSize));
    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 262144UZ, 524287UZ, 524289UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper292::validate_geometry(header(), 524288U, size));

    auto invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper292::validate_geometry(invalid, 524288U, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper292::validate_geometry(invalid, 524288U, kChrSize));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(Mapper292::validate_geometry(invalid, 524288U, kChrSize));
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
        REQUIRE_FALSE(Mapper292::validate_geometry(invalid, 524288U, kChrSize));
    }
}

TEST_CASE("Mapper 292 snapshots are bounded, replayable and atomically validated",
          "[mapper292][state]") {
    auto mapper = board();
    auto copy = board();
    set_bank(*mapper, 0U, 0xAAU);
    set_bank(*mapper, 1U, 0x55U);
    set_bank(*mapper, 2U, 0x12U);
    set_bank(*mapper, 3U, 0x34U);
    set_bank(*mapper, 4U, 0x56U);
    set_bank(*mapper, 5U, 0x78U);
    set_bank(*mapper, 6U, 0x3FU, 0xC0U);
    set_bank(*mapper, 7U, 0x21U, 0xC0U);
    set_extra(*mapper, 0U, 0x96U);
    set_extra(*mapper, 1U, 0x69U);
    write_bus(*mapper, 0xA000U, 0U);
    write_bus(*mapper, 0xA001U, 0xC0U);
    write_bus(*mapper, 0xC000U, 0U);
    write_bus(*mapper, 0xC001U, 0U);
    write_bus(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 28U);
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xFFFFU})
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated{std::span(saved).first(size)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }

    const auto reject = [&mapper, &saved](std::vector<Byte> invalid) {
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (std::size_t offset : {0UZ, 1UZ, 2UZ, 6UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(std::move(invalid));
    }
    for (auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 10>{{
             {10U, 0x08U},
             {11U, 0x01U},
             {12U, 0x01U},
             {17U, 0x40U},
             {18U, 0x40U},
             {19U, 0x02U},
             {22U, 0x08U},
             {22U, 0x05U},
             {23U, 0x01U},
             {23U, 0x3FU},
         }}) {
        auto invalid = saved;
        invalid[offset] = value;
        reject(std::move(invalid));
    }
    auto trailing = saved;
    trailing.push_back(0U);
    reject(std::move(trailing));

    for (std::size_t size : {32768UZ, 40960UZ, 131072UZ}) {
        auto other = board(size);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
    auto other_mirror = board(524288U, false);
    const auto before = state(*other_mirror);
    StateReader mismatch(saved);
    other_mirror->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_mirror) == before);

    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
