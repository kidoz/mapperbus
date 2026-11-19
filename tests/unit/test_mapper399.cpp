#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper399.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::size_t kPrgBanksOffset = 10UZ;
constexpr std::size_t kChrBanksOffset = 12UZ;
constexpr std::size_t kMirrorOffset = 14UZ;
constexpr std::size_t kIrqCounterOffset = 15UZ;
constexpr std::size_t kIrqLatchOffset = 16UZ;
constexpr std::size_t kIrqFlagsOffset = 17UZ;
constexpr std::size_t kChrRamOffset = 18UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 4> kPrgAddresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 399U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 20U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper399> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper399::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper399>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper399& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, const std::array<Byte, 2>& banks) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned bank = slot == 0U ? 0U : slot == 3U ? 127U : banks[slot - 1U];
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 399 validates the attested fixed ROM and volatile CHR RAM geometry",
          "[mapper399][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 32786U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 4U, 398U, 400U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper399::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper399::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper399::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x8000UZ, 0x80000UZ, kPrgSize - 1U, kPrgSize + 1U, 0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper399::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, kChrRamSize, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper399::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper399::validate_geometry(header(mirror), kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper399::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 399 decodes every documented alias and ignores writes below 8000",
          "[mapper399][bus][decoding]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 399 PRG windows use first last and two independent seven-bit banks",
          "[mapper399][banking]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 2U; ++reg) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(reg == 0U ? 0x8001U : 0x9FFFU, static_cast<Byte>(value));
            std::array<Byte, 2> banks{};
            banks[(value & 0x80U) != 0U ? 1U : 0U] = value & 0x7FU;
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, banks);
                CAPTURE(reg, value, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0x9FFFU) == cell(kPrgBankSize - 1U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(127UZ * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(kPrgSize - 1U));
}

TEST_CASE("Mapper 399 selects two independent three-bit CHR RAM banks",
          "[mapper399][banking][ram]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 2U; ++reg) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->power_on();
            const auto selected = static_cast<Byte>(value | (reg != 0U ? 0x80U : 0U));
            mapper->write_prg(reg == 0U ? 0x8000U : 0x9FFEU, selected);
            const unsigned slot = (selected & 0x80U) != 0U ? 1U : 0U;
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
            mapper->write_chr(addr, 0xA5U);
            const auto physical = (value & 7UZ) * kChrBankSize + 0x123U;
            CAPTURE(reg, value, slot, addr, physical);
            REQUIRE(mapper->read_chr(addr) == 0xA5U);
            REQUIRE(state(*mapper)[kChrRamOffset + physical] == 0xA5U);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == 0xA5U);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x4000U)) == 0xA5U);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x6000U)) == 0xA5U);

            mapper->write_prg(0x8000U,
                              static_cast<Byte>(((value + 1U) & 7U) | (slot != 0U ? 0x80U : 0U)));
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 399 CHR RAM covers all eight banks and survives only soft reset",
          "[mapper399][ram][reset]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            mapper->write_chr(offset, cell(bank * kChrBankSize + offset, 0x35U));
        }
    }
    const auto populated = state(*mapper);
    mapper->write_prg(0x8000U, 0x87U);
    mapper->write_prg(0x8001U, 0xFEU);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper) != populated);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kPrgBanksOffset] == 0U);
    REQUIRE(state(*mapper)[kPrgBanksOffset + 1U] == 0U);
    REQUIRE(state(*mapper)[kChrBanksOffset] == 0U);
    REQUIRE(state(*mapper)[kChrBanksOffset + 1U] == 0U);
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == cell(bank * kChrBankSize + offset, 0x35U));
        }
    }

    mapper->power_on();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == 0U);
        }
    }
}

TEST_CASE("Mapper 399 uses MMC3 horizontal vertical console CIRAM only", "[mapper399][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0xA000U + value * 2U), static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        const auto before = state(*mapper);
        mapper->write_prg(static_cast<Address>(0xA001U + value * 2U), static_cast<Byte>(~value));
        REQUIRE(state(*mapper) == before);
    }
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
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

