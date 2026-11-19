#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper382.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kSmallPrgSize = 0x080000UZ;
constexpr std::size_t kLargePrgSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kOuterBankOffset = 10UZ;
constexpr std::size_t kInnerBankOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A5AU, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 382U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 18U) * 59U + (offset >> 20U) * 61U +
                             salt);
}

std::vector<Byte> rom(std::size_t size = kLargePrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper382> board_with_rom(std::vector<Byte> prg,
                                          MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper382::validate_geometry(h, prg.size(), 0U));
    return std::make_unique<Mapper382>(h, std::move(prg), std::vector<Byte>{});
}

std::unique_ptr<Mapper382> board(std::size_t prg_size = kLargePrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    return board_with_rom(rom(prg_size, salt), mirror);
}

std::unique_ptr<Mapper382> open_board() {
    return board_with_rom(std::vector<Byte>(kLargePrgSize, 0xFFU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_latches(Mapper382& mapper, Byte outer, Byte inner) {
    auto bytes = state(mapper);
    bytes[kOuterBankOffset] = outer;
    bytes[kInnerBankOffset] = inner;
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void program_latches(Mapper382& mapper, Byte outer, Byte inner) {
    mapper.reset();
    const auto addr = static_cast<Address>(0x8000U | outer);
    mapper.write_prg(addr, 0xFFU);
    mapper.write_prg(addr, inner);
}

std::size_t expected_offset(Byte outer, Byte inner, Address addr) {
    const auto base = static_cast<std::size_t>(outer & 0x07U) * 0x020000UZ;
    if ((outer & 0x08U) != 0U) {
        return base + static_cast<std::size_t>(inner & 0x03U) * 0x8000UZ + (addr & 0x7FFFU);
    }
    const Byte bank = addr < 0xC000U ? inner : 0x07U;
    return base + static_cast<std::size_t>(bank) * 0x4000UZ + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 382 validates both documented 830928C NES 2.0 populations", "[mapper382]") {
    for (const std::size_t prg_size : {kSmallPrgSize, kLargePrgSize}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(prg_size, mirror);
            REQUIRE(Mapper382::validate_geometry(header(mirror), prg_size, 0U));
            auto mapper = board(prg_size, mirror);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }

    for (const unsigned id : {0U, 2U, 380U, 381U, 383U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper382::validate_geometry(h, kLargePrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper382::validate_geometry(legacy, kLargePrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper382::validate_geometry(h, kLargePrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x004000UZ,
                                   0x040000UZ,
                                   0x07FFFFUZ,
                                   0x080001UZ,
                                   0x0C0000UZ,
                                   0x0FFFFFUZ,
                                   0x100001UZ,
                                   0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper382::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper382::validate_geometry(header(), kLargePrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper382::validate_geometry(header(mirror), kLargePrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper382::validate_geometry(h, kLargePrgSize, 0U));
    }
}

TEST_CASE("Mapper 382 decodes address A5-A0 and data D2-D0 over the complete CPU range",
          "[mapper382][bus]") {
    auto mapper = open_board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        const auto saved = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(saved[kOuterBankOffset] == (raw >= 0x8000U ? raw & 0x3FU : 0U));
        REQUIRE(saved[kInnerBankOffset] == (raw >= 0x8000U ? value & 0x07U : 0U));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE(mapper->read_prg(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }

    mapper->write_prg_cpu(0xFFFFU, 0xA6U, true);
    REQUIRE(state(*mapper)[kOuterBankOffset] == 0x3FU);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0x06U);
    mapper->write_prg_cpu(0x7FFFU, 0U, false);
    REQUIRE(state(*mapper)[kOuterBankOffset] == 0x3FU);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0x06U);
}

TEST_CASE("Mapper 382 lock retains the entire outer register until reset", "[mapper382][bus]") {
    auto mapper = open_board();
    mapper->write_prg(0x803BU, 0xA5U);
    Byte expected_inner = 0x05U;
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        mapper->write_prg(addr, value);
        if (raw >= 0x8000U) {
            expected_inner = value & 0x07U;
        }
        const auto saved = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(saved[kOuterBankOffset] == 0x3BU);
        REQUIRE(saved[kInnerBankOffset] == expected_inner);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->reset();
    REQUIRE(state(*mapper)[kOuterBankOffset] == 0U);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0U);
}

TEST_CASE("Mapper 382 applies every PRG pin before wrapping to either fitted ROM size",
          "[mapper382][banking]") {
    for (const std::size_t prg_size : {kSmallPrgSize, kLargePrgSize}) {
        auto programmed = open_board();
        auto mapper = board(prg_size);
        for (Byte outer = 0U; outer < 64U; ++outer) {
            for (Byte inner = 0U; inner < 8U; ++inner) {
                program_latches(*programmed, outer, inner);
                set_latches(*mapper, outer, inner);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = expected_offset(outer, inner, addr);
                    CAPTURE(prg_size, outer, inner, addr, physical);
                    REQUIRE(programmed->read_prg(addr) == 0xFFU);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % prg_size));
                }
            }
        }

        for (const Byte outer : {0x00U, 0x07U, 0x08U, 0x0FU, 0x10U, 0x3BU, 0x3FU}) {
            for (const Byte inner : {0U, 1U, 3U, 7U}) {
                set_latches(*mapper, outer, inner);
                for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
                    const auto addr = static_cast<Address>(raw);
                    const auto physical = expected_offset(outer, inner, addr);
                    CAPTURE(prg_size, outer, inner, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % prg_size));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }
}

TEST_CASE("Mapper 382 bus conflicts sample the old mapping whether locked or unlocked",
          "[mapper382][bus][banking]") {
    auto mapper = board();
    for (const Byte initial_outer : {0x05U, 0x0DU, 0x25U, 0x3DU}) {
        for (const auto addr : kPrgAddresses) {
            Byte inner = initial_outer & 0x07U;
            Byte outer = initial_outer;
            set_latches(*mapper, outer, inner);
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto physical = expected_offset(outer, inner, addr);
                const auto rom_byte = cell(physical);
                const auto latched = static_cast<Byte>(value & rom_byte);
                mapper->write_prg(addr, static_cast<Byte>(value));
                if ((outer & 0x20U) == 0U) {
                    outer = static_cast<Byte>(addr) & 0x3FU;
                }
                inner = latched & 0x07U;
                const auto saved = state(*mapper);
                CAPTURE(initial_outer, addr, value, physical, rom_byte, latched, outer, inner);
                REQUIRE(saved[kOuterBankOffset] == outer);
                REQUIRE(saved[kInnerBankOffset] == inner);
            }
        }
    }
}

TEST_CASE("Mapper 382 provides flat volatile CHR RAM and outer-latch CIRAM control",
          "[mapper382][ram][ciram]") {
    auto mapper = board(kLargePrgSize, MirrorMode::Horizontal);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        const auto alias = static_cast<Address>((addr + 0x3FFFU) & 0x1FFFU);
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) == cell(alias, 0xA5U));
    }

    for (Byte outer = 0U; outer < 64U; ++outer) {
        program_latches(*mapper, outer, 0U);
        CAPTURE(outer);
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 382 reset restores zero latches and cold power also clears CHR RAM",
          "[mapper382][reset][ram]") {
    auto mapper = open_board();
    const auto initial = state(*mapper);
    REQUIRE(initial[kOuterBankOffset] == 0U);
    REQUIRE(initial[kInnerBankOffset] == 0U);
    REQUIRE(std::ranges::all_of(std::span(initial).subspan(kChrRamOffset),
                                [](Byte value) { return value == 0U; }));

    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto programmed = state(*mapper);
    REQUIRE(programmed[kOuterBankOffset] == 0x3FU);
    REQUIRE(programmed[kInnerBankOffset] == 0x07U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->reset();
    const auto reset = state(*mapper);
    REQUIRE(reset[kOuterBankOffset] == 0U);
    REQUIRE(reset[kInnerBankOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset),
                               std::span(programmed).subspan(kChrRamOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == 0xFFU);

    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 382 leaves cartridge CPU RAM peripheral IRQ and audio hardware absent",
          "[mapper382][bus][ram][irq][audio]") {
    auto mapper = open_board();
    mapper->write_prg(0xFFFFU, 0xA5U);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
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
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));

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

TEST_CASE("Mapper 382 bounded snapshots replay latches and CHR RAM without serializing ROM",
          "[mapper382][state]") {
    auto mapper = board();
    set_latches(*mapper, 0x3DU, 0x05U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8204U);
    REQUIRE(saved.size() < kSmallPrgSize);
    REQUIRE(saved[kOuterBankOffset] == 0x3DU);
    REQUIRE(saved[kInnerBankOffset] == 0x05U);

    auto copy = board(kLargePrgSize, MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
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

TEST_CASE("Mapper 382 rejects damaged truncated and incompatible states atomically",
          "[mapper382][state]") {
    auto mapper = board();
    set_latches(*mapper, 0x3DU, 0x05U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
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
    for (unsigned bit = 0U; bit < kOuterBankOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (unsigned outer = 0x40U; outer < 256U; ++outer) {
        auto invalid = saved;
        invalid[kOuterBankOffset] = static_cast<Byte>(outer);
        CAPTURE(outer);
        reject(invalid);
    }
    for (unsigned inner = 0x08U; inner < 256U; ++inner) {
        auto invalid = saved;
        invalid[kInnerBankOffset] = static_cast<Byte>(inner);
        CAPTURE(inner);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper382>(header(), rom(kSmallPrgSize), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
