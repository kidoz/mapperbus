#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper529.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kPrgBankOffset = 12UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 29UZ;
constexpr std::size_t kIrqLatchOffset = 30UZ;
constexpr std::size_t kIrqCounterOffset = 31UZ;
constexpr std::size_t kIrqPrescalerOffset = 32UZ;
constexpr std::size_t kIrqControlOffset = 34UZ;
constexpr std::size_t kIrqEnabledOffset = 35UZ;
constexpr std::size_t kIrqPendingOffset = 36UZ;
constexpr std::size_t kEepromPinsOffset = 37UZ;
constexpr std::size_t kEepromWriteEnabledOffset = 41UZ;
constexpr std::size_t kEepromPhaseOffset = 42UZ;
constexpr std::size_t kEepromPendingOffset = 43UZ;
constexpr std::size_t kEepromCommandOffset = 44UZ;
constexpr std::size_t kEepromCommandBitsOffset = 46UZ;
constexpr std::size_t kEepromAddressOffset = 47UZ;
constexpr std::size_t kEepromDataBitsOffset = 50UZ;
constexpr std::size_t kEepromBusyOffset = 51UZ;
constexpr std::size_t kEepromPendingAddressOffset = 55UZ;
constexpr std::size_t kPayloadOffset = 56UZ;
constexpr Word kEepromWriteCycles = 9000U;

