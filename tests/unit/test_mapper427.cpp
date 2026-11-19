#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper427.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kKiB = 1024UZ;
constexpr std::size_t kMiB = 1024UZ * kKiB;
constexpr std::size_t kMaxRomSize = 32UZ * kMiB;
constexpr std::size_t kStateSize = 56UZ;
constexpr std::size_t kSubmapperOffset = 14UZ;
constexpr std::size_t kOuterOffset = 15UZ;
constexpr std::size_t kControlOffset = 16UZ;
constexpr std::size_t kMirrorOffset = 17UZ;
constexpr std::size_t kPrgOffset = 18UZ;
constexpr std::size_t kMiddleOffset = 21UZ;
constexpr std::size_t kModeOffset = 22UZ;
constexpr std::size_t kChrOffset = 23UZ;
constexpr std::size_t kIntermediateOffset = 29UZ;
constexpr std::size_t kChrMiddleOffset = 30UZ;
constexpr std::size_t kLatchOffset = 31UZ;
constexpr std::size_t kCounterOffset = 32UZ;
constexpr std::size_t kIrqOffset = 33UZ;
constexpr std::size_t kPinsOffset = 34UZ;
constexpr std::size_t kProtectionStageOffset = 41UZ;
constexpr std::size_t kProtectionCommandOffset = 42UZ;
constexpr std::size_t kProtectionClockOffset = 44UZ;
constexpr std::size_t kEepromStageOffset = 47UZ;
constexpr std::size_t kEepromClockOffset = 48UZ;
constexpr std::size_t kEepromAddressOffset = 52UZ;
constexpr std::size_t kEepromBitOffset = 54UZ;
constexpr std::size_t kEepromDataOffset = 56UZ;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};

