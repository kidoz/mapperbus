#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper405.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kEepromSize = 256UZ;
constexpr std::size_t kRegisterOffset = 14UZ;
constexpr std::size_t kControlOffset = 14UZ;
constexpr std::size_t kMirrorOffset = 15UZ;
constexpr std::size_t kPrgOffset = 16UZ;
constexpr std::size_t kModeOffset = 20UZ;
constexpr std::size_t kChrOffset = 21UZ;
constexpr std::size_t kIntermediateOffset = 27UZ;
constexpr std::size_t kChrMiddleOffset = 28UZ;
constexpr std::size_t kLatchOffset = 29UZ;
constexpr std::size_t kIrqOffset = 31UZ;
constexpr std::size_t kPinsOffset = 32UZ;
constexpr std::size_t kChrRamOffset = 33UZ;
constexpr std::size_t kEepromOffset = kChrRamOffset + kChrRamSize;
constexpr std::size_t kStateSize = kEepromOffset;
constexpr std::size_t kEepromStateSize = kEepromOffset + kEepromSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, bool eeprom = false) {
    INesHeader result{};
    result.mapper_number = 405U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    result.prg_nvram_size = eeprom ? kEepromSize : 0U;
    result.has_battery = eeprom;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper405> board(std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 bool eeprom = false,
                                 Byte salt = 0U) {
    const auto h = header(mirror, eeprom);
    REQUIRE(Mapper405::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper405>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kChrRamOffset);
    return bytes;
}

void load(Mapper405& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void edge(Mapper405& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

std::size_t expected_prg(
    Address addr, Byte control, const std::array<Byte, 3>& prg, Byte middle, Byte mode) {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    if ((control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                            ? prg[slot]
                           : slot == 2U && (mode & 0x40U) != 0U ? prg[2]
                           : slot == 2U                         ? 254U
                                                                : 255U;
    constexpr std::array<unsigned, 8> masks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
    const unsigned mask = masks[mode & 7U];
    const unsigned bank = (inner & mask) | (middle & (255U ^ mask));
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(
    Address addr, Byte control, const std::array<Byte, 6>& chr, Byte intermediate, Byte middle) {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (chr[4U + slot / 2U] & 254U) | (slot & 1U) : chr[slot - 4U];
    constexpr std::array<unsigned, 8> masks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
    const unsigned mask = masks[middle & 7U];
    const unsigned bank =
        (inner & mask) | (middle & (255U ^ mask) & 248U) | ((intermediate >> 4U) << 8U);
    return (static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U))) &
           (kChrRamSize - 1U);
}

TEST_CASE("Mapper 405 validates bounded UM6578 memory geometry", "[mapper405][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const bool eeprom : {false, true}) {
            auto mapper = board(kPrgSize, mirror, eeprom);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*mapper).size() == (eeprom ? kEepromStateSize : kStateSize));
            REQUIRE(register_state(*mapper).size() == kChrRamOffset);
            REQUIRE(mapper->battery_ram().size() == (eeprom ? kEepromSize : 0U));
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(62UZ * kPrgBankSize));
        }
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size *= 2U) {
        CAPTURE(size);
        REQUIRE(Mapper405::validate_geometry(header(), size, 0U));
        REQUIRE(Mapper405::validate_geometry(header(MirrorMode::Horizontal, true), size, 0U));
    }

    for (const unsigned id : {0U, 4U, 256U, 404U, 406U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper405::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper405::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper405::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x18000UZ, kPrgSize + 1U, kPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper405::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kChrRamSize, kPrgSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper405::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper405::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 128U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_nvram_size = kEepromSize;
            break;
        case 4U:
            h = header(MirrorMode::Vertical, true);
            h.has_battery = false;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_nvram_size = kChrRamSize;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper405::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 405 decodes native write-only ports but no outer bank latch",
          "[mapper405][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    const auto initial = state(*actual);
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        actual->reset();
        expected->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const bool decoded = raw >= 0x4101U && raw <= 0x410BU;
        actual->write_expansion(addr, value);
        if (decoded) {
            expected->write_expansion(addr, value);
        }
        CAPTURE(raw, value, decoded);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_expansion(addr) == decoded);
        REQUIRE(actual->expansion_read_mask(addr) == (decoded ? 0U : 0xFFU));
        REQUIRE(actual->read_expansion(addr) == 0U);
    }
    actual->write_expansion(0x4100U, 0xFFU);
    REQUIRE(state(*actual) == initial);
    for (const Address addr :
         {0x4000U, 0x40FFU, 0x4100U, 0x410CU, 0x5101U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(actual->maps_expansion(addr));
        REQUIRE(actual->expansion_read_mask(addr) == 0xFFU);
    }

    for (unsigned raw = 0x2000U; raw < 0x4000U; ++raw) {
        actual->reset();
        const auto addr = static_cast<Address>(raw);
        const bool native = raw >= 0x2010U && raw <= 0x201FU;
        REQUIRE(actual->write_ppu_register(addr, 0xD5U) == native);
        REQUIRE(actual->read_ppu_register(addr, 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto wanted = initial;
        if (raw >= 0x2012U && raw <= 0x2017U) {
            wanted[kChrOffset + raw - 0x2012U] = 0xD5U;
        } else if (raw == 0x2018U) {
            wanted[kIntermediateOffset] = 0x50U;
        } else if (raw == 0x201AU) {
            wanted[kChrMiddleOffset] = 0xD5U;
        }
        CAPTURE(raw, native);
        REQUIRE(state(*actual) == wanted);
    }
}

TEST_CASE("Mapper 405 forwards every MMC3 alias and honors the native write gate",
          "[mapper405][decoding][banking]") {
    auto mapper = board();
    mapper->write_expansion(0x4105U, 0x20U);
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_expansion(0x4105U, 0x20U);
        mapper->write_prg(static_cast<Address>(raw), 0xA3U);
        auto wanted = initial;
        if (raw >= 0x8000U) {
            switch (raw & 0xE001U) {
            case 0x8000U:
                wanted[kControlOffset] = 0xA3U;
                break;
            case 0x8001U:
                wanted[kChrOffset + 4U] = 0xA3U;
                break;
            case 0xA000U:
                wanted[kMirrorOffset] = 1U;
                break;
            case 0xC000U:
                wanted[kLatchOffset] = 0xA3U;
                break;
            case 0xE001U:
                wanted[kIrqOffset] = 1U;
                break;
            default:
                break;
            }
        }
        CAPTURE(raw);
        REQUIRE(state(*mapper) == wanted);
    }
    for (unsigned index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_expansion(0x4105U, static_cast<Byte>(index));
        mapper->write_prg(0x9FFFU, 0xFFU);
        const auto saved = state(*mapper);
        CAPTURE(index);
        REQUIRE(saved[index < 6U ? (index < 2U ? kChrOffset + 4U + index : kChrOffset + index - 2U)
                                 : kPrgOffset + index - 6U] == 0xFFU);
    }
    mapper->write_expansion(0x410BU, 8U);
    const auto disabled = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        REQUIRE(state(*mapper) == disabled);
    }
    mapper->write_expansion(0x4107U, 9U);
    REQUIRE(state(*mapper)[kPrgOffset] == 9U);
}

