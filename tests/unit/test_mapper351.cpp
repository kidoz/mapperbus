#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper351.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x40000UZ;
constexpr std::size_t kChrSize = 0x40000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kChrBaseOffset = 10UZ;
constexpr std::size_t kPrgBaseOffset = 11UZ;
constexpr std::size_t kBankingOffset = 12UZ;
constexpr std::size_t kNromMirrorOffset = 13UZ;
constexpr std::size_t kMmc3SelectOffset = 14UZ;
constexpr std::size_t kMmc3BanksOffset = 15UZ;
constexpr std::size_t kMmc3MirrorOffset = 23UZ;
constexpr std::size_t kMmc3IrqFlagsOffset = 26UZ;
constexpr std::size_t kMmc1ShiftOffset = 27UZ;
constexpr std::size_t kMmc1CountOffset = 28UZ;
constexpr std::size_t kMmc1ControlOffset = 29UZ;
constexpr std::size_t kMmc1PrgOffset = 32UZ;
constexpr std::size_t kVrcPrgOffset = 33UZ;
constexpr std::size_t kVrcChrOffset = 35UZ;
constexpr std::size_t kVrcMirrorOffset = 51UZ;
constexpr std::size_t kVrcModeOffset = 52UZ;
constexpr std::size_t kVrcPrescalerOffset = 55UZ;
constexpr std::size_t kVrcControlOffset = 57UZ;
constexpr std::size_t kVrcEnabledOffset = 58UZ;
constexpr std::size_t kVrcPendingOffset = 59UZ;
constexpr std::size_t kChrRamOffset = 60UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 351U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + (offset >> 19U) * 13U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper351> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper351::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper351>(h, rom(prg_size, salt), rom(chr_size, salt + 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_mmc1(Mapper351& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
    }
}

std::size_t mmc3_prg_offset(Byte select, Byte r6, Byte r7, Address addr, std::size_t span) {
    const auto count = span / 0x2000UZ;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const auto penultimate = count - 2U;
    std::size_t bank = 0U;
    if ((select & 0x40U) == 0U) {
        switch (slot) {
        case 0U:
            bank = r6 % count;
            break;
        case 1U:
            bank = r7 % count;
            break;
        case 2U:
            bank = penultimate;
            break;
        default:
            bank = count - 1U;
            break;
        }
    } else {
        switch (slot) {
        case 0U:
            bank = penultimate;
            break;
        case 1U:
            bank = r7 % count;
            break;
        case 2U:
            bank = r6 % count;
            break;
        default:
            bank = count - 1U;
            break;
        }
    }
    return bank * 0x2000UZ + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 351 validates mapper submapper ROM RAM and CIRAM geometry", "[mapper351]") {
    for (std::size_t prg = 0x4000UZ; prg <= 0x100000UZ; prg *= 2U) {
        for (std::size_t chr = 0x2000UZ; chr <= 0x80000UZ; chr *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper351::validate_geometry(header(mirror), prg, chr));
            }
        }
    }
    for (const auto id : {0U, 1U, 4U, 23U, 350U, 352U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper351::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper351::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper351::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x6000UZ, 0x100001UZ, 0x200000UZ}) {
        REQUIRE_FALSE(Mapper351::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x3000UZ, 0x80001UZ, 0x100000UZ}) {
        REQUIRE_FALSE(Mapper351::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper351::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
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
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper351::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 351 outer register aliases are write-only and exact", "[mapper351]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_expansion(addr, value);
        switch (addr & 0xF003U) {
        case 0x5000U:
            expected[kChrBaseOffset] = value;
            break;
        case 0x5001U:
            expected[kPrgBaseOffset] = value & 0xFCU;
            break;
        case 0x5002U:
            expected[kBankingOffset] = value & 0xFDU;
            break;
        default:
            break;
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
    }

    mapper->write_expansion(0x5002U, 0x10U);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->write_expansion(0x5002U, 0x10U);
        auto expected = state(*mapper);
        const auto value = static_cast<Byte>(raw * 13U);
        mapper->write_expansion(static_cast<Address>(raw), value);
        if (raw == 0x4025U) {
            expected[kNromMirrorOffset] = (value >> 3U) & 1U;
        }
        switch (raw & 0xF003U) {
        case 0x5000U:
            expected[kChrBaseOffset] = value;
            break;
        case 0x5001U:
            expected[kPrgBaseOffset] = value & 0xFCU;
            break;
        case 0x5002U:
            expected[kBankingOffset] = value & 0xFDU;
            break;
        default:
            break;
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
    }
    for (const Address addr : {0x4025U, 0x5000U, 0x5001U, 0x5002U, 0x5004U, 0x5FFCU}) {
        REQUIRE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    for (const Address addr : {0x4020U, 0x4024U, 0x4026U, 0x5003U, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 351 MMC3 PRG decoding uses inner spans before outer base", "[mapper351]") {
    auto mapper = board();
    for (const Byte banking : {0x00U, 0x08U}) {
        const std::size_t span = banking == 0U ? 0x40000UZ : 0x20000UZ;
        mapper->write_expansion(0x5000U, 0U);
        mapper->write_expansion(0x5001U, 0U);
        mapper->write_expansion(0x5002U, banking);
        for (const Byte select : {0x00U, 0x40U, 0x80U, 0xC0U}) {
            mapper->write_prg(0x8000U, select);
            for (const Byte r6 : {0U, 1U, 14U, 31U, 63U}) {
                mapper->write_prg(0x8000U, static_cast<Byte>(select | 6U));
                mapper->write_prg(0x8001U, r6);
                for (const Byte r7 : {0U, 2U, 15U, 32U, 63U}) {
                    mapper->write_prg(0x8000U, static_cast<Byte>(select | 7U));
                    mapper->write_prg(0x8001U, r7);
                    for (const auto addr : kPrgAddresses) {
                        const auto offset = mmc3_prg_offset(select, r6, r7, addr, span);
                        CAPTURE(banking, select, r6, r7, addr, offset);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }
    mapper->reset();
    mapper->write_expansion(0x5001U, 0x0CU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0xC000UZ));
    mapper->write_expansion(0x5001U, 0xFCU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0xFC000UZ % kPrgSize));
}

TEST_CASE("Mapper 351 MMC3 CHR inversion and all documented size encodings", "[mapper351]") {
    auto mapper = board();
    for (const Byte banking : {0x00U, 0x20U, 0x40U, 0x60U, 0x80U, 0xA0U, 0xC0U, 0xE0U}) {
        const std::size_t span = (banking & 0x80U) != 0U   ? 0x2000UZ
                                 : (banking & 0x40U) != 0U ? 0x20000UZ
                                 : (banking & 0x20U) != 0U ? 0x8000UZ
                                                           : 0x40000UZ;
        mapper->write_expansion(0x5000U, 0U);
        mapper->write_expansion(0x5002U, banking);
        for (Byte index = 0U; index < 6U; ++index) {
            mapper->write_prg(0x8000U, index);
            mapper->write_prg(0x8001U, static_cast<Byte>(2U + index * 3U));
        }
        for (const Byte inversion : {0x00U, 0x80U}) {
            mapper->write_prg(0x8000U, inversion);
            for (unsigned addr = 0U; addr < 0x2000U; addr += 0x137U) {
                auto slot = (addr >> 10U) ^ (inversion != 0U ? 4U : 0U);
                const unsigned bank = slot < 4U ? ((2U + (slot / 2U) * 3U) & 0xFEU) | (slot & 1U)
                                                : 2U + (slot - 2U) * 3U;
                const auto offset = (bank * 0x400UZ + (addr & 0x3FFU)) & (span - 1U);
                CAPTURE(banking, inversion, addr, bank, offset);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(offset, 0x55U));
            }
        }
    }
}

TEST_CASE("Mapper 351 MMC3 aliases mirroring and scanline IRQs", "[mapper351]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = board();
        const auto value = static_cast<Byte>(raw * 7U);
        mapper->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(static_cast<Address>(0x8000U | (raw & 0xE001U)), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == state(*expected));
    }
    mapper->reset();
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x5000U, 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x5000U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 8U; ++cycle) {
        mapper->clock_irq_counter();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 351 MMC1 serial registers banking filtering and CIRAM", "[mapper351]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 2U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0x3C000UZ));
    for (const Byte mode : {0U, 1U, 2U, 3U}) {
        write_mmc1(*mapper, 0x8000U, static_cast<Byte>((mode << 2U) | 2U));
        write_mmc1(*mapper, 0xE000U, 5U);
        for (const auto addr : kPrgAddresses) {
            const auto low = addr < 0xC000U;
            std::size_t offset = 0U;
            if (mode < 2U) {
                offset = 4UZ * 0x4000UZ + (addr & 0x7FFFU);
            } else if (mode == 2U) {
                offset = low ? addr & 0x3FFFU : 5UZ * 0x4000UZ + (addr & 0x3FFFU);
            } else {
                offset =
                    low ? 5UZ * 0x4000UZ + (addr & 0x3FFFU) : 15UZ * 0x4000UZ + (addr & 0x3FFFU);
            }
            CAPTURE(mode, addr, offset);
            REQUIRE(mapper->read_prg(addr) == cell(offset));
        }
    }
    write_mmc1(*mapper, 0x8000U, 0x10U);
    write_mmc1(*mapper, 0xA000U, 3U);
    write_mmc1(*mapper, 0xC000U, 5U);
    REQUIRE(mapper->read_chr(0x0000U) == cell(0x3000UZ, 0x55U));
    REQUIRE(mapper->read_chr(0x1000U) == cell(0x5000UZ, 0x55U));
    write_mmc1(*mapper, 0x8000U, 0U);
    REQUIRE(mapper->read_chr(0x0000U) == cell(0x2000UZ, 0x55U));
    REQUIRE(mapper->read_chr(0x1000U) == cell(0x3000UZ, 0x55U));
    for (Byte mirror = 0U; mirror < 4U; ++mirror) {
        write_mmc1(*mapper, 0x8000U, mirror);
        const auto expected = mirror == 0U   ? MirrorMode::SingleLower
                              : mirror == 1U ? MirrorMode::SingleUpper
                              : mirror == 2U ? MirrorMode::Vertical
                                             : MirrorMode::Horizontal;
        REQUIRE(mapper->mirror_mode() == expected);
    }
    mapper->reset();
    mapper->write_expansion(0x5000U, 2U);
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper->write_prg_cpu(0xE000U, static_cast<Byte>((7U >> bit) & 1U), true);
    }
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper->write_prg_cpu(0xE000U, static_cast<Byte>((7U >> bit) & 1U), false);
    }
    REQUIRE(mapper->read_prg(0x8000U) == cell(7UZ * 0x4000UZ));
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(state(*mapper)[kMmc1CountOffset] == 0U);
    REQUIRE((state(*mapper)[kMmc1ControlOffset] & 0x0CU) == 0x0CU);
}