TEST_CASE("Mapper 399 exposes no CPU RAM expansion device bus conflicts or audio",
          "[mapper399][bus][ram][audio]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0x5AU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
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
    REQUIRE(mapper->expansion_read_mask(0x5000U) == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 399 MMC3 IRQ covers every latch reload acknowledge and disable",
          "[mapper399][irq]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xE001U, 0U);
        for (unsigned clock = 0U; clock < latch; ++clock) {
            mapper->clock_irq_counter();
            CAPTURE(latch, clock);
            REQUIRE_FALSE(mapper->irq_pending());
        }
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE((state(*mapper)[kIrqFlagsOffset] & 2U) != 0U);
        mapper->write_prg(0xFFFEU, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_irq_counter();
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned clock = 0U; clock < 3U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE((state(*mapper)[kIrqFlagsOffset] & 1U) != 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 2U);
}

TEST_CASE("Mapper 399 reset restores registers and header mirroring without clearing CHR RAM",
          "[mapper399][reset]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        auto fresh = board(initial);
        mapper->write_prg(0x8000U, 0x87U);
        mapper->write_prg(0x8001U, 0xFFU);
        mapper->write_prg(0xA000U, initial == MirrorMode::Horizontal ? 0U : 1U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        mapper->write_chr(0x1123U, 0xA5U);
        REQUIRE(mapper->irq_pending());
        REQUIRE(state(*mapper) != state(*fresh));

        mapper->reset();
        REQUIRE(state(*mapper) != state(*fresh));
        REQUIRE(mapper->mirror_mode() == initial);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xE000U) == cell(127UZ * kPrgBankSize));
        mapper->write_prg(0x8000U, 0x87U);
        REQUIRE(mapper->read_chr(0x1123U) == 0xA5U);

        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
        REQUIRE(mapper->read_chr(0x1123U) == 0U);
    }
}

TEST_CASE("Mapper 399 bounded snapshots replay registers CHR RAM and IRQ without ROM",
          "[mapper399][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 6U);
    mapper->write_prg(0x8000U, 0x85U);
    mapper->write_prg(0x8001U, 19U);
    mapper->write_prg(0x8001U, 0x93U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kPrgBanksOffset] == 19U);
    REQUIRE(saved[kPrgBanksOffset + 1U] == 19U);
    REQUIRE(saved[kChrBanksOffset] == 6U);
    REQUIRE(saved[kChrBanksOffset + 1U] == 5U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    const std::array<Byte, 2> banks{19U, 19U};
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        const auto physical = expected_prg(addr, banks);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
    }
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto ram_changed = saved;
    ram_changed[kChrRamOffset + 123U] ^= 0xFFU;
    load(*copy, ram_changed);
    REQUIRE(state(*copy) == ram_changed);
}

TEST_CASE("Mapper 399 rejects malformed and incompatible states atomically", "[mapper399][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x8000U, 0x86U);
    mapper->write_prg(0x8001U, 0xFFU);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    for (std::size_t offset = 0U; offset < kPrgBanksOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const std::size_t offset : {kPrgBanksOffset, kPrgBanksOffset + 1U}) {
        for (const Byte value : {0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            CAPTURE(offset, value);
            reject(invalid);
        }
    }
    for (const std::size_t offset : {kChrBanksOffset, kChrBanksOffset + 1U}) {
        for (const Byte value : {8U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            CAPTURE(offset, value);
            reject(invalid);
        }
    }
    for (const Byte value : {2U, 3U, 4U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        CAPTURE(value);
        reject(invalid);
    }
    for (const Byte value : {4U, 8U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = value;
        CAPTURE(value);
        reject(invalid);
    }

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = std::make_unique<Mapper399>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
