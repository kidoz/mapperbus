#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper313.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kKiB = 1024U;
constexpr std::size_t kStateSize = 24U;
constexpr std::array<std::size_t, 5> kPrgSizes{
    512U * kKiB, 1024U * kKiB, 512U * kKiB, 1024U * kKiB, 640U * kKiB};
constexpr std::array<std::size_t, 5> kGamePrgSizes{
    128U * kKiB, 256U * kKiB, 128U * kKiB, 256U * kKiB, 128U * kKiB};
constexpr std::array<std::size_t, 5> kGameChrSizes{
    128U * kKiB, 128U * kKiB, 256U * kKiB, 256U * kKiB, 128U * kKiB};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 313U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 3U + (offset >> 8U) * 5U + (offset >> 13U) * 17U +
                             (offset >> 17U) * 29U);
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 11U + (offset >> 10U) * 31U +
                             (offset >> 17U) * 37U);
}

std::size_t game_prg_base(Byte submapper, Byte game) {
    if (submapper != 4U)
        return static_cast<std::size_t>(game) * kGamePrgSizes[submapper];
    return game == 0U ? 0U : 256U * kKiB + static_cast<std::size_t>(game - 1U) * 128U * kKiB;
}

std::size_t game_prg_size(Byte submapper, Byte game) {
    return submapper == 4U && game == 0U ? 256U * kKiB : kGamePrgSizes[submapper];
}

std::unique_ptr<Mapper313> board(Byte submapper = 3U, MirrorMode mirror = MirrorMode::Horizontal) {
    const auto h = header(submapper, mirror);
    const auto prg_size = kPrgSizes[submapper];
    const auto chr_size = kGameChrSizes[submapper] * 4U;
    REQUIRE(Mapper313::validate_geometry(h, prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t offset = 0U; offset < prg.size(); ++offset)
        prg[offset] = prg_cell(offset);
    for (std::size_t offset = 0U; offset < chr.size(); ++offset)
        chr[offset] = chr_cell(offset);
    return std::make_unique<Mapper313>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 313 validates every documented submapper and memory population", "[mapper313]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            const auto h = header(submapper, mirror);
            const auto prg_size = kPrgSizes[submapper];
            const auto chr_size = kGameChrSizes[submapper] * 4U;
            REQUIRE(Mapper313::validate_geometry(h, prg_size, chr_size));
            for (std::size_t size : {0UZ,
                                     1UZ,
                                     prg_size - 1UZ,
                                     prg_size + 1UZ,
                                     game_prg_size(submapper, 0U),
                                     prg_size * 2U}) {
                CAPTURE(submapper, size);
                REQUIRE_FALSE(Mapper313::validate_geometry(h, size, chr_size));
            }
            for (std::size_t size : {0UZ,
                                     1UZ,
                                     chr_size - 1UZ,
                                     chr_size + 1UZ,
                                     kGameChrSizes[submapper],
                                     chr_size * 2U}) {
                CAPTURE(submapper, size);
                REQUIRE_FALSE(Mapper313::validate_geometry(h, prg_size, size));
            }
        }
    }

    auto invalid = header();
    invalid.mapper_number = 4U;
    REQUIRE_FALSE(Mapper313::validate_geometry(invalid, kPrgSizes[0], 512U * kKiB));
    invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper313::validate_geometry(invalid, kPrgSizes[0], 512U * kKiB));
    for (Byte submapper = 5U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Mapper313::validate_geometry(header(submapper), kPrgSizes[0], 512U * kKiB));
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper313::validate_geometry(invalid, kPrgSizes[0], 512U * kKiB));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 8192U;
            break;
        case 2U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3U:
            invalid.chr_ram_size = 8192U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper313::validate_geometry(invalid, kPrgSizes[0], 512U * kKiB));
    }
}

TEST_CASE("Mapper 313 power-on and reset select complete games with fresh MMC3 state",
          "[mapper313][reset]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        auto mapper = board(submapper, MirrorMode::Vertical);
        REQUIRE(state(*mapper).size() == kStateSize);
        for (unsigned selection = 0U; selection < 12U; ++selection) {
            const auto game = static_cast<Byte>(selection % 4U);
            const auto base = game_prg_base(submapper, game);
            const auto banks = game_prg_size(submapper, game) / 8192U;
            CAPTURE(submapper, selection, game);
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(base));
            REQUIRE(mapper->read_prg(0xA000U) == prg_cell(base));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(base + (banks - 2U) * 8192U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(base + banks * 8192U - 1U));
            const auto chr_base = static_cast<std::size_t>(game) * kGameChrSizes[submapper];
            constexpr std::array<unsigned, 8> chr_pages{0U, 1U, 0U, 1U, 0U, 0U, 0U, 0U};
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 17U)) ==
                        chr_cell(chr_base + chr_pages[slot] * 1024U + 17U));
            }
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE_FALSE(mapper->irq_pending());

            write_bank(*mapper, 6U, 7U, 0xC0U);
            mapper->write_prg(0xA000U, 0U);
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
            mapper->reset();
        }
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*board(submapper, MirrorMode::Vertical)));
    }
}

TEST_CASE("Mapper 313 PRG banking masks R6 and R7 and honors both MMC3 modes", "[mapper313]") {
    auto mapper = board();
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xDFFFU, 0xE000U, 0xFFFFU};
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Byte mode : {0U, 0x40U}) {
            write_bank(*mapper, 6U, static_cast<Byte>(value), mode);
            write_bank(*mapper, 7U, static_cast<Byte>(value), mode);
            const auto bank = value & 31U;
            const std::array<unsigned, 4> pages{
                mode == 0U ? bank : 30U, bank, mode == 0U ? 30U : bank, 31U};
            for (const auto addr : addresses) {
                const auto slot = (addr - 0x8000U) / 8192U;
                CAPTURE(value, mode, addr);
                REQUIRE(mapper->read_prg(addr) == prg_cell(pages[slot] * 8192U + (addr & 0x1FFFU)));
            }
        }
    }
}

