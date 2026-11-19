#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper431.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kInnerOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 431U;
    result.is_nes2 = true;
    result.submapper = submapper;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 31U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 37U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + (offset >> 20U) * 83U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper431> board(std::size_t prg_size = kPrgSize,
                                 Byte submapper = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper431::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper431>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper431& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

Address outer_address(Byte submapper) {
    return submapper == 3U ? 0x8000U : 0xA000U;
}

Address inner_address(Byte submapper) {
    return submapper == 3U ? 0xC000U : 0x8000U;
}

std::size_t expected_prg(Address addr, Byte outer, Byte inner, std::size_t prg_size = kPrgSize) {
    const unsigned selected = (addr & 0x4000U) == 0U ? inner & 7U : 7U;
    const unsigned bank = ((outer & 7U) << 3U) | selected;
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
}

TEST_CASE("Mapper 431 validates ROM RAM submapper trainer and CIRAM geometry",
          "[mapper431][geometry]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(submapper, mirror);
            auto mapper = board(kPrgSize, submapper, mirror);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(state(*mapper).size() == 8204U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper431::validate_geometry(header(), size, 0U));
    }

    for (const unsigned id : {0U, 2U, 396U, 430U, 432U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper431::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper431::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 4U; submapper < 16U; ++submapper) {
        REQUIRE_FALSE(Mapper431::validate_geometry(header(submapper), kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x100001UZ, 0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper431::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper431::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper431::validate_geometry(header(0U, mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper431::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 431 decodes every outer and inner alias for all submappers",
          "[mapper431][bus][decoding]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto mapper = board(kPrgSize, submapper);
        for (unsigned raw = 0x8000U; raw < 0x10000U; ++raw) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(raw), 0xA5U);
            const bool outer =
                submapper == 3U ? (raw & 0xC000U) == 0x8000U : (raw & 0xE000U) == 0xA000U;
            const auto expected_outer = static_cast<Byte>(outer ? 5U : 0U);
            const auto expected_inner = static_cast<Byte>(outer ? 0U : 5U);
            CAPTURE(submapper, raw, outer);
            REQUIRE(mapper->read_prg(0x8123U) ==
                    cell(expected_prg(0x8123U, expected_outer, expected_inner)));
            REQUIRE(mapper->read_prg(0xC123U) ==
                    cell(expected_prg(0xC123U, expected_outer, expected_inner)));
        }
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(outer_address(submapper), static_cast<Byte>(value));
            CAPTURE(submapper, value);
            REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x67U));
            REQUIRE(state(*mapper)[kInnerOffset] == 0U);
            mapper->write_prg(inner_address(submapper), static_cast<Byte>(value));
            REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x67U));
            REQUIRE(state(*mapper)[kInnerOffset] == (value & 7U));
        }
    }
}

TEST_CASE("Mapper 431 applies outer and inner PRG pins before fitted-ROM wrapping",
          "[mapper431][banking]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto mapper = board(kPrgSize, submapper);
        for (Byte outer = 0U; outer < 8U; ++outer) {
            mapper->write_prg(outer_address(submapper), outer);
            for (Byte inner = 0U; inner < 8U; ++inner) {
                mapper->write_prg(inner_address(submapper), inner);
                for (const Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
                    const auto physical = expected_prg(addr, outer, inner);
                    CAPTURE(submapper, outer, inner, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {0x8000UZ, 0x18000UZ, 0x48000UZ, 0x0FC000UZ}) {
        auto fitted = board(size, 3U);
        fitted->write_prg(0xBFFFU, 7U);
        fitted->write_prg(0xFFFFU, 5U);
        for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
            const auto physical = expected_prg(addr, 7U, 5U, size);
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 431 owns writable unbanked CHR RAM and console CIRAM only",
          "[mapper431][ciram][ram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto value = static_cast<Byte>(cell(addr, 0xA5U));
        mapper->write_chr(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
    }
    const auto populated = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == populated);
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0xA5U));
    mapper->power_on();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == 0U);
    }

    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto arranged = board(kPrgSize, submapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            arranged->write_prg(outer_address(submapper), static_cast<Byte>(value));
            const Byte mask = submapper == 1U ? 0x40U : submapper == 0U ? 0x60U : 0x20U;
            CAPTURE(submapper, value);
            REQUIRE(arranged->mirror_mode() ==
                    ((value & mask) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
        arranged->reset();
        REQUIRE(arranged->mirror_mode() == MirrorMode::Horizontal);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(arranged->maps_nametable(addr));
            REQUIRE_FALSE(arranged->nametable_address(addr));
            REQUIRE_FALSE(arranged->ciram_address(addr));
            REQUIRE_FALSE(arranged->read_nametable(addr));
            REQUIRE_FALSE(arranged->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(arranged->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 431 exposes no CPU RAM expansion device IRQ or audio",
          "[mapper431][bus][ram][irq][audio]") {
    auto mapper = board();
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    const auto cold = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    const auto before = state(*mapper);
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
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 431 soft reset clears latches and cold power also clears CHR RAM",
          "[mapper431][reset][ram]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(kPrgSize, submapper, initial);
            const auto cold = state(*mapper);
            mapper->write_prg(outer_address(submapper), 0x67U);
            mapper->write_prg(inner_address(submapper), 7U);
            mapper->write_chr(0x0123U, 0xA5U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

            mapper->reset();
            REQUIRE(state(*mapper)[kOuterOffset] == 0U);
            REQUIRE(state(*mapper)[kInnerOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(7U * kPrgBankSize));
            REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);

            mapper->write_prg(outer_address(submapper), 0x67U);
            mapper->write_prg(inner_address(submapper), 7U);
            mapper->power_on();
            REQUIRE(state(*mapper) == cold);
            REQUIRE(mapper->read_chr(0x0123U) == 0U);
            REQUIRE(mapper->battery_ram().empty());
        }
    }
}

TEST_CASE("Mapper 431 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper431][state]") {
    auto mapper = board(kPrgSize, 2U);
    mapper->write_prg(0xBFFFU, 0x67U);
    mapper->write_prg(0x9FFFU, 5U);
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        mapper->write_chr(addr, static_cast<Byte>(cell(addr, 0x5AU)));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0x67U);
    REQUIRE(saved[kInnerOffset] == 5U);

    auto copy = board(kPrgSize, 2U, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 0x67U, 5U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0x8000U, 2U);
    mapper->write_chr(0x0123U, 0xCCU);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 8> invalid_fields = {{{0U, 0U},
                                                                             {1U, 0xFFU},
                                                                             {2U, 1U},
                                                                             {6U, 1U},
                                                                             {10U, 0x80U},
                                                                             {10U, 0x98U},
                                                                             {11U, 8U},
                                                                             {11U, 0xFFU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_submapper = board(kPrgSize, 3U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);

    auto wrong_mirror = board(kPrgSize, 2U, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = board(kPrgSize / 2U, 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
