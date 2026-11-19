#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper367.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kStateSize = 27U;
constexpr std::size_t kBlockOffset = 12U;
constexpr std::size_t kBankSelectOffset = 13U;
constexpr std::size_t kFirstBankOffset = 14U;
constexpr std::size_t kMirrorOffset = 22U;
constexpr std::size_t kIrqFlagsOffset = 25U;
constexpr std::size_t kRamFlagsOffset = 26U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 367U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 8U) * 31U + (offset >> 13U) * 47U +
                             (offset >> 17U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper367> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper367::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper367>(h, rom(prg_size, salt), rom(chr_size, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper367& mapper, unsigned index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

unsigned inner_prg_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool mode = (select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return mode ? 62U : banks[6U] & 0x3FU;
    case 1U:
        return banks[7U] & 0x3FU;
    case 2U:
        return mode ? banks[6U] & 0x3FU : 62U;
    default:
        return 63U;
    }
}

unsigned inner_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return banks[slot - 2U];
}

unsigned wired_prg_bank(Byte block, unsigned bank) {
    return (bank & (block < 2U ? 0x1FU : 0x0FU)) | (static_cast<unsigned>(block) << 4U);
}

unsigned wired_chr_bank(Byte block, unsigned bank) {
    return (bank & (block < 2U ? 0xFFU : 0x7FU)) | (static_cast<unsigned>(block) << 7U);
}

TEST_CASE("Mapper 367 validates NES 2.0 ROM and CIRAM geometry", "[mapper367]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg : {0x8000UZ, 0x18000UZ, kPrgSize}) {
            for (const std::size_t chr : {0x2000UZ, 0x6000UZ, kChrSize}) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper367::validate_geometry(header(mirror), prg, chr));
            }
        }
    }

    for (const unsigned id : {0U, 4U, 205U, 364U, 365U, 366U, 368U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper367::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper367::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper367::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x88000UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper367::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2801UZ, 0x84000UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper367::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper367::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
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
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.prg_ram_size = 0x2000U;
            h.prg_nvram_size = 0x2000U;
            h.chr_ram_size = 0x2000U;
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper367::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 367 decodes A1 A0 throughout 6000-7FFF and E001 MMC3 aliases",
          "[mapper367][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper367& mapper) {
        mapper.reset();
        mapper.write_prg(0x6001U, 0xFEU);
        select_bank(mapper, 7U, 0xABU, 0x40U);
        mapper.write_prg(0xA000U, 1U);
        mapper.write_prg(0xA001U, 0xC0U);
        mapper.write_prg(0xC000U, 2U);
        mapper.write_prg(0xC001U, 0U);
        mapper.write_prg(0xE001U, 0U);
        mapper.clock_irq_counter();
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x6000U && raw < 0x8000U) {
            expected->write_prg(static_cast<Address>(0x6000U | (raw & 3U)), value);
        } else if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x6000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
        if (raw < 0x8000U) {
            REQUIRE(actual->read_prg(static_cast<Address>(raw)) == 0U);
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        actual->reset();
        actual->write_prg(0x7FFFU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual)[kBlockOffset] == 3U);
    }
}

