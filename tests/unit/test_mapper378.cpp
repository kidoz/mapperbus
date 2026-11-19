#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper378.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kOuterBankOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(bool nes2 = true, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 378U;
    result.is_nes2 = nes2;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    result.prg_ram_size = nes2 ? 0U : kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper378> board(bool nes2 = true,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(nes2, mirror);
    REQUIRE(Mapper378::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper378>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

unsigned expected_bank(Byte latch, Address addr) {
    const bool upper = addr >= 0xC000U;
    const unsigned inner = latch & 7U;
    if ((latch & 0x20U) == 0U) {
        return inner * 2U + static_cast<unsigned>(upper);
    }
    if (!upper) {
        return 16U + inner * 2U + ((latch >> 3U) & 1U);
    }
    return 23U + ((latch & 4U) << 1U);
}

MirrorMode expected_mirror(Byte latch) {
    const bool selected = (latch & 0x10U) != 0U;
    if ((latch & 0x20U) == 0U) {
        return selected ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return selected ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

TEST_CASE("Mapper 378 validates legacy and NES 2.0 board geometry", "[mapper378]") {
    for (const bool nes2 : {false, true}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(nes2, mirror);
            REQUIRE(Mapper378::validate_geometry(header(nes2, mirror), kPrgSize, 0U));
            auto mapper = board(nes2, mirror);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }

    for (const auto id : {0U, 2U, 7U, 375U, 377U, 379U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper378::validate_geometry(h, kPrgSize, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper378::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x4000UZ,
                                   0x8000UZ,
                                   0x040000UZ,
                                   kPrgSize - 1UZ,
                                   kPrgSize + 1UZ,
                                   kPrgSize * 2UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper378::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper378::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper378::validate_geometry(header(true, mirror), kPrgSize, 0U));
        REQUIRE_FALSE(Mapper378::validate_geometry(header(false, mirror), kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper378::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy_battery = header(false);
    legacy_battery.has_battery = true;
    REQUIRE_FALSE(Mapper378::validate_geometry(legacy_battery, kPrgSize, 0U));
}

TEST_CASE("Mapper 378 decodes the full 8000-FFFF outer register without conflicts",
          "[mapper378][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper378& mapper) {
        mapper.reset();
        mapper.write_prg(0x8000U, 0x25U);
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
        REQUIRE(state(*actual)[kOuterBankOffset] == (value & 0x3FU));
        REQUIRE(actual->mirror_mode() == expected_mirror(value & 0x3FU));
    }
    actual->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(state(*actual)[kOuterBankOffset] == 0x3FU);
}

TEST_CASE("Mapper 378 applies all AxROM and UNROM PRG address pins", "[mapper378][banking]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        const auto latch = static_cast<Byte>(value & 0x3FU);
        for (const auto addr : kPrgAddresses) {
            const auto bank = expected_bank(latch, addr);
            const auto physical = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
            CAPTURE(value, latch, addr, bank, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }

    for (const Byte latch : {0x00U, 0x08U, 0x0FU, 0x20U, 0x28U, 0x24U, 0x2FU, 0x3FU}) {
        mapper->write_prg(0x8000U, latch);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical =
                static_cast<std::size_t>(expected_bank(latch, addr)) * kPrgBankSize +
                (addr & 0x3FFFU);
            CAPTURE(latch, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 378 uses writable flat 8 KiB CHR RAM in every mode",
          "[mapper378][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) ==
                cell((static_cast<std::size_t>(addr) + 0x3FFFU) & 0x1FFFU, 0xA5U));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        const auto addr = static_cast<Address>(value * 73U % kChrRamSize);
        const auto previous = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(previous ^ 0xFFU));
        CAPTURE(value, addr);
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(previous ^ 0xFFU));
    }
}

TEST_CASE("Mapper 378 wires CIRAM A10 in AxROM and H/V in UNROM modes", "[mapper378][ciram]") {
    for (const bool nes2 : {false, true}) {
        auto mapper = board(nes2, MirrorMode::Horizontal);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            CAPTURE(nes2, value);
            REQUIRE(mapper->mirror_mode() == expected_mirror(value & 0x3FU));
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
}

TEST_CASE("Mapper 378 leaves CPU RAM expansion IRQ and audio absent",
          "[mapper378][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x3FU);
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

TEST_CASE("Mapper 378 reset clears the latch and cold power clears CHR RAM",
          "[mapper378][reset][ram]") {
    auto mapper = board(true, MirrorMode::Horizontal);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8000U, 0x3FU);
    REQUIRE(state(*mapper)[kOuterBankOffset] == 0x3FU);
    const auto programmed_state = state(*mapper);
    const auto programmed_chr = std::span(programmed_state).subspan(kChrRamOffset, kChrRamSize);

    mapper->reset();
    const auto reset_state = state(*mapper);
    REQUIRE(reset_state[kOuterBankOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(reset_state).subspan(kChrRamOffset, kChrRamSize),
                               programmed_chr));
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x7FFFU));
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    mapper->write_chr(0x0123U, 0x67U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 378 bounded snapshots replay latch and CHR RAM without ROM",
          "[mapper378][state]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8000U, 0x3DU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8203U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kOuterBankOffset] == 0x3DU);

    auto copy = board(false, MirrorMode::Horizontal, 0x80U);
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
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
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
    copy->reset();
    mapper->reset();
    REQUIRE(state(*copy) == state(*mapper));
}

TEST_CASE("Mapper 378 rejects damaged truncated and incompatible states atomically",
          "[mapper378][state]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8000U, 0x3DU);
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
    for (unsigned bit = 0U; bit < 80U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x40U, 0x80U, 0xC0U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterBankOffset] |= reserved;
        CAPTURE(reserved);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper378>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
