#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper453.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 453U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 14U) * 47U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper453> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper453::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper453>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kChrRamOffset);
    return bytes;
}

void load(Mapper453& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Byte latch) {
    const unsigned outer = latch & 0x40U;
    if ((latch & 0x20U) != 0U) {
        const unsigned bank = (outer >> 3U) | (latch & 7U);
        return bank * 0x8000UZ + (addr & 0x7FFFU);
    }
    const unsigned inner = addr < 0xC000U ? (latch & 7U) : 7U;
    const unsigned bank = (outer >> 2U) | (outer >> 3U) | inner;
    return bank * 0x4000UZ + (addr & 0x3FFFU);
}

Byte expected_latch(Byte first, Byte second) {
    Byte result = first & 0x77U;
    if ((first & 0x60U) == 0U) {
        result = static_cast<Byte>((result & 0x17U) | (second & 0x60U));
    }
    return static_cast<Byte>((result & 0x60U) | (second & 0x17U));
}

MirrorMode expected_mirror(Byte latch) {
    const bool selected = (latch & 0x10U) != 0U;
    if ((latch & 0x20U) != 0U) {
        return selected ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return selected ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void require_prg(Mapper453& mapper, Address addr, Byte latch, Byte salt = 0U) {
    const auto physical = expected_prg(addr, latch) % kPrgSize;
    CAPTURE(addr, latch, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 453 validates Realtec 8042 memory geometry", "[mapper453][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8203U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        require_prg(*mapper, 0x8000U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0x35U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper453::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 7U, 452U, 454U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x8000UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 3U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 453 decodes every CPU write address without bus conflicts",
          "[mapper453][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        const auto registers = register_state(*mapper);
        const Byte expected = raw >= 0x8000U ? static_cast<Byte>(value & 0x77U) : 0U;
        CAPTURE(raw, value, expected);
        REQUIRE(registers[kLatchOffset] == expected);
        REQUIRE(mapper->mirror_mode() == expected_mirror(expected));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->reset();
    mapper->write_prg_cpu(0xF123U, 0xF7U, true);
    REQUIRE(register_state(*mapper)[kLatchOffset] == 0x77U);
}

TEST_CASE("Mapper 453 locks only P and p after either becomes set",
          "[mapper453][decoding][banking]") {
    auto mapper = board();
    for (unsigned first = 0U; first < 256U; ++first) {
        for (unsigned second = 0U; second < 256U; ++second) {
            mapper->reset();
            mapper->write_prg(0x8000U, static_cast<Byte>(first));
            mapper->write_prg(0xFFFFU, static_cast<Byte>(second));
            const auto expected =
                expected_latch(static_cast<Byte>(first), static_cast<Byte>(second));
            CAPTURE(first, second, expected);
            REQUIRE(register_state(*mapper)[kLatchOffset] == expected);
            REQUIRE(mapper->mirror_mode() == expected_mirror(expected));
        }
    }

    for (const Byte initial : {0x20U, 0x40U, 0x60U}) {
        mapper->reset();
        mapper->write_prg(0x8000U, initial);
        const auto locked = register_state(*mapper);
        for (const Byte value : {0x00U, 0x20U, 0x40U, 0x60U, 0xFFU}) {
            mapper->write_prg(0xA000U, static_cast<Byte>(value & 0x60U));
            REQUIRE(register_state(*mapper)[kLatchOffset] == initial);
            REQUIRE(register_state(*mapper) == locked);
        }
    }
}

TEST_CASE("Mapper 453 maps UNROM and AOROM PRG pins before ROM wrapping",
          "[mapper453][banking][prg]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        const auto latch = static_cast<Byte>(value);
        mapper->write_prg(0x8000U, latch);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, latch, 0x35U);
        }
    }

    constexpr std::array<Byte, 10> representatives{
        0x00U, 0x07U, 0x20U, 0x27U, 0x40U, 0x47U, 0x60U, 0x77U, 0x97U, 0xF8U};
    for (const auto latch : representatives) {
        mapper->reset();
        mapper->write_prg(0xFFFFU, latch);
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            require_prg(*mapper, addr, latch, 0x35U);
        }
    }

    mapper->reset();
    mapper->write_prg(0x8000U, 0x00U);
    REQUIRE(mapper->read_prg(0xC000U) == cell(0x01C000U, 0x35U));
    mapper->reset();
    mapper->write_prg(0x8000U, 0x40U);
    REQUIRE(mapper->read_prg(0xC000U) == cell(0x07C000U, 0x35U));
    mapper->reset();
    mapper->write_prg(0x8000U, 0x67U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0x078000U, 0x35U));
}

TEST_CASE("Mapper 453 provides writable unbanked 8 KiB CHR RAM", "[mapper453][chr][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x77U);
    const auto before = register_state(*mapper);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x5AU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x5AU));
    }
    REQUIRE(register_state(*mapper) == before);
}

TEST_CASE("Mapper 453 uses mode-dependent H V and CIRAM A10 control", "[mapper453][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == expected_mirror(static_cast<Byte>(value)));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x0101U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 453 leaves low CPU RAM expansion IRQ and audio absent",
          "[mapper453][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x77U);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, static_cast<Byte>(raw * 41U));
        mapper->write_expansion(addr, static_cast<Byte>(raw * 37U));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 453 reset unlocks the latch and cold power clears CHR RAM",
          "[mapper453][reset][ram]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0x77U);
    mapper->write_chr(0x0123U, 0x67U);
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto soft = state(*mapper);
    REQUIRE(soft[kLatchOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(soft).subspan(kChrRamOffset),
                               std::span(programmed).subspan(kChrRamOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    require_prg(*mapper, 0x8000U, 0U, 0x35U);
    require_prg(*mapper, 0xFFFFU, 0U, 0x35U);
    mapper->write_prg(0x8000U, 0x20U);
    mapper->reset();
    mapper->write_prg(0x8000U, 0x40U);
    REQUIRE(register_state(*mapper)[kLatchOffset] == 0x40U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    mapper->write_chr(0x0123U, 0xA5U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 453 bounded snapshots replay latch and CHR RAM without source ROM",
          "[mapper453][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x10U);
    mapper->write_prg(0x8000U, 0x67U);
    mapper->write_prg(0xFFFFU, 0x95U);
    mapper->write_chr(0x0456U, 0x67U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrRamSize + 16U);
    REQUIRE(saved[kLatchOffset] == 0x75U);
    REQUIRE(saved[kChrRamOffset + 0x0456U] == 0x67U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->read_chr(0x0456U) == 0x67U);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, saved[kLatchOffset], 0x80U);
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 453 rejects damaged truncated and incompatible states atomically",
          "[mapper453][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x67U);
    mapper->write_prg(0xFFFFU, 0x95U);
    mapper->write_chr(0x0456U, 0x67U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < active.size(); ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (unsigned bit = 0U; bit < kLatchOffset * 8U; ++bit) {
        auto invalid = active;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x08U, 0x80U, 0x88U, 0xFFU}) {
        auto invalid = active;
        invalid[kLatchOffset] |= reserved;
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }

    const auto h = header();
    auto wrong_size = std::make_unique<Mapper453>(h, rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto before = state(*wrong_size);
    StateReader size_reader(active);
    wrong_size->load_state(size_reader);
    REQUIRE_FALSE(size_reader.ok());
    REQUIRE(state(*wrong_size) == before);

    auto changed = active;
    changed[kLatchOffset] = 0x17U;
    changed[kChrRamOffset + 0x456U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0x17U);
    }
}

} // namespace
