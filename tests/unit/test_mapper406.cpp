#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper406.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kFlashSectorSize = 0x010000UZ;
constexpr std::size_t kInitialMirrorOffset = 10UZ;
constexpr std::size_t kBankSelectOffset = 11UZ;
constexpr std::size_t kBanksOffset = 12UZ;
constexpr std::size_t kMirrorOffset = 20UZ;
constexpr std::size_t kIrqFlagsOffset = 23UZ;
constexpr std::size_t kFlashSizeOffset = 24UZ;
constexpr std::size_t kFlashOffset = 32UZ;
constexpr std::size_t kFlashCommandOffset = kFlashOffset + kPrgSize;
constexpr std::size_t kFlashOperationOffset = kFlashCommandOffset + 1UZ;
constexpr std::size_t kFlashIdOffset = kFlashCommandOffset + 2UZ;
constexpr std::size_t kFlashToggleOffset = kFlashCommandOffset + 3UZ;
constexpr std::size_t kPendingAddressOffset = kFlashCommandOffset + 5UZ;
constexpr std::size_t kFlashCyclesOffset = kFlashCommandOffset + 9UZ;
constexpr std::size_t kStateSize = kFlashCommandOffset + 13UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 406U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
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

std::unique_ptr<Mapper406> board(Byte submapper = 0U,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper406::validate_geometry(h, kPrgSize, chr_size));
    return std::make_unique<Mapper406>(h, rom(kPrgSize, salt), rom(chr_size, salt ^ 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kFlashSizeOffset);
    return bytes;
}

