#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper451.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x004000UZ;
constexpr std::size_t kBankSize = 0x002000UZ;
constexpr std::size_t kFlashSectorSize = 0x010000UZ;
constexpr std::size_t kRegisterStateSize = 15UZ;
constexpr std::size_t kBankOffset = 10UZ;
constexpr std::size_t kMirrorOffset = 11UZ;
constexpr std::size_t kIrqFlagsOffset = 14UZ;
constexpr std::size_t kFlashSizeOffset = 15UZ;
constexpr std::size_t kFlashOffset = 23UZ;
constexpr std::size_t kFlashCommandOffset = kFlashOffset + kPrgSize;
constexpr std::size_t kFlashOperationOffset = kFlashCommandOffset + 1UZ;
constexpr std::size_t kFlashIdOffset = kFlashCommandOffset + 2UZ;
constexpr std::size_t kFlashToggleOffset = kFlashCommandOffset + 3UZ;
constexpr std::size_t kPendingAddressOffset = kFlashCommandOffset + 5UZ;
constexpr std::size_t kFlashCyclesOffset = kFlashCommandOffset + 9UZ;
constexpr std::size_t kChrOffset = kFlashCommandOffset + 13UZ;
constexpr std::size_t kMp3PhaseOffset = kChrOffset + kChrRamSize;
constexpr std::size_t kMp3ShiftOffset = kMp3PhaseOffset + 1UZ;
constexpr std::size_t kMp3BitCountOffset = kMp3PhaseOffset + 2UZ;
constexpr std::size_t kMp3CyclesOffset = kMp3PhaseOffset + 3UZ;
constexpr std::size_t kMp3StatusIndexOffset = kMp3PhaseOffset + 7UZ;
constexpr std::size_t kMp3VolumeOffset = kMp3PhaseOffset + 8UZ;
constexpr std::size_t kMp3EqualizerOffset = kMp3PhaseOffset + 9UZ;
constexpr std::size_t kMp3FolderOffset = kMp3PhaseOffset + 10UZ;
constexpr std::size_t kMp3SongOffset = kMp3PhaseOffset + 11UZ;
constexpr std::size_t kMp3PlayingOffset = kMp3PhaseOffset + 12UZ;
constexpr std::size_t kStateSize = kMp3PhaseOffset + 13UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 451U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper451> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper451::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper451>(h, rom(salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kRegisterStateSize);
    return bytes;
}

