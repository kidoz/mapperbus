#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper377.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kBankSelectOffset = 11UZ;
constexpr std::size_t kFirstBankOffset = 12UZ;
constexpr std::size_t kMirrorOffset = 20UZ;
constexpr std::size_t kIrqCounterOffset = 21UZ;
constexpr std::size_t kIrqFlagsOffset = 23UZ;
constexpr std::size_t kStateSize = 24UZ;
constexpr std::array<Address, 4> kPrgAddresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 377U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 23U + (offset >> 8U) * 29U + (offset >> 13U) * 41U +
                             (offset >> 17U) * 53U + (offset >> 20U) * 67U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper377> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper377::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper377>(h, rom(prg_size, salt), rom(chr_size, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper377& mapper, unsigned index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

unsigned outer_bank(Byte value) {
    return ((value & 0x20U) >> 3U) | ((value & 0x06U) >> 1U);
}

unsigned expected_prg_bank(Byte outer,
                           Byte select,
                           const std::array<Byte, 8>& banks,
                           Address addr) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (select & 0x40U) != 0U;
    unsigned inner = 31U;
    if (slot == 0U) {
        inner = swapped ? 30U : banks[6U] & 0x1FU;
    } else if (slot == 1U) {
        inner = banks[7U] & 0x1FU;
    } else if (slot == 2U) {
        inner = swapped ? banks[6U] & 0x1FU : 30U;
    }
    return inner | (outer_bank(outer) << 5U);
}

Byte expected_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((banks[slot / 2U] & 0x7EU) | (slot & 1U));
    }
    return banks[slot - 2U] & 0x7FU;
}

TEST_CASE("Mapper 377 validates NES 2.0 pin-bounded ROM and CIRAM geometry", "[mapper377]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg :
             {0x8000UZ, 5UZ * kPrgBankSize, kPrgSize - kPrgBankSize, kPrgSize}) {
            for (const std::size_t chr :
                 {0x2000UZ, 11UZ * kChrBankSize, kChrSize - kChrBankSize, kChrSize}) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper377::validate_geometry(header(mirror), prg, chr));
            }
        }
    }

    for (const unsigned id : {0U, 4U, 267U, 376U, 378U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper377::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper377::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper377::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x8001UZ, kPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper377::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1800UZ, 0x2001UZ, kChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper377::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper377::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
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
        REQUIRE_FALSE(Mapper377::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 377 decodes only 6000 and ordinary MMC3 aliases", "[mapper377][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper377& mapper) {
        mapper.reset();
        mapper.write_prg(0x6000U, 0x26U);
        select_bank(mapper, 7U, 0xABU, 0x40U);
        mapper.write_prg(0xA000U, 1U);
        mapper.write_prg(0xC000U, 2U);
        mapper.write_prg(0xC001U, 0U);
        mapper.write_prg(0xE001U, 0U);
        mapper.clock_irq_counter();
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 43U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw == 0x6000U) {
            expected->write_prg(0x6000U, value);
        } else if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }

    actual->reset();
    for (unsigned value = 0U; value < 256U; ++value) {
        actual->write_prg(0x6000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual)[kOuterOffset] == (value & 0x26U));
    }
}

TEST_CASE("Mapper 377 outer bits select 256 KiB PRG banks without moving CHR",
          "[mapper377][banking]") {
    auto mapper = board();
    select_bank(*mapper, 6U, 7U);
    select_bank(*mapper, 7U, 9U);
    select_bank(*mapper, 2U, 11U);
    const std::array<unsigned, 4> inner{7U, 9U, 30U, 31U};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        const unsigned outer = outer_bank(static_cast<Byte>(value));
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = kPrgAddresses[slot];
            const auto physical = (outer * 32U + inner[slot]) * kPrgBankSize + (addr & 0x1FFFU);
            CAPTURE(value, outer, addr);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        CAPTURE(value, outer);
        REQUIRE(mapper->read_chr(0x1155U) == cell(11UZ * kChrBankSize + 0x155U, 0x35U));
    }
}

TEST_CASE("Mapper 377 applies five-bit inner PRG banks and fixed pages in both modes",
          "[mapper377][banking]") {
    auto mapper = board();
    for (unsigned reg = 6U; reg <= 7U; ++reg) {
        for (const bool swapped : {false, true}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                mapper->write_prg(0x6000U, 0x26U);
                const auto select = static_cast<Byte>(reg | (swapped ? 0x40U : 0U));
                select_bank(*mapper, reg, static_cast<Byte>(value), swapped ? 0x40U : 0U);
                std::array<Byte, 8> banks{};
                banks[reg] = static_cast<Byte>(value & 0x1FU);
                for (const auto addr : kPrgAddresses) {
                    const auto bank = expected_prg_bank(0x26U, select, banks, addr);
                    const auto physical =
                        static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
                    CAPTURE(reg, swapped, value, addr, bank);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }
}

TEST_CASE("Mapper 377 CHR pairing inversion and ROM protection stay inside 128 KiB",
          "[mapper377][banking]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 6U; ++reg) {
        for (const bool inversion : {false, true}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                mapper->write_prg(0x6000U, 0x26U);
                const auto select = static_cast<Byte>(reg | (inversion ? 0x80U : 0U));
                select_bank(*mapper, reg, static_cast<Byte>(value), inversion ? 0x80U : 0U);
                std::array<Byte, 8> banks{};
                banks[reg] = static_cast<Byte>(value & 0x7FU);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto bank = expected_chr_bank(select, banks, addr);
                    const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + 0x155U;
                    CAPTURE(reg, inversion, value, slot, bank);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                            cell(physical, 0x35U));
                    mapper->write_chr(addr, static_cast<Byte>(cell(physical, 0x35U) ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                }
            }
        }
    }
}

