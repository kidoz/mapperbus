#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper379.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x020000UZ;
constexpr std::size_t kChrSize = 0x008000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kOuterBankOffset = 10UZ;
constexpr std::size_t kStateSize = 11UZ;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};
constexpr std::array<Address, 4> kChrAddresses{0x0000U, 0x0123U, 0x1000U, 0x1FFFU};

INesHeader header(bool nes2 = true, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 379U;
    result.is_nes2 = nes2;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 17U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper379> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(true, mirror);
    REQUIRE(Mapper379::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper379>(h, rom(prg_size, salt), rom(chr_size, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 379 validates NES 2.0 pin-bounded ROM and CIRAM geometry", "[mapper379]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = kPrgBankSize; prg <= kPrgSize; prg += kPrgBankSize) {
            for (std::size_t chr = kChrBankSize; chr <= kChrSize; chr += kChrBankSize) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper379::validate_geometry(header(true, mirror), prg, chr));
                auto mapper = board(prg, chr, mirror);
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
            }
        }
    }

    for (const unsigned id : {0U, 38U, 378U, 380U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper379::validate_geometry(h, kPrgSize, kChrSize));
    }
    REQUIRE_FALSE(Mapper379::validate_geometry(header(false), kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper379::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0xA000UZ, 0xC000UZ, kPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper379::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2800UZ, 0x3000UZ, kChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper379::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper379::validate_geometry(header(true, mirror), kPrgSize, kChrSize));
        REQUIRE_FALSE(Mapper379::validate_geometry(header(false, mirror), kPrgSize, kChrSize));
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
        REQUIRE_FALSE(Mapper379::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 379 decodes the complete 8000-FFFF outer register without conflicts",
          "[mapper379][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper379& mapper) {
        mapper.reset();
        mapper.write_prg(0x8000U, 0x0BU);
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(0x8000U, value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }

    actual->reset();
    for (unsigned value = 0U; value < 256U; ++value) {
        actual->write_prg(0xFFFFU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual)[kOuterBankOffset] == (value & 0x0FU));
    }
    actual->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(state(*actual)[kOuterBankOffset] == 0x0FU);
}

TEST_CASE("Mapper 379 selects one 32 KiB PRG bank across both CPU windows",
          "[mapper379][banking]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        const auto bank = static_cast<std::size_t>(value & 3U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = bank * kPrgBankSize + (addr & 0x7FFFU);
            CAPTURE(value, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }

    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xFFFFU, bank);
        for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            const auto physical = static_cast<std::size_t>(bank) * kPrgBankSize + offset;
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 379 selects independent 8 KiB CHR ROM banks and ignores writes",
          "[mapper379][banking]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        const auto bank = static_cast<std::size_t>((value >> 2U) & 3U);
        for (const auto addr : kChrAddresses) {
            const auto physical = bank * kChrBankSize + addr;
            CAPTURE(value, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            mapper->write_chr(addr, static_cast<Byte>(cell(physical, 0x35U) ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
        }
    }

    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(bank << 2U));
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + addr;
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    cell(physical, 0x35U));
            const auto alias_physical =
                static_cast<std::size_t>(bank) * kChrBankSize + ((addr + 0x3FFFU) & 0x1FFFU);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) ==
                    cell(alias_physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 379 wraps only complete decoded addresses to fitted ROM sizes",
          "[mapper379][banking]") {
    for (std::size_t size = kPrgBankSize; size <= kPrgSize; size += kPrgBankSize) {
        auto mapper = board(size, kChrSize);
        mapper->write_prg(0x8000U, 0xFFU);
        for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto physical = 3UZ * kPrgBankSize + offset;
            CAPTURE(size, offset, physical);
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    cell(physical % size));
        }
    }
    for (std::size_t size = kChrBankSize; size <= kChrSize; size += kChrBankSize) {
        auto mapper = board(kPrgSize, size);
        mapper->write_prg(0x8000U, 0xFFU);
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            const auto physical = 3UZ * kChrBankSize + addr;
            CAPTURE(size, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical % size, 0x35U));
        }
    }
}

TEST_CASE("Mapper 379 retains ordinary header-selected CIRAM routing", "[mapper379][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, mirror);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            CAPTURE(mirror, value);
            REQUIRE(mapper->mirror_mode() == mirror);
        }
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
}

TEST_CASE("Mapper 379 leaves CPU RAM expansion IRQ and audio absent",
          "[mapper379][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0FU);
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
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
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

TEST_CASE("Mapper 379 reset and cold power select bank zero", "[mapper379][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0x0FU);
    REQUIRE(state(*mapper) != initial);
    REQUIRE(mapper->read_prg(0x8000U) == cell(3UZ * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == cell(3UZ * kChrBankSize, 0x35U));

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x7FFFU));
    REQUIRE(mapper->read_chr(0x1FFFU) == cell(0x1FFFU, 0x35U));

    mapper->write_prg(0xFFFFU, 0x0EU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 379 bounded snapshots replay the latch without serializing ROM",
          "[mapper379][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0DU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    REQUIRE(saved[kOuterBankOffset] == 0x0DU);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (const auto addr : kChrAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
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

TEST_CASE("Mapper 379 rejects damaged truncated and incompatible states atomically",
          "[mapper379][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0DU);
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
    for (const Byte reserved : {0x10U, 0x20U, 0x40U, 0x80U, 0xF0U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterBankOffset] |= reserved;
        CAPTURE(reserved);
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
