#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper293.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDocumentedPrgSize = 512U * 1024U;
constexpr std::size_t kMaximumPrgSize = 1024U * 1024U;
constexpr std::size_t kStateSize = 8200U;

INesHeader header(bool legacy = false, bool vertical = false) {
    INesHeader result{};
    result.mapper_number = 293U;
    result.is_nes2 = !legacy;
    result.prg_ram_size = legacy ? 8192U : 0U;
    result.chr_ram_size = 8192U;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(std::size_t offset) {
    const auto bank = offset / 0x4000U;
    const auto within = offset & 0x3FFFU;
    return static_cast<Byte>(bank * 41U + within + (within >> 8U));
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = prg_cell(offset);
    return data;
}

std::unique_ptr<Mapper293> board(std::size_t prg_size = kMaximumPrgSize,
                                 bool legacy = false,
                                 bool vertical = false) {
    const auto h = header(legacy, vertical);
    REQUIRE(Mapper293::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper293>(h, rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

unsigned outer_bank(Byte value) {
    return ((value >> 4U) & 3U) | ((value & 1U) << 2U);
}

std::pair<unsigned, unsigned> banks(Byte outer_value, Byte inner_value) {
    const unsigned first = outer_bank(outer_value) * 8U;
    const unsigned inner = inner_value & 7U;
    const unsigned mode = ((inner_value >> 3U) & 1U) * 2U + ((outer_value >> 6U) & 1U);
    unsigned low = first + inner;
    unsigned high = first + 7U;
    if (mode == 1U || mode == 3U)
        low &= ~1U;
    if (mode == 2U)
        high = low;
    if (mode == 3U)
        high = low + 1U;
    return {low, high};
}

Byte expected_prg(Byte outer, Byte inner, Address addr, std::size_t prg_size) {
    const auto [low, high] = banks(outer, inner);
    const unsigned bank = addr < 0xC000U ? low : high;
    const auto offset = (bank * 0x4000UZ + (addr & 0x3FFFU)) % prg_size;
    return prg_cell(offset);
}

void require_mapping(Mapper293& mapper,
                     Byte outer,
                     Byte inner,
                     std::size_t prg_size = kMaximumPrgSize) {
    for (Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xD456U, 0xFFFFU}) {
        INFO("outer=" << static_cast<unsigned>(outer) << " inner=" << static_cast<unsigned>(inner)
                      << " addr=" << addr);
        REQUIRE(mapper.read_prg(addr) == expected_prg(outer, inner, addr, prg_size));
    }
    REQUIRE(mapper.mirror_mode() ==
            ((outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
}

TEST_CASE("Mapper 293 powers up in UNROM bank zero with no unrelated hardware", "[mapper293]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(kDocumentedPrgSize, legacy);
        require_mapping(*mapper, 0U, 0U, kDocumentedPrgSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        for (Address addr : {0U, 0x4020U, 0x5000U, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
            REQUIRE(mapper->maps_prg(addr) == (addr >= 0x8000U));
            REQUIRE_FALSE(mapper->maps_expansion(addr));
        }
        const auto before = state(*mapper);
        for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
            mapper->clock_cpu();
            mapper->clock_irq_counter();
            mapper->clock_audio();
            mapper->on_ppu_hblank();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 293 implements every inner, outer, mode, and fixed-bank combination",
          "[mapper293]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0xA000U, static_cast<Byte>(outer));
        for (unsigned inner = 0U; inner < 256U; ++inner) {
            mapper->write_prg(0xC000U, static_cast<Byte>(inner));
            require_mapping(*mapper, static_cast<Byte>(outer), static_cast<Byte>(inner));
        }
    }
}

TEST_CASE("Mapper 293 decodes every upper CPU address including both overlap directions",
          "[mapper293]") {
    auto mapper = board();
    Byte outer = 0U;
    Byte inner = 0U;
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>(addr * 29U + (addr >> 8U));
        mapper->write_prg(static_cast<Address>(addr), value);
        if (addr < 0xC000U)
            outer = value;
        if ((addr & 0xA000U) == 0x8000U)
            inner = value;
        const auto saved = state(*mapper);
        REQUIRE(saved[6U] == outer);
        REQUIRE(saved[7U] == inner);
        require_mapping(*mapper, outer, inner);
    }
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 0x8000U; ++addr)
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 293 wraps only disconnected high ROM pins on the documented 512 KiB image",
          "[mapper293]") {
    auto mapper = board(kDocumentedPrgSize);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0xA000U, static_cast<Byte>(outer));
        for (unsigned inner : {0U, 1U, 7U, 8U, 9U, 15U, 0xA5U, 0xFFU}) {
            mapper->write_prg(0xC000U, static_cast<Byte>(inner));
            require_mapping(
                *mapper, static_cast<Byte>(outer), static_cast<Byte>(inner), kDocumentedPrgSize);
        }
    }
}

TEST_CASE("Mapper 293 CHR RAM is unbanked, soft reset is inert, and cold power is deterministic",
          "[mapper293]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0xD7U);
    mapper->write_prg(0xC000U, 0xABU);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto data = static_cast<Byte>(addr * 13U + (addr >> 8U));
        mapper->write_chr(static_cast<Address>(addr), data);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == data);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == data);
    }
    const auto running = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == running);
    require_mapping(*mapper, 0xD7U, 0xABU);

    mapper->power_on();
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    require_mapping(*mapper, 0U, 0U);
}