INesHeader header(std::size_t chr_size = kChrSize,
                  bool eeprom = false,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 529U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = chr_size;
    result.prg_nvram_size = eeprom ? Mapper529::kEepromSize : 0U;
    result.has_battery = eeprom;
    result.chr_ram_size = chr_size == 0U ? kChrRamSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 37U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 71U +
                             (offset >> 18U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper529> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 bool eeprom = false,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(chr_size, eeprom, mirror);
    h.prg_rom_size = prg_size;
    REQUIRE(Mapper529::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper529>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper529& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper529& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
}

void require_prg(Mapper529& mapper,
                 Address addr,
                 std::size_t physical,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    CAPTURE(addr, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical % prg_size, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper529& mapper,
                 Address addr,
                 std::size_t physical,
                 Byte salt = 0U,
                 std::size_t chr_size = kChrSize) {
    CAPTURE(addr, physical, salt, chr_size);
    REQUIRE(mapper.read_chr(addr) == cell(physical % chr_size, salt ^ 0x5AU));
}

void set_chr_low(Mapper529& mapper, unsigned slot, Byte value) {
    mapper.write_prg(static_cast<Address>(0xB000U + slot / 2U * 0x1000U + (slot & 1U) * 8U), value);
}

void set_chr_high(Mapper529& mapper, unsigned slot, Byte value) {
    mapper.write_prg(static_cast<Address>(0xB004U + slot / 2U * 0x1000U + (slot & 1U) * 8U), value);
}

void pins(Mapper529& mapper, bool select, bool clock, bool data = false) {
    mapper.write_prg(0x8800U,
                     static_cast<Byte>((select ? 4U : 0U) | (clock ? 2U : 0U) | (data ? 1U : 0U)));
}

Byte rising_bit(Mapper529& mapper, bool data = false) {
    pins(mapper, true, false, data);
    pins(mapper, true, true, data);
    return mapper.read_expansion(0x5000U);
}

void issue_command(Mapper529& mapper, unsigned opcode, unsigned address) {
    pins(mapper, false, false);
    pins(mapper, true, false);
    const unsigned command = 0x200U | ((opcode & 3U) << 7U) | (address & 0x7FU);
    for (unsigned bit = 0U; bit < 10U; ++bit) {
        static_cast<void>(rising_bit(mapper, (command & (1U << (9U - bit))) != 0U));
    }
}

void end_command(Mapper529& mapper) {
    pins(mapper, false, false);
}

void send_word(Mapper529& mapper, Word value) {
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        static_cast<void>(rising_bit(mapper, (value & (0x8000U >> bit)) != 0U));
    }
}

Word stream_word(Mapper529& mapper) {
    Word result = 0U;
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        result = static_cast<Word>((result << 1U) | rising_bit(mapper));
    }
    return result;
}

Word read_word(Mapper529& mapper, Byte address) {
    issue_command(mapper, 2U, address);
    REQUIRE(mapper.read_expansion(0x5000U) == 0U);
    const auto result = stream_word(mapper);
    end_command(mapper);
    return result;
}

void finish_write(Mapper529& mapper) {
    end_command(mapper);
    pins(mapper, true, false);
    REQUIRE(mapper.read_expansion(0x5000U) == 0U);
    for (unsigned cycle = 0U; cycle < kEepromWriteCycles; ++cycle) {
        mapper.clock_cpu();
    }
    REQUIRE(mapper.read_expansion(0x5000U) == 1U);
    end_command(mapper);
}

void enable_eeprom(Mapper529& mapper) {
    issue_command(mapper, 0U, 0x60U);
    end_command(mapper);
}

void write_word(Mapper529& mapper, Byte address, Word value) {
    issue_command(mapper, 1U, address);
    send_word(mapper, value);
    finish_write(mapper);
}

TEST_CASE("Mapper 529 validates ROM RAM EEPROM and CIRAM geometry with bounded cold state",
          "[mapper529][geometry][ram][state]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = 0x8000UZ; prg <= kPrgSize; prg *= 2U) {
            REQUIRE(Mapper529::validate_geometry(header(kChrSize, false, mirror), prg, kChrSize));
            REQUIRE(Mapper529::validate_geometry(header(0U, true, mirror), prg, 0U));
            for (std::size_t chr = 0x2000UZ; chr <= kChrSize; chr *= 2U) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper529::validate_geometry(header(chr, true, mirror), prg, chr));
            }
        }
    }
    for (const auto& [chr_size, eeprom] : {std::pair{kChrSize, false},
                                           std::pair{kChrSize, true},
                                           std::pair{0UZ, false},
                                           std::pair{0UZ, true}}) {
        auto mapper = board(kPrgSize, chr_size, eeprom, MirrorMode::Horizontal, 0x35U);
        auto different_rom = board(kPrgSize, chr_size, eeprom, MirrorMode::Horizontal, 0xA0U);
        const auto cold = state(*mapper);
        CAPTURE(chr_size, eeprom);
        REQUIRE(cold == state(*different_rom));
        REQUIRE(cold.size() == kPayloadOffset + (chr_size == 0U ? kChrRamSize : 0U) +
                                   (eeprom ? Mapper529::kEepromSize : 0U));
        REQUIRE(cold.size() <= 8504U);
        REQUIRE(cold[kPrgBankOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->battery_ram().size() == (eeprom ? Mapper529::kEepromSize : 0U));
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
        require_prg(*mapper, 0xB123U, 0x3123U, 0x35U);
        require_prg(*mapper, 0xC123U, kPrgSize - kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xF123U, kPrgSize - kPrgBankSize + 0x3123U, 0x35U);
        if (eeprom) {
            REQUIRE(std::ranges::all_of(mapper->battery_ram(),
                                        [](Byte value) { return value == 0xFFU; }));
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
        REQUIRE_FALSE(Mapper529::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 16U, 23U, 25U, 528U, 530U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0x0C000UZ, 0x100000UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x3000UZ, 0x060000UZ, 0x080001UZ, 0x100000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(kChrSize, false, mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 11U; ++field) {
        auto h = header();
        std::size_t chr = kChrSize;
        switch (field) {
        case 0U:
            h.has_trainer = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = Mapper529::kEepromSize;
            break;
        case 5U:
            h.chr_ram_size = 0x2000U;
            break;
        case 6U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 7U:
            h.chr_ram_size = 0U;
            chr = 0U;
            break;
        case 8U:
            h.chr_ram_size = 0x1000U;
            chr = 0U;
            break;
        case 9U:
            h.chr_ram_size = 0x4000U;
            chr = 0U;
            break;
        default:
            h.prg_ram_size = h.prg_nvram_size = Mapper529::kEepromSize;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, chr);
    }
}

TEST_CASE("Mapper 529 exhaustively decodes VRC4e and overlapping Microwire addresses",
          "[mapper529][decoding][banking][bus][irq][eeprom][state]") {
    auto mapper = board(kPrgSize, kChrSize, true);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = state(*mapper);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto group = raw >> 12U;
        const auto port = (raw >> 2U) & 3U;
        if (raw >= 0x8000U && (raw & 0x8800U) == 0x8800U) {
            expected[kEepromPinsOffset] = (value >> 2U) & 1U;
            expected[kEepromPinsOffset + 1U] = (value >> 1U) & 1U;
            expected[kEepromPinsOffset + 2U] = value & 1U;
            if ((value & 6U) == 6U) {
                expected[kEepromCommandOffset] = value & 1U;
                expected[kEepromCommandBitsOffset] = 1U;
            }
        }
        if (raw >= 0x8000U && group == 9U && port == 0U) {
            constexpr std::array<Byte, 4> modes{1U, 0U, 3U, 4U};
            expected[kMirrorOffset] = modes[value & 3U];
        } else if (raw >= 0x8000U && group == 10U) {
            expected[kPrgBankOffset] = value & 0x1FU;
        } else if (raw >= 0x8000U && group >= 11U && group <= 14U) {
            const auto offset = kChrBanksOffset + ((group - 11U) * 2U + port / 2U) * 2U;
            if ((port & 1U) == 0U) {
                expected[offset] = value & 0x0FU;
                expected[offset + 1U] = 0U;
            } else {
                expected[offset] = (value & 0x1FU) << 4U;
                expected[offset + 1U] = (value & 0x1FU) >> 4U;
            }
        } else if (raw >= 0x8000U && group == 15U && port == 0U) {
            expected[kIrqLatchOffset] = value & 0x0FU;
        } else if (raw >= 0x8000U && group == 15U && port == 1U) {
            expected[kIrqLatchOffset] = (value & 0x0FU) << 4U;
        } else if (raw >= 0x8000U && group == 15U && port == 2U) {
            expected[kIrqControlOffset] = value & 7U;
            expected[kIrqEnabledOffset] = (value >> 1U) & 1U;
        }

        CAPTURE(raw, value, group, port);
        mapper->write_prg(static_cast<Address>(raw), value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0U);
        }
        const bool expansion = raw >= 0x5000U && raw < 0x6000U;
        REQUIRE(mapper->maps_expansion(static_cast<Address>(raw)) == expansion);
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(raw)) == (expansion ? 1U : 0xFFU));
        mapper->write_expansion(static_cast<Address>(raw), static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
    }

    auto without_eeprom = board();
    const auto before = state(*without_eeprom);
    without_eeprom->write_prg(0x8806U, 7U);
    REQUIRE(state(*without_eeprom) == before);
    REQUIRE_FALSE(without_eeprom->maps_expansion(0x5000U));
    REQUIRE(without_eeprom->read_expansion(0x5000U) == 0U);
}

