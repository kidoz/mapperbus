#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper353.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kRamSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kSelectOffset = 11UZ;
constexpr std::size_t kBanksOffset = 12UZ;
constexpr std::size_t kMirrorOffset = 20UZ;
constexpr std::size_t kIrqFlagsOffset = 23UZ;
constexpr std::size_t kRamFlagsOffset = 24UZ;
constexpr std::size_t kPpuSlotOffset = 25UZ;
constexpr std::size_t kPrgRamOffset = 26UZ;
constexpr std::size_t kChrRamOffset = kPrgRamOffset + kRamSize;
constexpr std::size_t kAudioOffset = kChrRamOffset + kRamSize;
constexpr std::array<Address, 4> kOuterAddresses{0x8080U, 0xA080U, 0xC080U, 0xE080U};
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 353U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kRamSize;
    result.chr_ram_size = kRamSize;
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

std::unique_ptr<Mapper353> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper353::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper353>(h, rom(prg_size, salt), rom(chr_size, salt + 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_outer(Mapper353& mapper, Byte outer) {
    const auto addr = kOuterAddresses[outer & 3U];
    CAPTURE(addr);
    mapper.write_prg(addr, 0U);
    REQUIRE(state(mapper)[kOuterOffset] == (outer & 3U));
}

void set_banks(Mapper353& mapper, const std::array<Byte, 8>& banks, bool inversion = false) {
    for (Byte index = 0U; index < 6U; ++index) {
        mapper.write_prg(0x8000U, index);
        mapper.write_prg(0x8001U, banks[index]);
    }
    mapper.write_prg(0x8000U, inversion ? 0x80U : 0U);
}

void set_current_a17(Mapper353& mapper, bool high) {
    mapper.write_prg(0x8000U, 2U);
    mapper.write_prg(0x8001U, high ? 0x80U : 0U);
    mapper.write_prg(0x8000U, 0U);
    mapper.observe_ppu_address(0x1000U);
}

unsigned mmc3_bank(Byte select, Byte r6, Byte r7, unsigned slot, unsigned count) {
    const unsigned penultimate = count - 2U;
    if ((select & 0x40U) == 0U) {
        if (slot == 0U) {
            return (r6 & 0x3FU) % count;
        }
        if (slot == 1U) {
            return (r7 & 0x3FU) % count;
        }
        return slot == 2U ? penultimate : count - 1U;
    }
    if (slot == 0U) {
        return penultimate;
    }
    if (slot == 1U) {
        return (r7 & 0x3FU) % count;
    }
    return slot == 2U ? (r6 & 0x3FU) % count : count - 1U;
}

Byte expected_chr_bank(const std::array<Byte, 8>& banks, bool inversion, Address addr) {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if (inversion) {
        slot ^= 4U;
    }
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

TEST_CASE("Mapper 353 validates mapper submapper ROM RAM and CIRAM geometry", "[mapper353]") {
    for (std::size_t prg = 0x40000UZ; prg <= kPrgSize; prg *= 2U) {
        for (std::size_t chr = 0x20000UZ; chr <= kChrSize; chr *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper353::validate_geometry(header(mirror), prg, chr));
            }
        }
    }
    for (const auto id : {0U, 4U, 118U, 351U, 352U, 354U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper353::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper353::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper353::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x20000UZ, 0x3FFFFUZ, 0x60000UZ, 0x100001UZ, 0x200000UZ}) {
        REQUIRE_FALSE(Mapper353::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x10000UZ, 0x1FFFFUZ, 0x30000UZ, 0x80001UZ, 0x100000UZ}) {
        REQUIRE_FALSE(Mapper353::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper353::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_nvram_size = kRamSize;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper353::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 353 decodes outer aliases and forwards every MMC3 alias", "[mapper353]") {
    auto mapper = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(static_cast<Address>(0x8000U | (raw & 0xE001U)), value);
        auto bytes = state(*expected);
        if ((raw & 0x8080U) == 0x8080U) {
            bytes[kOuterOffset] = static_cast<Byte>((raw >> 13U) & 3U);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == bytes);
    }
    for (const auto addr : kOuterAddresses) {
        mapper->reset();
        select_outer(*mapper, 0U);
        mapper->write_prg(addr, 0U);
        const auto zero = state(*mapper);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper)[kOuterOffset] == zero[kOuterOffset]);
    }
}

