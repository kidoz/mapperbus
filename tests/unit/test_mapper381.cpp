#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper381.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kInnerBankOffset = 10UZ;
constexpr std::size_t kOuterBankOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 381U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper381> board_with_rom(std::vector<Byte> prg,
                                          MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper381::validate_geometry(h, prg.size(), 0U));
    return std::make_unique<Mapper381>(h, std::move(prg), std::vector<Byte>{});
}

std::unique_ptr<Mapper381> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    return board_with_rom(rom(kPrgSize, salt), mirror);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_banks(Mapper381& mapper, Byte inner, Byte outer) {
    auto bytes = state(mapper);
    bytes[kInnerBankOffset] = inner;
    bytes[kOuterBankOffset] = outer;
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

Byte decode_inner(Byte value) {
    return static_cast<Byte>(((value & 0x07U) << 1U) | ((value >> 4U) & 1U));
}

std::size_t expected_offset(Byte inner, Byte outer, Address addr) {
    const Byte selected = (addr & 0x4000U) != 0U ? 0x0FU : inner;
    const auto bank = static_cast<std::size_t>((outer << 4U) | selected);
    return bank * kPrgBankSize + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 381 validates the exact KN-42 NES 2.0 geometry", "[mapper381]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        REQUIRE(Mapper381::validate_geometry(header(mirror), kPrgSize, 0U));
        auto mapper = board(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 2U, 380U, 382U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper381::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper381::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper381::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x004000UZ, 0x020000UZ, 0x040000UZ, 0x07FFFFUZ, 0x080001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper381::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper381::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper381::validate_geometry(header(mirror), kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper381::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 381 decodes the complete 8000-FFFF latch without unrelated CPU mappings",
          "[mapper381][bus]") {
    auto mapper = board_with_rom(std::vector<Byte>(kPrgSize, 0xFFU));
    Byte expected_inner = 0U;
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        if (raw >= 0x8000U) {
            expected_inner = decode_inner(value);
        }
        const auto saved = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(saved[kInnerBankOffset] == expected_inner);
        REQUIRE(saved[kOuterBankOffset] == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE(mapper->read_prg(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }

    mapper->write_prg_cpu(0x8000U, 0x1FU, true);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0x0FU);
    mapper->write_prg_cpu(0x7FFFU, 0U, false);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0x0FU);
}

TEST_CASE("Mapper 381 maps aaab below C000 and fixed bank 15 above it in both outer banks",
          "[mapper381][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 2U; ++outer) {
        for (Byte inner = 0U; inner < 16U; ++inner) {
            set_banks(*mapper, inner, outer);
            for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
                const auto addr = static_cast<Address>(raw);
                const auto physical = expected_offset(inner, outer, addr);
                CAPTURE(outer, inner, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 381 applies AND bus conflicts using the old mapping at every write address",
          "[mapper381][bus][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 2U; ++outer) {
        for (const auto addr : kPrgAddresses) {
            Byte inner = static_cast<Byte>(outer == 0U ? 6U : 9U);
            set_banks(*mapper, inner, outer);
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto rom_byte = cell(expected_offset(inner, outer, addr));
                const auto latched = static_cast<Byte>(value & rom_byte);
                mapper->write_prg(addr, static_cast<Byte>(value));
                inner = decode_inner(latched);
                CAPTURE(outer, addr, value, rom_byte, latched, inner);
                REQUIRE(state(*mapper)[kInnerBankOffset] == inner);
                REQUIRE(state(*mapper)[kOuterBankOffset] == outer);
            }
        }
    }
}

TEST_CASE("Mapper 381 provides flat volatile 8 KiB CHR RAM and fixed H/V CIRAM metadata",
          "[mapper381][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            const auto alias = static_cast<Address>((addr + 0x3FFFU) & 0x1FFFU);
            CAPTURE(mirror, addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) == cell(alias, 0xA5U));
        }
        mapper->write_chr(0x2123U, 0x67U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

        for (const auto addr : kPrgAddresses) {
            mapper->write_prg(addr, 0U);
            mapper->reset();
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->maps_nametable(0x2000U));
            REQUIRE_FALSE(mapper->nametable_address(0x2000U));
            REQUIRE_FALSE(mapper->ciram_address(0x2000U));
            REQUIRE_FALSE(mapper->read_nametable(0x2000U));
            REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0xA5U));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 381 reset toggles only PRG A18 while cold power restores deterministic zero",
          "[mapper381][reset][ram]") {
    auto mapper = board_with_rom(std::vector<Byte>(kPrgSize, 0xFFU), MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    REQUIRE(initial[kInnerBankOffset] == 0U);
    REQUIRE(initial[kOuterBankOffset] == 0U);
    REQUIRE(std::ranges::all_of(std::span(initial).subspan(kChrRamOffset),
                                [](Byte value) { return value == 0U; }));

    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8000U, 0x1FU);
    REQUIRE(state(*mapper)[kInnerBankOffset] == 0x0FU);
    const auto programmed_chr = state(*mapper);

    for (unsigned reset_count = 1U; reset_count <= 8U; ++reset_count) {
        mapper->reset();
        const auto current = state(*mapper);
        CAPTURE(reset_count);
        REQUIRE(current[kInnerBankOffset] == 0x0FU);
        REQUIRE(current[kOuterBankOffset] == (reset_count & 1U));
        REQUIRE(std::ranges::equal(std::span(current).subspan(kChrRamOffset),
                                   std::span(programmed_chr).subspan(kChrRamOffset)));
        REQUIRE(mapper->read_prg(0x8000U) == 0xFFU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 381 leaves cartridge peripheral and persistent hardware absent",
          "[mapper381][bus][ram][irq][audio]") {
    auto mapper = board();
    set_banks(*mapper, 0x0DU, 1U);
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
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
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
}

TEST_CASE("Mapper 381 bounded snapshots replay banks and CHR RAM without serializing ROM",
          "[mapper381][state]") {
    auto mapper = board();
    set_banks(*mapper, 0x0DU, 1U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8204U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kInnerBankOffset] == 0x0DU);
    REQUIRE(saved[kOuterBankOffset] == 1U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
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

TEST_CASE("Mapper 381 rejects damaged truncated and incompatible states atomically",
          "[mapper381][state]") {
    auto mapper = board();
    set_banks(*mapper, 9U, 1U);
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
    for (unsigned bit = 0U; bit < kInnerBankOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (unsigned inner = 16U; inner < 256U; ++inner) {
        auto invalid = saved;
        invalid[kInnerBankOffset] = static_cast<Byte>(inner);
        CAPTURE(inner);
        reject(invalid);
    }
    for (unsigned outer = 2U; outer < 256U; ++outer) {
        auto invalid = saved;
        invalid[kOuterBankOffset] = static_cast<Byte>(outer);
        CAPTURE(outer);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper381>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