TEST_CASE("Mapper 313 decodes every CPU register alias without ROM bus conflicts",
          "[mapper313][aliases]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->power_on();
        const auto addr = static_cast<Address>(raw_addr);
        mapper->write_prg_cpu(addr, 0xA5U, (raw_addr & 1U) != 0U);
        const auto saved = state(*mapper);
        const bool even = (raw_addr & 1U) == 0U;
        const bool bank_register = raw_addr < 0xA000U;
        const bool mirror_register = raw_addr >= 0xA000U && raw_addr < 0xC000U;
        const bool irq_register = raw_addr >= 0xC000U && raw_addr < 0xE000U;
        CAPTURE(raw_addr);
        REQUIRE(saved[11U] == (bank_register && even ? 0x85U : 0U));
        REQUIRE(saved[12U] == (bank_register && !even ? 0xA5U : 0U));
        REQUIRE(mapper->mirror_mode() ==
                (mirror_register && even ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(saved[22U] == (irq_register && even ? 0xA5U : 0U));
        REQUIRE(saved[21U] == (irq_register && !even ? 0x80U : 0U));
        Byte irq = 0U;
        if (irq_register && !even)
            irq = 9U;
        else if (raw_addr >= 0xE000U && !even)
            irq = 2U;
        REQUIRE(saved[23U] == irq);
    }
}

TEST_CASE("Mapper 313 maps every CHR byte in both inversions and protects ROM", "[mapper313]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Byte inversion : {0U, 0x80U}) {
            std::array<Byte, 6> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = static_cast<Byte>(value + index * 19U);
                write_bank(*mapper, index, banks[index], inversion);
            }
            const std::array<unsigned, 8> pages{banks[0] & 0xFEU,
                                                (banks[0] & 0xFEU) + 1U,
                                                banks[1] & 0xFEU,
                                                (banks[1] & 0xFEU) + 1U,
                                                banks[2],
                                                banks[3],
                                                banks[4],
                                                banks[5]};
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto page = pages[(addr / 1024U) ^ (inversion == 0U ? 0U : 4U)];
                const auto expected = chr_cell(page * 1024U + (addr & 1023U));
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(~expected));
                CAPTURE(value, inversion, addr);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == expected);
            }
        }
    }
}

TEST_CASE("Mapper 313 exposes H/V CIRAM control and open bus outside PRG ROM", "[mapper313]") {
    for (auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(0U, initial);
        for (unsigned addr = 0xA000U; addr < 0xC000U; ++addr) {
            const auto value = static_cast<Byte>(addr * 13U);
            if ((addr & 1U) == 0U) {
                mapper->write_prg(static_cast<Address>(addr), value);
                REQUIRE(mapper->mirror_mode() ==
                        ((value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
            } else {
                const auto before = state(*mapper);
                mapper->write_prg(static_cast<Address>(addr), value);
                REQUIRE(state(*mapper) == before);
            }
        }

        const auto before = state(*mapper);
        for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_prg(addr) == 0U);
            mapper->write_prg(addr, 0xFFU);
            mapper->write_expansion(addr, 0xFFU);
        }
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->maps_prg(0x8000U));
        REQUIRE(mapper->maps_prg(0xFFFFU));
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper->battery_ram().empty());
        mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
}

TEST_CASE("Mapper 313 preserves MMC3 IRQ reload acknowledgement and zero-latch behavior",
          "[mapper313][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xC001U, 0xFFU);
    REQUIRE(state(*mapper)[21U] == 0x80U);
    REQUIRE(state(*mapper)[23U] == 9U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[21U] == 2U);
    REQUIRE(state(*mapper)[23U] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[21U] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[21U] == 0U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[21U] == 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[21U] == 2U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 4U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[23U] == 0U);
}

TEST_CASE("Mapper 313 snapshots replay all mutable state without ROM serialization",
          "[mapper313][state]") {
    auto mapper = board();
    mapper->reset();
    mapper->reset();
    write_bank(*mapper, 6U, 0xE3U, 0xC0U);
    write_bank(*mapper, 7U, 0x91U, 0xC0U);
    for (Byte index = 0U; index < 6U; ++index)
        write_bank(*mapper, index, static_cast<Byte>(index * 31U + 7U), 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 5U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[10U] == 2U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU})
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    for (Address addr : {0x0000U, 0x03FFU, 0x0400U, 0x0800U, 0x1000U, 0x1400U, 0x1800U, 0x1FFFU})
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    copy->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));
    copy->reset();
    mapper->reset();
    REQUIRE(state(*copy) == state(*mapper));
}

TEST_CASE("Mapper 313 rejects damaged truncated and mismatched states atomically",
          "[mapper313][state]") {
    auto mapper = board();
    mapper->reset();
    write_bank(*mapper, 6U, 0xFFU, 0xC0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset : {2UZ, 6UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields{{
        {10U, 4U},
        {10U, 0xFFU},
        {11U, 8U},
        {11U, 0x38U},
        {18U, 0x40U},
        {19U, 0x80U},
        {20U, 2U},
        {20U, 0xFFU},
        {23U, 0x10U},
        {23U, 4U},
        {23U, 5U},
        {23U, 8U},
        {23U, 0xFFU},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        CAPTURE(offset, value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        if (submapper == 3U)
            continue;
        auto other = board(submapper);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }
}

} // namespace
