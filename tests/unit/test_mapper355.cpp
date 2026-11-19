#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper355.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x8000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kAddressPinsOffset = 10UZ;
constexpr std::size_t kPcOffset = 12UZ;
constexpr std::size_t kWOffset = 14UZ;
constexpr std::size_t kRamOffset = 15UZ;
constexpr std::size_t kStackOffset = 47UZ;
constexpr std::size_t kStackDepthOffset = 51UZ;
constexpr std::size_t kTrisaOffset = 52UZ;
constexpr std::size_t kTimerDividerOffset = 55UZ;
constexpr std::size_t kClockDividerOffset = 57UZ;
constexpr std::size_t kSleepingOffset = 58UZ;
constexpr std::size_t kChrRamOffset = 59UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr Byte kCarry = 0x01U;
constexpr Byte kDigitCarry = 0x02U;
constexpr Byte kZero = 0x04U;

using PicRom = Mapper355::PicRom;

constexpr std::uint16_t movwf(Byte file) {
    return static_cast<std::uint16_t>(0x020U | file);
}

constexpr std::uint16_t file_op(std::uint16_t op, Byte file, bool to_file = false) {
    return static_cast<std::uint16_t>(op | (to_file ? 0x20U : 0U) | file);
}

constexpr std::uint16_t bit_op(std::uint16_t op, Byte bit, Byte file) {
    return static_cast<std::uint16_t>(op | (bit << 5U) | file);
}

PicRom program(std::initializer_list<std::uint16_t> instructions) {
    PicRom result{};
    std::size_t index = 0U;
    for (const auto instruction : instructions) {
        REQUIRE(index < result.size());
        result[index++] = instruction;
    }
    return result;
}

PicRom quiet_program() {
    return program({0x0C0EU, 0x006U, bit_op(0x500U, 0U, 5U), 0x0A03U});
}

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 355U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper355> board(const PicRom& firmware,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper355::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper355>(h, rom(salt), std::vector<Byte>{}, firmware);
}

std::unique_ptr<Mapper355> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    return board(quiet_program(), mirror, salt);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void step(Mapper355& mapper, unsigned instructions = 1U) {
    for (unsigned index = 0U; index < instructions * 4U; ++index) {
        mapper.clock_cpu();
    }
}

Byte port_b(Address addr) {
    return static_cast<Byte>(((addr >> 12U) & 1U) | ((addr >> 6U) & 2U) | ((addr >> 8U) & 4U) |
                             ((addr >> 8U) & 8U) | ((addr >> 5U) & 0x10U) | ((addr >> 3U) & 0x20U) |
                             ((addr >> 7U) & 0x40U) | ((addr >> 7U) & 0x80U));
}

Byte port_a(Address addr) {
    return static_cast<Byte>(1U | ((addr >> 5U) & 2U) | ((addr >> 3U) & 4U) | ((addr >> 1U) & 8U));
}