void load(Mapper406& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void write_reg(Mapper406& mapper, Byte submapper, Address addr, Byte value) {
    Address decoded = addr;
    if (submapper == 0U) {
        decoded = static_cast<Address>((addr & 0xE000U) | ((addr & 1U) << 1U));
    } else if ((addr & 0xE000U) == 0x8000U || (addr & 0xE000U) == 0xE000U) {
        decoded = static_cast<Address>(addr ^ 0x6000U);
    }
    mapper.write_prg(decoded, value);
}

void select_prg(Mapper406& mapper, Byte submapper, unsigned index, Byte value) {
    write_reg(mapper, submapper, 0x8000U, static_cast<Byte>(index));
    write_reg(mapper, submapper, 0x8001U, value);
}

void unlock(Mapper406& mapper, Byte submapper, Byte command) {
    select_prg(mapper, submapper, 7U, 1U);
    mapper.write_prg(0xD555U, 0xAAU);
    mapper.write_prg(0xAAAAU, 0x55U);
    mapper.write_prg(0xD555U, command);
}

void clocks(Mapper406& mapper, unsigned count) {
    for (unsigned cycle = 0U; cycle < count; ++cycle) {
        mapper.clock_cpu();
    }
}

std::size_t expected_prg(Address addr, Byte select, const std::array<Byte, 8>& banks) {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    const bool swapped = (select & 0x40U) != 0U;
    unsigned bank = 63U;
    if (slot == 1U) {
        bank = banks[7U] & 0x3FU;
    } else if (slot == 2U) {
        bank = swapped ? banks[6U] & 0x3FU : 62U;
    } else if (slot == 0U) {
        bank = swapped ? 62U : banks[6U] & 0x3FU;
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(Address addr,
                         Byte select,
                         const std::array<Byte, 8>& banks,
                         std::size_t chr_size) {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((select & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const auto bank = slot < 4U ? static_cast<unsigned>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                                : static_cast<unsigned>(banks[slot - 2U]);
    return static_cast<std::size_t>(bank % (chr_size / kChrBankSize)) * kChrBankSize +
           (addr & (kChrBankSize - 1U));
}

template <typename T> void overwrite(std::vector<Byte>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 406 validates both flash-board submappers and bounded memory geometry",
          "[mapper406][geometry]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const std::size_t chr_size : {0x2000UZ, 0x2400UZ, 0x10000UZ, kChrSize}) {
                CAPTURE(submapper, mirror, chr_size);
                auto mapper = board(submapper, chr_size, mirror);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->has_flash_storage());
                REQUIRE(mapper->battery_ram().size() == kPrgSize);
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(register_state(*mapper).size() == kFlashSizeOffset);
                REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
                REQUIRE(mapper->read_prg(0xC000U) == cell(62UZ * kPrgBankSize));
                REQUIRE(mapper->read_prg(0xE000U) == cell(63UZ * kPrgBankSize));
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
            }
        }
    }

    for (const unsigned id : {0U, 4U, 111U, 405U, 407U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper406::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper406::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        REQUIRE_FALSE(Mapper406::validate_geometry(header(submapper), kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper406::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, kChrSize + 1U, kChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper406::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper406::validate_geometry(header(0U, mirror), kPrgSize, kChrSize));
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
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 5U:
            h.prg_ram_size = 0x2000U;
            h.prg_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper406::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 406 decodes every CPU alias according to its submapper wiring",
          "[mapper406][decoding][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto actual = board(submapper);
        auto expected = board(submapper);
        for (unsigned group = 0x8000U; group <= 0xE000U; group += 0x2000U) {
            for (unsigned low = 0U; low < 256U; ++low) {
                const auto raw = static_cast<Address>(group | low);
                Address canonical = raw;
                if (submapper == 0U) {
                    canonical = static_cast<Address>((raw & 0xE000U) | ((raw >> 1U) & 1U));
                } else if (group == 0x8000U || group == 0xE000U) {
                    canonical = static_cast<Address>(raw ^ 0x6000U);
                }
                actual->reset();
                expected->reset();
                actual->write_prg(raw, 0xA3U);
                write_reg(*expected, submapper, static_cast<Address>(canonical & 0xE001U), 0xA3U);
                CAPTURE(submapper, raw, canonical);
                REQUIRE(register_state(*actual) == register_state(*expected));
            }
        }

        const auto initial = register_state(*actual);
        for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(actual->maps_prg(addr));
            REQUIRE(actual->prg_read_mask(addr) == 0U);
            REQUIRE(actual->read_prg(addr) == 0U);
            REQUIRE_FALSE(actual->maps_expansion(addr));
            REQUIRE(actual->expansion_read_mask(addr) == 0xFFU);
            REQUIRE(actual->read_expansion(addr) == 0U);
            actual->write_prg(addr, 0xA5U);
            actual->write_expansion(addr, 0x5AU);
        }
        REQUIRE(register_state(*actual) == initial);
        for (const Address addr : kPrgAddresses) {
            REQUIRE(actual->maps_prg(addr));
            REQUIRE(actual->prg_read_mask(addr) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 406 implements ordinary MMC3 PRG and CHR banking before fitted-ROM wrapping",
          "[mapper406][banking]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (const Byte select : {Byte{0U}, Byte{0x40U}}) {
            const std::array<Byte, 8> banks{0xFEU, 3U, 0U, 0U, 0U, 0U, 0xFFU, 0xA5U};
            for (unsigned index = 6U; index < 8U; ++index) {
                select_prg(*mapper, submapper, index, banks[index]);
            }
            write_reg(*mapper, submapper, 0x8000U, select);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, select, banks);
                CAPTURE(submapper, select, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }

        std::array<Byte, 8> banks{};
        for (unsigned index = 0U; index < banks.size(); ++index) {
            banks[index] = static_cast<Byte>(index * 31U + 0x83U);
            select_prg(*mapper, submapper, index, banks[index]);
        }
        for (const Byte select : {Byte{0U}, Byte{0x80U}}) {
            write_reg(*mapper, submapper, 0x8000U, select);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                const auto physical = expected_chr(addr, select, banks, kChrSize);
                CAPTURE(submapper, select, slot, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                        cell(physical, 0x35U));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            }
        }

        auto fitted = board(submapper, 0x2400UZ);
        std::array<Byte, 8> small_banks{9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U};
        for (unsigned index = 0U; index < small_banks.size(); ++index) {
            select_prg(*fitted, submapper, index, small_banks[index]);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x321U);
            const auto physical = expected_chr(addr, 0U, small_banks, 0x2400UZ);
            CAPTURE(submapper, slot, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 406 controls only H/V CIRAM and exposes no work RAM or expansion audio",
          "[mapper406][ciram][ram][audio]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper, kChrSize, MirrorMode::Horizontal);
        for (unsigned value = 0U; value < 256U; ++value) {
            write_reg(*mapper, submapper, 0xA000U, static_cast<Byte>(value));
            CAPTURE(submapper, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            for (const Address addr : {0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->ciram_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        const auto before = state(*mapper);
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->battery_ram().size() == kPrgSize);
    }
}

TEST_CASE("Mapper 406 returns submapper-specific software IDs and exits ID mode",
          "[mapper406][flash][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        unlock(*mapper, submapper, 0x90U);
        REQUIRE(mapper->read_prg(0x8000U) == (submapper == 0U ? 0xC2U : 0x01U));
        REQUIRE(mapper->read_prg(0x8001U) == 0xA4U);
        REQUIRE(mapper->read_prg(0x8002U) == 0xFFU);
        REQUIRE(mapper->read_prg(0x8003U) == 0xFFU);
        mapper->write_prg(0xD555U, 0xF0U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0x8001U) == cell(1U));

        mapper->write_prg(0xD555U, 0xAAU);
        mapper->write_prg(0xAAAAU, 0x54U);
        mapper->write_prg(0xD555U, 0x90U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        unlock(*mapper, submapper, 0x90U);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == (submapper == 0U ? 0xC2U : 0x01U));
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x8001U) == 0xA4U);
    }
}

TEST_CASE("Mapper 406 programs flash one-to-zero with busy polling and exact persistence",
          "[mapper406][flash][ram][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (const std::size_t size : {0UZ, 1UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        const std::vector<Byte> wrong(size, 0U);
        mapper->set_battery_ram(wrong);
        REQUIRE(state(*mapper) == initial);
    }

    unlock(*mapper, 0U, 0xA0U);
    mapper->write_prg(0x8123U, 0x5AU);
    REQUIRE(mapper->battery_ram()[0x123U] == cell(0x123U));
    REQUIRE((mapper->read_prg(0x8123U) & 0xC0U) == 0xC0U);
    REQUIRE((mapper->read_prg(0x8123U) & 0xC0U) == 0x80U);
    mapper->write_prg(0x8123U, 0xF0U);
    mapper->reset();
    clocks(*mapper, 31U);
    REQUIRE(mapper->battery_ram()[0x123U] == cell(0x123U));
    mapper->clock_cpu();
    REQUIRE(mapper->battery_ram()[0x123U] == static_cast<Byte>(cell(0x123U) & 0x5AU));
    REQUIRE(mapper->read_prg(0x8123U) == static_cast<Byte>(cell(0x123U) & 0x5AU));

    select_prg(*mapper, 0U, 6U, 0U);
    unlock(*mapper, 0U, 0xA0U);
    mapper->write_prg(0x8123U, 0xF0U);
    clocks(*mapper, 32U);
    REQUIRE(mapper->battery_ram()[0x123U] == static_cast<Byte>(cell(0x123U) & 0x5AU & 0xF0U));

    std::vector<Byte> imported(kPrgSize, 0xA5U);
    mapper->set_battery_ram(imported);
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
    mapper->power_on();
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
}

TEST_CASE("Mapper 406 erases 64 KiB sectors or the whole chip without reset cancellation",
          "[mapper406][flash][reset]") {
    auto mapper = board();
    mapper->set_battery_ram(std::vector<Byte>(kPrgSize, 0U));
    select_prg(*mapper, 0U, 6U, 8U);
    unlock(*mapper, 0U, 0x80U);
    mapper->write_prg(0xD555U, 0xAAU);
    mapper->write_prg(0xAAAAU, 0x55U);
    mapper->write_prg(0x8123U, 0x30U);
    REQUIRE((mapper->read_prg(0x8123U) & 0xC0U) == 0x40U);
    mapper->reset();
    clocks(*mapper, 35999U);
    REQUIRE(mapper->battery_ram()[kFlashSectorSize] == 0U);
    mapper->clock_cpu();
    for (const std::size_t offset : {0UZ,
                                     kFlashSectorSize - 1U,
                                     kFlashSectorSize,
                                     kFlashSectorSize * 2U - 1U,
                                     kFlashSectorSize * 2U}) {
        CAPTURE(offset);
        REQUIRE(mapper->battery_ram()[offset] ==
                (offset >= kFlashSectorSize && offset < kFlashSectorSize * 2U ? 0xFFU : 0U));
    }

    mapper->set_battery_ram(std::vector<Byte>(kPrgSize, 0U));
    unlock(*mapper, 0U, 0x80U);
    mapper->write_prg(0xD555U, 0xAAU);
    mapper->write_prg(0xAAAAU, 0x55U);
    mapper->write_prg(0xD555U, 0x10U);
    mapper->power_on();
    clocks(*mapper, 144000U);
    REQUIRE(std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0xFFU; }));
}