INesHeader make_header(Byte submapper = 0U, bool eeprom = false, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 427U;
    header.is_nes2 = true;
    header.submapper = submapper;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_nvram_size = eeprom ? Mapper427::kEepromSize : 0U;
    header.has_battery = eeprom;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 53U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + (bank >> 8U) * 73U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper427> make_board(std::size_t prg_size = 128UZ * kKiB,
                                      std::size_t chr_size = 128UZ * kKiB,
                                      Byte submapper = 0U,
                                      bool eeprom = false,
                                      bool vertical = true) {
    std::vector<Byte> prg(prg_size);
    std::vector<Byte> chr(chr_size);
    for (std::size_t index = 0U; index < prg.size(); ++index) {
        prg[index] =
            prg_cell(static_cast<unsigned>(index / 8192U), static_cast<unsigned>(index % 8192U));
    }
    for (std::size_t index = 0U; index < chr.size(); ++index) {
        chr[index] =
            chr_cell(static_cast<unsigned>(index / 1024U), static_cast<unsigned>(index % 1024U));
    }
    const auto header = make_header(submapper, eeprom, vertical);
    REQUIRE(Mapper427::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper427>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void edge(Mapper427& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

class ProtectionBus {
  public:
    ProtectionBus(Mapper427& mapper, Byte submapper)
        : mapper_(mapper), base_(submapper == 0U ? 0x4148U : 0x4150U),
          clock_(submapper == 0U ? 0x20U : 0x02U), data_(submapper == 0U ? 0x10U : 0x01U) {
        mapper_.write_expansion(base_, static_cast<Byte>(clock_ | data_));
        drive(true, true);
    }

    void start() {
        mapper_.write_expansion(base_, static_cast<Byte>(clock_ | data_));
        drive(true, true);
        drive(true, false);
        drive(false, false);
    }

    void stop() {
        drive(false, false);
        drive(true, false);
        drive(true, true);
        mapper_.write_expansion(base_, clock_);
    }

    void write_byte(Byte value) {
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool high = (value & (0x80U >> bit)) != 0U;
            drive(false, high);
            drive(true, high);
        }
        drive(false, true);
        drive(true, true);
        drive(false, true);
    }

    bool read_bit() {
        drive(false, true);
        drive(true, true);
        const bool result =
            (mapper_.read_expansion(static_cast<Address>(base_ + 3U)) & data_) != 0U;
        drive(false, true);
        return result;
    }

    Byte exchange(Byte value) {
        start();
        write_byte(0x80U);
        write_byte(value);
        stop();
        Byte result = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            result =
                static_cast<Byte>((static_cast<unsigned>(result) << 1U) | (read_bit() ? 1U : 0U));
        }
        return result;
    }

  private:
    void drive(bool clock, bool data) {
        auto latch = mapper_.read_expansion(static_cast<Address>(base_ + 2U));
        latch = static_cast<Byte>((latch & static_cast<Byte>(~(clock_ | data_))) |
                                  (clock ? clock_ : 0U) | (data ? data_ : 0U));
        mapper_.write_expansion(static_cast<Address>(base_ + 2U), latch);
    }

    Mapper427& mapper_;
    Address base_;
    Byte clock_;
    Byte data_;
};

class EepromBus {
  public:
    explicit EepromBus(Mapper427& mapper) : mapper_(mapper) {
        mapper_.write_expansion(0x4150U, 0x0CU);
        drive(true, true);
    }

    void start() {
        mapper_.write_expansion(0x4150U, 0x0CU);
        drive(true, true);
        drive(true, false);
        drive(false, false);
    }

    void repeated_start() {
        mapper_.write_expansion(0x4150U, 0x0CU);
        drive(false, true);
        drive(true, true);
        drive(true, false);
        drive(false, false);
    }

    void stop() {
        mapper_.write_expansion(0x4150U, 0x0CU);
        drive(false, false);
        drive(true, false);
        drive(true, true);
        mapper_.write_expansion(0x4150U, 0x04U);
    }

    bool send(Byte value) {
        mapper_.write_expansion(0x4150U, 0x0CU);
        bool level = false;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            level = (value & (0x80U >> bit)) != 0U;
            drive(false, level);
            drive(true, level);
            drive(false, level);
        }
        drive(false, true);
        mapper_.write_expansion(0x4150U, 0x04U);
        const bool acknowledged = (mapper_.read_expansion(0x4153U) & 0x08U) == 0U;
        drive(true, true);
        drive(false, true);
        return acknowledged;
    }

    bool read_bit() {
        drive(false, true);
        drive(true, true);
        const bool result = (mapper_.read_expansion(0x4153U) & 0x08U) != 0U;
        drive(false, true);
        return result;
    }

    Byte read_byte() {
        Byte value = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            value =
                static_cast<Byte>((static_cast<unsigned>(value) << 1U) | (read_bit() ? 1U : 0U));
        }
        drive(true, true);
        drive(false, true);
        return value;
    }

    void select_address(Word address) {
        start();
        REQUIRE(send(static_cast<Byte>(0xA0U | ((address >> 7U) & 2U))));
        REQUIRE(send(static_cast<Byte>(address)));
    }

    void write(Word address, Byte value) {
        select_address(address);
        REQUIRE(send(value));
        stop();
    }

    Byte read(Word address) {
        select_address(address);
        repeated_start();
        REQUIRE(send(static_cast<Byte>(0xA1U | ((address >> 7U) & 2U))));
        const auto result = read_byte();
        stop();
        return result;
    }

  private:
    void drive(bool clock, bool data) {
        auto latch = mapper_.read_expansion(0x4152U);
        latch = static_cast<Byte>((latch & 0xF3U) | (clock ? 0x04U : 0U) | (data ? 0x08U : 0U));
        mapper_.write_expansion(0x4152U, latch);
    }

    Mapper427& mapper_;
};

