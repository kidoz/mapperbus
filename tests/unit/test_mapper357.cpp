#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper357.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kSmbBankOffset = 10UZ;
constexpr std::size_t kSmbFixedBanksOffset = 11UZ;
constexpr std::size_t kUnromBankOffset = 12UZ;
constexpr std::size_t kIrqCounterOffset = 13UZ;
constexpr std::size_t kIrqFlagsOffset = 15UZ;
constexpr std::size_t kChrRamOffset = 16UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Byte, 8> kSmbBanks{4U, 3U, 5U, 3U, 6U, 3U, 7U, 3U};
constexpr std::array<Address, 6> kSmbAddresses{
    0x5123U, 0x6123U, 0x8123U, 0xA123U, 0xC123U, 0xE123U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 357U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper357> board(Byte dip = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper357::validate_geometry(h, kPrgSize, 0U));
    auto mapper = std::make_unique<Mapper357>(h, rom(salt), std::vector<Byte>{});
    REQUIRE(mapper->configure_board_jumpers(dip));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 357 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper357]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper357::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (const auto id : {0U, 2U, 356U, 358U, 368U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper357::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper357::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper357::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x40000UZ, 0x7FFFFUZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper357::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper357::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper357::validate_geometry(header(mirror), kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper357::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 357 DIP switches select mode CIRAM and low CPU decoding", "[mapper357][bus]") {
    for (Byte dip = 0U; dip < 4U; ++dip) {
        auto mapper = board(dip, MirrorMode::Horizontal);
        REQUIRE(mapper->mirror_mode() ==
                (dip == 3U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper->maps_nametable(0x2000U));
        REQUIRE(mapper->maps_nametable(0x3EFFU));
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
        REQUIRE_FALSE(mapper->unmirrored_nametables());

        for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
            const auto addr = static_cast<Address>(raw);
            const bool mapped = addr >= 0x8000U || (dip == 0U && addr >= 0x5000U && addr < 0x8000U);
            CAPTURE(dip, addr, mapped);
            REQUIRE(mapper->maps_prg(addr) == mapped);
            REQUIRE(mapper->prg_read_mask(addr) == (mapped ? 0xFFU : 0U));
            if (!mapped) {
                REQUIRE(mapper->read_prg(addr) == 0U);
            }
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        const auto before = state(*mapper);
        mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(state(*mapper) == before);
    }

    auto mapper = board(2U);
    const auto configured = state(*mapper);
    for (const Byte dip : {4U, 7U, 0xFFU}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(dip));
        REQUIRE(state(*mapper) == configured);
    }
}

TEST_CASE("Mapper 357 SMB2J mode maps every fixed and selected 8 KiB bank", "[mapper357]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4022U, static_cast<Byte>(value));
        mapper->write_expansion(0x4120U, static_cast<Byte>(value ^ 0xA5U));
        const unsigned fixed_banks = (value ^ 0xA5U) & 1U;
        const std::array<unsigned, 6> banks{16U,
                                            fixed_banks == 0U ? 2U : 0U,
                                            1U,
                                            0U,
                                            kSmbBanks[value & 7U],
                                            fixed_banks == 0U ? 10U : 8U};
        for (std::size_t slot = 0U; slot < banks.size(); ++slot) {
            const auto addr = kSmbAddresses[slot];
            const auto inner = addr == 0x5123U ? 0x0123UZ : 0x0123UZ;
            CAPTURE(value, slot, banks[slot]);
            REQUIRE(mapper->read_prg(addr) == cell(banks[slot] * 0x2000UZ + inner));
        }
    }
}

TEST_CASE("Mapper 357 UNROM modes use independent outer banks and fixed last pages",
          "[mapper357]") {
    for (Byte dip = 1U; dip < 4U; ++dip) {
        auto mapper = board(dip);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0x8000U | ((value * 0x101U) & 0x7FFFU)),
                              static_cast<Byte>(value));
            const auto lower = (static_cast<std::size_t>(dip) * 8U + (value & 7U)) * 0x4000UZ;
            const auto upper = (static_cast<std::size_t>(dip) * 8U + 7U) * 0x4000UZ;
            CAPTURE(dip, value, lower, upper);
            REQUIRE(mapper->read_prg(0x8123U) == cell(lower + 0x0123U));
            REQUIRE(mapper->read_prg(0xBFFFU) == cell(lower + 0x3FFFU));
            REQUIRE(mapper->read_prg(0xC123U) == cell(upper + 0x0123U));
            REQUIRE(mapper->read_prg(0xFFFFU) == cell(upper + 0x3FFFU));
            REQUIRE(state(*mapper)[kUnromBankOffset] == (value & 7U));
        }
    }
}

TEST_CASE("Mapper 357 decodes only the documented SMB2J expansion aliases", "[mapper357][bus]") {
    auto mapper = board();
    auto expected = board();
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        mapper->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_expansion(static_cast<Address>(raw), value);
        if ((raw & 0x71FFU) == 0x4022U) {
            expected->write_expansion(0x4022U, value);
        } else if ((raw & 0x71FFU) == 0x4120U) {
            expected->write_expansion(0x4120U, value);
        } else if ((raw & 0xF1FFU) == 0x4122U) {
            expected->write_expansion(0x4122U, value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == state(*expected));
    }

    mapper->reset();
    const auto smb_initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
    }
    REQUIRE(state(*mapper) == smb_initial);
    mapper->write_expansion(0x4022U, 0xA6U);
    mapper->write_prg_cpu(0x8022U, 0x5AU, true);
    REQUIRE(state(*mapper)[kSmbBankOffset] == 6U);

    auto unrom = board(3U);
    unrom->reset();
    const auto unrom_initial = state(*unrom);
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        unrom->write_expansion(static_cast<Address>(raw), static_cast<Byte>(raw));
    }
    REQUIRE(state(*unrom) == unrom_initial);
    unrom->write_prg_cpu(0xFFFFU, 0xFFU, false);
    REQUIRE(state(*unrom)[kUnromBankOffset] == 7U);
}

