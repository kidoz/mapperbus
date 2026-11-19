#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper452.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x100000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kDataOffset = 12UZ;
constexpr std::size_t kPrgRamOffset = 13UZ;
constexpr std::size_t kChrRamOffset = kPrgRamOffset + kPrgRamSize;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xDFFFU, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 452U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kPrgRamSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper452> board(std::size_t size = kMinPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper452::validate_geometry(h, size, 0U));
    return std::make_unique<Mapper452>(h, rom(size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kPrgRamOffset);
    return bytes;
}

void load(Mapper452& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

bool expected_ram(Address addr, Byte data) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned ram_slot = (data >> 5U) & 3U;
    const bool nrom128 = (data & 0x04U) == 0U && (data & 0x02U) != 0U;
    return slot == ram_slot || (nrom128 && slot == (ram_slot ^ 2U));
}

std::size_t expected_prg(Address addr, Address latch, Byte data) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned bank16 = (latch >> 6U) & 0x7FU;
    if ((data & 0x04U) == 0U) {
        if ((data & 0x02U) != 0U) {
            const unsigned bank8 = (latch >> 5U) & 0xFFU;
            return bank8 * kPrgBankSize + (addr & 0x1FFFU);
        }
        const unsigned selected = slot < 2U ? bank16 : 0U;
        return selected * 0x4000UZ + (addr & 0x3FFFU);
    }

    unsigned bank8 = (bank16 << 1U) | slot;
    if (slot == 3U && (data & 0x08U) != 0U) {
        bank8 |= 0x04U;
        if ((data & 0x10U) != 0U) {
            bank8 |= 0x08U;
        }
    }
    return bank8 * kPrgBankSize + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 452 validates DS-9-27 memory geometry", "[mapper452][geometry][ram]") {
    for (const std::size_t size : {kMinPrgSize, kMaxPrgSize}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0x80U);
            CAPTURE(size, mirror);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(state(*mapper).size() == 16397U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(mapper->read_prg(0x8000U) == 0U);
            REQUIRE(mapper->read_prg(0xA000U) == cell(0x2000U, 0x35U));
            REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x3FFFU, 0x35U));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
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
        REQUIRE_FALSE(Mapper452::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 4U, 451U, 453U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, 0U);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x080000UZ,
                                   kMinPrgSize - 1U,
                                   kMinPrgSize + 1U,
                                   0x180000UZ,
                                   kMaxPrgSize - 1U,
                                   kMaxPrgSize + 1U,
                                   kMaxPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 12U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x1000U;
            break;
        case 4U:
            h.prg_ram_size = 0x4000U;
            break;
        case 5U:
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 6U:
            h.chr_ram_size = 0U;
            break;
        case 7U:
            h.chr_ram_size = 0x1000U;
            break;
        case 8U:
            h.chr_ram_size = 0x4000U;
            break;
        case 9U:
            h.chr_nvram_size = kChrRamSize;
            break;
        case 10U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kPrgRamSize;
            h.has_battery = true;
            break;
        default:
            h.chr_ram_size = 0U;
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, 0U);
    }
}

TEST_CASE("Mapper 452 decodes address and data latches only below E000",
          "[mapper452][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper452& mapper) {
        mapper.reset();
        mapper.write_prg(0xA000U, 0x60U);
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(addr, value);
        if (raw >= 0x8000U && raw < 0xE000U) {
            expected->write_prg(static_cast<Address>(0x8000U | (raw & 0x1FE0U)),
                                static_cast<Byte>(value ^ 0x80U));
        } else if (raw >= 0xE000U) {
            expected->write_prg(addr, value);
        }
        CAPTURE(raw, value);
        REQUIRE(register_state(*actual) == register_state(*expected));
        REQUIRE(actual->mirror_mode() == expected->mirror_mode());
        REQUIRE(actual->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(addr));
        if (raw < 0x8000U) {
            REQUIRE(actual->read_prg(addr) == 0U);
        } else if (raw >= 0xE000U) {
            REQUIRE(actual->read_prg(addr) == value);
        }
    }

    actual->write_prg_cpu(0xD345U, 0xFFU, true);
    expected->write_prg(0xD340U, 0x7FU);
    REQUIRE(state(*actual) == state(*expected));
}

