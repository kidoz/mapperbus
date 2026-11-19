#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper400.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x400000UZ;
constexpr std::size_t kOuterPrgBankSize = 0x020000UZ;
constexpr std::size_t kInnerPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kInnerBankOffset = 10UZ;
constexpr std::size_t kLowerOuterOffset = 11UZ;
constexpr std::size_t kUpperOuterOffset = 12UZ;
constexpr std::size_t kChrBankOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 14UZ;
constexpr std::size_t kLedsOffset = 15UZ;
constexpr std::size_t kLcdControlOffset = 16UZ;
constexpr std::size_t kLcdCommandOffset = 17UZ;
constexpr std::size_t kLcdCountOffset = 18UZ;
constexpr std::size_t kChrRamOffset = 22UZ;
constexpr std::size_t kLcdDataOffset = kChrRamOffset + kChrRamSize;
constexpr std::size_t kStateSize = kLcdDataOffset + Mapper400::kLcdDataSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 400U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 17U) * 59U + (offset >> 21U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper400> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper400::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper400>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper400& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void cpu_write(Mapper400& mapper, Address addr, Byte value) {
    mapper.observe_cpu_write(addr, value);
    mapper.write_prg(addr, value);
}

void start_lcd_transfer(Mapper400& mapper) {
    cpu_write(mapper, 0x6800U, 0U);
    cpu_write(mapper, 0x00FFU, 0x2CU);
    cpu_write(mapper, 0x6800U, 2U);
}

std::size_t expected_prg(Address addr, Byte lower_outer, Byte upper_outer, Byte inner) {
    const bool upper = addr >= 0xC000U;
    const auto outer = upper ? upper_outer : lower_outer;
    return static_cast<std::size_t>(outer) * kOuterPrgBankSize +
           static_cast<std::size_t>(upper ? 7U : inner) * kInnerPrgBankSize + (addr & 0x3FFFU);
}

void write_u32(std::span<Byte> bytes, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 400 validates its fixed ROM and volatile CHR RAM geometry",
          "[mapper400][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->led_outputs() == 0U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 196630U);
    }

    for (const unsigned id : {0U, 30U, 399U, 401U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper400::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper400::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper400::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x200000UZ, kPrgSize - 1U, kPrgSize + 1U, 0x800000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper400::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kChrRamSize, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper400::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper400::validate_geometry(header(mirror), kPrgSize, 0U));
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
            h.chr_ram_size = 0x2000U;
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
        REQUIRE_FALSE(Mapper400::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 400 inner PRG and CHR decode covers every C000-FFFF alias",
          "[mapper400][decoding][banking]") {
    auto mapper = board();
    for (unsigned raw = 0xC000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        const auto inner = static_cast<Byte>(value & 7U);
        const auto chr = static_cast<Byte>((value >> 4U) & 3U);
        CAPTURE(raw, value, inner, chr);
        REQUIRE(mapper->read_prg(0x8123U) == cell(expected_prg(0x8123U, 0U, 0U, inner)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(expected_prg(0xC123U, 0U, 0U, inner)));

        mapper->write_chr(0x0123U, 0xA5U);
        mapper->write_prg(0xC000U, static_cast<Byte>((inner | ((chr + 1U) & 3U) << 4U)));
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
        mapper->write_prg(0xFFFFU, value);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
    }
}

TEST_CASE("Mapper 400 lower outer register checks all fixed low address bits",
          "[mapper400][decoding][banking]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0xF8U);
        const bool decoded = (raw & 0xF83FU) == 0x7000U;
        const auto outer = static_cast<Byte>(decoded ? 0x1FU : 0U);
        CAPTURE(raw, decoded);
        REQUIRE(mapper->read_prg(0x8123U) == cell(expected_prg(0x8123U, outer, 0U, 0U)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(expected_prg(0xC123U, outer, 0U, 0U)));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE("Mapper 400 upper outer register and CIRAM decode covers every low alias",
          "[mapper400][decoding][banking][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0xF8U);
        const bool decoded = (raw & 0xF800U) == 0x7800U;
        const auto outer = static_cast<Byte>(decoded ? 0x1FU : 0U);
        CAPTURE(raw, decoded);
        REQUIRE(mapper->read_prg(0x8123U) == cell(expected_prg(0x8123U, 0U, outer, 0U)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(expected_prg(0xC123U, 0U, outer, 0U)));
        REQUIRE(mapper->mirror_mode() == (decoded ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
}

TEST_CASE("Mapper 400 LCD command decode covers 6800-6FFF only", "[mapper400][decoding][lcd]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0U);
        mapper->observe_cpu_write(0x00FFU, 0x2CU);
        mapper->write_prg(static_cast<Address>(raw), 2U);
        mapper->observe_cpu_write(0x00FFU, 0xA5U);
        const bool decoded = (raw & 0xF800U) == 0x6800U;
        CAPTURE(raw, decoded);
        REQUIRE(mapper->lcd_data_count() == (decoded ? 1U : 0U));
    }
}

TEST_CASE("Mapper 400 uses independent outer banks and fixed inner bank seven",
          "[mapper400][banking][bus]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x7000U, static_cast<Byte>(value));
        const auto lower = static_cast<Byte>((value >> 3U) & 0x1FU);
        CAPTURE(value, lower);
        REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, lower, 0U, 0U)));
        REQUIRE(mapper->read_prg(0xBFFFU) == cell(expected_prg(0xBFFFU, lower, 0U, 0U)));
        REQUIRE(mapper->read_prg(0xC000U) == cell(expected_prg(0xC000U, lower, 0U, 0U)));

        mapper->reset();
        mapper->write_prg(0x7800U, static_cast<Byte>(value));
        const auto upper = static_cast<Byte>((value >> 3U) & 0x1FU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0U, upper, 0U)));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(expected_prg(0xFFFFU, 0U, upper, 0U)));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x10U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }

    mapper->reset();
    mapper->write_prg(0x7000U, 0xF8U);
    mapper->write_prg(0x7800U, 0x80U);
    mapper->write_prg(0xC000U, 7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(31UZ * kOuterPrgBankSize + 7UZ * kInnerPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(16UZ * kOuterPrgBankSize + 7UZ * kInnerPrgBankSize));
}

TEST_CASE("Mapper 400 LED writes are active low conflict free and cover every alias",
          "[mapper400][decoding][led][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw < 0xC000U; ++raw) {
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        CAPTURE(raw, value);
        REQUIRE(mapper->led_outputs() == static_cast<Byte>(~value));
        REQUIRE(mapper->read_prg(0x8123U) == cell(0x0123U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->led_outputs() == static_cast<Byte>(~value));
    }
}

TEST_CASE("Mapper 400 CHR RAM has four complete banks and reset retention",
          "[mapper400][ram][reset]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC000U, static_cast<Byte>(bank << 4U));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            mapper->write_chr(offset, cell(bank * kChrBankSize + offset, 0x35U));
        }
    }
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC000U, static_cast<Byte>(bank << 4U));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == cell(bank * kChrBankSize + offset, 0x35U));
            REQUIRE(mapper->read_chr(static_cast<Address>(offset + 0x2000U)) ==
                    cell(bank * kChrBankSize + offset, 0x35U));
        }
    }

    mapper->write_prg(0x7000U, 0xF8U);
    mapper->write_prg(0x7800U, 0x80U);
    mapper->write_prg(0xC000U, 0x37U);
    mapper->write_prg(0x8000U, 0U);
    start_lcd_transfer(*mapper);
    cpu_write(*mapper, 0x00FFU, 0xA5U);
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->led_outputs() == 0U);
    REQUIRE(mapper->lcd_data_count() == 0U);
    REQUIRE(mapper->lcd_data()[0] == 0xA5U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(7UZ * kInnerPrgBankSize));
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC000U, static_cast<Byte>(bank << 4U));
        REQUIRE(mapper->read_chr(0x123U) == cell(bank * kChrBankSize + 0x123U, 0x35U));
    }

    mapper->power_on();
    REQUIRE(mapper->lcd_data()[0] == 0U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC000U, static_cast<Byte>(bank << 4U));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == 0U);
        }
    }
}

