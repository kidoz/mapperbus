#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper448.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kPrgBankOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 14UZ;
constexpr std::size_t kIrqLatchOffset = 15UZ;
constexpr std::size_t kIrqCounterOffset = 16UZ;
constexpr std::size_t kIrqPrescalerOffset = 17UZ;
constexpr std::size_t kIrqControlOffset = 19UZ;
constexpr std::size_t kIrqFlagsOffset = 20UZ;
constexpr std::size_t kChrRamOffset = 21UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 10> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xC123U, 0xDABCU, 0xFFFCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 448U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 37U + (offset >> 20U) * 41U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper448> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    CAPTURE(prg_size, mirror, salt);
    const auto h = header(mirror);
    REQUIRE(Mapper448::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper448>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper448& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Byte outer, Byte bank) {
    if ((outer & 8U) != 0U) {
        const unsigned bank32 = ((outer & 3U) << 3U) | (bank & 7U);
        return bank32 * 0x8000UZ + (addr & 0x7FFFU);
    }
    const unsigned fixed = (outer & 4U) != 0U ? 15U : 7U;
    const unsigned inner = addr < 0xC000U ? bank & 0x0FU : fixed;
    const unsigned bank16 = ((outer & 7U) << 4U) | inner;
    return bank16 * kPrgBankSize + (addr & 0x1FFFU);
}

void require_prg(Mapper448& mapper, Address addr, Byte outer, Byte bank, Byte salt = 0U) {
    const auto physical = expected_prg(addr, outer, bank);
    CAPTURE(addr, outer, bank, physical);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 448 validates decoded geometry memory and CIRAM metadata",
          "[mapper448][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, kMaxPrgSize}) {
            auto mapper = board(size, mirror);
            auto different = board(size, mirror, 0x80U);
            CAPTURE(mirror, size);
            REQUIRE(state(*mapper) == state(*different));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper448::validate_geometry(header(), size, 0U));
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
        REQUIRE_FALSE(Mapper448::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 7U, 25U, 447U, 449U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x200001UZ, 0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x200000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x2000U;
            break;
        case 1U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
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
        case 7U:
            h.chr_nvram_size = kChrRamSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, 0U);
    }
}

TEST_CASE("Mapper 448 outer register latches CPU address without opening a RAM window",
          "[mapper448][decoding][bus][ram]") {
    auto mapper = board(kMinPrgSize);
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw < 0x2000U; ++raw) {
        const auto addr = static_cast<Address>(0x6000U + raw);
        auto expected = cold;
        expected[kOuterOffset] = raw & 0x0FU;
        CAPTURE(raw, addr);
        mapper->write_prg(addr, static_cast<Byte>(raw * 41U + 0xA5U));
        REQUIRE(state(*mapper) == expected);
        mapper->write_prg(addr, static_cast<Byte>(~raw));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (const Address addr : {0x4020U, 0x5FFFU, 0x8000U}) {
        const auto before = state(*mapper);
        mapper->write_prg(addr, 0U);
        if (addr < 0x8000U) {
            REQUIRE(state(*mapper) == before);
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 448 decodes VRC4 A2 A3 and ignores unused banking controls",
          "[mapper448][decoding][bus][ciram]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (const Address addr : {0x8000U, 0x8001U, 0x8002U, 0x8003U, 0x8FFCU}) {
        mapper->write_prg(addr, 0xF5U);
        REQUIRE(state(*mapper)[kPrgBankOffset] == 0x15U);
    }
    mapper->reset();
    for (Address addr = 0xA000U; addr < 0xF000U; addr += 0x101U) {
        if ((addr & 0xF000U) == 0x9000U && (addr & 0x0CU) == 0U) {
            continue;
        }
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(state(*mapper) == cold);
    }

    constexpr std::array modes{MirrorMode::Vertical,
                               MirrorMode::Horizontal,
                               MirrorMode::SingleLower,
                               MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        const auto expected_mirror = static_cast<Byte>(modes[value & 3U]);
        mapper->write_prg(0x9000U | (value & 3U), static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kMirrorOffset] == expected_mirror);
        REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
        mapper->write_prg(0x9004U | (value & 3U), static_cast<Byte>(~value));
        REQUIRE(state(*mapper)[kMirrorOffset] == expected_mirror);
        mapper->write_prg(0x9008U | (value & 3U), static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kMirrorOffset] == expected_mirror);
    }

    mapper->reset();
    mapper->write_prg(0xF000U, 0xAEU);
    mapper->write_prg(0xF004U, 0xBBU);
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 0xBEU);
    mapper->write_prg(0xF001U, 0x01U);
    mapper->write_prg(0xF005U, 0x00U);
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 0x01U);
    mapper->write_prg(0xF008U, 0xFFU);
    REQUIRE(state(*mapper)[kIrqControlOffset] == 7U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 1U);
}