TEST_CASE("Mapper 367 applies every PRG block to switchable and fixed banks",
          "[mapper367][banking]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (Byte block = 0U; block < 4U; ++block) {
        for (unsigned reg = 6U; reg <= 7U; ++reg) {
            for (const bool mode : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    mapper->write_prg(static_cast<Address>(0x6000U | block),
                                      static_cast<Byte>(~block));
                    const auto select = static_cast<Byte>(reg | (mode ? 0x40U : 0U));
                    select_bank(*mapper, reg, static_cast<Byte>(value), mode ? 0x40U : 0U);
                    std::array<Byte, 8> banks{};
                    banks[reg] = static_cast<Byte>(value & 0x3FU);
                    for (const auto addr : addresses) {
                        const auto inner = inner_prg_bank(select, banks, addr);
                        const auto bank = wired_prg_bank(block, inner);
                        const auto offset =
                            static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
                        CAPTURE(block, reg, mode, value, addr, inner, bank);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 367 applies CHR blocks after pair alignment and inversion",
          "[mapper367][banking]") {
    auto mapper = board();
    for (Byte block = 0U; block < 4U; ++block) {
        for (unsigned reg = 0U; reg < 6U; ++reg) {
            for (const bool inversion : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    mapper->write_prg(static_cast<Address>(0x6000U | block),
                                      static_cast<Byte>(value ^ 0xFFU));
                    const auto select = static_cast<Byte>(reg | (inversion ? 0x80U : 0U));
                    select_bank(*mapper, reg, static_cast<Byte>(value), inversion ? 0x80U : 0U);
                    std::array<Byte, 8> banks{};
                    banks[reg] = static_cast<Byte>(value);
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                        const auto inner = inner_chr_bank(select, banks, addr);
                        const auto bank = wired_chr_bank(block, inner);
                        const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + 0x155U;
                        CAPTURE(block, reg, inversion, value, slot, inner, bank);
                        REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
                        mapper->write_chr(addr, static_cast<Byte>(cell(offset, 0x35U) ^ 0xFFU));
                        REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 367 wraps fitted ROM only after outer and inner bank decoding",
          "[mapper367][banking]") {
    for (const std::size_t size : {0x8000UZ, 0x18000UZ, kPrgSize}) {
        auto mapper = board(size, kChrSize);
        mapper->write_prg(0x6003U, 0U);
        select_bank(*mapper, 6U, 0xFFU);
        for (unsigned offset = 0U; offset < 0x8000U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            const auto inner =
                inner_prg_bank(0U, std::array<Byte, 8>{0U, 0U, 0U, 0U, 0U, 0U, 0x3FU, 0U}, addr);
            const auto physical =
                (static_cast<std::size_t>(wired_prg_bank(3U, inner)) * kPrgBankSize +
                 (addr & 0x1FFFU)) %
                size;
            CAPTURE(size, addr, inner, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
    for (const std::size_t size : {0x2000UZ, 0x6000UZ, kChrSize}) {
        auto mapper = board(kPrgSize, size);
        mapper->write_prg(0x6003U, 0U);
        select_bank(*mapper, 2U, 0xFFU);
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            const auto physical =
                (static_cast<std::size_t>(wired_chr_bank(3U, 0xFFU)) * kChrBankSize + offset) %
                size;
            CAPTURE(size, offset, physical);
            REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) ==
                    cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 367 controls CIRAM through ordinary MMC3 mirroring writes",
          "[mapper367][ciram]") {
    auto actual = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    auto expected = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    for (unsigned raw = 0xA000U; raw <= 0xBFFFU; ++raw) {
        for (const Byte value : {0U, 1U, 0xFEU, 0xFFU}) {
            actual->reset();
            expected->reset();
            actual->write_prg(static_cast<Address>(raw), value);
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
            CAPTURE(raw, value);
            REQUIRE(actual->mirror_mode() == expected->mirror_mode());
            REQUIRE(state(*actual) == state(*expected));
        }
    }
    actual->reset();
    REQUIRE(actual->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        CAPTURE(addr);
        REQUIRE(actual->maps_nametable(addr));
        REQUIRE_FALSE(actual->nametable_address(addr));
        REQUIRE_FALSE(actual->ciram_address(addr));
        REQUIRE_FALSE(actual->read_nametable(addr));
        REQUIRE_FALSE(actual->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(actual->unmirrored_nametables());
}

TEST_CASE("Mapper 367 MMC3 IRQ reload enable acknowledge and zero repetition", "[mapper367][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x6002U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 367 has write-only low registers no RAM and no expansion audio",
          "[mapper367][ram][audio][bus]") {
    auto mapper = board();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    for (unsigned gate = 0U; gate < 256U; ++gate) {
        mapper->write_prg(0xA001U, static_cast<Byte>(gate));
        for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7000U, 0x7FFFU}) {
            CAPTURE(gate, addr);
            REQUIRE(mapper->maps_prg(addr) == (addr >= 0x6000U));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        mapper->write_prg(0x7FFFU, static_cast<Byte>(gate ^ 0xFFU));
        REQUIRE(state(*mapper)[kBlockOffset] == 3U);
        mapper->write_prg(0x6000U, static_cast<Byte>(gate));
        REQUIRE(state(*mapper)[kBlockOffset] == 0U);
    }

    const auto before = state(*mapper);
    mapper->write_expansion(0x6001U, 0xFFU);
    mapper->write_prg_cpu(0x5001U, 0xFFU, true);
    mapper->observe_cpu_read(0x7000U, 0xA5U);
    mapper->observe_cpu_write(0x7000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 367 reset restores block MMC3 registers IRQ and initial CIRAM",
          "[mapper367][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    auto expected = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_prg(0x6003U, 0U);
    expected->write_prg(0x6003U, 0U);
    expected->reset();
    select_bank(*mapper, 6U, 0x2AU, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));
    REQUIRE(state(*mapper) == state(*expected));

    mapper->write_prg(0x6002U, 0U);
    select_bank(*mapper, 7U, 0x3FU, 0xC0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board(kPrgSize, kChrSize, MirrorMode::Horizontal)));
}

TEST_CASE("Mapper 367 fixed bounded snapshots replay registers and IRQ without ROM",
          "[mapper367][state]") {
    auto mapper = board();
    mapper->write_prg(0x6002U, 0U);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 11U + 3U), 0xC0U);
    }
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        CAPTURE(slot);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
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

TEST_CASE("Mapper 367 rejects malformed and incompatible states atomically", "[mapper367][state]") {
    auto mapper = board();
    mapper->write_prg(0x6003U, 0U);
    select_bank(*mapper, 7U, 0x3FU, 0x40U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    for (std::size_t offset = 0U; offset < kBlockOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte value : {4U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kBlockOffset] = value;
        reject(invalid);
    }
    for (const Byte reserved : {0x08U, 0x10U, 0x20U, 0x38U}) {
        auto invalid = saved;
        invalid[kBankSelectOffset] |= reserved;
        reject(invalid);
    }
    for (const std::size_t offset : {kFirstBankOffset + 6U, kFirstBankOffset + 7U}) {
        auto invalid = saved;
        invalid[offset] = 0x40U;
        reject(invalid);
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {4U, 8U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {1U, 0x3FU, 0xFFU}) {
        auto invalid = saved;
        invalid[kRamFlagsOffset] = value;
        reject(invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