TEST_CASE("Mapper 293 accepts only documented ROM extents and board memory metadata",
          "[mapper293]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (std::size_t size : {kDocumentedPrgSize, kMaximumPrgSize})
                REQUIRE(Mapper293::validate_geometry(header(legacy, vertical), size, 0U));

    for (std::size_t size : {0UZ,
                             0x4000UZ,
                             0x20000UZ,
                             0x40000UZ,
                             kDocumentedPrgSize - 1UZ,
                             kDocumentedPrgSize + 0x4000UZ,
                             0xC0000UZ,
                             kMaximumPrgSize + 0x4000UZ,
                             0x200000UZ})
        REQUIRE_FALSE(Mapper293::validate_geometry(header(), size, 0U));
    REQUIRE_FALSE(Mapper293::validate_geometry(header(), kDocumentedPrgSize, 8192U));

    for (unsigned kind = 0U; kind < 11U; ++kind) {
        auto invalid = header();
        switch (kind) {
        case 0U:
            invalid.mapper_number = 281U;
            break;
        case 1U:
            invalid.submapper = 1U;
            break;
        case 2U:
            invalid.has_battery = true;
            break;
        case 3U:
            invalid.prg_ram_size = 8192U;
            break;
        case 4U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 5U:
            invalid.chr_ram_size = 0U;
            break;
        case 6U:
            invalid.chr_ram_size = 4096U;
            break;
        case 7U:
            invalid.chr_ram_size = 16384U;
            break;
        case 8U:
            invalid.chr_nvram_size = 8192U;
            break;
        case 9U:
            invalid.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            invalid.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        REQUIRE_FALSE(Mapper293::validate_geometry(invalid, kDocumentedPrgSize, 0U));
    }
}

TEST_CASE("Mapper 293 state replay is fixed-size and malformed loads are atomic",
          "[mapper293][state]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0xF7U);
    mapper->write_prg(0xC000U, 0xABU);
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U + 3U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[6U] == 0xF7U);
    REQUIRE(saved[7U] == 0xABU);

    auto copy = board();
    StateReader valid(saved);
    copy->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    require_mapping(*copy, 0xF7U, 0xABU);
    copy->reset();
    REQUIRE(state(*copy) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated{std::span(saved).first(length)};
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned offset = 0U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        invalid[8U] ^= 0xFFU;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
        require_mapping(*copy, 0xF7U, 0xABU);
    }

    auto other_size = board(kDocumentedPrgSize);
    const auto before = state(*other_size);
    StateReader wrong_geometry(saved);
    other_size->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other_size) == before);
}

} // namespace
