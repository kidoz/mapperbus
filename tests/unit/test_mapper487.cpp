#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper487.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x180000UZ;
constexpr std::size_t kPrgWindowSize = 0x8000UZ;
constexpr std::size_t kChrWindowSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kNinaOffset = 11UZ;
constexpr std::size_t kColorDreamsOffset = 12UZ;
constexpr std::size_t kStateSize = 13UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 5> kChrAddresses{0x0000U, 0x0123U, 0x07FFU, 0x1800U, 0x1FFFU};

struct Registers {
    Byte outer = 0U;
    Byte nina = 0U;
    Byte color_dreams = 0U;
};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 487U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kRomSize;
    result.chr_rom_size = kRomSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 13U +
                             (offset >> 16U) * 47U + (offset >> 19U) * 59U + salt);
}

std::vector<Byte> rom(Byte salt) {
    std::vector<Byte> data(kRomSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper487> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper487::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper487>(h, rom(salt), rom(static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper487& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t outer_base(const Registers& registers) {
    const bool color_dreams = (registers.outer & 0x20U) != 0U;
    const auto outer =
        static_cast<std::size_t>((registers.outer >> 1U) & (color_dreams ? 0x0FU : 0x07U));
    return (color_dreams ? 0x080000UZ : 0UZ) + outer * 0x10000UZ;
}

std::size_t prg_offset(Address addr, const Registers& registers) {
    const bool color_dreams = (registers.outer & 0x20U) != 0U;
    const bool large = (registers.outer & 0x40U) != 0U;
    const unsigned a15 = !large         ? static_cast<unsigned>(registers.outer & 1U)
                         : color_dreams ? static_cast<unsigned>(registers.color_dreams & 1U)
                                        : static_cast<unsigned>((registers.nina >> 3U) & 1U);
    return outer_base(registers) + static_cast<std::size_t>(a15) * kPrgWindowSize +
           (addr & 0x7FFFU);
}

std::size_t chr_offset(Address addr, const Registers& registers) {
    const bool color_dreams = (registers.outer & 0x20U) != 0U;
    const bool large = (registers.outer & 0x40U) != 0U;
    const Byte inner = color_dreams ? registers.color_dreams : registers.nina;
    const unsigned a15 = !large         ? static_cast<unsigned>(registers.outer & 1U)
                         : color_dreams ? static_cast<unsigned>((inner >> 6U) & 1U)
                                        : static_cast<unsigned>((inner >> 2U) & 1U);
    const unsigned a14_13 = color_dreams ? static_cast<unsigned>((inner >> 4U) & 3U)
                                         : static_cast<unsigned>(inner & 3U);
    return outer_base(registers) + static_cast<std::size_t>(a15) * kPrgWindowSize +
           static_cast<std::size_t>(a14_13) * kChrWindowSize + (addr & 0x1FFFU);
}

void require_prg(Mapper487& mapper, Address addr, const Registers& registers, Byte salt = 0U) {
    const auto physical = prg_offset(addr, registers);
    CAPTURE(addr, registers.outer, registers.nina, registers.color_dreams, physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper487& mapper, Address addr, const Registers& registers, Byte salt = 0x55U) {
    const auto physical = chr_offset(addr, registers);
    CAPTURE(addr, registers.outer, registers.nina, registers.color_dreams, physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

TEST_CASE("Mapper 487 validates exact NES 2.0 ROM-only geometry", "[mapper487][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        const auto h = header(mirror);
        CAPTURE(mirror);
        REQUIRE(Mapper487::validate_geometry(h, kRomSize, kRomSize));
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, Registers{}, 0x35U);
        require_prg(*mapper, 0xFFFFU, Registers{}, 0x35U);
        require_chr(*mapper, 0U, Registers{}, 0x60U);
        require_chr(*mapper, 0x1FFFU, Registers{}, 0x60U);
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
        REQUIRE_FALSE(Mapper487::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 11U, 79U, 234U, 486U, 488U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kRomSize, kRomSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kRomSize, kRomSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kRomSize, kRomSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, 0x080000UZ, 0x100000UZ, kRomSize - 1U, kRomSize + 1U}) {
        reject(header(), size, kRomSize);
        reject(header(), kRomSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kRomSize, kRomSize);
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
        reject(h, kRomSize, kRomSize);
    }
}

TEST_CASE("Mapper 487 exact outer and NINA decode leaves all other NINA-mode writes inert",
          "[mapper487][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw == 0x4180U) {
            expected[kOuterOffset] = value;
        } else if (raw == 0x4100U) {
            expected[kNinaOffset] = value & 0x0FU;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_expansion(addr, value);
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
}

TEST_CASE("Mapper 487 Color Dreams mode excludes NINA writes and decodes the high window",
          "[mapper487][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_expansion(0x4180U, 0x20U);
        auto expected = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U) + 0x5AU);
        if (raw == 0x4180U) {
            expected[kOuterOffset] = value;
        } else if (raw >= 0x8000U) {
            expected[kColorDreamsOffset] = value & 0x71U;
        }

        mapper->write_expansion(addr, value);
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    mapper->reset();
    mapper->write_expansion(0x4100U, 0x0FU);
    mapper->write_prg(0xFFFFU, 0x71U);
    REQUIRE(state(*mapper)[kNinaOffset] == 0x0FU);
    REQUIRE(state(*mapper)[kColorDreamsOffset] == 0U);
    mapper->write_expansion(0x4180U, 0x20U);
    mapper->write_expansion(0x4100U, 0U);
    mapper->write_prg(0x8000U, 0x71U);
    REQUIRE(state(*mapper)[kNinaOffset] == 0x0FU);
    REQUIRE(state(*mapper)[kColorDreamsOffset] == 0x71U);
    mapper->write_expansion(0x4180U, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    REQUIRE(state(*mapper)[kColorDreamsOffset] == 0x71U);
}

TEST_CASE("Mapper 487 exhaustively combines outer and active inner banking",
          "[mapper487][banking][prg][chr][ciram]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    for (unsigned outer_raw = 0U; outer_raw < 256U; ++outer_raw) {
        const auto outer = static_cast<Byte>(outer_raw);
        mapper->write_expansion(0x4180U, outer);
        for (unsigned inner_raw = 0U; inner_raw < 256U; ++inner_raw) {
            const auto inner = static_cast<Byte>(inner_raw);
            Registers registers{outer, 0U, 0U};
            if ((outer & 0x20U) == 0U) {
                mapper->write_expansion(0x4100U, inner);
                registers.nina = inner & 0x0FU;
            } else {
                mapper->write_prg(0xFFFFU, inner);
                registers.color_dreams = inner & 0x71U;
            }

            CAPTURE(outer_raw, inner_raw);
            REQUIRE(mapper->mirror_mode() ==
                    ((outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, registers, 0x35U);
            }
            for (const auto addr : kChrAddresses) {
                require_chr(*mapper, addr, registers, 0x60U);
            }
        }
    }
}

TEST_CASE("Mapper 487 chip bases and full windows preserve physical ROM addresses",
          "[mapper487][banking][prg][chr][bus]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    const std::array<Registers, 8> cases{{
        {0x00U, 0x00U, 0x00U},
        {0x01U, 0x00U, 0x00U},
        {0x0EU, 0x00U, 0x00U},
        {0x0FU, 0x00U, 0x00U},
        {0x20U, 0x00U, 0x00U},
        {0x2EU, 0x00U, 0x00U},
        {0x30U, 0x00U, 0x00U},
        {0x7FU, 0x00U, 0x71U},
    }};
    for (const auto& registers : cases) {
        mapper->reset();
        mapper->write_expansion(0x4180U, registers.outer);
        if ((registers.outer & 0x20U) == 0U) {
            mapper->write_expansion(0x4100U, registers.nina);
        } else {
            mapper->write_prg(0x8000U, registers.color_dreams);
        }
        CAPTURE(registers.outer);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; raw += 0x101U) {
            require_prg(*mapper, static_cast<Address>(raw), registers, 0x35U);
        }
        for (Address addr = 0U; addr < kChrWindowSize; ++addr) {
            require_chr(*mapper, addr, registers, 0x60U);
            mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(chr_offset(addr, registers), 0x60U));
        }
    }

    REQUIRE(prg_offset(0x8000U, Registers{0x0EU, 0U, 0U}) == 0x070000UZ);
    REQUIRE(prg_offset(0x8000U, Registers{0x0FU, 0U, 0U}) == 0x078000UZ);
    REQUIRE(prg_offset(0x8000U, Registers{0x20U, 0U, 0U}) == 0x080000UZ);
    REQUIRE(prg_offset(0x8000U, Registers{0x2EU, 0U, 0U}) == 0x0F0000UZ);
    REQUIRE(prg_offset(0x8000U, Registers{0x30U, 0U, 0U}) == 0x100000UZ);
    REQUIRE(prg_offset(0xFFFFU, Registers{0x7FU, 0U, 0x71U}) == 0x17FFFFUZ);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xFFU);
    mapper->write_chr(0xFFFFU, 0xFFU);
}