TEST_CASE("Mapper 351 VRC4 address wirings banking CIRAM and CPU IRQs", "[mapper351]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 3U);
    mapper->write_prg(0x8000U, 5U);
    mapper->write_prg(0xA000U, 7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(5UZ * 0x2000UZ));
    REQUIRE(mapper->read_prg(0xA000U) == cell(7UZ * 0x2000UZ));
    REQUIRE(mapper->read_prg(0xC000U) == cell(30UZ * 0x2000UZ));
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * 0x2000UZ));
    mapper->write_prg(0x9008U, 2U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(30UZ * 0x2000UZ));
    REQUIRE(mapper->read_prg(0xC000U) == cell(5UZ * 0x2000UZ));
    for (Byte mirror = 0U; mirror < 4U; ++mirror) {
        mapper->write_prg(0x9000U, mirror);
        const auto expected = mirror == 0U   ? MirrorMode::Vertical
                              : mirror == 1U ? MirrorMode::Horizontal
                              : mirror == 2U ? MirrorMode::SingleLower
                                             : MirrorMode::SingleUpper;
        REQUIRE(mapper->mirror_mode() == expected);
    }
    mapper->write_prg(0xB000U, 0x0AU);
    mapper->write_prg(0xB808U, 0x03U);
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x3AUZ * 0x400UZ + 0x123UZ, 0x55U));

    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 0x06U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF80CU, 0U);
    REQUIRE(mapper->irq_pending() == false);
    mapper->write_prg(0xF800U, 0x0FU);
    mapper->write_prg(0xF808U, 0x0FU);
    mapper->write_prg(0xF804U, 0x02U);
    const auto irq_state = state(*mapper);
    REQUIRE(irq_state[53] == 0xFFU);
    REQUIRE(irq_state[54] == 0xFFU);
    REQUIRE(irq_state[55] == 0x55U);
    REQUIRE(irq_state[56] == 0x01U);
    REQUIRE(irq_state[kVrcControlOffset] == 2U);
    REQUIRE(irq_state[kVrcEnabledOffset] == 1U);
    for (unsigned cycle = 0U; cycle < 113U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    const auto clocked = state(*mapper);
    INFO("counter=" << static_cast<unsigned>(clocked[54]) << " prescaler="
                    << (static_cast<unsigned>(clocked[56]) << 8U) +
                           static_cast<unsigned>(clocked[55])
                    << " enabled=" << static_cast<unsigned>(clocked[kVrcEnabledOffset])
                    << " pending=" << static_cast<unsigned>(clocked[kVrcPendingOffset]));
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 351 NROM spans mirroring and CHR-RAM combined PRG space", "[mapper351]") {
    auto mapper = board();
    for (const Byte pp : {0U, 4U, 8U, 12U}) {
        const std::size_t span = pp == 0U ? 0x8000UZ : pp == 4U ? 0x4000UZ : 0x2000UZ;
        mapper->write_expansion(0x5001U, 0U);
        mapper->write_expansion(0x5002U, static_cast<Byte>(0x10U | pp));
        for (const auto addr : kPrgAddresses) {
            CAPTURE(pp, addr);
            REQUIRE(mapper->read_prg(addr) == cell((addr & 0x7FFFU) & (span - 1U)));
        }
        mapper->write_prg(0x8000U, 0xFFU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    }
    mapper->write_expansion(0x4025U, 0x08U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_expansion(0x4025U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

    mapper->write_expansion(0x5002U, 0x11U);
    mapper->write_expansion(0x5001U, 0x3CU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0x3C000UZ));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U, 0x55U));
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
    }
    mapper->write_expansion(0x5002U, 0x10U);
    const auto protected_rom = mapper->read_chr(0x0123U);
    mapper->write_chr(0x0123U, static_cast<Byte>(protected_rom ^ 0xFFU));
    REQUIRE(mapper->read_chr(0x0123U) == protected_rom);
}

