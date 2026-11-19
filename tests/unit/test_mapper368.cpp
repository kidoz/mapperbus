#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper368.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x020000UZ;
constexpr std::size_t kChrSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kStateSize = 17UZ;
constexpr std::size_t kBankOffset = 12UZ;
constexpr std::size_t kLatchOffset = 13UZ;
constexpr std::size_t kIrqCounterOffset = 14UZ;
constexpr std::size_t kIrqFlagsOffset = 16UZ;
constexpr std::array<Byte, 8> kPrgBanks{4U, 3U, 5U, 3U, 6U, 3U, 7U, 3U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 368U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 16U) * 43U + salt);
}

std::vector<Byte> prg_rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < 0x10000UZ; ++offset) {
        data[offset] = cell(offset, salt);
    }
    for (std::size_t offset = 0x10000UZ; offset < data.size(); ++offset) {
        data[offset] = cell(0x10000UZ + (offset & 0x07FFUZ), salt);
    }
    return data;
}

std::vector<Byte> chr_rom(Byte salt = 0U) {
    std::vector<Byte> data(kChrSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper368> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper368::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper368>(h, prg_rom(salt), chr_rom(salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

unsigned selected_prg_bank(Byte selection, Address addr) {
    if (addr < 0x8000U) {
        return 2U;
    }
    if (addr < 0xA000U) {
        return 1U;
    }
    if (addr < 0xC000U) {
        return 0U;
    }
    if (addr < 0xE000U) {
        return kPrgBanks[selection & 7U];
    }
    return 8U;
}

std::size_t expected_prg_offset(Byte selection, Address addr) {
    const auto bank = selected_prg_bank(selection, addr);
    const auto inner = static_cast<std::size_t>(addr & 0x1FFFU);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (bank == 8U ? inner & 0x07FFUZ : inner);
}

TEST_CASE("Mapper 368 validates exact NES 2.0 ROM RAM and CIRAM geometry", "[mapper368]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper368::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (const unsigned id : {0U, 50U, 357U, 367U, 369U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper368::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper368::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper368::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, 0x10000UZ, 0x1FFFFUZ, 0x20001UZ, 0x40000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper368::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x4000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper368::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper368::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper368::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 368 maps every fixed selected and repeated PRG cell", "[mapper368][banking]") {
    auto mapper = board();
    const auto actual_rom = prg_rom();
    for (const Address base : {0x6000U, 0x8000U, 0xA000U, 0xE000U}) {
        for (Address offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto addr = static_cast<Address>(base + offset);
            CAPTURE(base, offset);
            REQUIRE(mapper->read_prg(addr) == actual_rom[expected_prg_offset(0U, addr)]);
        }
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4022U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kBankOffset] == (value & 7U));
        for (Address offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto addr = static_cast<Address>(0xC000U + offset);
            CAPTURE(value, offset);
            REQUIRE(mapper->read_prg(addr) == actual_rom[expected_prg_offset(value, addr)]);
        }
        for (const Address addr : {0x6123U, 0x8123U, 0xA123U, 0xE123U, 0xFFFFU}) {
            CAPTURE(value, addr);
            REQUIRE(mapper->read_prg(addr) == actual_rom[expected_prg_offset(value, addr)]);
        }
    }
    for (Address offset = 0U; offset < 0x0800U; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + offset)) ==
                mapper->read_prg(static_cast<Address>(0xE800U + offset)));
        REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + offset)) ==
                mapper->read_prg(static_cast<Address>(0xF000U + offset)));
        REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + offset)) ==
                mapper->read_prg(static_cast<Address>(0xF800U + offset)));
    }
}

TEST_CASE("Mapper 368 decodes only F1FF register aliases and preserves bus masks",
          "[mapper368][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_expansion(addr, value);
        const bool prg_selected = (raw & 0xF1FFU) == 0x4022U;
        const bool irq_selected = (raw & 0xF1FFU) == 0x4122U;
        if (prg_selected) {
            expected->write_expansion(0x4022U, value);
        } else if (irq_selected) {
            expected->write_expansion(0x4122U, value);
        }
        const auto expected_latch = static_cast<Byte>(irq_selected ? value & 0x35U : 0U);
        CAPTURE(raw, value, prg_selected, irq_selected);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_expansion(addr) == (prg_selected || irq_selected));
        REQUIRE(actual->expansion_read_mask(addr) == (irq_selected ? 0xFFU : 0U));
        REQUIRE(actual->read_expansion(addr) ==
                (irq_selected ? static_cast<Byte>(0x8AU | expected_latch) : 0U));
        REQUIRE(actual->maps_prg(addr) == (raw >= 0x6000U));
        REQUIRE(actual->prg_read_mask(addr) == (raw >= 0x6000U ? 0xFFU : 0U));
        if (raw < 0x6000U) {
            REQUIRE(actual->read_prg(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 368 latch reads drive documented constant and latched bits", "[mapper368][bus]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4022U, static_cast<Byte>(value ^ 0xA5U));
        mapper->write_expansion(0x4122U, static_cast<Byte>(value));
        const auto expected = static_cast<Byte>(0x8AU | (value & 0x35U));
        CAPTURE(value, expected);
        REQUIRE(mapper->read_expansion(0x4122U) == expected);
        REQUIRE(mapper->read_expansion(0x4F22U) == expected);
        REQUIRE(state(*mapper)[kLatchOffset] == (value & 0x35U));
        REQUIRE(mapper->irq_pending() == false);
        mapper->write_expansion(0x4022U, static_cast<Byte>(~value));
        REQUIRE(mapper->read_expansion(0x4122U) == expected);
    }
}