TEST_CASE("Mapper 353 MMC3 PRG banking is inner-first in outer banks zero and one", "[mapper353]") {
    auto mapper = board();
    for (const Byte outer : {0U, 1U}) {
        for (const Byte select : {0x00U, 0x40U}) {
            for (const Byte r6 : {0U, 1U, 14U, 31U, 63U}) {
                for (const Byte r7 : {0U, 2U, 15U, 30U, 63U}) {
                    select_outer(*mapper, outer);
                    mapper->write_prg(0x8000U, static_cast<Byte>(select | 6U));
                    mapper->write_prg(0x8001U, r6);
                    mapper->write_prg(0x8000U, static_cast<Byte>(select | 7U));
                    mapper->write_prg(0x8001U, r7);
                    for (const auto addr : kPrgAddresses) {
                        const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
                        const auto bank = mmc3_bank(select, r6, r7, slot, 32U);
                        const auto offset = static_cast<std::size_t>(outer) * 0x40000UZ +
                                            bank * 0x2000UZ + (addr & 0x1FFFU);
                        CAPTURE(outer, select, r6, r7, addr, offset);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 353 outer bank two uses CHR A17 as PRG A17", "[mapper353]") {
    auto mapper = board();
    for (const bool high : {false, true}) {
        for (const Byte select : {0x00U, 0x40U}) {
            select_outer(*mapper, 2U);
            set_current_a17(*mapper, high);
            mapper->write_prg(0x8000U, static_cast<Byte>(select | 6U));
            mapper->write_prg(0x8001U, 13U);
            mapper->write_prg(0x8000U, static_cast<Byte>(select | 7U));
            mapper->write_prg(0x8001U, 9U);
            for (const auto addr : kPrgAddresses) {
                const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
                const auto bank = mmc3_bank(select, 13U, 9U, slot, 16U);
                const auto offset =
                    0x80000UZ + bank * 0x2000UZ + (addr & 0x1FFFU) + (high ? 0x20000UZ : 0UZ);
                CAPTURE(high, select, addr, bank, offset);
                REQUIRE(mapper->read_prg(addr) == cell(offset));
            }
        }
    }
    mapper->reset();
    select_outer(*mapper, 2U);
    set_current_a17(*mapper, true);
    const auto high = mapper->read_prg(0x8123U);
    mapper->write_prg(0x8000U, 2U);
    mapper->write_prg(0x8001U, 0U);
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x80123UZ));
    REQUIRE(high == cell(0xA0123UZ));
    mapper->observe_ppu_address(0x0000U);
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x80123UZ));
}

TEST_CASE("Mapper 353 outer bank three replaces PRG A17 only while CHR A17 is low", "[mapper353]") {
    auto mapper = board();
    for (const bool high : {false, true}) {
        for (const Byte select : {0x00U, 0x40U}) {
            select_outer(*mapper, 3U);
            set_current_a17(*mapper, high);
            mapper->write_prg(0x8000U, static_cast<Byte>(select | 6U));
            mapper->write_prg(0x8001U, 21U);
            mapper->write_prg(0x8000U, static_cast<Byte>(select | 7U));
            mapper->write_prg(0x8001U, 10U);
            for (const auto addr : kPrgAddresses) {
                const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
                const auto bank = mmc3_bank(select, 21U, 10U, slot, 32U);
                auto offset = 0xC0000UZ + bank * 0x2000UZ + (addr & 0x1FFFU);
                if (!high) {
                    offset =
                        (offset & ~0x20000UZ) | (static_cast<std::size_t>(addr & 0x4000U) << 3U);
                }
                CAPTURE(high, select, addr, bank, offset);
                REQUIRE(mapper->read_prg(addr) == cell(offset));
            }
        }
    }
}

TEST_CASE("Mapper 353 CHR decoding precedes outer pins and bank-two RAM selection", "[mapper353]") {
    auto mapper = board();
    select_outer(*mapper, 2U);
    set_banks(*mapper, {0x80U, 0x82U, 0x84U, 0x86U, 0x88U, 0x8AU, 0U, 0U});
    for (unsigned addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }

    constexpr std::array<Byte, 8> banks{0x82U, 0x04U, 0x86U, 0x07U, 0x88U, 0x09U, 0U, 0U};
    for (const Byte outer : {0U, 1U, 2U, 3U}) {
        for (const bool inversion : {false, true}) {
            select_outer(*mapper, outer);
            set_banks(*mapper, banks, inversion);
            for (unsigned addr = 0U; addr < 0x2000U; addr += 0x137U) {
                const auto raw = static_cast<Address>(addr);
                const auto bank = expected_chr_bank(banks, inversion, raw);
                CAPTURE(outer, inversion, addr, bank);
                if (outer == 2U && (bank & 0x80U) != 0U) {
                    REQUIRE(mapper->read_chr(raw) == cell(addr, 0xA5U));
                } else {
                    const auto offset = static_cast<std::size_t>(outer) * 0x20000UZ +
                                        static_cast<std::size_t>(bank & 0x7FU) * 0x400UZ +
                                        (addr & 0x3FFU);
                    REQUIRE(mapper->read_chr(raw) == cell(offset, 0x55U));
                    const auto previous = mapper->read_chr(raw);
                    mapper->write_chr(raw, static_cast<Byte>(previous ^ 0xFFU));
                    REQUIRE(mapper->read_chr(raw) == previous);
                }
            }
        }
    }
}

TEST_CASE("Mapper 353 outer zero routes CHR A17 to CIRAM A10", "[mapper353]") {
    auto mapper = board();
    select_outer(*mapper, 0U);
    for (const bool inversion : {false, true}) {
        for (unsigned pattern = 0U; pattern < 16U; ++pattern) {
            std::array<Byte, 8> banks{};
            if (inversion) {
                for (unsigned bit = 0U; bit < 4U; ++bit) {
                    banks[2U + bit] = static_cast<Byte>((pattern & (1U << bit)) != 0U ? 0x80U : 0U);
                }
            } else {
                banks[0] = (pattern & 3U) != 0U ? 0x80U : 0U;
                banks[1] = (pattern & 12U) != 0U ? 0x80U : 0U;
            }
            set_banks(*mapper, banks, inversion);
            for (unsigned nt = 0U; nt < 4U; ++nt) {
                const auto addr = static_cast<Address>(0x2000U | (nt << 10U) | 0x235U);
                const auto bank = expected_chr_bank(banks, inversion, nt * 0x400U);
                const auto expected = static_cast<Address>(((bank & 0x80U) << 3U) | 0x235U);
                CAPTURE(inversion, pattern, nt, bank, expected);
                REQUIRE(mapper->nametable_address(addr) == expected);
                REQUIRE(mapper->nametable_address(static_cast<Address>(addr + 0x1000U)) ==
                        expected);
            }
        }
    }
    select_outer(*mapper, 1U);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    select_outer(*mapper, 0U);
    set_banks(*mapper, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U});
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    set_banks(*mapper, {0x80U, 0x80U, 0U, 0U, 0U, 0U, 0U, 0U});
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    set_banks(*mapper, {0x80U, 0U, 0U, 0U, 0U, 0U, 0U, 0U});
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    set_banks(*mapper, {0U, 0U, 0x80U, 0U, 0U, 0U, 0U, 0U}, true);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 353 exposes gated volatile PRG RAM and bounded CPU bus claims",
          "[mapper353][bus]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kRamSize; addr += 0x101U) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), cell(addr, 0x33U));
    }
    for (unsigned addr = 0U; addr < kRamSize; addr += 0x101U) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) == cell(addr, 0x33U));
    }
    mapper->write_prg(0x6123U, cell(0x123U, 0x33U));
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0xFFU);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6123U) == cell(0x123U, 0x33U));
    mapper->write_prg(0x6123U, 0xFFU);
    REQUIRE(mapper->read_prg(0x6123U) == cell(0x123U, 0x33U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xFFU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xFFU);

    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 353 MMC3 IRQ reload acknowledge repeat and disable", "[mapper353][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
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
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 353 routes only FDS sound registers and clocks conversion audio",
          "[mapper353][audio]") {
    auto mapper = board();
    REQUIRE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    const auto initial = state(*mapper);
    for (const Address addr : {0x4020U, 0x4021U, 0x4022U, 0x4024U, 0x4025U, 0x4026U}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->maps_expansion(0x4023U));
    REQUIRE(mapper->expansion_read_mask(0x4023U) == 0U);

    mapper->write_expansion(0x4023U, 0xFFU);
    mapper->write_expansion(0x4089U, 0x80U);
    for (unsigned index = 0U; index < 64U; ++index) {
        mapper->write_expansion(static_cast<Address>(0x4040U + index), 0x3FU);
    }
    mapper->write_expansion(0x4080U, 0xBFU);
    mapper->write_expansion(0x4087U, 0x80U);
    mapper->write_expansion(0x4082U, 0xFFU);
    mapper->write_expansion(0x4083U, 0x0FU);
    mapper->write_expansion(0x4089U, 0U);
    REQUIRE(mapper->read_expansion(0x4040U) == 0x7FU);
    REQUIRE(mapper->read_expansion(0x4090U) == 0x7FU);
    REQUIRE(mapper->expansion_read_mask(0x4080U) == 0U);
    REQUIRE(mapper->read_expansion(0x4080U) == 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 64U; ++cycle) {
        mapper->clock_audio();
    }
    REQUIRE(mapper->audio_output() > 0.14F);
    REQUIRE(mapper->audio_output() <= 0.15F);
    mapper->reset();
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->read_expansion(0x4090U) == 0x40U);
}