TEST_CASE("Mapper 406 preserves MMC3 reload enable acknowledge and zero-latch IRQ behavior",
          "[mapper406][irq]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (const unsigned latch : {0U, 1U, 2U, 0xFFU}) {
            mapper->reset();
            write_reg(*mapper, submapper, 0xC000U, static_cast<Byte>(latch));
            write_reg(*mapper, submapper, 0xC001U, 0U);
            write_reg(*mapper, submapper, 0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (latch == 0U));
            for (unsigned count = 0U; count < latch; ++count) {
                mapper->clock_irq_counter();
                REQUIRE(mapper->irq_pending() == (count + 1U == latch));
            }
            mapper->acknowledge_irq();
            REQUIRE(mapper->irq_pending());
            write_reg(*mapper, submapper, 0xE000U, 0U);
            REQUIRE_FALSE(mapper->irq_pending());
        }
        write_reg(*mapper, submapper, 0xC000U, 0U);
        write_reg(*mapper, submapper, 0xC001U, 0U);
        write_reg(*mapper, submapper, 0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
    }
}

TEST_CASE(
    "Mapper 406 soft and cold reset restore MMC3 state but retain flash contents and commands",
    "[mapper406][reset][flash]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper, kChrSize, MirrorMode::Horizontal);
        select_prg(*mapper, submapper, 6U, 0x95U);
        select_prg(*mapper, submapper, 7U, 0x59U);
        select_prg(*mapper, submapper, 0U, 0xFEU);
        write_reg(*mapper, submapper, 0x8000U, 0xC0U);
        write_reg(*mapper, submapper, 0xA000U, 1U);
        write_reg(*mapper, submapper, 0xC000U, 0U);
        write_reg(*mapper, submapper, 0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        unlock(*mapper, submapper, 0x90U);
        const auto flash_before = state(*mapper);
        const auto persistent =
            std::vector<Byte>(mapper->battery_ram().begin(), mapper->battery_ram().end());

        mapper->reset();
        REQUIRE(register_state(*mapper) ==
                register_state(*board(submapper, kChrSize, MirrorMode::Horizontal)));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x8000U) == (submapper == 0U ? 0xC2U : 0x01U));
        REQUIRE(std::ranges::equal(mapper->battery_ram(), persistent));
        REQUIRE(state(*mapper)[kFlashCommandOffset] == flash_before[kFlashCommandOffset]);

        mapper->power_on();
        REQUIRE(register_state(*mapper) ==
                register_state(*board(submapper, kChrSize, MirrorMode::Horizontal)));
        REQUIRE(mapper->read_prg(0x8001U) == 0xA4U);
        REQUIRE(std::ranges::equal(mapper->battery_ram(), persistent));
    }
}