TEST_CASE("Mapper 487 uses ordinary switchable CIRAM without custom nametable routes",
          "[mapper487][ciram][bus]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0U}, Byte{0x80U}, Byte{0x7FU}, Byte{0xFFU}}) {
        mapper->write_expansion(0x4180U, outer);
        CAPTURE(outer);
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 487 reset restores deterministic banks and header CIRAM",
          "[mapper487][reset][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto cold = state(*mapper);
        mapper->write_expansion(0x4100U, 0x0FU);
        mapper->write_expansion(0x4180U, 0xFFU);
        mapper->write_prg(0xFFFFU, 0x71U);
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        mapper->write_expansion(0x4180U, static_cast<Byte>(cold[kOuterOffset] ^ 0xFFU));
        mapper->write_expansion(0x4100U, 0x0FU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 487 has no cartridge RAM IRQ audio or observer side effects",
          "[mapper487][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_expansion(0x4100U, 0x0AU);
    mapper->write_expansion(0x4180U, 0xE5U);
    mapper->write_prg(0x8000U, 0x51U);
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
    mapper->write_expansion(0x6000U, 0x5AU);
    mapper->write_prg_cpu(0x6000U, 0x5AU, true);

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

TEST_CASE("Mapper 487 bounded snapshots replay latches without ROM",
          "[mapper487][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    mapper->write_expansion(0x4100U, 0x0FU);
    mapper->write_expansion(0x4180U, 0xFFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0xFFU);
    REQUIRE(saved[kNinaOffset] == 0x0FU);
    REQUIRE(saved[kColorDreamsOffset] == 0x71U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    const Registers registers{0xFFU, 0x0FU, 0x71U};
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, registers, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, registers, 0xD5U);
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

TEST_CASE("Mapper 487 rejects truncated damaged and mismatched states atomically",
          "[mapper487][state][ram][ciram]") {
    auto mapper = board();
    mapper->write_expansion(0x4100U, 0x0FU);
    mapper->write_expansion(0x4180U, 0xFFU);
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
    for (const Byte nina : {Byte{0x10U}, Byte{0x80U}, Byte{0xF0U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kNinaOffset] = nina;
        CAPTURE(nina);
        reject(invalid);
    }
    for (const Byte color : {Byte{0x02U}, Byte{0x04U}, Byte{0x08U}, Byte{0x80U}, Byte{0x8EU}}) {
        auto invalid = saved;
        invalid[kColorDreamsOffset] = color;
        CAPTURE(color);
        reject(invalid);
    }

    auto short_prg = rom(0U);
    short_prg.resize(kRomSize / 2U);
    auto wrong_prg = std::make_unique<Mapper487>(header(), std::move(short_prg), rom(0U));
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto short_chr = rom(0U);
    short_chr.resize(kRomSize / 2U);
    auto wrong_chr = std::make_unique<Mapper487>(header(), rom(0U), std::move(short_chr));
    const auto before_chr = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == before_chr);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before_mirror = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before_mirror);

    auto changed = saved;
    changed[kOuterOffset] = 0x40U;
    changed[kNinaOffset] = 0x03U;
    changed[kColorDreamsOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, Registers{0x40U, 0x03U, 0U});
    }
}

} // namespace
