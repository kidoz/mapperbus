#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper402.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::uint16_t kLowPrgEnable = 0x0800U;
constexpr std::uint16_t kChrWriteEnable = 0x0400U;
constexpr std::uint16_t kHorizontalMirroring = 0x0080U;
constexpr std::uint16_t kNrom128Mode = 0x0040U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 402U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper402> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper402::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper402>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper402& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, std::uint16_t latch) {
    const auto mapped = static_cast<Address>(addr < 0x8000U ? addr + 0x8000U : addr);
    unsigned bank = latch & 0x1FU;
    if ((latch & kNrom128Mode) == 0U) {
        bank = (bank & 0x1EU) | ((mapped & 0x4000U) != 0U ? 1U : 0U);
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (mapped & 0x3FFFU);
}

void write_u16(std::span<Byte> bytes, std::size_t offset, std::uint16_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 402 validates the J-2282 ROM and volatile CHR RAM geometry",
          "[mapper402][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8204U);
    }

    for (const unsigned id : {0U, 4U, 400U, 401U, 403U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper402::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper402::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper402::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x40000UZ, kPrgSize - 1U, kPrgSize + 1U, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper402::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, kChrRamSize, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper402::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper402::validate_geometry(header(mirror), kPrgSize, 0U));
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
            h.chr_nvram_size = kChrRamSize;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper402::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 402 decodes every address pin and ignores write data",
          "[mapper402][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        actual->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw * 41U + (raw >> 8U)));
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(0x8000U | (raw & 0x0CDFU)), 0U);
        }
        CAPTURE(raw);
        REQUIRE(state(*actual) == state(*expected));
    }

    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        actual->reset();
        expected->reset();
        actual->write_prg(0x8800U, 0U);
        expected->write_prg(0x8800U, 0U);
        actual->write_prg(static_cast<Address>(raw), 0xA5U);
        expected->write_prg(static_cast<Address>(raw + 0x8000U), 0x5AU);
        CAPTURE(raw);
        REQUIRE(state(*actual) == state(*expected));
    }

    actual->reset();
    actual->write_prg(0x8800U, 0U);
    const auto before = state(*actual);
    for (Address addr = 0U; addr < 0x6000U; ++addr) {
        actual->write_prg(addr, static_cast<Byte>(addr));
    }
    REQUIRE(state(*actual) == before);
}

TEST_CASE("Mapper 402 implements NROM-128 NROM-256 and low PRG mirroring",
          "[mapper402][banking][bus]") {
    auto mapper = board();
    constexpr std::array<Address, 6> addresses{
        0x8123U, 0xA123U, 0xC123U, 0xE123U, 0x6123U, 0x7123U};
    for (unsigned control = 0U; control < 16U; ++control) {
        for (unsigned bank = 0U; bank < 32U; ++bank) {
            const auto latch = static_cast<std::uint16_t>(
                ((control & 3U) << 6U) | ((control & 4U) << 9U) | ((control & 8U) << 7U) | bank);
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            for (const auto addr : addresses) {
                const bool mapped = addr >= 0x8000U || (latch & kLowPrgEnable) != 0U;
                const auto physical = expected_prg(addr, latch);
                CAPTURE(control, bank, latch, addr, physical, mapped);
                REQUIRE(mapper->maps_prg(addr) == mapped);
                REQUIRE(mapper->prg_read_mask(addr) == (mapped ? 0xFFU : 0U));
                REQUIRE(mapper->read_prg(addr) == (mapped ? cell(physical) : 0U));
            }
        }
    }

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(kPrgBankSize));
    mapper->write_prg(0x801FU, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(30UZ * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(31UZ * kPrgBankSize));
    mapper->write_prg(0x805FU, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(31UZ * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(31UZ * kPrgBankSize));
}

TEST_CASE("Mapper 402 CHR RAM is unbanked writable only under C and reset aware",
          "[mapper402][ram][reset]") {
    auto mapper = board(MirrorMode::Horizontal);
    mapper->write_prg(0x8400U, 0U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x35U));
    }
    mapper->write_prg(0x8000U, 0U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(~cell(addr, 0x35U)));
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x35U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x4000U)) == cell(addr, 0x35U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x6000U)) == cell(addr, 0x35U));
    }

    mapper->write_prg(0x8DDFU, 0U);
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(state(*mapper)[kLatchOffset] == 0U);
    REQUIRE(state(*mapper)[kLatchOffset + 1U] == 0U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
        mapper->write_chr(addr, 0U);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
    }

    mapper->power_on();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 402 uses M for horizontal vertical console CIRAM only", "[mapper402][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        const auto latch = static_cast<std::uint16_t>((value & 1U) << 7U);
        mapper->write_prg(static_cast<Address>(0x8000U | latch), static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 402 exposes no RAM expansion device IRQ or audio",
          "[mapper402][bus][ram][irq][audio]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
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
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->expansion_read_mask(0x5000U) == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
}

TEST_CASE("Mapper 402 bounded snapshots replay latch and CHR RAM without ROM",
          "[mapper402][state]") {
    auto mapper = board();
    constexpr std::uint16_t kLatch = kLowPrgEnable | kChrWriteEnable | kHorizontalMirroring | 0x15U;
    mapper->write_prg(static_cast<Address>(0x8000U | kLatch), 0U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x9234U, 0xC000U, 0xD234U, 0x6123U, 0x7FFFU}) {
        const auto physical = expected_prg(addr, kLatch);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
    }
    mapper->write_prg(0x6000U, 0U);
    copy->write_prg(0x6000U, 0xFFU);
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

TEST_CASE("Mapper 402 rejects malformed and incompatible states atomically", "[mapper402][state]") {
    auto mapper = board();
    mapper->write_prg(0x8DDFU, 0U);
    mapper->write_chr(0x0123U, 0xA5U);
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
    for (std::size_t offset = 0U; offset < kLatchOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const std::uint16_t latch : {0x0200U, 0x1000U, 0x2000U, 0x4000U, 0x8000U, 0xFFFFU}) {
        auto invalid = saved;
        write_u16(invalid, kLatchOffset, latch);
        CAPTURE(latch);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper402>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
