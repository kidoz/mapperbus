#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper470.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kMaxPrgSize = 0x1000000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kOuterBankSize = 0x040000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kInnerOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 12> kPrgAddresses{0x8000U,
                                                0x8001U,
                                                0x8123U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xC001U,
                                                0xD234U,
                                                0xE000U,
                                                0xFFFEU,
                                                0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 470U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 20U) * 79U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper470> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    REQUIRE(Mapper470::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper470>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper470& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Byte outer, Byte inner) {
    return static_cast<std::size_t>(outer) * kOuterBankSize + (inner & 0x07U) * kPrgBankSize +
           (addr & 0x7FFFU);
}

void require_prg(Mapper470& mapper,
                 Address addr,
                 Byte outer,
                 Byte inner,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto physical = expected_prg(addr, outer, inner) % prg_size;
    CAPTURE(addr, outer, inner, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 470 validates NES 2.0 geometry and owns only volatile CHR RAM",
          "[mapper470][geometry][ram]") {
    for (std::size_t prg = 0x008000UZ; prg <= kMaxPrgSize; prg *= 2U) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(prg, mirror);
            REQUIRE(Mapper470::validate_geometry(header(mirror), prg, 0U));
        }
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0U, 0x35U);
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
        REQUIRE_FALSE(Mapper470::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 7U, 469U, 471U, 4095U}) {
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
         {0UZ, 1UZ, 0x1000UZ, 0x4000UZ, 0x6000UZ, 0x018000UZ, kMaxPrgSize + 1U, kMaxPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, kChrRamSize, 0x100000UZ}) {
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
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
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
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 470 decodes every outer and AOROM write alias without read claims",
          "[mapper470][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x5000U && raw < 0x6000U) {
            expected[kOuterOffset] = value;
        }
        if (raw >= 0x8000U) {
            expected[kInnerOffset] = value & 0x17U;
        }

        mapper->write_expansion(addr, value);
        mapper->write_prg(addr, value);
        mapper->write_prg_cpu(addr, value, true);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 470 combines full-byte 256 KiB outer and three-bit 32 KiB inner banks",
          "[mapper470][banking][prg]") {
    auto mapper = board();
    for (unsigned raw_outer = 0U; raw_outer < 0x100U; ++raw_outer) {
        const auto outer = static_cast<Byte>(raw_outer);
        mapper->write_expansion(0x5A5AU, outer);
        for (Byte inner = 0U; inner < 8U; ++inner) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(inner | 0xE0U));
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, outer, inner);
            }
        }
    }

    mapper->write_expansion(0x5000U, 0xFFU);
    mapper->write_prg(0x8000U, 0x17U);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 0xFFU, 0x17U);
    }

    constexpr std::size_t kSmallSize = 0x008000UZ;
    auto small = board(MirrorMode::Vertical, 0x35U, kSmallSize);
    small->write_expansion(0x5FFFU, 0xFFU);
    small->write_prg(0xFFFFU, 0xFFU);
    for (const auto addr : kPrgAddresses) {
        require_prg(*small, addr, 0xFFU, 0x17U, 0x35U, kSmallSize);
    }
}

TEST_CASE("Mapper 470 provides writable 8 KiB CHR RAM and software-selected CIRAM",
          "[mapper470][ciram][chr][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(initial, addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
            REQUIRE(mapper->read_chr(addr + 0x2000U) == cell(addr, 0xA5U));
        }
        mapper->write_chr(0x2123U, 0x6CU);
        REQUIRE(mapper->read_chr(0x0123U) == 0x6CU);

        for (unsigned raw = 0U; raw < 0x100U; ++raw) {
            const auto value = static_cast<Byte>(raw);
            mapper->write_prg(0x8000U, value);
            CAPTURE(initial, raw);
            REQUIRE(state(*mapper)[kInnerOffset] == (value & 0x17U));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower));
            REQUIRE(mapper->read_chr(0x0123U) == 0x6CU);
        }
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

TEST_CASE("Mapper 470 has no PRG RAM IRQ audio or observer side effects",
          "[mapper470][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 0xA5U);
    mapper->write_prg(0x8000U, 0x17U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 470 reset clears both bank registers and cold power clears CHR RAM",
          "[mapper470][reset][ram][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        const auto cold = state(*mapper);
        mapper->write_expansion(0x5FFFU, 0xFFU);
        mapper->write_prg(0xFFFFU, 0xFFU);
        mapper->write_chr(0x0123U, 0xA6U);
        require_prg(*mapper, 0xFFFFU, 0xFFU, 0x17U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

        mapper->reset();
        CAPTURE(initial);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);
        REQUIRE(state(*mapper)[kInnerOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA6U);
        require_prg(*mapper, 0xFFFFU, 0U, 0U);

        mapper->write_expansion(0x5000U, 0xAAU);
        mapper->write_prg(0x8000U, 0x17U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    }
}

TEST_CASE("Mapper 470 bounded snapshots replay banks and CHR RAM without ROM bytes",
          "[mapper470][state][banking][ram]") {
    auto mapper = board();
    constexpr Byte kOuter = 0xA5U;
    constexpr Byte kInner = 0x15U;
    mapper->write_expansion(0x5000U, kOuter);
    mapper->write_prg(0xFFFFU, kInner);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == kOuter);
    REQUIRE(saved[kInnerOffset] == kInner);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, kOuter, kInner, 0x80U);
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
    }

    mapper->write_expansion(0x5000U, 0U);
    mapper->write_prg(0x8000U, 0U);
    mapper->write_chr(0U, 0xFFU);
    load(*mapper, saved);
    REQUIRE(state(*mapper) == saved);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 470 rejects truncated damaged and mismatched states atomically",
          "[mapper470][state]") {
    auto mapper = board();
    mapper->write_expansion(0x5FFFU, 0xFEU);
    mapper->write_prg(0xFFFFU, 0x17U);
    mapper->write_chr(0x0123U, 0xA6U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 8> kInvalidFields = {
        {{0U, 1U},
         {1U, 1U},
         {2U, 1U},
         {6U, 1U},
         {kInnerOffset, 0x08U},
         {kInnerOffset, 0x20U},
         {kInnerOffset, 0x80U},
         {kInnerOffset, 0xFFU}}};
    for (const auto& [offset, value] : kInvalidFields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgSize / 2U);
    const auto before = state(*wrong_prg);
    StateReader wrong_reader(saved);
    wrong_prg->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong_prg) == before);

    auto changed = saved;
    changed[kOuterOffset] = 0x12U;
    changed[kInnerOffset] = 0x03U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    require_prg(*mapper, 0xFFFFU, 0x12U, 0x03U);
}

} // namespace