TEST_CASE(
    "Mapper 406 snapshots replay registers flash bytes and pending operations without CHR ROM",
    "[mapper406][state][flash]") {
    auto mapper = board();
    select_prg(*mapper, 0U, 0U, 0xFEU);
    select_prg(*mapper, 0U, 1U, 0x81U);
    select_prg(*mapper, 0U, 6U, 6U);
    select_prg(*mapper, 0U, 7U, 9U);
    write_reg(*mapper, 0U, 0x8000U, 0xC6U);
    write_reg(*mapper, 0U, 0xA000U, 0U);
    write_reg(*mapper, 0U, 0xC000U, 1U);
    write_reg(*mapper, 0U, 0xC001U, 0U);
    write_reg(*mapper, 0U, 0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    select_prg(*mapper, 0U, 7U, 9U);
    write_reg(*mapper, 0U, 0x8000U, 0xC6U);
    mapper->write_prg(0xD555U, 0xAAU);
    mapper->write_prg(0xAAAAU, 0x55U);
    mapper->write_prg(0xD555U, 0xA0U);
    mapper->write_prg(0xD120U, 0x5AU);
    mapper->read_prg(0xD120U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 524333U);
    REQUIRE(saved[kBankSelectOffset] == 0xC6U);
    REQUIRE(saved[kBanksOffset + 6U] == 6U);
    REQUIRE(saved[kIrqFlagsOffset] == 6U);
    REQUIRE(saved[kFlashOperationOffset] == 1U);

    auto copy = board(0U, kChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(std::ranges::equal(copy->battery_ram(), mapper->battery_ram()));
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    const std::array<Byte, 8> banks{0xFEU, 0x81U, 0U, 0U, 0U, 0U, 6U, 9U};
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        const auto physical = expected_chr(addr, 0xC6U, banks, kChrSize);
        CAPTURE(slot, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xB5U));
    }
    clocks(*copy, 31U);
    clocks(*mapper, 31U);
    REQUIRE(state(*copy) == state(*mapper));
    copy->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(state(*copy) == state(*mapper));
    const auto programmed = 6UZ * kPrgBankSize + 0x1120U;
    std::uint32_t pending_address = 0U;
    std::memcpy(&pending_address, saved.data() + kPendingAddressOffset, sizeof(pending_address));
    CAPTURE(pending_address,
            programmed,
            static_cast<unsigned>(copy->battery_ram()[programmed]),
            static_cast<unsigned>(cell(programmed)),
            static_cast<unsigned>(mapper->battery_ram()[programmed]));
    REQUIRE(copy->battery_ram()[programmed] == static_cast<Byte>(cell(programmed) & 0x5AU));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto changed_flash = saved;
    changed_flash[kFlashOffset + 0x123U] ^= 0xFFU;
    load(*copy, changed_flash);
    REQUIRE(state(*copy) == changed_flash);
}