TEST_CASE("Mapper 400 LCD watches only 2C data with bounded big endian raw bytes",
          "[mapper400][lcd][bus]") {
    auto mapper = board();
    cpu_write(*mapper, 0x00FFU, 0x2CU);
    REQUIRE(mapper->lcd_data_count() == 0U);

    start_lcd_transfer(*mapper);
    const std::array<Byte, 6> prefix{0x12U, 0x34U, 0xABU, 0xCDU, 0x00U, 0xFFU};
    const std::array<Address, 6> addresses{0x00FFU, 0x1234U, 0x5000U, 0x9000U, 0x00FFU, 0x0200U};
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        cpu_write(*mapper, addresses[index], prefix[index]);
    }
    REQUIRE(mapper->lcd_data_count() == prefix.size());
    REQUIRE(std::equal(prefix.begin(), prefix.end(), mapper->lcd_data().begin()));

    const auto before_command_write = mapper->lcd_data_count();
    cpu_write(*mapper, 0x6900U, 2U);
    REQUIRE(mapper->lcd_data_count() == before_command_write);

    cpu_write(*mapper, 0x6800U, 0U);
    cpu_write(*mapper, 0x00FFU, 0x11U);
    cpu_write(*mapper, 0x6800U, 2U);
    cpu_write(*mapper, 0x00FFU, 0xA5U);
    REQUIRE(mapper->lcd_data_count() == 0U);

    start_lcd_transfer(*mapper);
    for (std::size_t index = 0U; index < Mapper400::kLcdDataSize; ++index) {
        cpu_write(*mapper, 0x00FFU, cell(index, 0x5AU));
    }
    REQUIRE(mapper->lcd_data_count() == Mapper400::kLcdDataSize);
    for (std::size_t index = 0U; index < Mapper400::kLcdDataSize; ++index) {
        CAPTURE(index);
        REQUIRE(mapper->lcd_data()[index] == cell(index, 0x5AU));
    }
    for (unsigned index = 0U; index < 32U; ++index) {
        cpu_write(*mapper, 0x00FFU, static_cast<Byte>(~index));
    }
    REQUIRE(mapper->lcd_data_count() == Mapper400::kLcdDataSize);
    REQUIRE(mapper->lcd_data()[0] == cell(0U, 0x5AU));
    REQUIRE(mapper->lcd_data()[Mapper400::kLcdDataSize - 1U] ==
            cell(Mapper400::kLcdDataSize - 1U, 0x5AU));
    cpu_write(*mapper, 0x6800U, 3U);
    REQUIRE(mapper->lcd_data_count() == Mapper400::kLcdDataSize);
}