void load(Mapper451& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void clocks(Mapper451& mapper, unsigned count) {
    for (unsigned cycle = 0U; cycle < count; ++cycle) {
        mapper.clock_cpu();
    }
}

void select_bank(Mapper451& mapper, Byte bank) {
    mapper.write_prg(static_cast<Address>(0xE000U | (bank & 3U)), 0U);
}

void unlock(Mapper451& mapper, Byte command) {
    mapper.write_prg(0xD555U, 0xAAU);
    mapper.write_prg(0xAAAAU, 0x55U);
    mapper.write_prg(0xD555U, command);
}

Byte read_mp3(Mapper451& mapper) {
    const auto value = mapper.read_prg(0x6003U);
    mapper.observe_cpu_read(0x6003U, value);
    return value;
}

void send_mp3(Mapper451& mapper, Byte command) {
    read_mp3(mapper);
    for (unsigned mask = 0x80U; mask != 0U; mask >>= 1U) {
        clocks(mapper, 1000U);
        if ((command & mask) == 0U) {
            read_mp3(mapper);
        } else {
            mapper.clock_cpu();
        }
    }
    read_mp3(mapper);
}

std::array<Byte, 8> query_mp3(Mapper451& mapper) {
    send_mp3(mapper, 0U);
    std::array<Byte, 8> bytes{};
    for (auto& value : bytes) {
        value = read_mp3(mapper);
    }
    return bytes;
}

std::size_t expected_prg(Address addr, Byte bank) {
    const unsigned slot = (addr - 0x8000U) / kBankSize;
    const unsigned selectable = 0x10U + (bank & 1U) + ((bank & 2U) << 2U);
    const unsigned selected = slot == 0U   ? 0U
                              : slot == 1U ? selectable
                              : slot == 2U ? selectable + 0x10U
                                           : 0x30U;
    return selected * kBankSize + (addr & (kBankSize - 1U));
}

void require_prg(Mapper451& mapper, Address addr, Byte bank, Byte salt = 0U) {
    const auto physical = expected_prg(addr, bank);
    CAPTURE(addr, bank, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

template <typename T> void overwrite(std::vector<Byte>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 451 validates exact Haratyler memory geometry", "[mapper451][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        CAPTURE(mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->has_flash_storage());
        REQUIRE(mapper->battery_ram().size() == kPrgSize);
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 540721U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xA000U) == cell(0x10UZ * kBankSize));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0x20UZ * kBankSize));
        REQUIRE(mapper->read_prg(0xE000U) == cell(0x30UZ * kBankSize));
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
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
        REQUIRE_FALSE(Mapper451::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 406U, 450U, 452U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, 0x4000UZ, 0x8000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
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
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        case 5U:
            h.chr_ram_size = 0x8000U;
            break;
        case 6U:
            h.chr_nvram_size = kChrRamSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 451 decodes address pins and ignores register data",
          "[mapper451][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned group = 0x8000U; group <= 0xE000U; group += 0x2000U) {
        for (unsigned low = 0U; low < 256U; ++low) {
            const auto raw = static_cast<Address>(group | low | 0x1500U);
            const auto value = static_cast<Byte>(raw * 41U + 0xA5U);
            Address canonical = 0x8000U;
            if (group == 0xA000U) {
                canonical = static_cast<Address>(0xA000U | (raw & 1U));
            } else if (group == 0xC000U) {
                canonical = static_cast<Address>(0xC000U | (raw & 0xFFU));
            } else if (group == 0xE000U) {
                canonical = static_cast<Address>(0xE000U | (raw & 3U));
            }
            actual->write_prg(0x8000U, 0xF0U);
            expected->write_prg(0x8000U, 0xF0U);
            actual->write_prg(raw, value);
            expected->write_prg(canonical, static_cast<Byte>(~value));
            CAPTURE(group, low, raw, canonical, value);
            REQUIRE(actual->mirror_mode() == expected->mirror_mode());
            for (const auto addr : kPrgAddresses) {
                REQUIRE(actual->read_prg(addr) == expected->read_prg(addr));
            }
            actual->clock_irq_counter();
            expected->clock_irq_counter();
            REQUIRE(actual->irq_pending() == expected->irq_pending());
        }
    }

    const auto initial = register_state(*actual);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE(actual->maps_prg(addr) == (addr == 0x6003U));
        REQUIRE(actual->prg_read_mask(addr) == (addr == 0x6003U ? 0xFFU : 0U));
        if (addr != 0x6003U) {
            REQUIRE(actual->read_prg(addr) == 0U);
        }
        REQUIRE_FALSE(actual->maps_expansion(addr));
        actual->write_prg(addr, static_cast<Byte>(raw * 37U));
        actual->write_expansion(addr, static_cast<Byte>(raw * 41U));
    }
    REQUIRE(register_state(*actual) == initial);
}

TEST_CASE("Mapper 451 maps fixed and selected PRG banks and two CHR RAM banks",
          "[mapper451][banking][prg][chr][ram]") {
    auto mapper = board();
    for (Byte bank = 0U; bank < 4U; ++bank) {
        select_bank(*mapper, bank);
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            require_prg(*mapper, addr, bank);
        }
    }
    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(static_cast<Address>(0xE000U | bank), 0xFFU);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, bank);
        }
    }

    for (Byte bank = 0U; bank < 2U; ++bank) {
        select_bank(*mapper, bank);
        for (Address addr = 0U; addr < kBankSize; ++addr) {
            mapper->write_chr(addr, cell(static_cast<std::size_t>(bank) * kBankSize + addr, 0xA5U));
        }
    }
    for (Byte bank = 0U; bank < 4U; ++bank) {
        select_bank(*mapper, bank);
        for (Address addr = 0U; addr < kBankSize; ++addr) {
            const auto physical = static_cast<std::size_t>(bank & 1U) * kBankSize + addr;
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    cell(physical, 0xA5U));
        }
    }
}