TEST_CASE("Mapper 529 ignores 800x and selects one 16 KiB A00x bank plus fixed final bank",
          "[mapper529][banking][prg][bus]") {
    auto mapper = board(kPrgSize, kChrSize, false, MirrorMode::Vertical, 0x35U);
    for (unsigned value = 0U; value < 256U; ++value) {
        for (const Address reg : {0x8000U, 0x8004U, 0x8008U, 0x800CU, 0x8FFFU}) {
            mapper->write_prg(reg, static_cast<Byte>(value ^ 0xFFU));
        }
        mapper->write_prg(static_cast<Address>(0xA000U | (value & 0xF0U)),
                          static_cast<Byte>(value));
        CAPTURE(value);
        require_prg(*mapper, 0x8123U, (value & 0x1FU) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xB123U, (value & 0x1FU) * kPrgBankSize + 0x3123U, 0x35U);
        require_prg(*mapper, 0xC123U, kPrgSize - kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xF123U, kPrgSize - kPrgBankSize + 0x3123U, 0x35U);
    }
    auto small = board(0x8000UZ, 0x2000UZ, false, MirrorMode::Vertical, 0x35U);
    small->write_prg(0xA000U, 0x1FU);
    require_prg(*small, 0x8123U, 0x4123U, 0x35U, 0x8000UZ);
    require_prg(*small, 0xC123U, 0x4123U, 0x35U, 0x8000UZ);
}

