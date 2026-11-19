#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper446.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kPrgSize = 0x400000UZ;
constexpr std::size_t kMaxPrgSize = 0x1000000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x040000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::size_t kSelectionOffset = 14UZ;
constexpr std::size_t kOuterPrgLsbOffset = 15UZ;
constexpr std::size_t kOuterPrgMsbOffset = 16UZ;
constexpr std::size_t kPrgMaskOffset = 17UZ;
constexpr std::size_t kFlagsOffset = 18UZ;
constexpr std::size_t kChrProtectOffset = 19UZ;
constexpr std::size_t kOuterChrOffset = 20UZ;
constexpr std::size_t kLockOffset = 21UZ;
constexpr std::size_t kMmc1PrgOffset = 29UZ;
constexpr std::size_t kMmc3SelectOffset = 30UZ;
constexpr std::size_t kMmc3BanksOffset = 31UZ;
constexpr std::size_t kMmc3IrqFlagsOffset = 43UZ;
constexpr std::size_t kVrcIrqPendingOffset = 79UZ;
constexpr std::size_t kH3001IrqPendingOffset = 96UZ;
constexpr std::size_t kAudioControlOffset = 119UZ;
constexpr std::size_t kRamLengthsOffset = 120UZ;

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(Byte submapper = 1U,
                  MirrorMode mirror = MirrorMode::Vertical,
                  RamKind ram = RamKind::Volatile,
                  std::size_t chr_ram = kChrRamSize) {
    INesHeader result{};
    result.mapper_number = 446U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kPrgRamSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kPrgRamSize : 0U;
    result.chr_ram_size = chr_ram;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 37U + (offset >> 20U) * 41U + (offset >> 21U) * 23U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper446> board(Byte submapper = 1U,
                                 std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 std::size_t chr_ram = kChrRamSize,
                                 Byte salt = 0U) {
    CAPTURE(submapper, prg_size, mirror, ram, chr_ram, salt);
    const auto h = header(submapper, mirror, ram, chr_ram);
    REQUIRE(Mapper446::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper446>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper446& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select(Mapper446& mapper, Byte code) {
    mapper.write_expansion(0x5000U, code);
}

void outer_prg(Mapper446& mapper, unsigned bank, Byte mask = 0U) {
    mapper.write_expansion(0x5001U, static_cast<Byte>(bank));
    mapper.write_expansion(0x5002U, static_cast<Byte>(bank >> 8U));
    mapper.write_expansion(0x5003U, mask);
}

std::size_t expected_prg(Address addr,
                         unsigned inner_bank,
                         unsigned outer_bank = 0U,
                         Byte mask = 0U,
                         Byte submapper = 1U) {
    const unsigned bank = submapper == 2U ? (inner_bank & mask) | (outer_bank & ~mask)
                                          : (inner_bank & ~mask) | (outer_bank & mask);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

void require_prg(Mapper446& mapper,
                 Address addr,
                 unsigned inner_bank,
                 unsigned outer_bank = 0U,
                 Byte mask = 0U,
                 Byte submapper = 1U,
                 Byte salt = 0U) {
    const auto physical = expected_prg(addr, inner_bank, outer_bank, mask, submapper);
    CAPTURE(addr, inner_bank, outer_bank, mask, submapper, physical);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void mmc1_write(Mapper446& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
    }
}

void mmc3_bank(Mapper446& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void physical_chr_write(Mapper446& mapper, std::size_t offset, Byte value) {
    select(mapper, 3U);
    mapper.write_prg(0x8000U, static_cast<Byte>((offset / 0x2000UZ) & 3U));
    mapper.write_expansion(0x5004U, 0xC0U);
    mapper.write_expansion(0x5005U, 0U);
    mapper.write_expansion(0x5006U, static_cast<Byte>(offset / 0x2000UZ));
    mapper.write_chr(static_cast<Address>(offset & 0x1FFFU), value);
    REQUIRE(mapper.read_chr(static_cast<Address>(offset & 0x1FFFU)) == value);
}

TEST_CASE("Mapper 446 validates FPGA geometry memory and CIRAM metadata",
          "[mapper446][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
                for (const std::size_t chr : {0x008000UZ, 0x020000UZ, kChrRamSize}) {
                    auto mapper = board(submapper, kMinPrgSize, mirror, ram, chr);
                    auto different = board(submapper, kMinPrgSize, mirror, ram, chr, 0x80U);
                    CAPTURE(submapper, mirror, ram, chr);
                    REQUIRE(state(*mapper) == state(*different));
                    REQUIRE(state(*mapper).size() ==
                            kRamLengthsOffset + 16U + chr +
                                (ram == RamKind::Absent ? 0U : kPrgRamSize));
                    REQUIRE(mapper->battery_ram().size() ==
                            (ram == RamKind::Battery ? kPrgRamSize : 0U));
                    REQUIRE(mapper->mirror_mode() ==
                            (submapper == 0U ? MirrorMode::SingleLower : mirror));
                    REQUIRE_FALSE(mapper->irq_pending());
                }
            }
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        REQUIRE(Mapper446::validate_geometry(header(), size, 0U));
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
        REQUIRE_FALSE(Mapper446::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 445U, 447U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, 0U);
    for (Byte submapper = 4U; submapper < 16U; ++submapper) {
        reject(header(submapper), kMinPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x1000001UZ, 0x2000000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t chr : {0UZ, 0x2000UZ, 0x10000UZ, 0x080000UZ}) {
        auto h = header();
        h.chr_ram_size = chr;
        reject(h, kMinPrgSize, 0U);
    }
    reject(header(), kMinPrgSize, 0x2000UZ);
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(1U, mirror), kMinPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 0x2000U;
            h.has_battery = false;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 5U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, 0U);
    }
}

TEST_CASE("Mapper 446 decodes only seven supervisor ports until the lock",
          "[mapper446][decoding][bus][reset]") {
    auto mapper = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Absent);
    const auto cold = state(*mapper);
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const bool mapped = raw >= 0x5000U && raw <= 0x5006U;
        mapper->write_expansion(addr, value);
        auto expected = cold;
        if (mapped) {
            switch (raw) {
            case 0x5000U:
                expected[kSelectionOffset] = value & 0x1FU;
                expected[kLockOffset] = (value & 0x80U) != 0U ? 1U : 0U;
                break;
            case 0x5001U:
                expected[kOuterPrgLsbOffset] = value;
                break;
            case 0x5002U:
                expected[kOuterPrgMsbOffset] = value;
                break;
            case 0x5003U:
                expected[kPrgMaskOffset] = value;
                break;
            case 0x5004U:
                expected[kFlagsOffset] = value & 0xC1U;
                break;
            case 0x5005U:
                expected[kChrProtectOffset] = value & 4U;
                break;
            default:
                expected[kOuterChrOffset] = value & 0x1FU;
                break;
            }
        }
        CAPTURE(raw, value, mapped);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_expansion(addr) == mapped);
        REQUIRE(mapper->expansion_read_mask(addr) == (mapped ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    mapper->reset();
    outer_prg(*mapper, 0x123U, 0xF0U);
    mapper->write_expansion(0x5004U, 0xC1U);
    mapper->write_expansion(0x5005U, 4U);
    mapper->write_expansion(0x5006U, 0x1FU);
    mapper->write_expansion(0x5000U, 0x80U);
    const auto locked = state(*mapper);
    for (Address addr = 0x5000U; addr <= 0x5006U; ++addr) {
        mapper->write_expansion(addr, 0U);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == locked);
    mapper->reset();
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 446 submapper zero and three expose their documented selections",
          "[mapper446][decoding][banking]") {
    auto skrom = board(0U);
    require_prg(*skrom, 0x8000U, 0U, 0U, 0U, 0U);
    mmc1_write(*skrom, 0xE000U, 3U);
    require_prg(*skrom, 0x8000U, 6U, 0U, 0U, 0U);
    require_prg(*skrom, 0xC000U, 30U, 0U, 0U, 0U);
    for (const Byte code : {Byte{0U}, Byte{2U}, Byte{0x1FU}}) {
        select(*skrom, code);
        REQUIRE_FALSE(skrom->maps_prg(0x8000U));
        REQUIRE(skrom->prg_read_mask(0x8000U) == 0U);
        REQUIRE(skrom->read_prg(0x8000U) == 0U);
    }

    auto h3001 = board(3U);
    h3001->write_prg(0x8000U, 3U);
    h3001->write_prg(0xA000U, 5U);
    require_prg(*h3001, 0x8000U, 3U, 0U, 0U, 3U);
    require_prg(*h3001, 0xA000U, 5U, 0U, 0U, 3U);
    require_prg(*h3001, 0xC000U, 62U, 0U, 0U, 3U);
    require_prg(*h3001, 0xE000U, 63U, 0U, 0U, 3U);
    h3001->write_prg(0x9000U, 0x80U);
    require_prg(*h3001, 0x8000U, 62U, 0U, 0U, 3U);
    require_prg(*h3001, 0xC000U, 3U, 0U, 0U, 3U);
    select(*h3001, 0U);
    REQUIRE_FALSE(h3001->maps_prg(0x8000U));
}

TEST_CASE("Mapper 446 implements every submapper one and two inner PRG board",
          "[mapper446][decoding][banking][prg]") {
    for (const Byte submapper : {Byte{1U}, Byte{2U}}) {
        auto mapper = board(submapper);

        select(*mapper, 0U);
        mapper->write_prg(0x8000U, 3U);
        require_prg(*mapper, 0x8000U, 6U, 0U, 0U, submapper);
        require_prg(*mapper, 0xA000U, 7U, 0U, 0U, submapper);
        require_prg(*mapper, 0xC000U, 14U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 15U, 0U, 0U, submapper);

        for (const Byte code : {Byte{1U}, Byte{9U}}) {
            if (submapper == 1U && code == 9U) {
                continue;
            }
            select(*mapper, code);
            mmc3_bank(*mapper, 6U, 5U);
            mmc3_bank(*mapper, 7U, 7U);
            require_prg(*mapper, 0x8000U, 5U, 0U, 0U, submapper);
            require_prg(*mapper, 0xA000U, 7U, 0U, 0U, submapper);
            require_prg(*mapper, 0xC000U, 30U, 0U, 0U, submapper);
            require_prg(*mapper, 0xE000U, 31U, 0U, 0U, submapper);
            mapper->write_prg(0x8000U, 0x40U);
            require_prg(*mapper, 0x8000U, 30U, 0U, 0U, submapper);
            require_prg(*mapper, 0xC000U, 5U, 0U, 0U, submapper);
        }

        select(*mapper, 2U);
        mapper->write_prg(0xFFFFU, 3U);
        require_prg(*mapper, 0x8000U, 12U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 15U, 0U, 0U, submapper);

        select(*mapper, 3U);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 3U, 0U, 0U, submapper);

        select(*mapper, 4U);
        mapper->write_prg(0x8000U, 3U);
        require_prg(*mapper, 0x8000U, 12U, 0U, 0U, submapper);

        for (const Byte code : {Byte{5U}, Byte{6U}, Byte{7U}}) {
            select(*mapper, code);
            mmc1_write(*mapper, 0xE000U, 3U);
            require_prg(*mapper, 0x8000U, code == 7U ? 6U : 6U, 0U, 0U, submapper);
            require_prg(*mapper, 0xC000U, code == 7U ? 62U : 30U, 0U, 0U, submapper);
        }
        select(*mapper, 7U);
        mmc1_write(*mapper, 0xA000U, 0x10U);
        mmc1_write(*mapper, 0xE000U, 3U);
        require_prg(*mapper, 0x8000U, 38U, 0U, 0U, submapper);

        select(*mapper, 8U);
        mapper->write_prg(0x8000U, 0x20U);
        require_prg(*mapper, 0x8000U, 8U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 11U, 0U, 0U, submapper);

        if (submapper == 1U) {
            select(*mapper, 9U);
            mapper->write_prg(0xA000U, 5U);
            require_prg(*mapper, 0x8000U, 5U, 0U, 0U, submapper);
            require_prg(*mapper, 0xA000U, 13U, 0U, 0U, submapper);
            require_prg(*mapper, 0xC000U, 14U, 0U, 0U, submapper);
            require_prg(*mapper, 0xE000U, 15U, 0U, 0U, submapper);
        }

        select(*mapper, 0x0AU);
        mapper->write_prg(0xA000U, 5U);
        require_prg(*mapper, 0x8000U, 10U, 0U, 0U, submapper);
        require_prg(*mapper, 0xC000U, 30U, 0U, 0U, submapper);

        select(*mapper, 0x0BU);
        mapper->write_prg(0x8000U, 0x30U);
        require_prg(*mapper, 0x8000U, 6U, 0U, 0U, submapper);
        require_prg(*mapper, 0xC000U, 14U, 0U, 0U, submapper);

        select(*mapper, 0x0EU);
        mmc3_bank(*mapper, 6U, 9U);
        require_prg(*mapper, 0x8000U, 9U, 0U, 0U, submapper);

        select(*mapper, 0x0FU);
        mapper->write_expansion(0x5000U + 0x100U, 0xA5U);
        require_prg(*mapper, 0x8000U, 60U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 63U, 0U, 0U, submapper);

        select(*mapper, 0x10U);
        mapper->write_prg(0x8000U, 3U);
        mapper->write_prg(0xC000U, 5U);
        require_prg(*mapper, 0x8000U, 6U, 0U, 0U, submapper);
        require_prg(*mapper, 0xC000U, 5U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 31U, 0U, 0U, submapper);

        for (const Byte code : {Byte{0x12U}, Byte{0x15U}, Byte{0x18U}}) {
            mapper->reset();
            select(*mapper, code);
            CAPTURE(code);
            mapper->write_prg(0x8000U, 3U);
            mapper->write_prg(0xA000U, 5U);
            require_prg(*mapper, 0x8000U, 3U, 0U, 0U, submapper);
            require_prg(*mapper, 0xA000U, 5U, 0U, 0U, submapper);
            require_prg(*mapper, 0xC000U, 30U, 0U, 0U, submapper);
            require_prg(*mapper, 0xE000U, 31U, 0U, 0U, submapper);
            if (code != 0x12U) {
                mapper->write_prg(0x9001U, 2U);
                require_prg(*mapper, 0x8000U, 30U, 0U, 0U, submapper);
                require_prg(*mapper, 0xC000U, 3U, 0U, 0U, submapper);
            }
        }

        select(*mapper, 0x1AU);
        mapper->write_prg(0x8000U, 3U);
        mapper->write_prg(0xA000U, 5U);
        mapper->write_prg(0xC000U, 7U);
        require_prg(*mapper, 0x8000U, 3U, 0U, 0U, submapper);
        require_prg(*mapper, 0xA000U, 5U, 0U, 0U, submapper);
        require_prg(*mapper, 0xC000U, 7U, 0U, 0U, submapper);
        require_prg(*mapper, 0xE000U, 15U, 0U, 0U, submapper);
    }

    auto mapper = board(1U);
    for (const Byte code : {Byte{0x0CU}, Byte{0x0DU}, Byte{0x11U}, Byte{0x13U}, Byte{0x1FU}}) {
        select(*mapper, code);
        REQUIRE_FALSE(mapper->maps_prg(0x8000U));
    }
    select(*mapper, 0x0DU);
    auto second = board(2U);
    select(*second, 0x0DU);
    mmc3_bank(*second, 6U, 11U);
    require_prg(*second, 0x8000U, 11U, 0U, 0U, 2U);
}

TEST_CASE("Mapper 446 combines outer PRG pins before fitted-ROM wrapping",
          "[mapper446][banking][prg]") {
    for (const Byte submapper : {Byte{1U}, Byte{2U}}) {
        auto mapper = board(submapper);
        select(*mapper, 0U);
        mapper->write_prg(0x8000U, 3U);
        outer_prg(*mapper, 0x123U, 0xF0U);
        require_prg(*mapper, 0x8000U, 6U, 0x123U, 0xF0U, submapper);
        require_prg(*mapper, 0xC000U, 14U, 0x123U, 0xF0U, submapper);
        REQUIRE(state(*mapper)[kOuterPrgLsbOffset] == 0x23U);
        REQUIRE(state(*mapper)[kOuterPrgMsbOffset] == 1U);
        REQUIRE(state(*mapper)[kPrgMaskOffset] == 0xF0U);
    }
    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(1U, size);
        fitted->write_prg(0x8000U, 7U);
        outer_prg(*fitted, 0x1FFU, 0xFFU);
        const auto physical = expected_prg(0x8123U, 14U, 0x1FFU, 0xFFU) % size;
        REQUIRE(fitted->read_prg(0x8123U) == cell(physical));
    }
}

TEST_CASE("Mapper 446 banks CHR RAM for every inner family and honors masks",
          "[mapper446][banking][chr][ram]") {
    auto mapper = board();
    for (const auto [code, inner_bank] : std::array<std::pair<Byte, unsigned>, 5>{
             {{3U, 2U}, {8U, 2U}, {0x0BU, 3U}, {0x10U, 2U}, {0x12U, 5U}}}) {
        select(*mapper, code);
        mapper->write_prg(0x8000U, static_cast<Byte>(inner_bank));
        if (code == 0x10U) {
            mapper->write_prg(0xD000U, 16U);
        } else if (code == 0x12U) {
            mapper->write_prg(0xB000U, 0x50U);
        }
        const auto physical = inner_bank * 0x2000UZ + 0x123UZ;
        physical_chr_write(*mapper, physical, cell(physical, 0x55U));
        select(*mapper, code);
        if (code == 3U || code == 8U || code == 0x0BU) {
            mapper->write_prg(0x8000U, static_cast<Byte>(inner_bank));
        } else if (code == 0x10U) {
            mapper->write_prg(0xD000U, 16U);
        } else {
            mapper->write_prg(0xB000U, 0x50U);
        }
        CAPTURE(code, inner_bank, physical);
        REQUIRE(mapper->read_chr(0x0123U) == cell(physical, 0x55U));
    }

    select(*mapper, 3U);
    mapper->write_prg(0x8000U, 2U);
    for (const auto [flags, expected_bank] : std::array<std::pair<Byte, unsigned>, 4>{
             {{0U, 2U}, {0x40U, 2U}, {0x80U, 0x12U}, {0xC0U, 0x16U}}}) {
        mapper->write_expansion(0x5004U, flags);
        mapper->write_expansion(0x5006U, 0x15U);
        const auto physical = expected_bank * 0x2000UZ + 0x155UZ;
        physical_chr_write(*mapper, physical, cell(physical, 0x55U));
        select(*mapper, 3U);
        mapper->write_prg(0x8000U, 2U);
        mapper->write_expansion(0x5004U, flags);
        mapper->write_expansion(0x5006U, 0x15U);
        CAPTURE(flags, expected_bank, physical);
        REQUIRE(mapper->read_chr(0x0155U) == cell(physical, 0x55U));
    }

    mapper->write_chr(0x0155U, 0xA6U);
    const auto protected_value = mapper->read_chr(0x0155U);
    mapper->write_expansion(0x5005U, 4U);
    mapper->write_chr(0x0155U, static_cast<Byte>(protected_value ^ 0xFFU));
    REQUIRE(mapper->read_chr(0x0155U) == protected_value);
    mapper->write_expansion(0x5005U, 0xFBU);
    mapper->write_chr(0x0155U, 0x5AU);
    REQUIRE(mapper->read_chr(0x0155U) == 0x5AU);
}

TEST_CASE("Mapper 446 implements MMC1 MMC3 VRC and H3001 CHR registers",
          "[mapper446][banking][chr][decoding]") {
    auto mapper = board();
    select(*mapper, 5U);
    mmc1_write(*mapper, 0x8000U, 0x1CU);
    mmc1_write(*mapper, 0xA000U, 0x08U);
    mmc1_write(*mapper, 0xC000U, 0x0CU);
    physical_chr_write(*mapper, 0x8123U, 0x61U);
    physical_chr_write(*mapper, 0xC123U, 0x62U);
    select(*mapper, 5U);
    mmc1_write(*mapper, 0x8000U, 0x1CU);
    mmc1_write(*mapper, 0xA000U, 0x08U);
    mmc1_write(*mapper, 0xC000U, 0x0CU);
    REQUIRE(mapper->read_chr(0x0123U) == 0x61U);
    REQUIRE(mapper->read_chr(0x1123U) == 0x62U);

    select(*mapper, 1U);
    mmc3_bank(*mapper, 2U, 0x24U, 0x80U);
    physical_chr_write(*mapper, 0x9123U, 0x63U);
    select(*mapper, 1U);
    mmc3_bank(*mapper, 2U, 0x24U, 0x80U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x63U);

    select(*mapper, 0x15U);
    mapper->write_prg(0xB000U, 4U);
    mapper->write_prg(0xB008U, 1U);
    physical_chr_write(*mapper, 0x5123U, 0x64U);
    select(*mapper, 0x15U);
    mapper->write_prg(0xB000U, 4U);
    mapper->write_prg(0xB008U, 1U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x64U);

    auto h3001 = board(3U);
    h3001->write_prg(0xB003U, 0x24U);
    physical_chr_write(*h3001, 0x9123U, 0x65U);
    select(*h3001, 1U);
    h3001->write_prg(0xB003U, 0x24U);
    REQUIRE(h3001->read_chr(0x0D23U) == 0x65U);
}

TEST_CASE("Mapper 446 PNROM and HKROM pattern fetches drive their CHR latches",
          "[mapper446][banking][chr][decoding]") {
    auto mapper = board();
    select(*mapper, 9U);
    mapper->write_prg(0xB000U, 2U);
    mapper->write_prg(0xD000U, 6U);
    physical_chr_write(*mapper, 0x2FD8U, 0x71U);
    physical_chr_write(*mapper, 0x6FD8U, 0x72U);
    physical_chr_write(*mapper, 0x2FE8U, 0x73U);
    physical_chr_write(*mapper, 0x6FE8U, 0x74U);
    select(*mapper, 9U);
    mapper->write_prg(0xB000U, 2U);
    mapper->write_prg(0xD000U, 6U);
    const auto latch_state = state(*mapper);
    REQUIRE(latch_state[kRamLengthsOffset + 16U + kPrgRamSize + 0x6FE8U] == 0x74U);
    REQUIRE(latch_state[kSelectionOffset] == 9U);
    REQUIRE(latch_state[kFlagsOffset] == 0xC0U);
    REQUIRE(latch_state[kOuterChrOffset] == 3U);
    REQUIRE(latch_state[47U] == 6U);
    REQUIRE(mapper->read_chr(0x0FD8U) == 0x71U);
    REQUIRE(state(*mapper)[49U] == 0U);
    REQUIRE(mapper->read_chr(0x0FE8U) == 0x74U);
    REQUIRE(state(*mapper)[49U] == 1U);

    mapper->reset();
    select(*mapper, 0x0AU);
    mapper->write_prg(0xB000U, 2U);
    mapper->write_prg(0xD000U, 6U);
    REQUIRE(mapper->read_chr(0x0FD8U) == 0x71U);
    REQUIRE(mapper->read_chr(0x0FE8U) == 0x73U);
    REQUIRE(mapper->read_chr(0x0FE8U) == 0x74U);
}

TEST_CASE("Mapper 446 routes fixed software single-screen and TxSROM CIRAM",
          "[mapper446][ciram][decoding]") {
    auto mapper = board(1U, kPrgSize, MirrorMode::Horizontal);
    select(*mapper, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_expansion(0x5004U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    select(*mapper, 4U);
    mapper->write_prg(0x8000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    mapper->write_prg(0x8000U, 0x10U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    select(*mapper, 0x0BU);
    mapper->write_prg(0x8000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

    select(*mapper, 5U);
    for (const auto [control, expected] :
         std::array<std::pair<Byte, MirrorMode>, 4>{{{0U, MirrorMode::SingleLower},
                                                     {1U, MirrorMode::SingleUpper},
                                                     {2U, MirrorMode::Vertical},
                                                     {3U, MirrorMode::Horizontal}}}) {
        mmc1_write(*mapper, 0x8000U, control);
        REQUIRE(mapper->mirror_mode() == expected);
    }
    select(*mapper, 1U);
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    select(*mapper, 0x0AU);
    mapper->write_prg(0xF000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    select(*mapper, 0x10U);
    mapper->write_prg(0xB003U, 0x0CU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

    select(*mapper, 0x0EU);
    mmc3_bank(*mapper, 0U, 0x80U);
    mmc3_bank(*mapper, 1U, 0x80U);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
        const auto mapped = mapper->nametable_address(addr);
        REQUIRE(mapped);
        REQUIRE(*mapped == (0x400U | (addr & 0x3FFU)));
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    select(*mapper, 1U);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
}

TEST_CASE("Mapper 446 CPU RAM gates persistence and VRC2 microwire bus mask",
          "[mapper446][bus][ram][reset]") {
    auto mapper = board();
    select(*mapper, 1U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x1EU);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0x6123U, 0x1EU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x1EU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x1EU);
    mapper->reset();
    select(*mapper, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x1EU);
    mapper->power_on();
    select(*mapper, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    select(*mapper, 5U);
    mapper->write_prg(0x6123U, 0x6AU);
    mmc1_write(*mapper, 0xE000U, 0x10U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    mmc1_write(*mapper, 0xE000U, 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x6AU);

    select(*mapper, 0x15U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    mapper->write_prg(0x9001U, 1U);
    REQUIRE(mapper->maps_prg(0x6123U));
    mapper->write_prg(0x6123U, 0x7AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x7AU);

    auto microwire = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Absent);
    select(*microwire, 0x12U);
    REQUIRE(microwire->maps_prg(0x6000U));
    REQUIRE(microwire->prg_read_mask(0x6000U) == 1U);
    REQUIRE(microwire->prg_read_mask(0x7000U) == 0U);
    microwire->write_prg(0x6123U, 0xFEU);
    REQUIRE(microwire->read_prg(0x6123U) == 0U);
    microwire->write_prg(0x6123U, 0xFFU);
    REQUIRE(microwire->read_prg(0x6123U) == 1U);

    auto persistent = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kPrgRamSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    select(*persistent, 1U);
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    persistent->power_on();
    select(*persistent, 1U);
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    persistent->set_battery_ram(std::array<Byte, 4>{9U, 8U, 7U, 6U});
    REQUIRE(persistent->read_prg(0x6003U) == 6U);
    REQUIRE(persistent->read_prg(0x6004U) == bytes[4U]);
}

TEST_CASE("Mapper 446 MMC3 VRC and H3001 IRQs remain selected-device state",
          "[mapper446][irq][reset]") {
    auto mapper = board();
    select(*mapper, 1U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    select(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned clock = 0U; clock < 8U; ++clock) {
        mapper->clock_irq_counter();
    }
    REQUIRE(state(*mapper)[kMmc3IrqFlagsOffset] == 2U);
    select(*mapper, 1U);
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    select(*mapper, 0x15U);
    mapper->write_prg(0xF000U, 0xFEU);
    mapper->write_prg(0xF008U, 0x0FU);
    mapper->write_prg(0xF001U, 6U);
    REQUIRE(state(*mapper)[77U] == 6U);
    REQUIRE(state(*mapper)[78U] == 1U);
    REQUIRE(state(*mapper)[74U] == 0xFEU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF001U, 2U);
    REQUIRE(state(*mapper)[kVrcIrqPendingOffset] == 0U);
}

TEST_CASE("Mapper 446 VRC6 audio clocks every CPU cycle and gates with selection",
          "[mapper446][audio][irq]") {
    auto mapper = board();
    select(*mapper, 0x10U);
    REQUIRE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->write_prg(0x9000U, 0x8FU);
    mapper->write_prg(0x9002U, 0U);
    mapper->write_prg(0x9001U, 0x80U);
    REQUIRE(mapper->audio_output() == Catch::Approx(15.0F * 0.006F));
    mapper->write_prg(0xB000U, 0x3FU);
    mapper->write_prg(0xB002U, 0U);
    mapper->write_prg(0xB001U, 0x80U);
    mapper->clock_audio();
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() > 15.0F * 0.006F);
    const auto running = state(*mapper);
    mapper->write_prg(0x9003U, 1U);
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() > 15.0F * 0.006F);
    REQUIRE(state(*mapper)[kAudioControlOffset] == 1U);

    mapper->write_prg(0xF000U, 0xFFU);
    mapper->write_prg(0xF002U, 6U);
    mapper->clock_audio();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF001U, 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    select(*mapper, 0U);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    const auto inactive = state(*mapper);
    mapper->clock_audio();
    REQUIRE(state(*mapper) == inactive);
    REQUIRE(running != inactive);
}

TEST_CASE("Mapper 446 H3001 CPU timer IRQ counts down and acknowledges at its port",
          "[mapper446][irq]") {
    auto mapper = board(3U);
    mapper->write_prg(0x9005U, 0U);
    mapper->write_prg(0x9006U, 2U);
    mapper->write_prg(0x9004U, 0U);
    mapper->write_prg(0x9003U, 0x80U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kH3001IrqPendingOffset] == 1U);
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x9003U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 446 reset restores default selection supervisor lock and RAM policy",
          "[mapper446][reset][ram][ciram]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}, Byte{2U}, Byte{3U}}) {
        auto mapper = board(submapper, kPrgSize, MirrorMode::Horizontal);
        const auto initial = state(*mapper);
        select(*mapper, 1U);
        mapper->write_prg(0x6123U, 0xA7U);
        physical_chr_write(*mapper, 0x123U, 0x5AU);
        mapper->write_expansion(0x5004U, 0xC1U);
        mapper->reset();
        REQUIRE(state(*mapper)[kSelectionOffset] == initial[kSelectionOffset]);
        REQUIRE(state(*mapper)[kLockOffset] == 0U);
        REQUIRE(state(*mapper)[kFlagsOffset] == 0U);
        select(*mapper, 1U);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x5AU);
        mapper->write_prg(0x6123U, 0x5AU);
        mapper->power_on();
        REQUIRE(state(*mapper) == initial);
        select(*mapper, 1U);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
    }
}

TEST_CASE("Mapper 446 bounded snapshots replay all state without ROM bytes",
          "[mapper446][state][banking][ram][audio][irq]") {
    auto mapper = board();
    select(*mapper, 0x10U);
    outer_prg(*mapper, 0x123U, 0xF0U);
    mapper->write_expansion(0x5004U, 0x81U);
    mapper->write_expansion(0x5006U, 0x12U);
    mapper->write_prg(0x8000U, 3U);
    mapper->write_prg(0xC000U, 5U);
    mapper->write_prg(0xD000U, 0x24U);
    mapper->write_prg(0x9000U, 0x8FU);
    mapper->write_prg(0x9001U, 0x80U);
    mapper->write_prg(0xF000U, 0xFEU);
    mapper->write_prg(0xF002U, 6U);
    mapper->clock_audio();
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_chr(0x0123U, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kRamLengthsOffset + 16U + kPrgRamSize + kChrRamSize);
    REQUIRE(saved.size() == 270472U);
    REQUIRE(saved[kVrcIrqPendingOffset] == 0U);

    auto copy = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Volatile, kChrRamSize, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->has_expansion_audio());
    REQUIRE(copy->audio_output() == mapper->audio_output());
    REQUIRE(copy->read_prg(0x6123U) == 0xA5U);
    REQUIRE(copy->read_chr(0x0123U) == 0x5AU);
    copy->clock_audio();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 446 rejects truncated damaged and mismatched states atomically",
          "[mapper446][state][ram]") {
    auto mapper = board();
    select(*mapper, 0x10U);
    outer_prg(*mapper, 0x123U, 0xF0U);
    mapper->write_expansion(0x5004U, 0x81U);
    mapper->write_expansion(0x5005U, 4U);
    mapper->write_prg(0x6123U, 0x05U);
    mapper->write_prg(0x9000U, 0x8FU);
    mapper->write_prg(0x9001U, 0x80U);
    mapper->write_prg(0xF000U, 0xFFU);
    mapper->write_prg(0xF002U, 6U);
    mapper->clock_audio();
    mapper->write_chr(0x0123U, 0xA5U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < kRamLengthsOffset; ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (const std::size_t length : {kRamLengthsOffset,
                                     kRamLengthsOffset + 1U,
                                     kRamLengthsOffset + 8U,
                                     kRamLengthsOffset + 16U,
                                     active.size() - 0x2000U,
                                     active.size() - 1U}) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 16>{{{0U, 0U},
                                                       {2U, 1U},
                                                       {6U, 1U},
                                                       {10U, 1U},
                                                       {kSelectionOffset, 0x20U},
                                                       {kFlagsOffset, 2U},
                                                       {kChrProtectOffset, 2U},
                                                       {kOuterChrOffset, 0x20U},
                                                       {kLockOffset, 2U},
                                                       {kMmc1PrgOffset, 0x20U},
                                                       {kMmc3SelectOffset, 8U},
                                                       {kMmc3BanksOffset + 6U, 0x40U},
                                                       {kMmc3IrqFlagsOffset, 8U},
                                                       {kVrcIrqPendingOffset, 2U},
                                                       {kH3001IrqPendingOffset, 2U},
                                                       {kAudioControlOffset, 8U}}}) {
        auto invalid = active;
        invalid[offset] = value;
        invalid[kRamLengthsOffset + 8U + 0x123U] ^= 0xFFU;
        invalid.back() ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper446>& other) {
        const auto before = state(*other);
        StateReader reader(active);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_submapper = board(2U);
    mismatch(wrong_submapper);
    auto wrong_prg = board(1U, kMinPrgSize);
    mismatch(wrong_prg);
    auto wrong_chr = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Volatile, 0x8000U);
    mismatch(wrong_chr);
    auto no_ram = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Absent);
    mismatch(no_ram);
    auto battery = board(1U, kPrgSize, MirrorMode::Vertical, RamKind::Battery);
    mismatch(battery);
    auto wrong_mirror = board(1U, kPrgSize, MirrorMode::Horizontal);
    mismatch(wrong_mirror);

    auto changed = active;
    changed[kSelectionOffset] = 0U;
    changed[kFlagsOffset] = 1U;
    changed[kRamLengthsOffset + 8U + 0x123U] ^= 0xFFU;
    changed.back() ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    select(*mapper, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x05U ^ 0xFFU));
}

} // namespace