TEST_CASE("Mapper 427 validates submappers ROM EEPROM and CIRAM geometry",
          "[mapper427][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 2U; ++submapper) {
        for (bool eeprom : {false, true}) {
            const auto header = make_header(submapper, eeprom);
            REQUIRE(Mapper427::validate_geometry(header, 32768U, 8192U));
        }
    }
    for (std::size_t prg = 32UZ * kKiB; prg <= kMaxRomSize; prg <<= 1U) {
        REQUIRE(Mapper427::validate_geometry(make_header(), prg, 8192U));
    }
    REQUIRE(Mapper427::validate_geometry(make_header(), 32768U, 0U));
    for (std::size_t chr = 8UZ * kKiB; chr <= kMaxRomSize; chr <<= 1U) {
        REQUIRE(Mapper427::validate_geometry(make_header(), 32768U, chr));
    }
    REQUIRE(Mapper427::validate_geometry(make_header(1U, true, false), 32768U, 8192U));

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        CAPTURE(header.mapper_number,
                header.is_nes2,
                header.submapper,
                header.has_battery,
                header.has_trainer,
                header.prg_ram_size,
                header.prg_nvram_size,
                header.chr_ram_size,
                header.chr_nvram_size,
                header.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper427::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg : {0UZ, 16384UZ, 32767UZ, 49152UZ, 2U * kMaxRomSize}) {
        reject(make_header(), prg, 8192U);
    }
    for (std::size_t chr : {1UZ, 4096UZ, 12288UZ, 2U * kMaxRomSize}) {
        reject(make_header(), 32768U, chr);
    }
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        reject(make_header(submapper), 32768U, 8192U);
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto header = make_header();
        header.mirror_mode = mirror;
        reject(header, 32768U, 8192U);
    }
    for (std::size_t nvram : {1UZ, 256UZ, 511UZ, 513UZ, 8192UZ}) {
        auto header = make_header();
        header.prg_nvram_size = nvram;
        header.has_battery = true;
        reject(header, 32768U, 8192U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.mapper_number = 426U;
            break;
        case 1U:
            header.is_nes2 = false;
            break;
        case 2U:
            header.has_battery = true;
            break;
        case 3U:
            header.prg_ram_size = 512U;
            break;
        case 4U:
            header.prg_nvram_size = 512U;
            break;
        case 5U:
            header.chr_ram_size = 8192U;
            break;
        case 6U:
            header.chr_nvram_size = 8192U;
            break;
        default:
            header.has_trainer = true;
            break;
        }
        reject(header, 32768U, 8192U);
    }

    auto plain = make_board();
    REQUIRE(save(*plain).size() == kStateSize);
    REQUIRE(plain->battery_ram().empty());
    REQUIRE(plain->shared_nvram().empty());
    REQUIRE_FALSE(plain->has_flash_storage());
    REQUIRE_FALSE(plain->has_expansion_audio());
    REQUIRE(plain->audio_output() == 0.0F);
    auto eeprom = make_board(128UZ * kKiB, 128UZ * kKiB, 1U, true);
    REQUIRE(save(*eeprom).size() == kStateSize + Mapper427::kEepromSize);
    REQUIRE(eeprom->battery_ram().size() == Mapper427::kEepromSize);
    REQUIRE(std::ranges::all_of(eeprom->battery_ram(), [](Byte value) { return value == 0xFFU; }));
}

