#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper365.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr std::size_t kChrRamSize = 0x040000UZ;
constexpr std::size_t kPrgBanksOffset = 19UZ;
constexpr std::size_t kChrBanksOffset = 23UZ;
constexpr std::size_t kUnknownPrgOffset = 31UZ;
constexpr std::size_t kPrinterOutputOffset = 35UZ;
constexpr std::size_t kKeyboardRowOffset = 37UZ;
constexpr std::size_t kUnknown4905Offset = 38UZ;
constexpr std::size_t kUnknown4111Offset = 39UZ;
constexpr std::size_t kUnknown491xOffset = 40UZ;
constexpr std::size_t kPrgRamLengthOffset = 56UZ;
constexpr std::size_t kPrgRamDataOffset = 64UZ;
constexpr std::size_t kChrRamLengthOffset = kPrgRamDataOffset + kPrgRamSize;
constexpr std::size_t kChrRamDataOffset = kChrRamLengthOffset + 8UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, std::size_t chr_ram = kChrRamSize) {
    INesHeader result{};
    result.mapper_number = 365U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kPrgRamSize;
    result.chr_ram_size = chr_ram;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 8U) * 31U + (offset >> 13U) * 47U +
                             (offset >> 17U) * 59U + (offset >> 21U) * 61U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper365> board(MirrorMode mirror = MirrorMode::Vertical,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_ram = kChrRamSize,
                                 Byte salt = 0U) {
    const auto h = header(mirror, chr_ram);
    REQUIRE(Mapper365::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper365>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void initialize_chr(Mapper365& mapper, std::size_t size, Byte salt = 0U) {
    for (std::size_t bank = 0U; bank < size / kChrBankSize; ++bank) {
        mapper.write_prg(0xA000U, static_cast<Byte>(bank));
        for (std::size_t offset = 0U; offset < kChrBankSize; ++offset) {
            mapper.write_chr(static_cast<Address>(offset),
                             cell(bank * kChrBankSize + offset, salt));
        }
    }
}

TEST_CASE("Mapper 365 validates decoder-width ROM RAM and CIRAM geometry", "[mapper365]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = 0x8000UZ; prg <= kPrgSize; prg *= 2U) {
            for (std::size_t chr = 0x2000UZ; chr <= kChrRamSize; chr *= 2U) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper365::validate_geometry(header(mirror, chr), prg, 0U));
            }
        }
    }
    for (const unsigned id : {0U, 4U, 241U, 364U, 366U, 531U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper365::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper365::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper365::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x7FFFUZ, 0xC000UZ, kPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper365::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t chr_rom : {1UZ, 0x2000UZ, kChrRamSize}) {
        REQUIRE_FALSE(Mapper365::validate_geometry(header(), kPrgSize, chr_rom));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x3000UZ, 0x2800UZ, kChrRamSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(
            Mapper365::validate_geometry(header(MirrorMode::Vertical, size), kPrgSize, 0U));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper365::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 0x4000U;
            break;
        case 3U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kPrgRamSize;
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 5U:
            h.chr_nvram_size = kChrRamSize;
            break;
        default:
            h.chr_ram_size = 0U;
            h.chr_nvram_size = kChrRamSize;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper365::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 365 decodes all PRG CHR and unknown register aliases", "[mapper365][bus]") {
    auto actual = board(MirrorMode::Vertical, 0x8000UZ, 0x2000UZ);
    auto expected = board(MirrorMode::Vertical, 0x8000UZ, 0x2000UZ);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U && raw < 0xA000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE003U), value);
        } else if (raw >= 0xA000U && raw < 0xC000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE007U), value);
        } else if (raw >= 0xC000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        } else if (raw >= 0x6000U) {
            expected->write_prg(static_cast<Address>(raw), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x6000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x6000U ? 0xFFU : 0U));
    }
}

TEST_CASE("Mapper 365 selects four independent eight-bit PRG banks", "[mapper365][banking]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (unsigned selected = 0U; selected < 4U; ++selected) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0x8000U + selected), static_cast<Byte>(value));
            std::array<Byte, 4> banks{0U, 0U, 0U, 1U};
            banks[selected] = static_cast<Byte>(selected == 3U ? value | 1U : value);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto offset = static_cast<std::size_t>(banks[slot]) * kPrgBankSize + 0x123U;
                CAPTURE(selected, value, slot, banks[slot]);
                REQUIRE(mapper->read_prg(addresses[slot]) == cell(offset));
            }
            REQUIRE(state(*mapper)[kPrgBanksOffset + selected] == banks[selected]);
        }
    }
}