TEST_CASE("Mapper 357 has flat writable CHR RAM and no persistent memory", "[mapper357][ram]") {
    for (Byte dip = 0U; dip < 4U; ++dip) {
        auto mapper = board(dip);
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
        }
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(dip, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
        }
        REQUIRE(mapper->read_chr(0x0123U) == mapper->read_chr(0x2123U));
        mapper->write_prg(0xFFFFU, 0xFFU);
        REQUIRE(mapper->read_chr(0x0321U) == cell(0x0321U, 0xA5U));
    }
}

TEST_CASE("Mapper 357 SMB2J IRQ counts exactly 4096 M2 cycles to overflow", "[mapper357][irq]") {
    auto mapper = board();
    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == (cycle & 0xFFU));
        REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == (cycle >> 8U));
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0U);

    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 1U);
    mapper->write_expansion(0x4122U, 0xFFU);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 1U);
    mapper->write_expansion(0x4122U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4122U, 0xFEU);
    REQUIRE_FALSE(mapper->irq_pending());

    auto unrom = board(1U);
    const auto before = state(*unrom);
    unrom->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle) {
        unrom->clock_cpu();
    }
    REQUIRE_FALSE(unrom->irq_pending());
    REQUIRE(state(*unrom) == before);
}

TEST_CASE("Mapper 357 has no expansion audio or unrelated clock side effects",
          "[mapper357][audio][irq]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4120U, 1U);
    mapper->write_expansion(0x4122U, 1U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_irq_counter();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 357 soft reset retains CHR RAM and cold power clears it", "[mapper357][reset]") {
    for (Byte dip = 0U; dip < 4U; ++dip) {
        auto mapper = board(dip);
        auto fresh = board(dip);
        mapper->write_expansion(0x4022U, 6U);
        mapper->write_expansion(0x4120U, 1U);
        mapper->write_expansion(0x4122U, 1U);
        mapper->write_prg(0xFFFFU, 5U);
        mapper->clock_cpu();
        mapper->write_chr(0x0321U, 0xA7U);
        REQUIRE(state(*mapper) != state(*fresh));

        mapper->reset();
        REQUIRE(state(*mapper)[kSmbBankOffset] == 0U);
        REQUIRE(state(*mapper)[kSmbFixedBanksOffset] == 0U);
        REQUIRE(state(*mapper)[kUnromBankOffset] == 0U);
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
        REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
        REQUIRE(mapper->mirror_mode() ==
                (dip == 3U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        if (dip == 0U) {
            REQUIRE(mapper->read_prg(0xC123U) == cell(4U * 0x2000U + 0x0123U));
        } else {
            REQUIRE(mapper->read_prg(0x8123U) == cell(dip * 0x20000U + 0x0123U));
        }

        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
        REQUIRE(mapper->read_chr(0x0321U) == 0U);
    }
}

TEST_CASE("Mapper 357 snapshots replay every mode without ROM", "[mapper357][state]") {
    for (Byte dip = 0U; dip < 4U; ++dip) {
        auto mapper = board(dip);
        mapper->write_expansion(0x4022U, 6U);
        mapper->write_expansion(0x4120U, 1U);
        mapper->write_expansion(0x4122U, 1U);
        mapper->write_prg(0xFFFFU, 5U);
        const unsigned irq_cycles = dip == 0U ? 4096U : 123U;
        for (unsigned cycle = 0U; cycle < irq_cycles; ++cycle) {
            mapper->clock_cpu();
        }
        for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
            mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
        }
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(saved.size() < 0x4000U);

        auto copy = board(dip, MirrorMode::Horizontal, 0x80U);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->irq_pending() == (dip == 0U));
        for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
            CAPTURE(dip, addr);
            REQUIRE(copy->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
        }
        if (dip == 0U) {
            REQUIRE(copy->read_prg(0xC123U) == cell(7U * 0x2000U + 0x0123U, 0x80U));
            REQUIRE(copy->read_prg(0xE123U) == cell(8U * 0x2000U + 0x0123U, 0x80U));
        } else {
            REQUIRE(copy->read_prg(0x8123U) ==
                    cell(dip * 0x20000U + 5U * 0x4000U + 0x0123U, 0x80U));
        }
        mapper->clock_cpu();
        copy->clock_cpu();
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
}

TEST_CASE("Mapper 357 rejects malformed and incompatible states atomically", "[mapper357][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 6U);
    mapper->write_expansion(0x4120U, 1U);
    mapper->write_expansion(0x4122U, 1U);
    mapper->write_prg(0xFFFFU, 5U);
    mapper->write_chr(0x0234U, 0xA7U);
    for (unsigned cycle = 0U; cycle < 123U; ++cycle) {
        mapper->clock_cpu();
    }
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
    replace(4U, 0U);
    replace(6U, 1U);
    replace(kSmbBankOffset, 8U);
    replace(kSmbFixedBanksOffset, 2U);
    replace(kUnromBankOffset, 8U);
    replace(kIrqCounterOffset + 1U, 0x10U);
    replace(kIrqFlagsOffset, 4U);
    replace(kIrqFlagsOffset, 2U);
    replace(kIrqFlagsOffset, 0U);

    auto other_dip = board(1U);
    const auto other_before = state(*other_dip);
    StateReader wrong_dip(saved);
    other_dip->load_state(wrong_dip);
    REQUIRE_FALSE(wrong_dip.ok());
    REQUIRE(state(*other_dip) == other_before);
}

} // namespace