TEST_CASE("Mapper 448 combines outer pins with discrete UNROM UOROM and AOROM banking",
          "[mapper448][banking][prg][decoding]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 16U; ++outer) {
        mapper->write_prg(0x6000U | outer, 0U);
        for (const Byte bank : {Byte{0x05U}, Byte{0x1BU}}) {
            mapper->write_prg(0x8000U, bank);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, outer, bank);
            }
        }
    }

    mapper->write_prg(0x8000U, 0x05U);
    mapper->write_prg(0xA000U, 0x1AU);
    mapper->write_prg(0x9008U, 2U);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0x0FU, 0x05U);
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(size);
        fitted->write_prg(0x600FU, 0U);
        fitted->write_prg(0x8000U, 0xFFU);
        for (const auto addr : kPrgAddresses) {
            const auto physical = expected_prg(addr, 0x0FU, 0x1FU) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 448 uses fixed unbanked writable CHR RAM in every PRG mode",
          "[mapper448][banking][chr][ram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x55U));
    }
    for (Byte outer = 0U; outer < 16U; ++outer) {
        mapper->write_prg(0x6000U | outer, 0U);
        mapper->write_prg(0x8000U, static_cast<Byte>(outer * 5U));
        for (const Address addr :
             {0x0000U, 0x0001U, 0x0123U, 0x03FFU, 0x1000U, 0x1555U, 0x1FFEU, 0x1FFFU}) {
            CAPTURE(outer, addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0x55U));
            REQUIRE(mapper->read_chr(addr | 0x2000U) == cell(addr, 0x55U));
        }
    }
    for (Address addr = 0xB000U; addr < 0xF000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
    }
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x123U, 0x55U));
    mapper->write_chr(0x2123U, 0xA6U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA6U);
}

TEST_CASE("Mapper 448 routes VRC4 and AOROM single-screen CIRAM modes",
          "[mapper448][ciram][decoding][banking]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, initial);
        constexpr std::array modes{MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x9000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
        }
        mapper->write_prg(0x6008U, 0U);
        for (unsigned value = 0U; value < 32U; ++value) {
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower));
        }
        mapper->write_prg(0x9000U, 1U);
        mapper->write_prg(0x8000U, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        mapper->write_prg(0x6000U, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);

        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    }
}

TEST_CASE("Mapper 448 VRC4 IRQ supports cycle prescaled acknowledge and reenable",
          "[mapper448][irq][decoding]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFEU);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);

    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned clock = 0U; clock < 113U; ++clock) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqPrescalerOffset] == 2U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);
    mapper->write_prg(0xF008U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);

    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF008U, 0U);
    const auto disabled = state(*mapper);
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    REQUIRE(state(*mapper) == disabled);
}

TEST_CASE("Mapper 448 reset clears registers but not CHR and has no other bus or audio device",
          "[mapper448][reset][bus][ram][audio][irq]") {
    auto mapper = board(kMinPrgSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    mapper->write_prg(0x600FU, 0U);
    mapper->write_prg(0x8000U, 0x1DU);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->reset();
    const auto reset_state = state(*mapper);
    REQUIRE(std::equal(reset_state.begin(), reset_state.begin() + kChrRamOffset, initial.begin()));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);

    const auto before = state(*mapper);
    for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
        mapper->write_prg(addr, 0xA5U);
    }
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu_falling_edge();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));

    mapper->write_chr(0x0123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 448 bounded snapshots replay banking IRQ and CHR without ROM",
          "[mapper448][state][banking][ram][irq][ciram]") {
    auto mapper = board();
    mapper->write_prg(0x600EU, 0U);
    mapper->write_prg(0x8000U, 0x0DU);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x55U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8213U);
    REQUIRE(saved[kOuterOffset] == 0x0EU);
    REQUIRE(saved[kIrqCounterOffset] == 0xFFU);
    REQUIRE(saved[kIrqFlagsOffset] == 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);

    auto copy = board(kMaxPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleLower);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x0EU, 0x0DU, 0x80U);
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x55U));
    }
    copy->clock_cpu();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());
    copy->write_prg(0x6006U, 0U);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(state(*board(kMinPrgSize)) == state(*board(kMinPrgSize, MirrorMode::Vertical, 0x35U)));
}

TEST_CASE("Mapper 448 rejects truncated damaged and mismatched states atomically",
          "[mapper448][state][ram][irq]") {
    auto mapper = board();
    mapper->write_prg(0x600EU, 0U);
    mapper->write_prg(0x8000U, 0x1DU);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    mapper->write_chr(0x0123U, 0x05U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 14> invalid_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 0U},
        {kOuterOffset, 0x10U},
        {kPrgBankOffset, 0x20U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 5U},
        {kIrqControlOffset, 8U},
        {kIrqFlagsOffset, 4U},
        {kIrqFlagsOffset, 2U},
        {kIrqPrescalerOffset, 0U},
        {kIrqPrescalerOffset + 1U, 2U},
        {11U, 1U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = active;
        invalid[offset] = value;
        if (offset == kIrqPrescalerOffset) {
            invalid[kIrqPrescalerOffset + 1U] = 0U;
        }
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto bad_prescaler = active;
    bad_prescaler[kIrqPrescalerOffset] = 0x56U;
    bad_prescaler[kIrqPrescalerOffset + 1U] = 1U;
    reject(bad_prescaler);

    const auto mismatch = [&](std::unique_ptr<Mapper448>& other) {
        const auto before = state(*other);
        StateReader reader(active);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(kMinPrgSize);
    mismatch(wrong_prg);
    auto wrong_mirror = board(kMaxPrgSize, MirrorMode::Horizontal);
    mismatch(wrong_mirror);

    auto changed = active;
    changed[kOuterOffset] = 3U;
    changed[kPrgBankOffset] = 7U;
    changed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
    changed[kIrqFlagsOffset] = 0U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_chr(0x0123U) == static_cast<Byte>(0x05U ^ 0xFFU));
}

} // namespace