TEST_CASE("Mapper 353 reset selects menu and cold power clears only volatile RAM",
          "[mapper353][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    auto fresh = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    select_outer(*mapper, 2U);
    set_current_a17(*mapper, true);
    mapper->write_prg(0x8000U, 0xC6U);
    mapper->write_prg(0x8001U, 0x2AU);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_chr(0x0321U, 0xA7U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper) != state(*fresh));

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(state(*mapper)[kSelectOffset] == 0U);
    REQUIRE(state(*mapper)[kMirrorOffset] == 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA6U);
    REQUIRE_FALSE(mapper->irq_pending());
    select_outer(*mapper, 2U);
    set_banks(*mapper, {0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0U, 0U});
    REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    select_outer(*mapper, 2U);
    set_banks(*mapper, {0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0U, 0U});
    REQUIRE(mapper->read_chr(0x0321U) == 0U);
}

TEST_CASE("Mapper 353 snapshots replay RAM registers and audio without ROM", "[mapper353][state]") {
    auto mapper = board();
    select_outer(*mapper, 2U);
    set_current_a17(*mapper, true);
    mapper->write_prg(0x8000U, 0xC6U);
    mapper->write_prg(0x8001U, 0x2AU);
    mapper->write_prg(0xC000U, 0x5AU);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_chr(0x0321U, 0xA7U);
    mapper->write_expansion(0x4023U, 2U);
    mapper->write_expansion(0x4089U, 0x80U);
    mapper->write_expansion(0x4040U, 0x2AU);
    mapper->write_expansion(0x4080U, 0xBFU);
    mapper->write_expansion(0x4087U, 0x80U);
    mapper->write_expansion(0x4082U, 0xFFU);
    mapper->write_expansion(0x4083U, 0x0FU);
    mapper->write_expansion(0x4089U, 0U);
    for (unsigned cycle = 0U; cycle < 37U; ++cycle) {
        mapper->clock_audio();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() > kAudioOffset);
    REQUIRE(saved.size() < kAudioOffset + 1024UZ);
    REQUIRE(saved.size() == state(*board()).size());
    REQUIRE(saved.size() < kPrgSize / 16U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6123U) == 0xA6U);
    REQUIRE(copy->read_chr(0x0321U) == 0xA7U);
    REQUIRE(copy->audio_output() == mapper->audio_output());
    REQUIRE(copy->read_prg(0x8123U) == static_cast<Byte>(mapper->read_prg(0x8123U) + 0x80U));
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 353 rejects bounded malformed and incompatible states atomically",
          "[mapper353][state]") {
    auto mapper = board();
    select_outer(*mapper, 3U);
    set_current_a17(*mapper, true);
    mapper->write_prg(0x8000U, 0xC6U);
    mapper->write_prg(0x8001U, 0x2AU);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0x6123U, 0xA6U);
    select_outer(*mapper, 2U);
    set_current_a17(*mapper, true);
    mapper->write_chr(0x0234U, 0xA7U);
    mapper->write_expansion(0x4023U, 2U);
    mapper->write_expansion(0x4080U, 0xBFU);
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
        CAPTURE(offset, value);
        auto invalid = saved;
        invalid[offset] = value;
        reject(invalid);
    };
    replace(kOuterOffset, 4U);
    replace(kSelectOffset, 0x08U);
    replace(kBanksOffset + 6U, 0x40U);
    replace(kBanksOffset + 7U, 0x40U);
    replace(kMirrorOffset, 2U);
    replace(kIrqFlagsOffset, 8U);
    replace(kIrqFlagsOffset, 4U);
    replace(kRamFlagsOffset, 1U);
    replace(kPpuSlotOffset, 8U);
    replace(kAudioOffset, 1U);

    auto smaller = board(kPrgSize / 2U, kChrSize / 2U);
    const auto before = state(*smaller);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*smaller) == before);
    auto other_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);
}

TEST_CASE("Mapper 353 state validation accepts all raw outer and CHR values",
          "[mapper353][state]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        auto valid = saved;
        valid[kOuterOffset] = static_cast<Byte>(outer);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        auto valid = saved;
        valid[kBanksOffset] = static_cast<Byte>(bank);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