TEST_CASE("Mapper 452 writes PRG RAM through the old overlay while relatching",
          "[mapper452][decoding][ram][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8123U, 0x5AU);
    const auto registers = register_state(*mapper);
    REQUIRE(registers[kLatchOffset] == 0x20U);
    REQUIRE(registers[kLatchOffset + 1U] == 0x01U);
    REQUIRE(registers[kDataOffset] == 0x5AU);
    REQUIRE(mapper->read_prg(0xC123U) == 0x5AU);

    mapper->write_prg(0xC000U, 0x62U);
    mapper->write_prg(0xE123U, 0xA5U);
    REQUIRE(mapper->read_prg(0xA123U) == 0xA5U);
    REQUIRE(mapper->read_prg(0xE123U) == 0xA5U);
    REQUIRE(register_state(*mapper)[kDataOffset] == 0x62U);
}

TEST_CASE("Mapper 452 maps every latch and data combination in all PRG modes",
          "[mapper452][banking][prg][ram]") {
    for (const std::size_t size : {kMinPrgSize, kMaxPrgSize}) {
        auto mapper = board(size, MirrorMode::Horizontal, 0x35U);
        for (Address latch = 0U; latch <= 0x1FE0U; latch += 0x20U) {
            for (Byte data = 0U; data < 0x80U; ++data) {
                mapper->write_prg(static_cast<Address>(0x8000U | latch), data);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8000U + slot * kPrgBankSize + 0x1FFFU);
                    const auto physical = expected_prg(addr, latch, data) % size;
                    const auto expected = expected_ram(addr, data) ? 0U : cell(physical, 0x35U);
                    CAPTURE(size, latch, data, slot, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == expected);
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
                CAPTURE(size, latch, data);
                REQUIRE(mapper->mirror_mode() ==
                        ((data & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
        }
    }
}

TEST_CASE("Mapper 452 overlays one or two writable 8 KiB PRG RAM windows",
          "[mapper452][banking][ram][bus][decoding]") {
    auto mapper = board();
    constexpr Address latch = 0x12A0U;
    constexpr Byte salt = 0xA5U;
    for (const Byte mode : {0x00U, 0x02U, 0x04U, 0x06U}) {
        for (Byte window = 0U; window < 4U; ++window) {
            const auto data = static_cast<Byte>(mode | (window << 5U) | 1U);
            mapper->power_on();
            mapper->write_prg(static_cast<Address>(0xA000U | latch), data);
            for (Address offset = 0U; offset < kPrgRamSize; ++offset) {
                const auto addr = static_cast<Address>(0x8000U + window * kPrgBankSize + offset);
                mapper->reset();
                mapper->write_prg(static_cast<Address>(0xA000U | latch), data);
                mapper->write_prg(addr, cell(offset, salt));
            }
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0xA000U | latch), data);
            CAPTURE(mode, window, data, latch);
            REQUIRE(register_state(*mapper)[kLatchOffset] == static_cast<Byte>(latch));
            REQUIRE(register_state(*mapper)[kLatchOffset + 1U] == static_cast<Byte>(latch >> 8U));
            REQUIRE(register_state(*mapper)[kDataOffset] == data);

            for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
                const auto addr = static_cast<Address>(raw);
                const auto expected = expected_ram(addr, data)
                                          ? cell(addr & 0x1FFFU, salt)
                                          : cell(expected_prg(addr, latch, data) % kMinPrgSize);
                CAPTURE(addr, expected);
                REQUIRE(mapper->read_prg(addr) == expected);
            }
        }
    }
}

TEST_CASE("Mapper 452 provides writable unbanked 8 KiB CHR RAM", "[mapper452][chr][ram]") {
    auto mapper = board();
    const auto before = register_state(*mapper);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x5AU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x5AU));
    }
    REQUIRE(register_state(*mapper) == before);
}

