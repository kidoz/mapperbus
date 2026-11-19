#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper524.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kDefaultPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kDefaultChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 25UZ;
constexpr std::size_t kPrgBank0Offset = 10UZ;
constexpr std::size_t kPrgBank1Offset = 11UZ;
constexpr std::size_t kChrBanksOffset = 12UZ;
constexpr std::size_t kMirrorOffset = 20UZ;
constexpr std::size_t kMicrowireOffset = 21UZ;
constexpr std::size_t kIrqCounterOffset = 22UZ;
constexpr std::size_t kIrqEnabledOffset = 24UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 524U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_rom_size = kDefaultChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 37U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 83U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper524> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper524::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper524>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper524& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper524& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto latch = mapper.read_prg(0x6123U);
    const auto irq = mapper.irq_pending();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.read_prg(0x6123U) == latch);
    REQUIRE(mapper.irq_pending() == irq);
}

void require_prg(
    Mapper524& mapper, Address addr, std::size_t physical, std::size_t prg_size, Byte salt) {
    const auto offset = physical % prg_size;
    CAPTURE(addr, physical, offset, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(offset, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper524& mapper, Address addr, std::size_t physical, std::size_t chr_size, Byte salt) {
    const auto offset = physical % chr_size;
    CAPTURE(addr, physical, offset, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(offset, salt ^ 0x5AU));
}

TEST_CASE("Mapper 524 validates VRC2 ROM geometry and bounded initial state",
          "[mapper524][geometry][ram][ciram][state]") {
    for (const auto prg : {kMinPrgSize, 0x010000UZ, 0x020000UZ, kDefaultPrgSize}) {
        for (const auto chr : {kMinChrSize, 0x004000UZ, 0x020000UZ, kDefaultChrSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper524::validate_geometry(header(mirror), prg, chr));
                auto mapper = board(prg, chr, mirror, 0x35U);
                auto different_rom = board(prg, chr, mirror, 0xA0U);
                REQUIRE(state(*mapper) == state(*different_rom));
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE_FALSE(mapper->has_flash_storage());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                require_prg(*mapper, 0x8000U, 0U, prg, 0x35U);
                require_prg(*mapper, 0xA000U, kPrgBankSize, prg, 0x35U);
                require_prg(*mapper, 0xC000U, prg - 2UZ * kPrgBankSize, prg, 0x35U);
                require_prg(*mapper, 0xE000U, prg - kPrgBankSize, prg, 0x35U);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    require_chr(*mapper,
                                static_cast<Address>(slot * kChrBankSize + 0x234U),
                                0x234U,
                                chr,
                                0x35U);
                }
            }
        }
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
        REQUIRE_FALSE(Mapper524::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 22U, 23U, 25U, 523U, 525U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, kDefaultChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x6000UZ, 0x8001UZ, 0xC000UZ, kDefaultPrgSize * 2UZ}) {
        reject(header(), size, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1000UZ, 0x2001UZ, 0x3000UZ, kDefaultChrSize * 2UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, kDefaultChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
}

TEST_CASE("Mapper 524 exhaustively decodes VRC4e register aliases and no others",
          "[mapper524][decoding][banking][bus][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto group = raw >> 12U;
        const auto port = (raw >> 2U) & 3U;
        if (group == 8U || group == 10U) {
            expected[group == 8U ? kPrgBank0Offset : kPrgBank1Offset] = value & 0x1FU;
        } else if (group == 9U && port == 0U) {
            expected[kMirrorOffset] = value & 1U;
        } else if (group >= 11U && group <= 14U) {
            const auto offset = kChrBanksOffset + (group - 11U) * 2U + (port >> 1U);
            if ((port & 1U) == 0U) {
                expected[offset] = (expected[offset] & 0xF0U) | (value & 0x0FU);
            } else {
                expected[offset] = (expected[offset] & 0x0FU) | ((value & 0x0FU) << 4U);
            }
        } else if (group == 15U && port == 2U) {
            expected[kIrqEnabledOffset] = 1U;
        }

        CAPTURE(raw, value, group, port);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 524 selects five-bit PRG banks without VRC4 swap or RAM modes",
          "[mapper524][banking][prg][bus]") {
    for (const std::size_t size : {kMinPrgSize, 0x010000UZ, 0x020000UZ, kDefaultPrgSize}) {
        auto mapper = board(size, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto value = static_cast<Byte>(raw);
            const auto other = static_cast<Byte>(raw ^ 0xA5U);
            mapper->write_prg(static_cast<Address>(0x8000U | ((raw & 0x0FU) << 0U)), value);
            mapper->write_prg(static_cast<Address>(0xA000U | (raw & 0x0FU)), other);
            mapper->write_prg(0x9004U, 0xFFU);
            mapper->write_prg(0x9008U, 0xFFU);
            mapper->write_prg(0x900CU, 0xFFU);
            const std::size_t bank0 = value & 0x1FU;
            const std::size_t bank1 = other & 0x1FU;
            CAPTURE(size, raw, bank0, bank1);
            for (const Address offset : {Address{0U}, Address{0x123U}, Address{0x1FFFU}}) {
                require_prg(*mapper,
                            static_cast<Address>(0x8000U + offset),
                            bank0 * kPrgBankSize + offset,
                            size,
                            0x35U);
                require_prg(*mapper,
                            static_cast<Address>(0xA000U + offset),
                            bank1 * kPrgBankSize + offset,
                            size,
                            0x35U);
                require_prg(*mapper,
                            static_cast<Address>(0xC000U + offset),
                            size - 2UZ * kPrgBankSize + offset,
                            size,
                            0x35U);
                require_prg(*mapper,
                            static_cast<Address>(0xE000U + offset),
                            size - kPrgBankSize + offset,
                            size,
                            0x35U);
            }
        }
    }

    auto mapper = board(kMinPrgSize, kDefaultChrSize, MirrorMode::Horizontal, 0xA0U);
    mapper->write_prg(0x8003U, 0x1FU);
    mapper->write_prg(0xA003U, 0x1EU);
    for (Address addr = 0x8000U;; ++addr) {
        const std::size_t bank = addr < 0xA000U   ? 0x1FU
                                 : addr < 0xC000U ? 0x1EU
                                 : addr < 0xE000U ? 0x02U
                                                  : 0x03U;
        require_prg(*mapper, addr, bank * kPrgBankSize + (addr & 0x1FFFU), kMinPrgSize, 0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 524 combines VRC2 CHR nibbles and protects pattern ROM",
          "[mapper524][banking][chr][bus]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    std::array<Byte, 8> banks{};
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const unsigned group = 11U + slot / 2U;
        const unsigned low_port = (slot & 1U) * 2U;
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto low = static_cast<Byte>(raw);
            const auto high = static_cast<Byte>(raw ^ 0xA5U);
            mapper->write_prg(static_cast<Address>((group << 12U) | (low_port << 2U) | (raw & 3U)),
                              low);
            mapper->write_prg(
                static_cast<Address>((group << 12U) | ((low_port + 1U) << 2U) | ((raw + 1U) & 3U)),
                high);
            banks[slot] = static_cast<Byte>((low & 0x0FU) | ((high & 0x0FU) << 4U));
            CAPTURE(slot, raw, low, high);
            for (unsigned window = 0U; window < 8U; ++window) {
                const auto addr = static_cast<Address>(window * kChrBankSize + 0x234U);
                require_chr(*mapper,
                            addr,
                            static_cast<std::size_t>(banks[window]) * kChrBankSize + 0x234U,
                            kDefaultChrSize,
                            0x35U);
                mapper->write_chr(addr, static_cast<Byte>(raw ^ 0xFFU));
                require_chr(*mapper,
                            addr,
                            static_cast<std::size_t>(banks[window]) * kChrBankSize + 0x234U,
                            kDefaultChrSize,
                            0x35U);
            }
        }
    }

    auto small = board(kMinPrgSize, kMinChrSize, MirrorMode::Horizontal, 0xA0U);
    small->write_prg(0xB000U, 0x0FU);
    small->write_prg(0xB004U, 0xF0U);
    require_chr(*small, 0x0234U, 0xFFU * kChrBankSize + 0x234U, kMinChrSize, 0xA0U);
    REQUIRE(small->read_chr(0x2000U) == 0U);
    REQUIRE(small->read_chr(0xFFFFU) == 0U);
    const auto before = state(*small);
    small->write_chr(0x2000U, 0xA5U);
    small->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*small) == before);
}