TEST_CASE("Mapper 377 wraps complete decoded addresses to fitted bank-aligned ROMs",
          "[mapper377][banking]") {
    constexpr std::size_t prg_size = 5UZ * kPrgBankSize;
    constexpr std::size_t chr_size = 11UZ * kChrBankSize;
    auto mapper = board(prg_size, chr_size);
    mapper->write_prg(0x6000U, 0x26U);
    select_bank(*mapper, 6U, 31U);
    select_bank(*mapper, 7U, 30U);
    select_bank(*mapper, 2U, 127U);
    constexpr std::array<unsigned, 4> inner{31U, 30U, 30U, 31U};
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const unsigned slot = (raw - 0x8000U) / kPrgBankSize;
        const auto physical = (7UZ * 32U + inner[slot]) * kPrgBankSize + (addr & 0x1FFFU);
        CAPTURE(addr, physical);
        REQUIRE(mapper->read_prg(addr) == cell(physical % prg_size));
    }
    for (Address offset = 0U; offset < kChrBankSize; ++offset) {
        const auto physical = 127UZ * kChrBankSize + offset;
        CAPTURE(offset, physical);
        REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) ==
                cell(physical % chr_size, 0x35U));
    }
}

TEST_CASE("Mapper 377 uses only MMC3-controlled ordinary CIRAM routing", "[mapper377][ciram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    for (unsigned raw = 0xA000U; raw <= 0xBFFFU; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), 0U);
        const bool even = (raw & 1U) == 0U;
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() == (even ? MirrorMode::Vertical : MirrorMode::Horizontal));
        mapper->write_prg(static_cast<Address>(raw), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 377 leaves cartridge RAM expansion and audio absent",
          "[mapper377][ram][bus][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x26U);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        if (raw == 0x6000U) {
            continue;
        }
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, static_cast<Byte>(raw * 41U));
        mapper->write_expansion(addr, static_cast<Byte>(raw * 37U));
    }
    REQUIRE(state(*mapper) == before);
    for (const Byte value : {0x00U, 0x40U, 0x80U, 0xC0U}) {
        mapper->write_prg(0xA001U, value);
        REQUIRE(state(*mapper) == before);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
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
    mapper->write_prg_cpu(0x6001U, 0xA5U, true);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 377 IRQ latch reload enable acknowledge and zero repetition",
          "[mapper377][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x6000U, 0x26U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 377 reset restores outer MMC3 IRQ and header CIRAM defaults",
          "[mapper377][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_prg(0x6000U, 0x26U);
    select_bank(*mapper, 6U, 0x2AU, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != state(*board(kPrgSize, kChrSize, MirrorMode::Horizontal)));

    mapper->reset();
    REQUIRE(state(*mapper) == state(*board(kPrgSize, kChrSize, MirrorMode::Horizontal)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));

    mapper->write_prg(0x6000U, 0x26U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board(kPrgSize, kChrSize, MirrorMode::Horizontal)));
}

TEST_CASE("Mapper 377 fixed snapshots replay all state without serializing ROM",
          "[mapper377][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x26U);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 13U + 3U), 0xC0U);
    }
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
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

TEST_CASE("Mapper 377 rejects malformed and incompatible states atomically", "[mapper377][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x26U);
    select_bank(*mapper, 7U, 0x1FU, 0x40U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte reserved : {0x01U, 0x08U, 0x40U, 0x80U, 0xD9U}) {
        auto invalid = saved;
        invalid[kOuterOffset] |= reserved;
        reject(invalid);
    }
    for (const Byte reserved : {0x08U, 0x10U, 0x20U, 0x38U}) {
        auto invalid = saved;
        invalid[kBankSelectOffset] |= reserved;
        reject(invalid);
    }
    for (unsigned index = 0U; index < 6U; ++index) {
        auto invalid = saved;
        invalid[kFirstBankOffset + index] = 0x80U;
        reject(invalid);
    }
    for (const std::size_t offset : {kFirstBankOffset + 6U, kFirstBankOffset + 7U}) {
        for (const Byte value : {0x20U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            reject(invalid);
        }
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {4U, 8U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = value;
        reject(invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