TEST_CASE("Mapper 452 uses latch-controlled ordinary H/V CIRAM", "[mapper452][ciram]") {
    auto mapper = board(kMinPrgSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Byte value = 0U; value < 0x80U; ++value) {
        mapper->write_prg(0xA000U, value);
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x0101U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 452 leaves low CPU RAM expansion IRQ and audio absent",
          "[mapper452][bus][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xDFFFU, 0xFFU);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
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

TEST_CASE("Mapper 452 reset clears latches and cold power clears volatile RAM",
          "[mapper452][reset][ram]") {
    auto mapper = board(kMinPrgSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0xA000U | 0x12A0U, 0x7FU);
    mapper->write_prg(0xE123U, 0xA5U);
    mapper->write_chr(0x0123U, 0x67U);
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto soft = state(*mapper);
    REQUIRE(soft[kLatchOffset] == 0U);
    REQUIRE(soft[kLatchOffset + 1U] == 0U);
    REQUIRE(soft[kDataOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(soft).subspan(kPrgRamOffset),
                               std::span(programmed).subspan(kPrgRamOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x3FFFU, 0x35U));
    mapper->write_prg(0xA000U, 0x60U);
    REQUIRE(mapper->read_prg(0xE123U) == 0xA5U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    mapper->write_prg(0xDFFFU, 0xFFU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    mapper->write_prg(0xA000U, 0x60U);
    REQUIRE(mapper->read_prg(0xE123U) == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 452 bounded snapshots replay latches and RAM without source ROM",
          "[mapper452][state][banking][ram]") {
    auto mapper = board(kMinPrgSize, MirrorMode::Vertical, 0x10U);
    mapper->write_prg(0xA000U | 0x03A0U, 0x7FU);
    mapper->write_prg(0xE123U, 0xA5U);
    mapper->write_chr(0x0456U, 0x67U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgRamSize + kChrRamSize + 16U);
    REQUIRE(saved[kLatchOffset] == 0xA0U);
    REQUIRE(saved[kLatchOffset + 1U] == 0x03U);
    REQUIRE(saved[kDataOffset] == 0x7FU);
    REQUIRE(saved[kPrgRamOffset + 0x0123U] == 0xA5U);
    REQUIRE(saved[kChrRamOffset + 0x0456U] == 0x67U);

    auto copy = board(kMinPrgSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->read_prg(0xE123U) == 0xA5U);
    REQUIRE(copy->read_chr(0x0456U) == 0x67U);
    for (const auto addr : kPrgAddresses) {
        const auto expected = expected_ram(addr, 0x7FU)
                                  ? mapper->read_prg(addr)
                                  : cell(expected_prg(addr, 0x03A0U, 0x7FU) % kMinPrgSize, 0x80U);
        CAPTURE(addr, expected);
        REQUIRE(copy->read_prg(addr) == expected);
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

TEST_CASE("Mapper 452 rejects damaged truncated and incompatible states atomically",
          "[mapper452][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0xA000U | 0x03A0U, 0x7FU);
    mapper->write_prg(0xE123U, 0xA5U);
    mapper->write_chr(0x0456U, 0x67U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < active.size(); ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (unsigned bit = 0U; bit < kLatchOffset * 8U; ++bit) {
        auto invalid = active;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kPrgRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x01U, 0x02U, 0x04U, 0x08U, 0x10U}) {
        auto invalid = active;
        invalid[kLatchOffset] |= reserved;
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }
    for (const Byte reserved : {0x20U, 0x40U, 0x80U, 0xE0U}) {
        auto invalid = active;
        invalid[kLatchOffset + 1U] |= reserved;
        invalid[kPrgRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }
    auto invalid_data = active;
    invalid_data[kDataOffset] |= 0x80U;
    invalid_data[kPrgRamOffset + 0x123U] ^= 0xFFU;
    reject(invalid_data);

    auto wrong_size = board(kMaxPrgSize);
    const auto before = state(*wrong_size);
    StateReader size_reader(active);
    wrong_size->load_state(size_reader);
    REQUIRE_FALSE(size_reader.ok());
    REQUIRE(state(*wrong_size) == before);

    auto changed = active;
    changed[kLatchOffset] = 0xA0U;
    changed[kLatchOffset + 1U] = 0x15U;
    changed[kDataOffset] = 0x5AU;
    changed[kPrgRamOffset + 0x123U] ^= 0xFFU;
    changed[kChrRamOffset + 0x456U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

} // namespace