TEST_CASE("Mapper 451 provides only address-controlled H V CIRAM", "[mapper451][ciram][decoding]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0xA500U | (value & 1U)), static_cast<Byte>(~value));
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

TEST_CASE("Mapper 451 IRQ writes perform MMC3 latch reload and enable operations",
          "[mapper451][irq][decoding]") {
    auto mapper = board();
    for (unsigned encoded = 0U; encoded < 256U; ++encoded) {
        mapper->reset();
        mapper->write_prg(0x8000U, 0xF0U);
        mapper->write_prg(static_cast<Address>(0xC500U | encoded), 0U);
        mapper->clock_irq_counter();
        const unsigned latch = (encoded - 1U) & 0xFFU;
        CAPTURE(encoded, latch);
        REQUIRE(mapper->irq_pending() == (encoded == 1U));
        if (encoded != 0xFFU) {
            for (unsigned count = 0U; count < latch; ++count) {
                mapper->clock_irq_counter();
                REQUIRE(mapper->irq_pending() == (count + 1U == latch));
            }
            mapper->acknowledge_irq();
            REQUIRE(mapper->irq_pending());
        } else {
            REQUIRE_FALSE(mapper->irq_pending());
        }
        mapper->write_prg(0xCFFFU, 0xA5U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC002U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC0FFU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 451 returns AMIC software IDs and programs persistent flash",
          "[mapper451][flash][bus][ram][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (const std::size_t size : {0UZ, 1UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        const std::vector<Byte> wrong(size, 0U);
        mapper->set_battery_ram(wrong);
        REQUIRE(state(*mapper) == initial);
    }

    unlock(*mapper, 0x90U);
    const auto unlocked = state(*mapper);
    CAPTURE(unlocked[kFlashCommandOffset], unlocked[kFlashIdOffset]);
    REQUIRE(mapper->read_prg(0x8000U) == 0x37U);
    REQUIRE(mapper->read_prg(0x8001U) == 0x86U);
    REQUIRE(mapper->read_prg(0x8002U) == 0xFFU);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0x37U);
    mapper->write_prg(0x8000U, 0xF0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));

    unlock(*mapper, 0xA0U);
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

    unlock(*mapper, 0xA0U);
    mapper->write_prg(0x8123U, 0xF0U);
    clocks(*mapper, 32U);
    REQUIRE(mapper->battery_ram()[0x123U] == static_cast<Byte>(cell(0x123U) & 0x5AU & 0xF0U));

    std::vector<Byte> imported(kPrgSize, 0xA5U);
    mapper->set_battery_ram(imported);
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
    mapper->power_on();
    REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
}

TEST_CASE("Mapper 451 erases 64 KiB flash sectors and the complete chip",
          "[mapper451][flash][reset]") {
    auto mapper = board();
    mapper->set_battery_ram(std::vector<Byte>(kPrgSize, 0U));
    unlock(*mapper, 0x80U);
    mapper->write_prg(0xD555U, 0xAAU);
    mapper->write_prg(0xAAAAU, 0x55U);
    mapper->write_prg(0x8123U, 0x30U);
    REQUIRE((mapper->read_prg(0x8123U) & 0xC0U) == 0x40U);
    mapper->reset();
    clocks(*mapper, 35999U);
    REQUIRE(mapper->battery_ram()[0U] == 0U);
    mapper->clock_cpu();
    for (const std::size_t offset : {0UZ, kFlashSectorSize - 1U, kFlashSectorSize, kPrgSize - 1U}) {
        CAPTURE(offset);
        REQUIRE(mapper->battery_ram()[offset] == (offset < kFlashSectorSize ? 0xFFU : 0U));
    }

    mapper->set_battery_ram(std::vector<Byte>(kPrgSize, 0U));
    unlock(*mapper, 0x80U);
    mapper->write_prg(0xD555U, 0xAAU);
    mapper->write_prg(0xAAAAU, 0x55U);
    mapper->write_prg(0xD555U, 0x10U);
    mapper->power_on();
    clocks(*mapper, 144000U);
    REQUIRE(std::ranges::all_of(mapper->battery_ram(), [](Byte value) { return value == 0xFFU; }));
}