TEST_CASE("Mapper 368 has flat read-only CHR and header-selected CIRAM",
          "[mapper368][ciram][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        for (Address addr = 0U; addr < kChrSize; ++addr) {
            CAPTURE(mirror, addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
            mapper->write_chr(addr, static_cast<Byte>(~cell(addr, 0x35U)));
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x35U));
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
    }
}

TEST_CASE("Mapper 368 IRQ counts 4096 M2 cycles and only D0 acknowledges it", "[mapper368][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(state(*mapper) == initial);

    mapper->write_expansion(0x4122U, 0x35U);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        CAPTURE(cycle);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == (cycle & 0xFFU));
        REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == (cycle >> 8U));
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0U);
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    REQUIRE(mapper->read_expansion(0x4122U) == 0xBFU);

    mapper->write_expansion(0x4122U, 0x34U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
    REQUIRE(mapper->read_expansion(0x4122U) == 0xBEU);
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 123U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto partial = state(*mapper);
    mapper->write_expansion(0x4122U, 0x35U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == partial[kIrqCounterOffset]);
    mapper->write_expansion(0x4122U, 0xFEU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
}

TEST_CASE("Mapper 368 has no RAM writes expansion audio or unrelated clock side effects",
          "[mapper368][ram][audio][bus]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4122U, 0x35U);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        mapper->write_prg_cpu(static_cast<Address>(raw), static_cast<Byte>(~raw), true);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->read_prg(0x6123U) == prg_rom()[0x4123U]);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    mapper->write_expansion(0x6001U, 0U);
    mapper->observe_cpu_read(0x4122U, 0xA5U);
    mapper->observe_cpu_write(0x4122U, 0U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_irq_counter();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 368 reset restores deterministic registers and initial CIRAM",
          "[mapper368][reset]") {
    auto mapper = board(MirrorMode::Horizontal);
    auto fresh = board(MirrorMode::Horizontal);
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4122U, 0x35U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0xC123U) == prg_rom()[4U * kPrgBankSize + 0x0123U]);
    REQUIRE(mapper->read_expansion(0x4122U) == 0x8AU);

    mapper->write_expansion(0x4022U, 2U);
    mapper->write_expansion(0x4122U, 1U);
    mapper->clock_cpu();
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));
}

TEST_CASE("Mapper 368 fixed bounded snapshots replay latch banks and IRQ without ROM",
          "[mapper368][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4122U, 0x35U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->read_expansion(0x4122U) == 0xBFU);
    for (const Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xA123U, 0xC123U, 0xE123U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == prg_rom(0x80U)[expected_prg_offset(6U, addr)]);
    }
    REQUIRE(copy->read_chr(0x0123U) == cell(0x0123U, 0xB5U));
    mapper->clock_cpu();
    copy->clock_cpu();
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 368 rejects malformed and incompatible states atomically", "[mapper368][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4122U, 0x35U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    const auto replace = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte value : {8U, 0x80U, 0xFFU}) {
        replace(kBankOffset, value);
    }
    for (const Byte value : {2U, 8U, 0x40U, 0xCAU, 0xFFU}) {
        replace(kLatchOffset, value);
    }
    for (const std::uint16_t counter : {0x1000U, 0x1234U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqCounterOffset, &counter, sizeof(counter));
        CAPTURE(counter);
        reject(invalid);
    }
    for (const Byte value : {0U, 2U, 4U, 8U, 0xFFU}) {
        replace(kIrqFlagsOffset, value);
    }
    auto disabled_with_count = saved;
    disabled_with_count[kLatchOffset] = 0x34U;
    disabled_with_count[kIrqFlagsOffset] = 0U;
    constexpr std::uint16_t kNonzeroCounter = 1U;
    std::memcpy(
        disabled_with_count.data() + kIrqCounterOffset, &kNonzeroCounter, sizeof(kNonzeroCounter));
    reject(disabled_with_count);
    auto enabled_without_latch = saved;
    enabled_without_latch[kLatchOffset] = 0x34U;
    enabled_without_latch[kIrqFlagsOffset] = 1U;
    reject(enabled_without_latch);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
