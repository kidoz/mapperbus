#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper493.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x100000UZ;
constexpr std::size_t kPrgWindowSize = 0x8000UZ;
constexpr std::size_t kChrWindowSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kInnerOffset = 11UZ;
constexpr std::size_t kStateSize = 12UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 5> kChrAddresses{0x0000U, 0x0123U, 0x07FFU, 0x1800U, 0x1FFFU};

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 493U;
    result.is_nes2 = true;
    result.mirror_mode = MirrorMode::Horizontal;
    result.prg_rom_size = kRomSize;
    result.chr_rom_size = kRomSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 13U +
                             (offset >> 16U) * 47U + (offset >> 19U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt, bool conflict_free = false) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
        if (conflict_free) {
            data[offset] |= 0x71U;
        }
    }
    return data;
}

std::unique_ptr<Mapper493> board(Byte salt = 0U, bool conflict_free = true) {
    const auto h = header();
    REQUIRE(Mapper493::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper493>(
        h, rom(kRomSize, salt, conflict_free), rom(kRomSize, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper493& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t prg_offset(Address addr, Byte outer, Byte inner) {
    return static_cast<std::size_t>(outer & 0x0FU) * 0x10000UZ +
           static_cast<std::size_t>(inner & 1U) * kPrgWindowSize + (addr & 0x7FFFU);
}

std::size_t chr_offset(Address addr, Byte outer, Byte inner) {
    return static_cast<std::size_t>(outer & 0x0FU) * 0x10000UZ +
           static_cast<std::size_t>((inner >> 4U) & 7U) * kChrWindowSize + (addr & 0x1FFFU);
}

Byte prg_cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(cell(offset, salt) | 0x71U);
}

void require_prg(Mapper493& mapper, Address addr, Byte outer, Byte inner, Byte salt = 0U) {
    const auto physical = prg_offset(addr, outer, inner);
    CAPTURE(addr, outer, inner, physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_prg(addr) == prg_cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper493& mapper, Address addr, Byte outer, Byte inner, Byte salt = 0x55U) {
    const auto physical = chr_offset(addr, outer, inner);
    CAPTURE(addr, outer, inner, physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

TEST_CASE("Mapper 493 validates exact NES 2.0 ROM-only geometry and fixed CIRAM",
          "[mapper493][geometry][ram][ciram]") {
    const auto h = header();
    REQUIRE(Mapper493::validate_geometry(h, kRomSize, kRomSize));
    auto mapper = board(0x35U);
    auto different_rom = board(0x80U);
    REQUIRE(state(*mapper) == state(*different_rom));
    REQUIRE(state(*mapper).size() == kStateSize);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    require_prg(*mapper, 0x8000U, 0U, 0U, 0x35U);
    require_prg(*mapper, 0xFFFFU, 0U, 0U, 0x35U);
    require_chr(*mapper, 0U, 0U, 0U, 0x60U);
    require_chr(*mapper, 0x1FFFU, 0U, 0U, 0x60U);

    const auto reject = [](const INesHeader& candidate, std::size_t prg, std::size_t chr) {
        CAPTURE(candidate.mapper_number,
                candidate.is_nes2,
                candidate.submapper,
                candidate.has_battery,
                candidate.has_trainer,
                candidate.prg_ram_size,
                candidate.prg_nvram_size,
                candidate.chr_ram_size,
                candidate.chr_nvram_size,
                candidate.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper493::validate_geometry(candidate, prg, chr));
    };
    for (const unsigned id : {0U, 11U, 46U, 492U, 494U, 4095U}) {
        auto invalid = header();
        invalid.mapper_number = static_cast<std::uint16_t>(id);
        reject(invalid, kRomSize, kRomSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kRomSize, kRomSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        reject(invalid, kRomSize, kRomSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, 0x080000UZ, kRomSize - 1U, kRomSize + 1U, kRomSize * 2U}) {
        reject(header(), size, kRomSize);
        reject(header(), kRomSize, size);
    }
    for (const auto mirror : {MirrorMode::Vertical,
                              MirrorMode::FourScreen,
                              MirrorMode::SingleLower,
                              MirrorMode::SingleUpper}) {
        auto invalid = header();
        invalid.mirror_mode = mirror;
        reject(invalid, kRomSize, kRomSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.has_trainer = true;
            break;
        case 2U:
            invalid.prg_ram_size = 0x2000U;
            break;
        case 3U:
            invalid.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            invalid.chr_ram_size = 0x2000U;
            break;
        default:
            invalid.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(invalid, kRomSize, kRomSize);
    }
}

TEST_CASE("Mapper 493 decodes both register ranges and leaves other CPU routes undriven",
          "[mapper493][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x6000U && raw < 0x8000U) {
            expected[kOuterOffset] = value & 0x0FU;
        } else if (raw >= 0x8000U) {
            expected[kInnerOffset] = value & 0x71U;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
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
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    mapper->write_prg_cpu(0xFFFFU, 0xFFU, true);
    REQUIRE(state(*mapper)[kInnerOffset] == 0x71U);
}

TEST_CASE("Mapper 493 exhaustively combines outer and inner PRG CHR banking",
          "[mapper493][banking][prg][chr][bus]") {
    auto mapper = board(0x35U);
    for (unsigned outer_raw = 0U; outer_raw < 256U; ++outer_raw) {
        const auto outer = static_cast<Byte>(outer_raw & 0x0FU);
        mapper->write_prg(0x7FFFU, static_cast<Byte>(outer_raw));
        for (unsigned inner_raw = 0U; inner_raw < 256U; ++inner_raw) {
            const auto inner = static_cast<Byte>(inner_raw & 0x71U);
            mapper->write_prg(0xFFFFU, static_cast<Byte>(inner_raw));
            CAPTURE(outer_raw, inner_raw);
            REQUIRE(state(*mapper)[kOuterOffset] == outer);
            REQUIRE(state(*mapper)[kInnerOffset] == inner);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, outer, inner, 0x35U);
            }
            for (const auto addr : kChrAddresses) {
                require_chr(*mapper, addr, outer, inner, 0x60U);
            }
        }
    }

    const std::array<std::pair<Byte, Byte>, 4> edges{{
        {0x00U, 0x00U},
        {0x00U, 0x71U},
        {0x0FU, 0x00U},
        {0x0FU, 0x71U},
    }};
    for (const auto [outer, inner] : edges) {
        mapper->write_prg(0x6000U, outer);
        mapper->write_prg(0x8000U, inner);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; raw += 0x101U) {
            require_prg(*mapper, static_cast<Address>(raw), outer, inner, 0x35U);
        }
        for (Address addr = 0U; addr < kChrWindowSize; ++addr) {
            require_chr(*mapper, addr, outer, inner, 0x60U);
        }
    }
    REQUIRE(prg_offset(0xFFFFU, 0x0FU, 0x01U) == kRomSize - 1U);
    REQUIRE(chr_offset(0x1FFFU, 0x0FU, 0x70U) == kRomSize - 1U);
}

TEST_CASE("Mapper 493 inner writes AND data with the old mapping's driven ROM byte",
          "[mapper493][decoding][banking][bus]") {
    auto mapper = board(0U, false);
    const auto initial = state(*mapper);
    for (const Byte outer : {Byte{0U}, Byte{0x0FU}}) {
        for (const Byte old_inner : {Byte{0U}, Byte{0x71U}}) {
            for (const Address addr :
                 {Address{0x8000U}, Address{0x9234U}, Address{0xC000U}, Address{0xFFFFU}}) {
                for (unsigned data = 0U; data < 256U; ++data) {
                    auto before = initial;
                    before[kOuterOffset] = outer;
                    before[kInnerOffset] = old_inner;
                    load(*mapper, before);
                    const auto driven = mapper->read_prg(addr);
                    const auto inner = static_cast<Byte>(data & driven & 0x71U);
                    mapper->write_prg(addr, static_cast<Byte>(data));
                    CAPTURE(outer, old_inner, addr, data, driven, inner);
                    REQUIRE(state(*mapper)[kOuterOffset] == outer);
                    REQUIRE(state(*mapper)[kInnerOffset] == inner);
                    REQUIRE(mapper->read_prg(0x8000U) == cell(prg_offset(0x8000U, outer, inner)));
                }
            }
        }
    }
}

TEST_CASE("Mapper 493 CHR ROM and Horizontal CIRAM ignore writes and bank changes",
          "[mapper493][chr][ciram][bus]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0U}, Byte{0x0FU}}) {
        for (const Byte inner : {Byte{0U}, Byte{0x70U}}) {
            mapper->write_prg(0x6000U, outer);
            mapper->write_prg(0x8000U, inner);
            for (Address addr = 0U; addr < kChrWindowSize; ++addr) {
                const auto expected = mapper->read_chr(addr);
                mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                CAPTURE(outer, inner, addr);
                REQUIRE(mapper->read_chr(addr) == expected);
            }
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->ciram_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
        }
    }
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xFFU);
    mapper->write_chr(0xFFFFU, 0xFFU);
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 493 reset clears both latches and preserves fixed CIRAM",
          "[mapper493][reset][banking][ciram]") {
    auto mapper = board(0x35U);
    const auto cold = state(*mapper);
    mapper->write_prg(0x6000U, 0xFFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0U, 0U, 0x35U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*mapper, addr, 0U, 0U, 0x60U);
    }

    mapper->write_prg(0x7FFFU, 0x0FU);
    mapper->write_prg(0x8000U, 0x71U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 493 has no cartridge RAM IRQ audio or observer side effects",
          "[mapper493][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x0FU);
    mapper->write_prg(0x8000U, 0x71U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 493 bounded snapshots replay latches without ROM",
          "[mapper493][state][banking][ram]") {
    auto mapper = board(0x35U);
    mapper->write_prg(0x6000U, 0x0FU);
    mapper->write_prg(0xFFFFU, 0x71U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0x0FU);
    REQUIRE(saved[kInnerOffset] == 0x71U);

    auto copy = board(0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x0FU, 0x71U, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 0x0FU, 0x71U, 0xD5U);
    }

    copy->power_on();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 493 rejects truncated damaged and mismatched states atomically",
          "[mapper493][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x0FU);
    mapper->write_prg(0xFFFFU, 0x71U);
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
    for (const Byte outer : {Byte{0x10U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kOuterOffset] = outer;
        CAPTURE(outer);
        reject(invalid);
    }
    for (const Byte inner : {Byte{0x02U}, Byte{0x08U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kInnerOffset] = inner;
        CAPTURE(inner);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper493>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg =
        std::make_unique<Mapper493>(header(), rom(kRomSize / 2U, 0U, true), rom(kRomSize, 0x55U));
    mismatch(wrong_prg);
    auto wrong_chr =
        std::make_unique<Mapper493>(header(), rom(kRomSize, 0U, true), rom(kRomSize / 2U, 0x55U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kOuterOffset] = 0x0AU;
    changed[kInnerOffset] = 0x60U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0x0AU, 0x60U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*mapper, addr, 0x0AU, 0x60U);
    }
}

} // namespace