TEST_CASE("Mapper 529 combines nine-bit CHR registers and protects ROM or banks 8 KiB RAM",
          "[mapper529][banking][chr][ram][bus]") {
    auto mapper = board(kPrgSize, kChrSize, false, MirrorMode::Vertical, 0x35U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned low = 0U; low < 256U; low += 17U) {
            for (unsigned high = 0U; high < 256U; high += 31U) {
                set_chr_low(*mapper, slot, static_cast<Byte>(low));
                set_chr_high(*mapper, slot, static_cast<Byte>(high));
                const auto bank = (low & 0x0FU) | ((high & 0x1FU) << 4U);
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x0234U);
                CAPTURE(slot, low, high, bank);
                require_chr(*mapper, addr, bank * kChrBankSize + 0x0234U, 0x35U);
                mapper->write_chr(addr, static_cast<Byte>(low ^ high));
                require_chr(*mapper, addr, bank * kChrBankSize + 0x0234U, 0x35U);
            }
        }
    }

    auto ram = board(kPrgSize, 0U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_low(*ram, slot, static_cast<Byte>(slot + 4U));
        set_chr_high(*ram, slot, static_cast<Byte>(slot + 1U));
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x0123U);
        ram->write_chr(addr, static_cast<Byte>(0xA0U + slot));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x0123U);
        CAPTURE(slot);
        REQUIRE(ram->read_chr(addr) == static_cast<Byte>(0xA0U + slot));
        REQUIRE(ram->read_chr(static_cast<Address>(addr + 0x2000U)) == 0U);
    }
    const auto before = state(*ram);
    ram->write_chr(0x2000U, 0xA5U);
    ram->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*ram) == before);
}