TEST_CASE("Mapper 400 exposes console CIRAM and no CPU RAM IRQ or expansion audio",
          "[mapper400][bus][ciram][ram][irq][audio]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());

    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
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
}

TEST_CASE("Mapper 400 snapshots replay RAM registers LED and LCD without ROM",
          "[mapper400][state]") {
    auto mapper = board();
    mapper->write_prg(0x7000U, 0xA8U);
    mapper->write_prg(0x7800U, 0x59U);
    mapper->write_prg(0xC000U, 0x26U);
    mapper->write_prg(0x8000U, 0x5AU);
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x35U));
    }
    start_lcd_transfer(*mapper);
    for (std::size_t index = 0U; index < 513U; ++index) {
        cpu_write(*mapper, 0x00FFU, cell(index, 0xA5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kInnerBankOffset] == 6U);
    REQUIRE(saved[kLowerOuterOffset] == 21U);
    REQUIRE(saved[kUpperOuterOffset] == 11U);
    REQUIRE(saved[kChrBankOffset] == 2U);
    REQUIRE(saved[kMirrorOffset] == 1U);
    REQUIRE(saved[kLedsOffset] == 0x5AU);
    REQUIRE(saved[kLcdControlOffset] == 2U);
    REQUIRE(saved[kLcdCommandOffset] == 0x2CU);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->led_outputs() == mapper->led_outputs());
    REQUIRE(copy->lcd_data_count() == 513U);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 21U, 11U, 6U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x35U));
    }
    for (std::size_t index = 0U; index < 513U; ++index) {
        REQUIRE(copy->lcd_data()[index] == cell(index, 0xA5U));
    }
    cpu_write(*mapper, 0x00FFU, 0x77U);
    cpu_write(*copy, 0x00FFU, 0x77U);
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

TEST_CASE("Mapper 400 rejects malformed and incompatible states atomically", "[mapper400][state]") {
    auto mapper = board();
    mapper->write_prg(0x7000U, 0xF8U);
    mapper->write_prg(0x7800U, 0x91U);
    mapper->write_prg(0xC000U, 0x37U);
    mapper->write_prg(0x8000U, 0U);
    mapper->write_chr(0x0123U, 0xA5U);
    start_lcd_transfer(*mapper);
    for (std::size_t index = 0U; index < 64U; ++index) {
        cpu_write(*mapper, 0x00FFU, cell(index));
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    std::vector<std::size_t> lengths;
    for (std::size_t length = 0U; length <= kChrRamOffset; ++length) {
        lengths.push_back(length);
    }
    for (std::size_t length = kChrRamOffset + 1U; length < saved.size(); length += 0x4000U) {
        lengths.push_back(length);
    }
    lengths.insert(lengths.end(), {kLcdDataOffset - 1U, kLcdDataOffset, saved.size() - 1U});
    for (const auto length : lengths) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }

    for (std::size_t offset = 0U; offset < kInnerBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 8>{std::pair{kInnerBankOffset, Byte{8U}},
                                                     std::pair{kLowerOuterOffset, Byte{0x20U}},
                                                     std::pair{kUpperOuterOffset, Byte{0xFFU}},
                                                     std::pair{kChrBankOffset, Byte{4U}},
                                                     std::pair{kMirrorOffset, Byte{2U}},
                                                     std::pair{kMirrorOffset, Byte{0xFFU}},
                                                     std::pair{kLcdControlOffset, Byte{4U}},
                                                     std::pair{kLcdControlOffset, Byte{0xFFU}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    for (const std::uint32_t count :
         {static_cast<std::uint32_t>(Mapper400::kLcdDataSize) + 1U, 0xFFFFFFFFU}) {
        auto invalid = saved;
        write_u32(invalid, kLcdCountOffset, count);
        CAPTURE(count);
        reject(invalid);
    }
    auto wrong_command = saved;
    wrong_command[kLcdCommandOffset] = 0x11U;
    reject(wrong_command);
    wrong_command = saved;
    wrong_command[kLcdCommandOffset] = 0x11U;
    write_u32(wrong_command, kLcdCountOffset, 0U);
    load(*mapper, wrong_command);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = std::make_unique<Mapper400>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