TEST_CASE("Mapper 351 reset restores outer and ASIC defaults while power clears CHR RAM",
          "[mapper351][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    auto fresh = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_expansion(0x5000U, 0xFFU);
    mapper->write_expansion(0x5001U, 0xFCU);
    mapper->write_expansion(0x5002U, 0xEDU);
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0x8001U, 0xFFU);
    mapper->write_chr(0x0321U, 0xA7U);
    REQUIRE(state(*mapper) != state(*fresh));

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(30UZ * 0x2000UZ));
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * 0x2000UZ));
    mapper->write_expansion(0x5002U, 1U);
    REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
    REQUIRE(state(*mapper) != state(*fresh));
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));
    mapper->write_expansion(0x5002U, 1U);
    REQUIRE(mapper->read_chr(0x0321U) == 0U);
}

TEST_CASE("Mapper 351 exposes no CPU RAM persistence expansion audio or foreign nametables",
          "[mapper351]") {
    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_audio();
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 351 snapshots replay all ASIC state without ROM serialization",
          "[mapper351][state]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 0xD6U);
    mapper->write_expansion(0x5001U, 0xA8U);
    mapper->write_expansion(0x5002U, 0x51U);
    mapper->write_expansion(0x4025U, 0x08U);
    mapper->write_expansion(0x5000U, 0U);
    mapper->write_prg(0x8000U, 0xC6U);
    mapper->write_prg(0x8001U, 0x2AU);
    mapper->write_prg(0xC000U, 0x5AU);
    mapper->write_expansion(0x5000U, 2U);
    write_mmc1(*mapper, 0x8000U, 0x13U);
    write_mmc1(*mapper, 0xA000U, 0x0BU);
    write_mmc1(*mapper, 0xC000U, 0x15U);
    write_mmc1(*mapper, 0xE000U, 0x07U);
    mapper->write_expansion(0x5000U, 3U);
    mapper->write_prg(0x8000U, 0x0CU);
    mapper->write_prg(0xB000U, 0x09U);
    mapper->write_prg(0xF000U, 0x03U);
    mapper->write_prg(0xF004U, 0x02U);
    mapper->clock_cpu();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0x91U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8252UZ);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == cell(addr, 0x91U));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 351 rejects bounded malformed and incompatible states atomically",
          "[mapper351][state]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 3U);
    mapper->write_expansion(0x5001U, 0xD4U);
    mapper->write_expansion(0x5002U, 0x41U);
    mapper->write_prg(0xF000U, 1U);
    mapper->write_prg(0xF004U, 2U);
    mapper->write_chr(0x0234U, 0xA6U);
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
    for (unsigned bit = 0U; bit < 80U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    const auto replace = [&](std::size_t offset, Byte value) {
        CAPTURE(offset, value);
        auto invalid = saved;
        invalid[offset] = value;
        reject(invalid);
    };
    replace(kPrgBaseOffset, 0x01U);
    replace(kBankingOffset, 0x02U);
    replace(kNromMirrorOffset, 2U);
    replace(kMmc3SelectOffset, 0x08U);
    replace(kMmc3BanksOffset + 6U, 0x40U);
    replace(kMmc3MirrorOffset, 2U);
    replace(kMmc3IrqFlagsOffset, 8U);
    replace(kMmc3IrqFlagsOffset, 4U);
    replace(kMmc1ShiftOffset, 0x20U);
    replace(kMmc1CountOffset, 5U);
    replace(kMmc1ControlOffset, 0x20U);
    replace(kMmc1PrgOffset, 0x20U);
    replace(kVrcPrgOffset, 0x20U);
    replace(kVrcMirrorOffset, 4U);
    replace(kVrcModeOffset, 1U);
    replace(kVrcPrescalerOffset + 1U, 2U);
    replace(kVrcControlOffset, 8U);
    replace(kVrcEnabledOffset, 2U);
    replace(kVrcPendingOffset, 2U);
    auto invalid = saved;
    invalid[kVrcPrescalerOffset] = 0U;
    invalid[kVrcPrescalerOffset + 1U] = 0U;
    reject(invalid);
    invalid = saved;
    invalid[kVrcEnabledOffset] = 0U;
    invalid[kVrcPendingOffset] = 1U;
    reject(invalid);
    invalid = saved;
    invalid[kVrcChrOffset + 1U] = 2U;
    reject(invalid);

    auto other_geometry = board(0x20000UZ, 0x20000UZ);
    const auto before = state(*other_geometry);
    StateReader wrong_geometry(saved);
    other_geometry->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other_geometry) == before);
    auto other_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);
}

} // namespace