TEST_CASE("Mapper 406 rejects truncated damaged and mismatched snapshots atomically",
          "[mapper406][state]") {
    auto mapper = board();
    select_prg(*mapper, 0U, 6U, 0x95U);
    select_prg(*mapper, 0U, 7U, 0x59U);
    write_reg(*mapper, 0U, 0x8000U, 0xC6U);
    write_reg(*mapper, 0U, 0xA000U, 1U);
    write_reg(*mapper, 0U, 0xC000U, 0U);
    write_reg(*mapper, 0U, 0xE001U, 0U);
    mapper->clock_irq_counter();
    unlock(*mapper, 0U, 0xA0U);
    select_prg(*mapper, 0U, 7U, 0x59U);
    write_reg(*mapper, 0U, 0x8000U, 0xC6U);
    mapper->write_prg(0x8123U, 0x5AU);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (const std::size_t length : {0UZ,
                                     1UZ,
                                     kInitialMirrorOffset,
                                     kBankSelectOffset,
                                     kFlashSizeOffset,
                                     kFlashOffset,
                                     kFlashOffset + 1U,
                                     kFlashOffset + 100U,
                                     kFlashCommandOffset - 1U,
                                     kFlashCommandOffset,
                                     kFlashCyclesOffset,
                                     saved.size() - 1U}) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset <= kInitialMirrorOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 11>{{
             {kBankSelectOffset, 0x08U},
             {kBanksOffset + 6U, 0x40U},
             {kBanksOffset + 7U, 0xFFU},
             {kMirrorOffset, 2U},
             {kIrqFlagsOffset, 8U},
             {kIrqFlagsOffset, 4U},
             {kFlashCommandOffset, 7U},
             {kFlashOperationOffset, 4U},
             {kFlashIdOffset, 2U},
             {kFlashToggleOffset, 2U},
             {kFlashOperationOffset, 0U},
         }}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    for (const std::size_t offset : {kFlashSizeOffset, kPendingAddressOffset, kFlashCyclesOffset}) {
        auto invalid = saved;
        overwrite<std::uint32_t>(invalid, offset, 0xFFFFFFFFU);
        if (offset == kFlashSizeOffset) {
            overwrite<std::uint64_t>(invalid, offset, 0xFFFFFFFFFFFFFFFFULL);
        }
        CAPTURE(offset);
        reject(invalid);
    }
    auto bad_sector = saved;
    bad_sector[kFlashOperationOffset] = 2U;
    overwrite<std::uint32_t>(bad_sector, kPendingAddressOffset, 1U);
    overwrite<std::uint32_t>(bad_sector, kFlashCyclesOffset, 1U);
    reject(bad_sector);
    auto bad_chip = saved;
    bad_chip[kFlashOperationOffset] = 3U;
    overwrite<std::uint32_t>(bad_chip, kPendingAddressOffset, 1U);
    overwrite<std::uint32_t>(bad_chip, kFlashCyclesOffset, 1U);
    reject(bad_chip);
    auto bad_idle_toggle = saved;
    bad_idle_toggle[kFlashOperationOffset] = 0U;
    overwrite<std::uint32_t>(bad_idle_toggle, kFlashCyclesOffset, 0U);
    bad_idle_toggle[kFlashToggleOffset] = 1U;
    reject(bad_idle_toggle);
    auto bad_id_busy = saved;
    bad_id_busy[kFlashIdOffset] = 1U;
    reject(bad_id_busy);

    auto wrong_submapper = board(1U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);

    auto wrong_chr = board(0U, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(0U, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