TEST_CASE("Mapper 451 MP3 read strobes transfer commands and status without expansion audio",
          "[mapper451][audio][bus][decoding]") {
    auto mapper = board();
    REQUIRE(query_mp3(*mapper) == std::array<Byte, 8>{0U, 0xA0U, 0x20U, 0U, 0U, 0U, 0U, 0U});
    send_mp3(*mapper, 0xDEU);
    send_mp3(*mapper, 0xE5U);
    send_mp3(*mapper, 0xFCU);
    send_mp3(*mapper, 0xBFU);
    auto current = state(*mapper);
    REQUIRE(current[kMp3VolumeOffset] == 30U);
    REQUIRE(current[kMp3EqualizerOffset] == 5U);
    REQUIRE(current[kMp3FolderOffset] == 12U);
    REQUIRE(current[kMp3SongOffset] == 0xBFU);
    REQUIRE(current[kMp3PlayingOffset] == 1U);
    REQUIRE(query_mp3(*mapper) ==
            std::array<Byte, 8>{0U, 0xA0U, 0x20U, 0x50U, 0xE0U, 0x10U, 0xC0U, 0U});

    send_mp3(*mapper, 0xC0U);
    send_mp3(*mapper, 0xE0U);
    send_mp3(*mapper, 0xF0U);
    REQUIRE(query_mp3(*mapper) == std::array<Byte, 8>{0U, 0xA0U, 0x20U, 0U, 0U, 0U, 0U, 0U});
    send_mp3(*mapper, 0xFDU);
    current = state(*mapper);
    REQUIRE(current[kMp3SongOffset] == 0xBFU);
    REQUIRE(current[kMp3PlayingOffset] == 1U);
    send_mp3(*mapper, 0xFFU);
    current = state(*mapper);
    REQUIRE(current[kMp3SongOffset] == 0U);
    REQUIRE(current[kMp3PlayingOffset] == 0U);

    REQUIRE(mapper->maps_prg(0x6003U));
    REQUIRE(mapper->prg_read_mask(0x6003U) == 0xFFU);
    for (const Address addr : {0x5FFFU, 0x6000U, 0x6002U, 0x6004U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    const auto before = state(*mapper);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->battery_ram().size() == kPrgSize);
}

TEST_CASE("Mapper 451 reset restores board registers and cold power clears volatile state",
          "[mapper451][reset][ram][audio]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    select_bank(*mapper, 3U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    select_bank(*mapper, 1U);
    mapper->write_chr(0x123U, 0xA5U);
    send_mp3(*mapper, 0xDEU);
    send_mp3(*mapper, 0xE5U);
    send_mp3(*mapper, 0xFCU);
    send_mp3(*mapper, 0xBFU);
    unlock(*mapper, 0x90U);
    const auto before = state(*mapper);

    mapper->reset();
    const auto soft = state(*mapper);
    REQUIRE(soft[kBankOffset] == 0U);
    REQUIRE(soft[kMirrorOffset] == static_cast<Byte>(MirrorMode::Horizontal));
    REQUIRE(soft[kIrqFlagsOffset] == 0U);
    REQUIRE(
        std::equal(soft.begin() + kFlashSizeOffset, soft.end(), before.begin() + kFlashSizeOffset));
    REQUIRE(mapper->read_prg(0x8000U) == 0x37U);
    select_bank(*mapper, 1U);
    REQUIRE(mapper->read_chr(0x123U) == 0xA5U);

    mapper->power_on();
    const auto cold = state(*mapper);
    REQUIRE(cold[kBankOffset] == 0U);
    REQUIRE(cold[kMirrorOffset] == static_cast<Byte>(MirrorMode::Horizontal));
    REQUIRE(std::all_of(cold.begin() + kChrOffset, cold.begin() + kMp3PhaseOffset, [](Byte value) {
        return value == 0U;
    }));
    REQUIRE(std::all_of(
        cold.begin() + kMp3PhaseOffset, cold.end(), [](Byte value) { return value == 0U; }));
    REQUIRE(std::equal(cold.begin() + kFlashSizeOffset,
                       cold.begin() + kChrOffset,
                       before.begin() + kFlashSizeOffset));
    REQUIRE(mapper->read_prg(0x8001U) == 0x86U);
}

TEST_CASE("Mapper 451 bounded snapshots replay RAM registers and flash without source ROM",
          "[mapper451][state][banking][flash]") {
    auto mapper = board(MirrorMode::Vertical, 0x10U);
    select_bank(*mapper, 3U);
    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0xC002U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    select_bank(*mapper, 1U);
    mapper->write_chr(0x123U, 0xA5U);
    send_mp3(*mapper, 0xDEU);
    send_mp3(*mapper, 0xE5U);
    send_mp3(*mapper, 0xFCU);
    send_mp3(*mapper, 0xBFU);
    select_bank(*mapper, 3U);
    unlock(*mapper, 0xA0U);
    select_bank(*mapper, 0U);
    mapper->write_prg(0x8123U, 0x5AU);
    mapper->read_prg(0x8123U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kBankOffset] == 0U);
    REQUIRE(saved[kMirrorOffset] == static_cast<Byte>(MirrorMode::Vertical));
    REQUIRE(saved[kIrqFlagsOffset] == 7U);
    REQUIRE(saved[kFlashOperationOffset] == 1U);
    REQUIRE(saved[kFlashToggleOffset] == 1U);
    REQUIRE(saved[kChrOffset + 0x2123U] == 0xA5U);
    REQUIRE(saved[kMp3VolumeOffset] == 30U);
    REQUIRE(saved[kMp3EqualizerOffset] == 5U);
    REQUIRE(saved[kMp3FolderOffset] == 12U);
    REQUIRE(saved[kMp3SongOffset] == 0xBFU);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    select_bank(*copy, 1U);
    REQUIRE(copy->read_chr(0x123U) == 0xA5U);
    select_bank(*copy, 0U);
    REQUIRE(query_mp3(*copy) == query_mp3(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto changed = saved;
    changed[kFlashOffset + 0x123U] ^= 0xFFU;
    changed[kChrOffset + 0x2123U] ^= 0xFFU;
    load(*copy, changed);
    REQUIRE(state(*copy) == changed);
}

TEST_CASE("Mapper 451 snapshots preserve an in-flight MP3 command", "[mapper451][state][audio]") {
    auto mapper = board();
    read_mp3(*mapper);
    constexpr Byte command = 0xA5U;
    for (unsigned mask = 0x80U; mask >= 0x10U; mask >>= 1U) {
        clocks(*mapper, 1000U);
        if ((command & mask) == 0U) {
            read_mp3(*mapper);
        } else {
            mapper->clock_cpu();
        }
    }
    const auto saved = state(*mapper);
    REQUIRE(saved[kMp3PhaseOffset] == 1U);
    REQUIRE(saved[kMp3ShiftOffset] == 0x0AU);
    REQUIRE(saved[kMp3BitCountOffset] == 4U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (unsigned mask = 0x08U; mask != 0U; mask >>= 1U) {
        clocks(*mapper, 1000U);
        clocks(*copy, 1000U);
        if ((command & mask) == 0U) {
            read_mp3(*mapper);
            read_mp3(*copy);
        } else {
            mapper->clock_cpu();
            copy->clock_cpu();
        }
        REQUIRE(state(*copy) == state(*mapper));
    }
    read_mp3(*mapper);
    read_mp3(*copy);
    REQUIRE(state(*copy) == state(*mapper));
    REQUIRE(state(*copy)[kMp3SongOffset] == command);
}

TEST_CASE("Mapper 451 rejects damaged truncated and incompatible snapshots atomically",
          "[mapper451][state][ram][audio]") {
    auto mapper = board();
    select_bank(*mapper, 3U);
    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    unlock(*mapper, 0xA0U);
    select_bank(*mapper, 0U);
    mapper->write_prg(0x8123U, 0x5AU);
    select_bank(*mapper, 1U);
    mapper->write_chr(0x123U, 0xA5U);
    send_mp3(*mapper, 0xDEU);
    send_mp3(*mapper, 0xBFU);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (const std::size_t length : {0UZ,
                                     1UZ,
                                     kBankOffset,
                                     kFlashSizeOffset,
                                     kFlashOffset,
                                     kFlashOffset + 1U,
                                     kFlashCommandOffset - 1U,
                                     kFlashCommandOffset,
                                     kChrOffset,
                                     kMp3PhaseOffset,
                                     kMp3CyclesOffset,
                                     saved.size() - 1U}) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 22>{{
             {kBankOffset, 4U},           {kMirrorOffset, 2U},       {kIrqFlagsOffset, 8U},
             {kIrqFlagsOffset, 4U},       {kFlashCommandOffset, 7U}, {kFlashOperationOffset, 4U},
             {kFlashOperationOffset, 1U}, {kFlashIdOffset, 2U},      {kFlashToggleOffset, 2U},
             {kMp3PhaseOffset, 4U},       {kMp3PhaseOffset, 2U},     {kMp3BitCountOffset, 9U},
             {kMp3StatusIndexOffset, 8U}, {kMp3VolumeOffset, 31U},   {kMp3EqualizerOffset, 6U},
             {kMp3FolderOffset, 13U},     {kMp3SongOffset, 192U},    {kMp3SongOffset, 0U},
             {kMp3PlayingOffset, 2U},     {kMp3PlayingOffset, 0U},   {kMp3ShiftOffset, 1U},
             {kMp3BitCountOffset, 8U},
         }}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    for (const std::size_t offset :
         {kFlashSizeOffset, kPendingAddressOffset, kFlashCyclesOffset, kMp3CyclesOffset}) {
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
    bad_id_busy[kFlashOperationOffset] = 1U;
    overwrite<std::uint32_t>(bad_id_busy, kFlashCyclesOffset, 1U);
    reject(bad_id_busy);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before = state(*wrong_mirror);
    StateReader mirror_reader(saved);
    wrong_mirror->load_state(mirror_reader);
    REQUIRE_FALSE(mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before);
}

} // namespace