TEST_CASE("Mapper 365 wraps fitted PRG only after full-byte bank selection",
          "[mapper365][banking]") {
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size *= 2U) {
        auto mapper = board(MirrorMode::Vertical, size);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(static_cast<Address>(0x8000U + slot), 0xFFU);
            const auto addr = static_cast<Address>(0x8000U + slot * kPrgBankSize + 0x1FFFU);
            const auto offset = (0xFFUZ * kPrgBankSize + 0x1FFFUZ) % size;
            CAPTURE(size, slot, offset);
            REQUIRE(mapper->read_prg(addr) == cell(offset));
        }
    }
}

TEST_CASE("Mapper 365 selects and writes eight independent CHR RAM banks", "[mapper365][banking]") {
    auto mapper = board();
    initialize_chr(*mapper, kChrRamSize);
    for (unsigned selected = 0U; selected < 8U; ++selected) {
        mapper->reset();
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0xA000U + selected), static_cast<Byte>(value));
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const unsigned bank = slot == selected ? value : 0U;
                const auto offset = bank * kChrBankSize + 0x155U;
                CAPTURE(selected, value, slot, bank);
                REQUIRE(mapper->read_chr(addr) == cell(offset));
            }
            REQUIRE(state(*mapper)[kChrBanksOffset + selected] == value);
        }
    }

    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(0xF8U + slot));
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x321U);
        mapper->write_chr(addr, static_cast<Byte>(slot ^ 0xA5U));
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(slot ^ 0xA5U));
        mapper->write_prg(0xA000U, static_cast<Byte>(0xF8U + slot));
        REQUIRE(mapper->read_chr(0x321U) == static_cast<Byte>(slot ^ 0xA5U));
    }
}