TEST_CASE("Mapper 427 decodes native and submapper GPIO apertures with exact bus masks",
          "[mapper427][decoding][bus]") {
    for (Byte submapper = 0U; submapper < 2U; ++submapper) {
        for (bool eeprom : {false, true}) {
            auto mapper = make_board(128UZ * kKiB, 128UZ * kKiB, submapper, eeprom);
            const Address protection = submapper == 0U ? 0x4148U : 0x4150U;
            for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
                const bool native = addr >= 0x4100U && addr <= 0x410BU;
                const bool protection_mapped = addr >= protection && addr < protection + 8U;
                const bool eeprom_mapped = eeprom && addr >= 0x4150U && addr < 0x4158U;
                const bool gpio = protection_mapped || eeprom_mapped;
                const auto address = static_cast<Address>(addr);
                CAPTURE(submapper, eeprom, addr);
                REQUIRE(mapper->maps_expansion(address) == (native || gpio));
                const bool readable =
                    gpio &&
                    (addr == protection || addr == protection + 2U || addr == protection + 3U ||
                     (eeprom_mapped && (addr == 0x4150U || addr == 0x4152U || addr == 0x4153U)));
                REQUIRE(mapper->expansion_read_mask(address) == (!native && !gpio ? 0xFFU
                                                                 : readable       ? 0xFFU
                                                                                  : 0U));
                if (!readable) {
                    REQUIRE(mapper->read_expansion(address) == 0U);
                }
            }
            mapper->write_expansion(protection, 0xF0U);
            mapper->write_expansion(static_cast<Address>(protection + 2U), 0xA5U);
            REQUIRE(mapper->read_expansion(protection) == 0xF0U);
            REQUIRE(mapper->read_expansion(static_cast<Address>(protection + 2U)) == 0xA5U);
            REQUIRE(mapper->read_expansion(static_cast<Address>(protection + 3U)) == 0x0FU);
            mapper->write_expansion(static_cast<Address>(protection + 3U), 0x5AU);
            REQUIRE(mapper->read_expansion(static_cast<Address>(protection + 2U)) == 0x5AU);
        }
    }

    auto mapper = make_board();
    const auto initial = save(*mapper);
    for (unsigned addr = 0x2000U; addr < 0x4000U; ++addr) {
        mapper->reset();
        const bool native = addr >= 0x2010U && addr <= 0x201FU;
        REQUIRE(mapper->write_ppu_register(static_cast<Address>(addr), 0xD5U) == native);
        REQUIRE(mapper->read_ppu_register(static_cast<Address>(addr), 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto expected = initial;
        if (addr >= 0x2012U && addr <= 0x2017U) {
            expected[kChrOffset + addr - 0x2012U] = 0xD5U;
        } else if (addr == 0x2018U) {
            expected[kIntermediateOffset] = 0x50U;
        } else if (addr == 0x201AU) {
            expected[kChrMiddleOffset] = 0xD5U;
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
    }
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
}

TEST_CASE("Mapper 427 native writes and MMC3 aliases update shared registers only",
          "[mapper427][decoding][banking][irq]") {
    auto mapper = make_board();
    const auto initial = save(*mapper);
    for (unsigned addr = 0x4100U; addr <= 0x410BU; ++addr) {
        mapper->reset();
        mapper->write_expansion(static_cast<Address>(addr), 0xD5U);
        auto expected = initial;
        if (addr == 0x4100U) {
            expected[kOuterOffset] = 0xD5U;
        } else if (addr == 0x4101U) {
            expected[kLatchOffset] = 0xD5U;
        } else if (addr == 0x4104U) {
            expected[kIrqOffset] = 1U;
        } else if (addr == 0x4105U) {
            expected[kControlOffset] = 0xC5U;
        } else if (addr == 0x4106U) {
            expected[kMirrorOffset] = 1U;
        } else if (addr >= 0x4107U && addr <= 0x4109U) {
            expected[kPrgOffset + addr - 0x4107U] = 0xD5U;
        } else if (addr == 0x410AU) {
            expected[kMiddleOffset] = 0xD5U;
        } else if (addr == 0x410BU) {
            expected[kModeOffset] = 0xC5U;
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
    }

    mapper->reset();
    mapper->write_expansion(0x4105U, 0x20U);
    const auto forwarding = save(*mapper);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(0x4105U, 0x20U);
        mapper->write_prg(static_cast<Address>(addr), 0xA3U);
        auto expected = forwarding;
        if (addr >= 0x8000U) {
            switch (addr & 0xE001U) {
            case 0x8000U:
                expected[kControlOffset] = 0xA3U;
                break;
            case 0x8001U:
                expected[kChrOffset + 4U] = 0xA3U;
                break;
            case 0xA000U:
                expected[kMirrorOffset] = 1U;
                break;
            case 0xC000U:
                expected[kLatchOffset] = 0xA3U;
                break;
            case 0xE001U:
                expected[kIrqOffset] = 1U;
                break;
            default:
                break;
            }
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
    }
    for (unsigned index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_expansion(0x4105U, static_cast<Byte>(index));
        mapper->write_prg(0x9FFFU, 0xFFU);
        const auto bytes = save(*mapper);
        REQUIRE(bytes[index < 6U ? (index < 2U ? kChrOffset + 4U + index : kChrOffset + index - 2U)
                                 : kPrgOffset + index - 6U] == 0xFFU);
    }
    mapper->write_expansion(0x410BU, 8U);
    const auto disabled = save(*mapper);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(save(*mapper) == disabled);
    }
    mapper->write_expansion(0x4107U, 9U);
    REQUIRE(save(*mapper)[kPrgOffset] == 9U);
}

TEST_CASE("Mapper 427 applies PRG masks middle substitution and COMR6 swap",
          "[mapper427][banking][bus]") {
    auto mapper = make_board(8UZ * kMiB, 8192U);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value : {0U, 1U, 0x55U, 0xFEU, 0xFFU}) {
            for (unsigned outer : {0U, 3U, 15U}) {
                for (Byte middle : {0U, 0x55U, 0xAAU, 0xFFU}) {
                    for (bool third : {false, true}) {
                        for (bool swap : {false, true}) {
                            mapper->write_expansion(0x4100U, static_cast<Byte>(outer << 4U));
                            mapper->write_expansion(0x4105U, swap ? 0x40U : 0U);
                            mapper->write_expansion(0x4107U, static_cast<Byte>(value));
                            mapper->write_expansion(0x4108U, 0x59U);
                            mapper->write_expansion(0x4109U, 0xA6U);
                            mapper->write_expansion(0x410AU, middle);
                            mapper->write_expansion(0x410BU,
                                                    static_cast<Byte>(mode | (third ? 0x40U : 0U)));
                            const std::array<unsigned, 4> pages{swap ? (third ? 0xA6U : 254U)
                                                                     : value,
                                                                0x59U,
                                                                swap    ? value
                                                                : third ? 0xA6U
                                                                        : 254U,
                                                                255U};
                            for (unsigned slot = 0U; slot < 4U; ++slot) {
                                const unsigned bank =
                                    ((pages[slot] & kPrgMasks[mode]) |
                                     (middle & (255U ^ kPrgMasks[mode])) | (outer << 8U)) %
                                    1024U;
                                const auto addr =
                                    static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                                CAPTURE(mode, value, outer, middle, third, swap, slot, bank);
                                REQUIRE(mapper->read_prg(addr) == prg_cell(bank, 0x123U));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 427 applies CHR masks pairing inversion and shared ROM storage",
          "[mapper427][banking][chr]") {
    auto mapper = make_board(8UZ * kMiB, 4UZ * kMiB);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value : {0U, 1U, 0x55U, 0xFEU, 0xFFU}) {
            for (unsigned intermediate : {0U, 5U, 7U}) {
                for (unsigned outer : {0U, 7U, 15U}) {
                    for (bool flip : {false, true}) {
                        mapper->write_expansion(0x4100U, static_cast<Byte>(outer));
                        mapper->write_expansion(0x4105U, flip ? 0x80U : 0U);
                        const auto middle = static_cast<Byte>((value & 248U) | mode);
                        mapper->write_ppu_register(0x201AU, middle);
                        mapper->write_ppu_register(0x2018U, static_cast<Byte>(intermediate << 4U));
                        std::array<unsigned, 6> banks{};
                        for (unsigned reg = 0U; reg < 6U; ++reg) {
                            banks[reg] = (value + reg * 17U) & 255U;
                            mapper->write_ppu_register(static_cast<Address>(0x2012U + reg),
                                                       static_cast<Byte>(banks[reg]));
                        }
                        const std::array<unsigned, 8> pages{banks[4] & 254U,
                                                            banks[4] | 1U,
                                                            banks[5] & 254U,
                                                            banks[5] | 1U,
                                                            banks[0],
                                                            banks[1],
                                                            banks[2],
                                                            banks[3]};
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const unsigned bank =
                                ((pages[slot ^ (flip ? 4U : 0U)] & kChrMasks[mode]) |
                                 (middle & (255U ^ kChrMasks[mode]) & 248U) | (intermediate << 8U) |
                                 (outer << 11U)) %
                                4096U;
                            const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                            CAPTURE(mode, value, intermediate, outer, flip, slot, bank);
                            REQUIRE(mapper->read_chr(addr) == chr_cell(bank, 0x123U));
                        }
                    }
                }
            }
        }
    }

    auto shared = make_board(128UZ * kKiB, 0U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        shared->write_ppu_register(0x2012U, static_cast<Byte>(bank));
        const unsigned physical = (bank * 1024U + 0x123U) % (128U * 1024U);
        REQUIRE(shared->read_chr(0x1123U) == prg_cell(physical / 8192U, physical % 8192U));
    }
}

TEST_CASE("Mapper 427 preserves CIRAM immutable pattern ROM and absent expansion audio",
          "[mapper427][ciram][ram][audio][reset]") {
    auto mapper = make_board(128UZ * kKiB, 8192U, 0U, false, false);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_expansion(0x4106U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xBFFEU, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    for (Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU, 0x2000U, 0x3EFFU}) {
        REQUIRE_FALSE(mapper->ciram_address(addr));
    }
    mapper->write_expansion(0x4105U, 0x20U);
    for (Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU}) {
        REQUIRE(mapper->ciram_address(addr) == static_cast<Address>(addr & 0x07FFU));
    }
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->unmirrored_nametables());

    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto before = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == before);
    }
    const auto before = save(*mapper);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_frame_start();
    REQUIRE(save(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(save(*mapper) == save(*make_board(128UZ * kKiB, 8192U)));
}

TEST_CASE("Mapper 427 clocks qualified A12 and HSYNC IRQs", "[mapper427][irq][reset]") {
    auto mapper = make_board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_expansion(0x4101U, static_cast<Byte>(latch));
        mapper->write_expansion(0x4102U, 0U);
        mapper->write_expansion(0x4104U, 0U);
        mapper->on_ppu_hblank();
        REQUIRE_FALSE(mapper->irq_pending());
        edge(*mapper);
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        REQUIRE(save(*mapper)[kCounterOffset] == latch);
        for (unsigned count = 0U; count < latch; ++count) {
            edge(*mapper);
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    for (unsigned low = 0U; low <= 3U; ++low) {
        mapper->reset();
        mapper->write_expansion(0x4104U, 0U);
        edge(*mapper, low);
        REQUIRE(mapper->irq_pending() == (low == 3U));
    }

    mapper->reset();
    mapper->write_expansion(0x410BU, 0x80U);
    mapper->write_expansion(0x4101U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4103U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 427 protection transform works on both documented GPIO wirings",
          "[mapper427][protection][bus]") {
    for (Byte submapper = 0U; submapper < 2U; ++submapper) {
        auto mapper = make_board(128UZ * kKiB, 128UZ * kKiB, submapper);
        ProtectionBus bus(*mapper, submapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            auto expected = static_cast<Byte>(0U - value);
            expected = static_cast<Byte>((expected >> 4U) | (expected << 4U));
            const auto actual = bus.exchange(static_cast<Byte>(value));
            const auto state = save(*mapper);
            CAPTURE(submapper,
                    value,
                    expected,
                    actual,
                    state[kProtectionStageOffset],
                    state[kProtectionCommandOffset],
                    state[kProtectionStageOffset + 2U],
                    state[kProtectionStageOffset + 5U]);
            REQUIRE(actual == expected);
        }
        REQUIRE(save(*mapper)[kProtectionStageOffset] == 19U);
        REQUIRE(save(*mapper)[kProtectionCommandOffset] == 0x80U);
        mapper->reset();
        REQUIRE(save(*mapper) == save(*make_board(128UZ * kKiB, 128UZ * kKiB, submapper)));
        REQUIRE(mapper->read_expansion(submapper == 0U ? 0x414BU : 0x4153U) == 0xFFU);
    }
}

TEST_CASE("Mapper 427 optional 24C04 reads writes persists and rejects foreign addresses",
          "[mapper427][eeprom][bus][ram][reset]") {
    for (Byte submapper = 0U; submapper < 2U; ++submapper) {
        auto mapper = make_board(128UZ * kKiB, 128UZ * kKiB, submapper, true);
        EepromBus bus(*mapper);
        for (Word address : {0x0000U, 0x0001U, 0x007FU, 0x00FFU, 0x0100U, 0x0180U, 0x01FFU}) {
            const auto value = static_cast<Byte>(address * 29U + 0x5BU);
            CAPTURE(submapper, address, value);
            bus.write(address, value);
            REQUIRE(bus.read(address) == value);
            REQUIRE(mapper->battery_ram()[address] == value);
        }

        bus.write(0x00FEU, static_cast<Byte>(0x00FEU * 29U + 0x5BU));
        bus.select_address(0x00FEU);
        bus.repeated_start();
        REQUIRE(bus.send(0xA1U));
        REQUIRE(bus.read_byte() == static_cast<Byte>(0x00FEU * 29U + 0x5BU));
        REQUIRE(bus.read_byte() == static_cast<Byte>(0x00FFU * 29U + 0x5BU));
        bus.stop();

        bus.start();
        REQUIRE_FALSE(bus.send(0xA4U));
        bus.stop();
        const auto before = save(*mapper);
        mapper->reset();
        REQUIRE(mapper->battery_ram().data() != nullptr);
        REQUIRE(std::equal(mapper->battery_ram().begin(),
                           mapper->battery_ram().end(),
                           before.begin() + static_cast<std::ptrdiff_t>(kEepromDataOffset)));
        REQUIRE(bus.read(0x0100U) == static_cast<Byte>(0x0100U * 29U + 0x5BU));
        mapper->power_on();
        REQUIRE(bus.read(0x01FFU) == static_cast<Byte>(0x01FFU * 29U + 0x5BU));
    }

    auto imported = make_board(128UZ * kKiB, 128UZ * kKiB, 0U, true);
    std::array<Byte, Mapper427::kEepromSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<Byte>(index * 17U + 3U);
    }
    imported->set_battery_ram(bytes);
    REQUIRE(std::ranges::equal(imported->battery_ram(), bytes));
    const auto before = save(*imported);
    imported->set_battery_ram(std::span<const Byte>(bytes).first(511U));
    REQUIRE(save(*imported) == before);
    EepromBus bus(*imported);
    REQUIRE(bus.read(0x0123U) == bytes[0x0123U]);
}

TEST_CASE("Mapper 427 reset restores bounded defaults and retains only EEPROM bytes",
          "[mapper427][reset][irq][protection][eeprom]") {
    auto mapper = make_board(128UZ * kKiB, 128UZ * kKiB, 0U, true);
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4101U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4106U, 1U);
    mapper->write_expansion(0x4107U, 0x12U);
    mapper->write_expansion(0x410AU, 0x34U);
    mapper->write_expansion(0x410BU, 0x4FU);
    mapper->write_ppu_register(0x2012U, 0x56U);
    mapper->write_ppu_register(0x2018U, 0x70U);
    mapper->write_ppu_register(0x201AU, 0x78U);
    edge(*mapper);
    ProtectionBus protection(*mapper, 0U);
    REQUIRE(protection.exchange(0x42U) == 0xEBU);
    EepromBus eeprom(*mapper);
    eeprom.write(0x0142U, 0x96U);
    REQUIRE(mapper->irq_pending());
    REQUIRE_FALSE(save(*mapper) == save(*make_board(128UZ * kKiB, 128UZ * kKiB, 0U, true)));

    mapper->reset();
    const auto expected = save(*make_board(128UZ * kKiB, 128UZ * kKiB, 0U, true));
    auto current = save(*mapper);
    REQUIRE(std::equal(expected.begin(),
                       expected.begin() + static_cast<std::ptrdiff_t>(kEepromDataOffset),
                       current.begin()));
    REQUIRE(current[kEepromDataOffset + 0x0142U] == 0x96U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_expansion(0x414BU) == 0xFFU);
    REQUIRE(protection.exchange(0x42U) == 0xEBU);
    REQUIRE(eeprom.read(0x0142U) == 0x96U);
}

TEST_CASE("Mapper 427 bounded snapshots replay partial serial state and reject damage atomically",
          "[mapper427][state][irq][protection][eeprom]") {
    auto mapper = make_board(128UZ * kKiB, 128UZ * kKiB, 0U, true);
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4105U, 0xA5U);
    mapper->write_expansion(0x4106U, 1U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x4108U, 0x59U);
    mapper->write_expansion(0x4109U, 0xA6U);
    mapper->write_expansion(0x410AU, 0x5AU);
    mapper->write_expansion(0x410BU, 0x47U);
    for (unsigned reg = 0U; reg < 6U; ++reg) {
        mapper->write_ppu_register(static_cast<Address>(0x2012U + reg),
                                   static_cast<Byte>(0x31U + reg * 19U));
    }
    mapper->write_ppu_register(0x2018U, 0x50U);
    mapper->write_ppu_register(0x201AU, 0xABU);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();

    ProtectionBus protection(*mapper, 0U);
    protection.start();
    protection.write_byte(0x80U);
    protection.write_byte(0x69U);
    protection.stop();
    for (unsigned bit = 0U; bit < 3U; ++bit) {
        static_cast<void>(protection.read_bit());
    }

    EepromBus eeprom(*mapper);
    eeprom.write(0x0040U, 0xA6U);
    eeprom.select_address(0x0040U);
    eeprom.repeated_start();
    REQUIRE(eeprom.send(0xA1U));
    Byte leading = 0U;
    for (unsigned bit = 0U; bit < 3U; ++bit) {
        leading = static_cast<Byte>((static_cast<unsigned>(leading) << 1U) |
                                    (eeprom.read_bit() ? 1U : 0U));
    }

    const auto expected = save(*mapper);
    REQUIRE(expected.size() == kStateSize + Mapper427::kEepromSize);
    REQUIRE(expected[kIrqOffset] == 1U);
    REQUIRE(expected[kPinsOffset] == 4U);
    REQUIRE(expected[kProtectionStageOffset] == 19U);
    REQUIRE(expected[kEepromStageOffset] == 11U);
    REQUIRE(expected[kEepromBitOffset] == 3U);
    REQUIRE(expected[kEepromAddressOffset] == 0x40U);
    REQUIRE(expected[kEepromAddressOffset + 1U] == 0U);

    mapper->reset();
    load(*mapper, expected);
    REQUIRE(save(*mapper) == expected);
    Byte trailing = 0U;
    for (unsigned bit = 3U; bit < 8U; ++bit) {
        trailing = static_cast<Byte>((static_cast<unsigned>(trailing) << 1U) |
                                     (eeprom.read_bit() ? 1U : 0U));
    }
    REQUIRE(((static_cast<unsigned>(leading) << 5U) | trailing) == 0xA6U);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(true);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    load(*mapper, expected);

    auto with_tail = expected;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(save(*mapper) == expected);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == expected);
    };
    for (std::size_t length = 0U; length < expected.size(); ++length) {
        CAPTURE(length);
        reject(std::span(expected).first(length));
    }
    const std::array<std::pair<std::size_t, Byte>, 20> bad_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 1U},
        {kSubmapperOffset, 1U},
        {kControlOffset, 0x08U},
        {kMirrorOffset, 2U},
        {kModeOffset, 0x10U},
        {kIntermediateOffset, 0x80U},
        {kIrqOffset, 2U},
        {kIrqOffset, 4U},
        {kPinsOffset, 3U},
        {kPinsOffset, 7U},
        {kProtectionStageOffset, 20U},
        {kProtectionCommandOffset, 0x81U},
        {kProtectionClockOffset, 2U},
        {kEepromStageOffset, 15U},
        {kEepromClockOffset, 2U},
        {kEepromAddressOffset + 1U, 2U},
        {kEepromBitOffset, 9U},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto bad = expected;
        bad[offset] = value;
        CAPTURE(offset, value);
        reject(bad);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper427>& other) {
        const auto before = save(*other);
        StateReader reader(expected);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*other) == before);
    };
    auto submapper = make_board(128UZ * kKiB, 128UZ * kKiB, 1U, true);
    mismatch(submapper);
    auto prg = make_board(256UZ * kKiB, 128UZ * kKiB, 0U, true);
    mismatch(prg);
    auto chr = make_board(128UZ * kKiB, 256UZ * kKiB, 0U, true);
    mismatch(chr);
    auto plain = make_board();
    mismatch(plain);

    auto changed = expected;
    changed[kOuterOffset] ^= 0xFFU;
    changed[kEepromDataOffset + 0x40U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(save(*mapper) == changed);
}

} // namespace