TEST_CASE("Mapper 524 exposes only the VRC2 D0 latch below PRG ROM",
          "[mapper524][bus][ram][decoding][state]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        const auto before = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U) + 0xA5U);
        const bool latch = raw >= 0x6000U && raw < 0x7000U;
        mapper->write_prg(addr, value);
        CAPTURE(raw, value, latch);
        REQUIRE(mapper->maps_prg(addr) == latch);
        REQUIRE(mapper->prg_read_mask(addr) == (latch ? 0x01U : 0x00U));
        REQUIRE(mapper->read_prg(addr) == (latch ? value & 1U : 0U));
        auto expected = before;
        if (latch) {
            expected[kMicrowireOffset] = value & 1U;
        }
        REQUIRE(state(*mapper) == expected);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 524 switches only horizontal and vertical CIRAM",
          "[mapper524][ciram][bus][reset]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x9000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
    }
}

TEST_CASE("Mapper 524 fixed IRQ asserts for 1024 M2 cycles then self acknowledges",
          "[mapper524][irq][bus][state]") {
    auto mapper = board();
    mapper->write_prg(0xF008U, 0U);
    for (unsigned cycle = 1U; cycle <= 4096U; ++cycle) {
        mapper->clock_cpu();
        CAPTURE(cycle);
        REQUIRE(mapper->irq_pending() == ((cycle & 0x0400U) != 0U));
    }

    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    const auto stopped = state(*mapper);
    REQUIRE(stopped[kIrqCounterOffset] == 0U);
    REQUIRE(stopped[kIrqCounterOffset + 1U] == 0U);
    REQUIRE(stopped[kIrqEnabledOffset] == 0U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(state(*mapper) == stopped);

    mapper->write_prg(0xF008U, 0xA5U);
    for (unsigned cycle = 0U; cycle < 512U; ++cycle) {
        mapper->clock_cpu();
    }
    mapper->write_prg(0xF00BU, 0x5AU);
    for (unsigned cycle = 0U; cycle < 512U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper) == stopped);

    for (Address alias = 0xF008U; alias <= 0xF00BU; ++alias) {
        mapper->reset();
        mapper->write_prg(alias, static_cast<Byte>(alias));
        for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(alias);
        REQUIRE(mapper->irq_pending());
    }
    for (Address alias = 0xF00CU; alias <= 0xF00FU; ++alias) {
        mapper->write_prg(alias, static_cast<Byte>(alias));
        CAPTURE(alias);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        REQUIRE(state(*mapper)[kIrqEnabledOffset] == 0U);
    }

    mapper->write_prg(0xF008U, 0U);
    const auto before = state(*mapper);
    for (unsigned cycle = 0U; cycle < 2048U; ++cycle) {
        mapper->clock_audio();
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 524 reset restores all documented defaults",
          "[mapper524][reset][banking][ciram][ram][irq]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, mirror, 0x35U);
        const auto cold = state(*mapper);
        mapper->write_prg(0x8000U, 0x1FU);
        mapper->write_prg(0xA000U, 0x1EU);
        mapper->write_prg(0xB000U, 0x0FU);
        mapper->write_prg(0xB004U, 0x0FU);
        mapper->write_prg(0x9000U, static_cast<Byte>(mirror == MirrorMode::Horizontal ? 0U : 1U));
        mapper->write_prg(0x6123U, 1U);
        mapper->write_prg(0xF008U, 0U);
        for (unsigned cycle = 0U; cycle < 1100U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE(mapper->irq_pending());
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        require_prg(*mapper, 0x8000U, 0U, kDefaultPrgSize, 0x35U);
        require_prg(*mapper, 0xA000U, kPrgBankSize, kDefaultPrgSize, 0x35U);
        require_chr(*mapper, 0x0234U, 0x0234U, kDefaultChrSize, 0x35U);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);

        mapper->write_prg(0x8000U, 0x1FU);
        mapper->write_prg(0x6123U, 1U);
        mapper->write_prg(0xF008U, 0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 524 bounded snapshots replay without ROM bytes",
          "[mapper524][state][atomic][banking][irq]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x8000U, 0x12U);
    mapper->write_prg(0xA000U, 0x03U);
    mapper->write_prg(0xB000U, 0x0BU);
    mapper->write_prg(0xB004U, 0x07U);
    mapper->write_prg(0x9000U, 1U);
    mapper->write_prg(0x6123U, 1U);
    mapper->write_prg(0xF008U, 0U);
    for (unsigned cycle = 0U; cycle < 1100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x20UZ);
    REQUIRE(saved[0U] == 0x41U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kPrgBank0Offset] == 0x12U);
    REQUIRE(saved[kPrgBank1Offset] == 0x03U);
    REQUIRE(saved[kChrBanksOffset] == 0x7BU);
    REQUIRE(saved[kMirrorOffset] == 1U);
    REQUIRE(saved[kMicrowireOffset] == 1U);
    REQUIRE(saved[kIrqCounterOffset] == 0x4CU);
    REQUIRE(saved[kIrqCounterOffset + 1U] == 0x04U);
    REQUIRE(saved[kIrqEnabledOffset] == 1U);
    REQUIRE(mapper->irq_pending());

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->read_prg(0x6123U) == 1U);
    require_prg(*copy, 0x8123U, 0x12U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0xA0U);
    require_prg(*copy, 0xA123U, 0x03U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0xA0U);
    require_chr(*copy, 0x0234U, 0x7BU * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    REQUIRE(copy->irq_pending());
    for (unsigned cycle = 0U; cycle < 948U; ++cycle) {
        copy->clock_cpu();
    }
    REQUIRE_FALSE(copy->irq_pending());
    for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
        copy->clock_cpu();
    }
    REQUIRE(copy->irq_pending());
}