TEST_CASE("Mapper 365 wraps smaller CHR RAM after bank selection", "[mapper365][banking]") {
    for (std::size_t size = 0x2000UZ; size <= kChrRamSize; size *= 2U) {
        auto mapper = board(MirrorMode::Vertical, kPrgSize, size);
        initialize_chr(*mapper, size);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            mapper->write_prg(static_cast<Address>(0xA000U + slot), 0xFFU);
            for (std::size_t offset = 0U; offset < kChrBankSize; offset += 0x101U) {
                const auto physical = (0xFFUZ * kChrBankSize + offset) % size;
                const auto addr = static_cast<Address>(slot * kChrBankSize + offset);
                CAPTURE(size, slot, offset, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical));
                mapper->write_chr(addr, static_cast<Byte>(cell(physical) ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(cell(physical) ^ 0xFFU));
                mapper->write_chr(addr, cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 365 exposes fixed PRG RAM and no persistent storage", "[mapper365][ram][bus]") {
    auto mapper = board();
    for (std::size_t offset = 0U; offset < kPrgRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }
    for (std::size_t offset = 0U; offset < kPrgRamSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    for (const Address addr : {0U, 0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 365 retains expansion writes and drives only printer ready", "[mapper365][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U));
        mapper->write_expansion(addr, value);
        CAPTURE(addr);
        REQUIRE(mapper->maps_expansion(addr) == (addr == 0x4902U));
        REQUIRE(mapper->expansion_read_mask(addr) == (addr == 0x4902U ? 0x80U : 0U));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    mapper->write_expansion(0x4111U, 0x11U);
    mapper->write_expansion(0x4900U, 0U);
    mapper->write_expansion(0x4901U, 1U);
    mapper->write_expansion(0x4904U, 4U);
    mapper->write_expansion(0x4905U, 5U);
    for (unsigned index = 0U; index < 16U; ++index) {
        mapper->write_expansion(static_cast<Address>(0x4910U + index),
                                static_cast<Byte>(0x10U + index));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved[kPrinterOutputOffset] == 0U);
    REQUIRE(saved[kPrinterOutputOffset + 1U] == 1U);
    REQUIRE(saved[kKeyboardRowOffset] == 4U);
    REQUIRE(saved[kUnknown4905Offset] == 5U);
    REQUIRE(saved[kUnknown4111Offset] == 0x11U);
    for (unsigned index = 0U; index < 16U; ++index) {
        REQUIRE(saved[kUnknown491xOffset + index] == 0x10U + index);
    }
    for (const Address addr :
         {0x4903U, 0x4906U, 0x4910U, 0x491FU, 0x5000U, 0x5003U, 0x5080U, 0x5083U}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
    }
}

TEST_CASE("Mapper 365 keeps header CIRAM because its control register is unknown",
          "[mapper365][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (unsigned raw = 0xC000U; raw <= 0xFFFFU; raw += 0x101U) {
            mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
            CAPTURE(mirror, raw);
            REQUIRE(mapper->mirror_mode() == mirror);
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 365 has no documented IRQ audio or pin side effects", "[mapper365][irq][audio]") {
    auto mapper = board();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    const auto before = state(*mapper);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->write_prg_cpu(0x5001U, 0xFFU, true);
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 365 soft reset clears latches and cold power also clears RAM",
          "[mapper365][reset]") {
    auto mapper = board(MirrorMode::Horizontal);
    auto expected = board(MirrorMode::Horizontal);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(0x80U + slot));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(0xF0U + slot));
    }
    mapper->write_prg(0xC001U, 0xA6U);
    mapper->write_prg(0xE000U, 0x5AU);
    mapper->write_expansion(0x4900U, 0x12U);
    mapper->write_expansion(0x4904U, 0x34U);
    mapper->write_expansion(0x491FU, 0x56U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x1FFFU, 0x5CU);
    expected->write_prg(0x6123U, 0xA7U);
    expected->write_prg(0xA007U, 0xF7U);
    expected->write_chr(0x1FFFU, 0x5CU);
    expected->reset();

    mapper->reset();
    REQUIRE(state(*mapper) == state(*expected));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    mapper->write_prg(0xA007U, 0xF7U);
    REQUIRE(mapper->read_chr(0x1FFFU) == 0x5CU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(kPrgBankSize));
    REQUIRE(state(*mapper)[kPrgBanksOffset + 3U] == 1U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board(MirrorMode::Horizontal)));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
}

TEST_CASE("Mapper 365 bounded snapshots replay RAM and latches without ROM", "[mapper365][state]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(slot * 31U + 3U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(slot * 17U + 5U));
    }
    mapper->write_prg(0xC000U, 0x12U);
    mapper->write_prg(0xC001U, 0x34U);
    mapper->write_prg(0xE000U, 0x56U);
    mapper->write_prg(0xE001U, 0x78U);
    mapper->write_expansion(0x4111U, 0x9AU);
    mapper->write_expansion(0x4900U, 0xBCU);
    mapper->write_expansion(0x4901U, 0xDEU);
    mapper->write_expansion(0x4904U, 0xF0U);
    mapper->write_expansion(0x4905U, 0x0FU);
    for (unsigned index = 0U; index < 16U; ++index) {
        mapper->write_expansion(static_cast<Address>(0x4910U + index),
                                static_cast<Byte>(index * 7U));
    }
    for (std::size_t offset = 0U; offset < kPrgRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        mapper->write_chr(addr, static_cast<Byte>(slot * 29U + 7U));
    }

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kChrRamDataOffset + kChrRamSize);
    REQUIRE(saved.size() == 270408U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(std::vector<Byte>(saved.begin() + kUnknownPrgOffset,
                              saved.begin() + kUnknownPrgOffset + 4U) ==
            std::vector<Byte>{0x12U, 0x34U, 0x56U, 0x78U});
    auto copy = board(MirrorMode::Vertical, kPrgSize, kChrRamSize, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        CAPTURE(slot);
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }
    for (std::size_t offset = 0U; offset < kPrgRamSize; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto small = board(MirrorMode::Vertical, 0x8000UZ, 0x2000UZ);
    REQUIRE(state(*small).size() == kChrRamDataOffset + 0x2000UZ);
}

TEST_CASE("Mapper 365 rejects malformed and incompatible states atomically", "[mapper365][state]") {
    auto mapper = board();
    mapper->write_prg(0x8003U, 0xA5U);
    mapper->write_prg(0xA007U, 0xC3U);
    mapper->write_prg(0xE001U, 0x5AU);
    mapper->write_expansion(0x4904U, 0x6BU);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x1C99U, 0x5CU);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    std::vector<std::size_t> lengths;
    for (std::size_t length = 0U; length < kPrgRamLengthOffset; ++length) {
        lengths.push_back(length);
    }
    lengths.insert(lengths.end(),
                   {63UZ,
                    64UZ,
                    100UZ,
                    kChrRamLengthOffset - 1U,
                    kChrRamLengthOffset,
                    kChrRamDataOffset - 1U,
                    kChrRamDataOffset,
                    saved.size() - 1U});
    for (const auto length : lengths) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kPrgBanksOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte value : {0U, 0xA4U, 0xFEU}) {
        auto invalid = saved;
        invalid[kPrgBanksOffset + 3U] = value;
        reject(invalid);
    }
    for (const std::size_t offset : {kPrgRamLengthOffset, kChrRamLengthOffset}) {
        constexpr std::array<std::uint64_t, 4> invalid_lengths{
            0U, 1U, kChrRamSize + 1U, 0xFFFFFFFFFFFFFFFFULL};
        for (const std::uint64_t length : invalid_lengths) {
            auto invalid = saved;
            std::memcpy(invalid.data() + offset, &length, sizeof(length));
            CAPTURE(offset, length);
            reject(invalid);
        }
    }

    auto wrong_prg = board(MirrorMode::Vertical, kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(MirrorMode::Vertical, kPrgSize, kChrRamSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