TEST_CASE("Mapper 529 register 9000 selects V H S0 S1 with ordinary console CIRAM",
          "[mapper529][ciram][banking][bus]") {
    auto mapper = board(kPrgSize, kChrSize, false, MirrorMode::Horizontal);
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0x9000U | (value & 0xF0U)),
                          static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
        for (Address addr = 0x2000U; addr < 0x3F00U; addr += 0x0101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 529 VRC IRQ supports every latch cycle mode prescaled mode and acknowledgements",
          "[mapper529][irq][audio][bus][state]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch & 0x0FU));
        mapper->write_prg(0xF004U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF008U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(latch);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xF00CU, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 3U);
    for (const unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(period);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF00CU, 0U);
    }
    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 1U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF008U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    const auto before = state(*mapper);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->clock_audio();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->observe_ppu_a12((cycle & 1U) != 0U);
        mapper->observe_ppu_address(static_cast<Address>(cycle & 0x3FFFU));
        mapper->observe_ppu_read(static_cast<Address>(cycle & 0x3FFFU));
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    mapper->acknowledge_irq();
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->led_outputs() == std::nullopt);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 529 Microwire enforces write enable busy timing and 16-bit word addressing",
          "[mapper529][eeprom][bus][ram][state]") {
    auto mapper = board(kPrgSize, kChrSize, true);
    REQUIRE(mapper->maps_expansion(0x5000U));
    REQUIRE(mapper->maps_expansion(0x5FFFU));
    REQUIRE(mapper->expansion_read_mask(0x5000U) == 1U);
    REQUIRE(mapper->read_expansion(0x5000U) == 1U);

    issue_command(*mapper, 1U, 7U);
    send_word(*mapper, 0x1234U);
    end_command(*mapper);
    REQUIRE(read_word(*mapper, 7U) == 0xFFFFU);

    enable_eeprom(*mapper);
    REQUIRE(state(*mapper)[kEepromWriteEnabledOffset] == 1U);
    write_word(*mapper, 7U, 0x1234U);
    write_word(*mapper, 127U, 0xABCDU);
    write_word(*mapper, 0U, 0x5678U);
    REQUIRE(read_word(*mapper, 7U) == 0x1234U);
    REQUIRE(mapper->battery_ram()[14U] == 0x34U);
    REQUIRE(mapper->battery_ram()[15U] == 0x12U);

    issue_command(*mapper, 2U, 127U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(stream_word(*mapper) == 0xABCDU);
    REQUIRE(stream_word(*mapper) == 0x5678U);
    end_command(*mapper);

    issue_command(*mapper, 0U, 0U);
    end_command(*mapper);
    REQUIRE(state(*mapper)[kEepromWriteEnabledOffset] == 0U);
    issue_command(*mapper, 1U, 7U);
    send_word(*mapper, 0xFFFFU);
    end_command(*mapper);
    REQUIRE(read_word(*mapper, 7U) == 0x1234U);

    enable_eeprom(*mapper);
    issue_command(*mapper, 1U, 7U);
    send_word(*mapper, 0xBEEFU);
    end_command(*mapper);
    pins(*mapper, true, false);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    for (unsigned cycle = 0U; cycle < kEepromWriteCycles - 1U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(mapper->battery_ram()[14U] == 0x34U);
    REQUIRE(mapper->battery_ram()[15U] == 0x12U);
    mapper->clock_cpu();
    end_command(*mapper);
    REQUIRE(read_word(*mapper, 7U) == 0xBEEFU);
    issue_command(*mapper, 1U, 7U);
    send_word(*mapper, 0xBEEFU);
    finish_write(*mapper);
    REQUIRE(read_word(*mapper, 7U) == 0xBEEFU);

    issue_command(*mapper, 3U, 7U);
    finish_write(*mapper);
    REQUIRE(read_word(*mapper, 7U) == 0xFFFFU);
    issue_command(*mapper, 0U, 0x20U);
    send_word(*mapper, 0xCAFEU);
    finish_write(*mapper);
    REQUIRE(read_word(*mapper, 0U) == 0xCAFEU);
    REQUIRE(read_word(*mapper, 63U) == 0xCAFEU);
    REQUIRE(read_word(*mapper, 127U) == 0xCAFEU);
    issue_command(*mapper, 0U, 0x40U);
    finish_write(*mapper);
    REQUIRE(std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0xFFU; }));
}