TEST_CASE("Mapper 405 composes native PRG masks without outer bank bits",
          "[mapper405][banking][bus]") {
    auto mapper = board();
    constexpr std::array<Byte, 4> middles{0U, 0x55U, 0xAAU, 0xFFU};
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            for (const Byte middle : middles) {
                for (const bool third : {false, true}) {
                    for (const bool swap : {false, true}) {
                        const auto control = static_cast<Byte>(swap ? 0x40U : 0U);
                        const auto full_mode = static_cast<Byte>(mode | (third ? 0x40U : 0U));
                        const std::array<Byte, 3> banks{static_cast<Byte>(raw), 0x59U, 0xA6U};
                        mapper->write_expansion(0x4100U, 0xF0U);
                        mapper->write_expansion(0x4105U, control);
                        mapper->write_expansion(0x4107U, banks[0]);
                        mapper->write_expansion(0x4108U, banks[1]);
                        mapper->write_expansion(0x4109U, banks[2]);
                        mapper->write_expansion(0x410AU, middle);
                        mapper->write_expansion(0x410BU, full_mode);
                        for (const auto addr : kPrgAddresses) {
                            const auto physical =
                                expected_prg(addr, control, banks, middle, full_mode) &
                                (kPrgSize - 1U);
                            CAPTURE(mode, raw, middle, third, swap, addr, physical);
                            REQUIRE(mapper->maps_prg(addr));
                            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                            REQUIRE(mapper->read_prg(addr) == cell(physical));
                        }
                    }
                }
            }
        }
    }
    auto fitted = board(0x8000UZ);
    fitted->write_expansion(0x4107U, 0xFFU);
    fitted->write_expansion(0x4109U, 0xFEU);
    fitted->write_expansion(0x410AU, 0xFFU);
    fitted->write_expansion(0x410BU, 0x47U);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0U, {0xFFU, 1U, 0xFEU}, 0xFFU, 0x47U) & 0x7FFFU;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_prg(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 405 banks writable 32 KiB CHR RAM and routes pattern CIRAM",
          "[mapper405][banking][ram][ciram]") {
    auto mapper = board();
    constexpr std::array<Byte, 9> values{0U, 1U, 2U, 3U, 0x1FU, 0x55U, 0xAAU, 0xFEU, 0xFFU};
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (const Byte value : values) {
            for (const Byte intermediate : {Byte{0U}, Byte{0x10U}, Byte{0x70U}}) {
                for (const bool flip : {false, true}) {
                    const auto control = static_cast<Byte>(flip ? 0x80U : 0U);
                    const auto middle = static_cast<Byte>((value & 248U) | mode);
                    std::array<Byte, 6> banks{};
                    mapper->write_expansion(0x4100U, 0x0FU);
                    mapper->write_expansion(0x4105U, control);
                    mapper->write_ppu_register(0x2018U, intermediate);
                    mapper->write_ppu_register(0x201AU, middle);
                    for (unsigned index = 0U; index < banks.size(); ++index) {
                        banks[index] = static_cast<Byte>(value + index * 17U);
                        mapper->write_ppu_register(static_cast<Address>(0x2012U + index),
                                                   banks[index]);
                    }
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                        const auto physical =
                            expected_chr(addr, control, banks, intermediate, middle);
                        const auto written = static_cast<Byte>(cell(physical, 0x35U) ^ 0xFFU);
                        CAPTURE(mode, value, intermediate, flip, slot, physical);
                        mapper->write_chr(addr, written);
                        REQUIRE(mapper->read_chr(addr) == written);
                        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == written);
                    }
                }
            }
        }
    }

    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_expansion(0x4105U, static_cast<Byte>(raw));
        mapper->write_expansion(0x4106U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr :
             {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU, 0x2000U, 0x2FFFU, 0x3EFFU}) {
            const auto expected = addr < 0x2000U && (raw & 0x20U) != 0U
                                      ? std::optional<Address>(addr & 0x7FFU)
                                      : std::nullopt;
            REQUIRE(mapper->ciram_address(addr) == expected);
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 405 retains RAM on reset and bounds optional EEPROM persistence",
          "[mapper405][ram][reset][bus]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal, true);
    std::array<Byte, kEepromSize> imported{};
    for (std::size_t index = 0U; index < imported.size(); ++index) {
        imported[index] = cell(index, 0x5AU);
    }
    mapper->set_battery_ram(imported);
    REQUIRE(
        std::equal(mapper->battery_ram().begin(), mapper->battery_ram().end(), imported.begin()));
    const auto before = state(*mapper);
    for (const std::size_t size : {0UZ, 1UZ, kEepromSize - 1U, kEepromSize + 1U, 8192UZ}) {
        const std::vector<Byte> wrong_size(size, 0U);
        mapper->set_battery_ram(wrong_size);
        REQUIRE(state(*mapper) == before);
    }
    for (const Address addr : {0x4020U, 0x4100U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0x5AU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->shared_nvram().empty());

    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4107U, 0x92U);
    mapper->write_ppu_register(0x2012U, 0x11U);
    mapper->write_chr(0x1123U, 0xA7U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE(register_state(*mapper) ==
            register_state(*board(kPrgSize, MirrorMode::Vertical, true)));
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_ppu_register(0x2012U, 0x11U);
    REQUIRE(mapper->read_chr(0x1123U) == 0xA7U);
    REQUIRE(
        std::equal(mapper->battery_ram().begin(), mapper->battery_ram().end(), imported.begin()));

    mapper->power_on();
    REQUIRE(register_state(*mapper) ==
            register_state(*board(kPrgSize, MirrorMode::Vertical, true)));
    for (Address addr = 0U; addr < 0x2000U; addr = static_cast<Address>(addr + 0x101U)) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
    REQUIRE(
        std::equal(mapper->battery_ram().begin(), mapper->battery_ram().end(), imported.begin()));

    auto absent = board();
    absent->set_battery_ram(imported);
    REQUIRE(absent->battery_ram().empty());
    REQUIRE(state(*absent).size() == kStateSize);
}

TEST_CASE("Mapper 405 clocks native IRQ from qualified A12 or HSYNC", "[mapper405][irq][audio]") {
    auto mapper = board(0x20000UZ);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_expansion(0x4101U, static_cast<Byte>(latch));
        mapper->write_expansion(0x4102U, 0U);
        mapper->write_expansion(0x4104U, 0U);
        mapper->on_ppu_hblank();
        REQUIRE_FALSE(mapper->irq_pending());
        edge(*mapper);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            edge(*mapper);
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    for (unsigned low = 0U; low <= 4U; ++low) {
        mapper->reset();
        mapper->write_expansion(0x4104U, 0U);
        edge(*mapper, low);
        CAPTURE(low);
        REQUIRE(mapper->irq_pending() == (low >= 3U));
    }
    mapper->reset();
    mapper->write_expansion(0x410BU, 0x80U);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4103U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    const auto before = register_state(*mapper);
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_frame_start();
    mapper->clock_cpu_falling_edge();
    REQUIRE(register_state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 405 bounded snapshots replay native state and RAM without ROM",
          "[mapper405][state]") {
    auto mapper = board(kPrgSize, MirrorMode::Vertical, true);
    std::array<Byte, kEepromSize> imported{};
    for (std::size_t index = 0U; index < imported.size(); ++index) {
        imported[index] = cell(index, 0x5AU);
    }
    mapper->set_battery_ram(imported);
    mapper->write_expansion(0x4105U, 0x60U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x4108U, 0x59U);
    mapper->write_expansion(0x4109U, 0xA6U);
    mapper->write_expansion(0x410AU, 0x40U);
    mapper->write_expansion(0x410BU, 0x40U);
    mapper->write_ppu_register(0x2012U, 0x12U);
    mapper->write_ppu_register(0x2016U, 0xFEU);
    mapper->write_ppu_register(0x2018U, 0x70U);
    mapper->write_ppu_register(0x201AU, 0x20U);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_chr(0x1123U, 0x5CU);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kEepromStateSize);
    REQUIRE(saved.size() == 33057U);
    REQUIRE(saved[kControlOffset] == 0x60U);
    REQUIRE(saved[kPrgOffset] == 0x95U);
    REQUIRE(saved[kPinsOffset] == 4U);

    auto copy = board(kPrgSize, MirrorMode::Horizontal, true, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->ciram_address(0x123U) == mapper->ciram_address(0x123U));
    for (const auto addr : kPrgAddresses) {
        const auto physical =
            expected_prg(addr, 0x60U, {0x95U, 0x59U, 0xA6U}, 0x40U, 0x40U) & (kPrgSize - 1U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA7U);
    REQUIRE(copy->read_chr(0x1123U) == 0x5CU);
    REQUIRE(std::equal(copy->battery_ram().begin(), copy->battery_ram().end(), imported.begin()));
    copy->clock_cpu();
    copy->observe_ppu_a12(true);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(true);
    REQUIRE(state(*copy) == state(*mapper));
    edge(*copy);
    edge(*mapper);
    REQUIRE(copy->irq_pending());
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

TEST_CASE("Mapper 405 rejects damaged truncated and mismatched states atomically",
          "[mapper405][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x410BU, 0x40U);
    mapper->write_ppu_register(0x2012U, 0xFFU);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
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
    for (std::size_t offset = 0U; offset < kRegisterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields{{
        {kControlOffset, 0x08U},
        {kControlOffset, 0x18U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 0xFFU},
        {kModeOffset, 0x10U},
        {kModeOffset, 0x30U},
        {kIntermediateOffset, 0x01U},
        {kIntermediateOffset, 0x80U},
        {kIrqOffset, 2U},
        {kIrqOffset, 4U},
        {kPinsOffset, 3U},
        {kPinsOffset, 7U},
        {kPinsOffset, 8U},
    }};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[kControlOffset] ^= 0xFFU;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto with_eeprom = board(kPrgSize, MirrorMode::Vertical, true);
    const auto eeprom_before = state(*with_eeprom);
    StateReader missing_eeprom_reader(saved);
    with_eeprom->load_state(missing_eeprom_reader);
    REQUIRE_FALSE(missing_eeprom_reader.ok());
    REQUIRE(state(*with_eeprom) == eeprom_before);
    StateReader unexpected_eeprom_reader(eeprom_before);
    mapper->load_state(unexpected_eeprom_reader);
    REQUIRE_FALSE(unexpected_eeprom_reader.ok());
    REQUIRE(state(*mapper) == saved);

    auto changed_ram = saved;
    changed_ram[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed_ram);
    REQUIRE(state(*mapper) == changed_ram);
}

} // namespace