TEST_CASE("Mapper 355 validates NROM PIC RAM and CIRAM geometry", "[mapper355]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper355::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (const auto id : {0U, 2U, 354U, 356U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper355::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper355::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper355::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper355::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper355::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper355::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
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
        REQUIRE_FALSE(Mapper355::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 355 decodes exactly 512 little-endian 12-bit PIC words", "[mapper355]") {
    std::vector<Byte> bytes(1024U);
    for (std::size_t index = 0U; index < 512U; ++index) {
        const auto word = static_cast<std::uint16_t>((index * 7U) & 0x0FFFU);
        bytes[index * 2U] = static_cast<Byte>(word);
        bytes[index * 2U + 1U] = static_cast<Byte>(word >> 8U);
    }
    const auto decoded = Mapper355::decode_pic_rom(bytes);
    REQUIRE(decoded);
    for (std::size_t index = 0U; index < decoded->size(); ++index) {
        REQUIRE((*decoded)[index] == (index * 7U));
    }
    for (const std::size_t size : {0UZ, 1UZ, 1022UZ, 1023UZ, 1025UZ, 2048UZ}) {
        REQUIRE_FALSE(
            Mapper355::decode_pic_rom(std::span(bytes).first(size < bytes.size() ? size : 0UZ)));
        if (size > bytes.size()) {
            auto oversized = bytes;
            oversized.resize(size);
            REQUIRE_FALSE(Mapper355::decode_pic_rom(oversized));
        }
    }
    bytes[1] = 0x10U;
    REQUIRE_FALSE(Mapper355::decode_pic_rom(bytes));
    bytes[1] = 0x0FU;
    REQUIRE(Mapper355::decode_pic_rom(bytes));
}

TEST_CASE("Mapper 355 is fixed NROM-256 with flat writable CHR RAM", "[mapper355][bus]") {
    auto mapper = board(MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        if (addr < 0x8000U) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
        } else {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            REQUIRE(mapper->read_prg(addr) == cell(addr & 0x7FFFU));
        }
        mapper->write_prg(addr, static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == initial);
    mapper->write_prg_cpu(0xFFFFU, 0xFFU, true);
    REQUIRE(state(*mapper) == initial);

    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
    }
    REQUIRE(mapper->read_chr(0x0123U) == mapper->read_chr(0x2123U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE(mapper->maps_nametable(0x3EFFU));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->unmirrored_nametables());

    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 355 PIC observes only documented address pins on reads and writes",
          "[mapper355][irq][bus]") {
    constexpr Address target = 0x5A50U;
    const auto expected_b = port_b(target);
    const auto expected_a = static_cast<Byte>(port_a(target) & 0x0EU);
    auto firmware = program({0x0C0EU,
                             0x006U,
                             bit_op(0x500U, 0U, 5U),
                             file_op(0x200U, 6U),
                             static_cast<std::uint16_t>(0x0F00U | expected_b),
                             bit_op(0x700U, 2U, 3U),
                             0x0A0EU,
                             file_op(0x200U, 5U),
                             0x0E0EU,
                             static_cast<std::uint16_t>(0x0F00U | expected_a),
                             bit_op(0x700U, 2U, 3U),
                             0x0A0EU,
                             bit_op(0x400U, 0U, 5U),
                             0x0A03U,
                             bit_op(0x500U, 0U, 5U),
                             0x0A03U});
    auto mapper = board(firmware);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto data = static_cast<Byte>(raw * 37U + (raw >> 8U));
        if ((raw & 1U) == 0U) {
            mapper->observe_cpu_read(addr, data);
        } else {
            mapper->observe_cpu_write(addr, data);
        }
        step(*mapper, 20U);
        CAPTURE(raw, data);
        REQUIRE(mapper->irq_pending() == ((raw & 0x7FF0U) == target));
    }

    mapper->observe_cpu_read(target, 0U);
    step(*mapper, 20U);
    REQUIRE(mapper->irq_pending());
    mapper->read_prg(target ^ 0x7FF0U);
    mapper->write_prg(target ^ 0x7FF0U, 0xFFU);
    step(*mapper, 20U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 355 executes one PIC instruction per four M2 cycles", "[mapper355][irq]") {
    auto firmware =
        program({0x0C0EU, 0x006U, bit_op(0x500U, 0U, 5U), bit_op(0x400U, 0U, 5U), 0x0A02U});
    auto mapper = board(firmware);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[kPcOffset] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    step(*mapper);
    REQUIRE(mapper->irq_pending());
    step(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
        mapper->clock_audio();
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    step(*mapper);
    step(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 355 PIC implements arithmetic logic file and literal operations", "[mapper355]") {
    auto firmware = program({0x0C0FU,
                             movwf(8U),
                             0x0C01U,
                             file_op(0x1C0U, 8U),
                             movwf(9U),
                             0x0CF0U,
                             file_op(0x1C0U, 9U),
                             0x0C01U,
                             file_op(0x080U, 9U),
                             0x0FFFU,
                             0x0D0FU,
                             0x0EAAU,
                             movwf(10U),
                             file_op(0x240U, 10U),
                             file_op(0x280U, 10U, true),
                             file_op(0x0C0U, 10U),
                             file_op(0x380U, 10U),
                             file_op(0x100U, 10U),
                             file_op(0x140U, 10U),
                             file_op(0x180U, 10U),
                             file_op(0x060U, 10U),
                             0x040U});
    auto mapper = board(firmware);
    step(*mapper, 22U);
    const auto bytes = state(*mapper);
    REQUIRE(bytes[kWOffset] == 0U);
    REQUIRE(bytes[kRamOffset + 8U] == 0x0FU);
    REQUIRE(bytes[kRamOffset + 9U] == 0x10U);
    REQUIRE(bytes[kRamOffset + 10U] == 0U);
    REQUIRE((bytes[kRamOffset + 3U] & kCarry) != 0U);
    REQUIRE((bytes[kRamOffset + 3U] & kDigitCarry) == 0U);
    REQUIRE((bytes[kRamOffset + 3U] & kZero) != 0U);
}

TEST_CASE("Mapper 355 PIC implements rotates bit tests and skip instructions", "[mapper355]") {
    auto firmware = program({0x0C01U,
                             movwf(8U),
                             bit_op(0x500U, 0U, 3U),
                             file_op(0x340U, 8U),
                             movwf(9U),
                             bit_op(0x400U, 0U, 3U),
                             file_op(0x300U, 9U),
                             bit_op(0x400U, 0U, 8U),
                             bit_op(0x600U, 0U, 8U),
                             0x0C5AU,
                             bit_op(0x700U, 0U, 8U),
                             0x0CA5U,
                             bit_op(0x500U, 0U, 8U),
                             bit_op(0x600U, 0U, 8U),
                             0x0CFFU,
                             bit_op(0x700U, 0U, 8U),
                             0x0CEEU,
                             file_op(0x280U, 8U, true),
                             file_op(0x2C0U, 8U, true),
                             0x0C11U,
                             file_op(0x2C0U, 8U, true),
                             0x0C22U,
                             file_op(0x3C0U, 8U, true),
                             0x0C33U});
    auto mapper = board(firmware);
    step(*mapper, 24U);
    const auto bytes = state(*mapper);
    REQUIRE(bytes[kWOffset] == 0x33U);
    REQUIRE(bytes[kRamOffset + 8U] == 1U);
    REQUIRE(bytes[kRamOffset + 9U] == 3U);
    REQUIRE((bytes[kRamOffset + 3U] & kCarry) != 0U);
}

TEST_CASE("Mapper 355 PIC implements indirect access calls and PCL page writes", "[mapper355]") {
    auto indirect = program({0x0C12U,
                             movwf(4U),
                             0x0CA6U,
                             movwf(0U),
                             file_op(0x200U, 0U),
                             file_op(0x280U, 4U, true),
                             movwf(0U),
                             0x0910U,
                             movwf(20U),
                             file_op(0x200U, 0x12U),
                             file_op(0x200U, 0x13U)});
    indirect[16] = 0x0C5AU;
    indirect[17] = 0x08C3U;
    auto mapper = board(indirect);
    step(*mapper, 13U);
    auto bytes = state(*mapper);
    REQUIRE(bytes[kRamOffset + 0x12U] == 0xA6U);
    REQUIRE(bytes[kRamOffset + 0x13U] == 0xA6U);
    REQUIRE(bytes[kRamOffset + 20U] == 0xC3U);
    REQUIRE(bytes[kWOffset] == 0xA6U);
    REQUIRE(bytes[kStackDepthOffset] == 0U);

    PicRom pcl{};
    pcl[0] = bit_op(0x500U, 5U, 3U);
    pcl[1] = 0x0C34U;
    pcl[2] = movwf(2U);
    pcl[0x134] = 0x0C77U;
    mapper = board(pcl);
    step(*mapper, 4U);
    bytes = state(*mapper);
    REQUIRE(bytes[kWOffset] == 0x77U);
    REQUIRE(bytes[kPcOffset] == 0x35U);
    REQUIRE(bytes[kPcOffset + 1U] == 0x01U);

    PicRom call_page{};
    call_page[0x100] = 0x09FFU;
    call_page[0x101] = 0x0A02U;
    call_page[0x0FF] = 0x08A7U;
    call_page[0] = 0x0B00U;
    mapper = board(call_page);
    step(*mapper, 3U);
    bytes = state(*mapper);
    REQUIRE(bytes[kWOffset] == 0xA7U);
    REQUIRE(bytes[kPcOffset] == 0x01U);
    REQUIRE(bytes[kPcOffset + 1U] == 0x01U);
    REQUIRE(bytes[kStackDepthOffset] == 0U);
}

TEST_CASE("Mapper 355 PIC clocks TMR0 through its selected prescaler", "[mapper355]") {
    auto firmware = program({0x0C00U,
                             0x002U,
                             file_op(0x060U, 1U),
                             0x000U,
                             0x0C03U,
                             0x002U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x000U,
                             0x0C20U,
                             0x002U,
                             0x000U,
                             0x000U});
    auto mapper = board(firmware);
    step(*mapper, 4U);
    REQUIRE(state(*mapper)[kRamOffset + 1U] == 2U);
    step(*mapper, 2U);
    REQUIRE(state(*mapper)[kRamOffset + 1U] == 3U);
    step(*mapper, 6U);
    REQUIRE(state(*mapper)[kRamOffset + 1U] == 3U);
    step(*mapper);
    REQUIRE(state(*mapper)[kRamOffset + 1U] == 4U);
    const auto before_external = state(*mapper)[kRamOffset + 1U];
    step(*mapper, 4U);
    REQUIRE(state(*mapper)[kRamOffset + 1U] == before_external);
}

TEST_CASE("Mapper 355 has no expansion audio or independent IRQ acknowledgement",
          "[mapper355][irq][audio]") {
    auto mapper = board();
    step(*mapper, 4U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 355 soft reset retains RAM and cold power clears volatile state",
          "[mapper355][reset]") {
    auto firmware = program({0x0C0EU,
                             0x006U,
                             bit_op(0x500U, 0U, 5U),
                             0x0CA6U,
                             movwf(8U),
                             bit_op(0x400U, 0U, 5U),
                             0x003U,
                             0x0A03U});
    auto mapper = board(firmware, MirrorMode::Horizontal);
    auto fresh = board(firmware, MirrorMode::Horizontal);
    mapper->write_chr(0x0321U, 0xA7U);
    step(*mapper, 7U);
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kSleepingOffset] == 1U);
    const auto pc = state(*mapper)[kPcOffset];
    step(*mapper, 4U);
    REQUIRE(state(*mapper)[kPcOffset] == pc);

    mapper->reset();
    REQUIRE(state(*mapper)[kRamOffset + 8U] == 0xA6U);
    REQUIRE(state(*mapper)[kSleepingOffset] == 0U);
    REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    step(*mapper, 5U);
    REQUIRE(state(*mapper)[kRamOffset + 8U] == 0xA6U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->read_chr(0x0321U) == 0U);
}

TEST_CASE("Mapper 355 snapshots replay PIC and CHR state without ROM", "[mapper355][state]") {
    auto firmware = program({0x0C0EU,
                             0x006U,
                             bit_op(0x500U, 0U, 5U),
                             0x0C03U,
                             0x002U,
                             file_op(0x060U, 1U),
                             0x0CA6U,
                             movwf(8U),
                             0x0A08U});
    auto mapper = board(firmware);
    mapper->observe_cpu_write(0x5A55U, 0xFFU);
    step(*mapper, 13U);
    for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kRamOffset + 1U] != 0U);

    auto copy = board(firmware, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8123U) == cell(0x0123U, 0x80U));
    for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
    }
    copy->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 355 rejects malformed and incompatible states atomically", "[mapper355][state]") {
    auto firmware = quiet_program();
    auto mapper = board(firmware);
    mapper->observe_cpu_read(0x5A50U, 0U);
    mapper->write_chr(0x0234U, 0xA7U);
    step(*mapper, 3U);
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
    const auto replace = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    };
    replace(0U, 0U);
    replace(2U, 1U);
    replace(6U, saved[6U] ^ 1U);
    replace(kAddressPinsOffset, 1U);
    replace(kPcOffset + 1U, 2U);
    replace(kRamOffset + 2U, saved[kRamOffset + 2U] ^ 1U);
    replace(kRamOffset + 3U, 0x80U);
    replace(kRamOffset + 4U, 0x20U);
    replace(kRamOffset + 5U, 0x10U);
    replace(kStackOffset + 1U, 2U);
    replace(kStackDepthOffset, 3U);
    replace(kTrisaOffset, 0x10U);
    replace(kTimerDividerOffset, 128U);
    replace(kClockDividerOffset, 4U);
    replace(kSleepingOffset, 2U);

    auto other_mirror = board(firmware, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);

    auto other_firmware = firmware;
    other_firmware[3] ^= 1U;
    auto other_pic = board(other_firmware);
    const auto pic_before = state(*other_pic);
    StateReader wrong_pic(saved);
    other_pic->load_state(wrong_pic);
    REQUIRE_FALSE(wrong_pic.ok());
    REQUIRE(state(*other_pic) == pic_before);
}

} // namespace