TEST_CASE("Mapper 529 reset restores VRC state retains CHR RAM and preserves independent EEPROM",
          "[mapper529][reset][ram][banking][ciram][irq][eeprom]") {
    auto mapper = board(kPrgSize, 0U, true, MirrorMode::Horizontal, 0x35U);
    const auto cold = state(*mapper);
    mapper->write_prg(0xA000U, 0x17U);
    set_chr_low(*mapper, 7U, 0x0FU);
    mapper->write_chr(0x1FFFU, 0xA7U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF008U, 6U);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
    }
    enable_eeprom(*mapper);
    write_word(*mapper, 3U, 0x2468U);
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto reset = state(*mapper);
    REQUIRE(std::ranges::equal(std::span(reset).first(kEepromPinsOffset),
                               std::span(cold).first(kEepromPinsOffset)));
    REQUIRE(std::ranges::equal(std::span(reset).subspan(kEepromPinsOffset),
                               std::span(programmed).subspan(kEepromPinsOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    set_chr_low(*mapper, 7U, 7U);
    REQUIRE(mapper->read_chr(0x1FFFU) == 0xA7U);
    REQUIRE(read_word(*mapper, 3U) == 0x2468U);
    require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
    require_prg(*mapper, 0xC123U, kPrgSize - kPrgBankSize + 0x0123U, 0x35U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    const auto powered = state(*mapper);
    REQUIRE(powered != cold);
    REQUIRE(std::ranges::all_of(std::span<const Byte>(powered).subspan(kPayloadOffset, kChrRamSize),
                                [](Byte value) { return value == 0U; }));
    REQUIRE(read_word(*mapper, 3U) == 0x2468U);
    REQUIRE(state(*mapper)[kEepromWriteEnabledOffset] == 0U);

    const auto saved_data =
        std::vector<Byte>(mapper->battery_ram().begin(), mapper->battery_ram().end());
    std::array<Byte, Mapper529::kEepromSize> imported{};
    for (std::size_t index = 0U; index < imported.size(); ++index) {
        imported[index] = static_cast<Byte>(index * 37U + 5U);
    }
    mapper->set_battery_ram(imported);
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
    mapper->set_battery_ram(std::span(imported).first(255U));
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
    mapper->set_battery_ram(saved_data);
    REQUIRE(std::ranges::equal(mapper->battery_ram(), saved_data));
    mapper->set_shared_nvram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 529 snapshots replay partial IRQ CHR RAM and Microwire reads without ROM",
          "[mapper529][state][atomic][banking][irq][ram][eeprom]") {
    auto mapper = board(kPrgSize, 0U, true, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0xA000U, 0x15U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_low(*mapper, slot, static_cast<Byte>(slot + 3U));
        set_chr_high(*mapper, slot, static_cast<Byte>(slot));
        mapper->write_chr(static_cast<Address>(slot * kChrBankSize + 0x123U),
                          static_cast<Byte>(slot * 29U + 7U));
    }
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    enable_eeprom(*mapper);
    write_word(*mapper, 9U, 0x1369U);
    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    issue_command(*mapper, 2U, 9U);
    Word prefix = 0U;
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        prefix = static_cast<Word>((prefix << 1U) | rising_bit(*mapper));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kPayloadOffset + kChrRamSize + Mapper529::kEepromSize);
    REQUIRE(saved.size() == 8504U);
    REQUIRE(saved[0U] == 0x90U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kPrgBankOffset] == 0x15U);
    REQUIRE(saved[kMirrorOffset] == static_cast<Byte>(MirrorMode::SingleUpper));
    REQUIRE(saved[kIrqCounterOffset] == 0xFFU);
    REQUIRE(saved[kEepromDataBitsOffset] == 5U);

    auto copy = board(kPrgSize, 0U, true, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    require_prg(*copy, 0x8123U, 0x15U * kPrgBankSize + 0x0123U, 0xA0U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(copy->read_chr(static_cast<Address>(slot * kChrBankSize + 0x123U)) ==
                static_cast<Byte>(slot * 29U + 7U));
    }
    Word suffix = 0U;
    for (unsigned bit = 0U; bit < 11U; ++bit) {
        suffix = static_cast<Word>((suffix << 1U) | rising_bit(*copy));
    }
    REQUIRE(static_cast<Word>((prefix << 11U) | suffix) == 0x1369U);
    end_command(*copy);
    copy->clock_cpu();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE_FALSE(copy->irq_pending());
}

TEST_CASE("Mapper 529 snapshots resume a partially shifted EEPROM write atomically",
          "[mapper529][state][atomic][eeprom][ram]") {
    auto mapper = board(kPrgSize, kChrSize, true);
    enable_eeprom(*mapper);
    issue_command(*mapper, 1U, 12U);
    constexpr Word value = 0xA6B8U;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        static_cast<void>(rising_bit(*mapper, (value & (0x8000U >> bit)) != 0U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved[kEepromPhaseOffset] == 3U);
    REQUIRE(saved[kEepromPendingOffset] == 0U);
    REQUIRE(saved[kEepromDataBitsOffset] == 8U);

    auto copy = board(kPrgSize, kChrSize, true, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (unsigned bit = 8U; bit < 16U; ++bit) {
        static_cast<void>(rising_bit(*copy, (value & (0x8000U >> bit)) != 0U));
    }
    finish_write(*copy);
    REQUIRE(read_word(*copy, 12U) == value);
}

TEST_CASE("Mapper 529 snapshots resume bounded EEPROM programming without duplicating ROM",
          "[mapper529][state][atomic][eeprom][ram]") {
    auto mapper = board(kPrgSize, kChrSize, true);
    enable_eeprom(*mapper);
    issue_command(*mapper, 1U, 11U);
    send_word(*mapper, 0x55AAU);
    end_command(*mapper);
    pins(*mapper, true, false);
    for (unsigned cycle = 0U; cycle < 123U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kPayloadOffset + Mapper529::kEepromSize);
    REQUIRE((saved[kEepromBusyOffset] != 0U || saved[kEepromBusyOffset + 1U] != 0U));

    auto copy = board(kPrgSize, kChrSize, true, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (unsigned cycle = 123U; cycle < kEepromWriteCycles; ++cycle) {
        copy->clock_cpu();
    }
    end_command(*copy);
    REQUIRE(read_word(*copy, 11U) == 0x55AAU);
    for (unsigned cycle = 123U; cycle < kEepromWriteCycles; ++cycle) {
        mapper->clock_cpu();
    }
    end_command(*mapper);
    REQUIRE(read_word(*mapper, 11U) == 0x55AAU);
}

TEST_CASE("Mapper 529 rejects truncated damaged and mismatched states atomically",
          "[mapper529][state][atomic][geometry][irq][eeprom][ram]") {
    auto mapper = board(kPrgSize, 0U, true);
    mapper->write_prg(0xA000U, 0x15U);
    set_chr_low(*mapper, 7U, 0x0FU);
    mapper->write_chr(0x1FFFU, 0xA7U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    enable_eeprom(*mapper);
    issue_command(*mapper, 1U, 4U);
    send_word(*mapper, 0xAA55U);
    end_command(*mapper);
    pins(*mapper, true, false);
    for (unsigned cycle = 0U; cycle < 50U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.insert(trailing.end(), {1U, 2U, 3U});
    load_rejected(*mapper, trailing);
    for (unsigned bit = 0U; bit < kPrgBankOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        load_rejected(*mapper, invalid);
    }

    const std::array<std::pair<std::size_t, Byte>, 22> invalid_fields{{
        {kPrgBankOffset, 0x20U},
        {kChrBanksOffset + 1U, 0x02U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 5U},
        {kIrqControlOffset, 8U},
        {kIrqEnabledOffset, 2U},
        {kIrqPendingOffset, 2U},
        {kEepromPinsOffset, 2U},
        {kEepromPinsOffset + 1U, 2U},
        {kEepromPinsOffset + 2U, 2U},
        {kEepromPinsOffset + 3U, 2U},
        {kEepromWriteEnabledOffset, 2U},
        {kEepromPhaseOffset, 6U},
        {kEepromPendingOffset, 5U},
        {kEepromCommandOffset + 1U, 0x04U},
        {kEepromCommandBitsOffset, 11U},
        {kEepromAddressOffset, 0x80U},
        {kEepromDataBitsOffset, 17U},
        {kEepromPendingAddressOffset, 0x80U},
        {kEepromPhaseOffset, 4U},
        {kEepromPendingOffset, 0U},
        {kEepromPhaseOffset, 0U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }
    for (const std::uint16_t prescaler : {0U, 342U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqPrescalerOffset, &prescaler, sizeof(prescaler));
        CAPTURE(prescaler);
        load_rejected(*mapper, invalid);
    }
    for (const std::uint16_t busy : {9001U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kEepromBusyOffset, &busy, sizeof(busy));
        CAPTURE(busy);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U, 0U, true);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kPrgSize, kChrSize, true);
    load_rejected(*wrong_chr, saved);
    auto wrong_eeprom = board(kPrgSize, 0U, false);
    load_rejected(*wrong_eeprom, saved);
    auto wrong_mirror = board(kPrgSize, 0U, true, MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kPrgBankOffset] = 3U;
    changed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
    changed[kPayloadOffset + 0x0123U] = 0x5AU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    require_prg(*mapper, 0x8123U, 3U * kPrgBankSize + 0x0123U);
}

} // namespace