TEST_CASE("Mapper 524 rejects truncated damaged and mismatched states atomically",
          "[mapper524][state][atomic][banking][ram][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x12U);
    mapper->write_prg(0xB000U, 0x0BU);
    mapper->write_prg(0xB004U, 0x07U);
    mapper->write_prg(0x6123U, 1U);
    mapper->write_prg(0xF008U, 0U);
    for (unsigned cycle = 0U; cycle < 1100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.push_back(0U);
    load_rejected(*mapper, trailing);
    for (std::size_t offset = 0U; offset < kPrgBank0Offset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 9> bad_fields = {{
        {kPrgBank0Offset, 0x20U},
        {kPrgBank1Offset, 0xFFU},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 0xFFU},
        {kMicrowireOffset, 2U},
        {kIrqCounterOffset + 1U, 0x08U},
        {kIrqCounterOffset + 1U, 0xFFU},
        {kIrqEnabledOffset, 2U},
        {kIrqEnabledOffset, 0xFFU},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kDefaultPrgSize, kDefaultChrSize / 2U);
    load_rejected(*wrong_chr, saved);
    auto wrong_mirror = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kPrgBank0Offset] = 0x03U;
    changed[kChrBanksOffset] ^= 0xFFU;
    changed[kMirrorOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8123U, 0x03U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 524 has no expansion audio RAM or unrelated observer side effects",
          "[mapper524][audio][ram][bus][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x12U);
    mapper->write_prg(0xB000U, 0x0BU);
    mapper->write_prg(0xF008U, 0U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->write_chr(0x0123U, 0xFFU);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

} // namespace
