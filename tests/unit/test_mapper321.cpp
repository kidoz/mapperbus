#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper321.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 256U * 1024U;
constexpr std::size_t kChrSize = 256U * 1024U;
constexpr std::size_t kStateSize = 25U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 321U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) + (offset >> 15U) + salt);
}

Byte chr_cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 7U) + (offset >> 13U) + salt);
}

std::vector<Byte> prg_image(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = prg_cell(offset, salt);
    }
    return data;
}

std::vector<Byte> chr_image(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = chr_cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper321> board(bool vertical = true,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Byte salt = 0U) {
    const auto h = header(vertical);
    REQUIRE(Mapper321::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper321>(h, prg_image(prg_size, salt), chr_image(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper321& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

void write_outer(Mapper321& mapper, Byte latch) {
    mapper.write_prg(static_cast<Address>(0x6000U | latch), 0xA5U);
}

TEST_CASE("Mapper 321 validates its complete native memory geometry", "[mapper321]") {
    for (std::size_t prg = 32768U; prg <= kPrgSize; prg += 8192U) {
        for (std::size_t chr = 8192U; chr <= kChrSize; chr += 1024U) {
            REQUIRE(Mapper321::validate_geometry(header(), prg, chr));
        }
    }
    for (const bool vertical : {false, true}) {
        REQUIRE(Mapper321::validate_geometry(header(vertical), kPrgSize, kChrSize));
    }

    for (const auto mapper : {0U, 4U, 287U, 320U, 322U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper);
        REQUIRE_FALSE(Mapper321::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper321::validate_geometry(h, kPrgSize, kChrSize));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper321::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t prg :
         {0UZ, 8192UZ, 16384UZ, 32767UZ, 33792UZ, 262145UZ, 270336UZ, 524288UZ}) {
        REQUIRE_FALSE(Mapper321::validate_geometry(header(), prg, kChrSize));
    }
    for (const std::size_t chr : {0UZ, 1024UZ, 8191UZ, 9000UZ, 262145UZ, 263168UZ, 524288UZ}) {
        REQUIRE_FALSE(Mapper321::validate_geometry(header(), kPrgSize, chr));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        case 5U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        REQUIRE_FALSE(Mapper321::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 321 powers on with bounded ROM-only MMC3 state", "[mapper321]") {
    for (const bool vertical : {false, true}) {
        auto mapper = board(vertical);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(0x123U));
        REQUIRE(mapper->read_prg(0xA123U) == prg_cell(0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == prg_cell(14U * 8192U + 0x123U));
        REQUIRE(mapper->read_prg(0xE123U) == prg_cell(15U * 8192U + 0x123U));
        for (Address addr = 0U; addr < 8192U; ++addr) {
            REQUIRE(mapper->read_chr(addr) ==
                    chr_cell(addr < 0x1000U ? addr & 0x7FFU : addr & 0x3FFU));
            mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) ==
                    chr_cell(addr < 0x1000U ? addr & 0x7FFU : addr & 0x3FFU));
        }
        REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
        REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    }
}

TEST_CASE("Mapper 321 decodes every CPU address alias and ignores outer write data",
          "[mapper321]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>(addr * 29U + (addr >> 8U));
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto actual = state(*mapper);

        mapper->power_on();
        if (addr >= 0x8000U) {
            mapper->write_prg(static_cast<Address>(addr & 0xE001U), value);
        } else if (addr >= 0x6000U) {
            mapper->write_prg(static_cast<Address>(0x6000U | (addr & 0x003CU)),
                              static_cast<Byte>(value ^ 0xFFU));
        }
        const auto expected = state(*mapper);
        CAPTURE(addr, value);
        REQUIRE(actual == expected);
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 321 outer register obeys both MMC3 RAM gates", "[mapper321]") {
    auto mapper = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        for (unsigned latch = 0U; latch < 16U; ++latch) {
            mapper->power_on();
            write_outer(*mapper, 0x28U);
            mapper->write_prg(0xA001U, static_cast<Byte>(gate));
            write_outer(*mapper, static_cast<Byte>(latch << 2U));
            const bool writable = (gate & 0x80U) != 0U && (gate & 0x40U) == 0U;
            CAPTURE(gate, latch);
            REQUIRE(state(*mapper)[10U] == (writable ? latch << 2U : 0x28U));
        }
    }
}

TEST_CASE("Mapper 321 MMC3 PRG banking uses outer A17 and ignores NROM page bits", "[mapper321]") {
    auto mapper = board();
    constexpr std::array<Byte, 8> values{0U, 1U, 14U, 15U, 16U, 31U, 63U, 255U};
    for (unsigned outer = 0U; outer < 2U; ++outer) {
        for (unsigned page = 0U; page < 4U; ++page) {
            for (const bool mode : {false, true}) {
                for (const Byte r6 : values) {
                    for (const Byte r7 : values) {
                        write_outer(*mapper, static_cast<Byte>((page << 4U) | (outer << 2U)));
                        select_bank(*mapper, 6U, r6, mode);
                        select_bank(*mapper, 7U, r7, mode);
                        const std::size_t base = outer * 128U * 1024U;
                        const std::array<unsigned, 4> banks{
                            mode ? 14U : static_cast<unsigned>(r6 & 15U),
                            static_cast<unsigned>(r7 & 15U),
                            mode ? static_cast<unsigned>(r6 & 15U) : 14U,
                            15U,
                        };
                        for (unsigned slot = 0U; slot < 4U; ++slot) {
                            const std::size_t offset = base + banks[slot] * 8192U + 0x1A5U;
                            CAPTURE(outer, page, mode, r6, r7, slot);
                            REQUIRE(mapper->read_prg(static_cast<Address>(
                                        0x8000U + slot * 8192U + 0x1A5U)) == prg_cell(offset));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 321 NROM mode combines outer A17 inner pages and CPU A14 A13", "[mapper321]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 2U; ++outer) {
        for (unsigned page = 0U; page < 4U; ++page) {
            select_bank(*mapper, 6U, 13U, true);
            select_bank(*mapper, 7U, 11U, true);
            write_outer(*mapper, static_cast<Byte>(0x08U | (page << 4U) | (outer << 2U)));
            const std::size_t base = outer * 128U * 1024U + page * 32768U;
            for (unsigned offset = 0U; offset < 32768U; ++offset) {
                CAPTURE(outer, page, offset);
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                        prg_cell(base + offset));
            }
        }
    }
}

TEST_CASE("Mapper 321 wraps disconnected ROM pins only after outer decoding", "[mapper321]") {
    auto mapper = board(true, 32768U, 8192U);
    write_outer(*mapper, 0x3CU);
    for (unsigned offset = 0U; offset < 32768U; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) == prg_cell(offset));
    }

    write_outer(*mapper, 0x04U);
    select_bank(*mapper, 0U, 0xFFU);
    for (Address addr = 0U; addr < 1024U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == chr_cell((128U * 1024U + 0x7EU * 1024U + addr) % 8192U));
    }
}

TEST_CASE("Mapper 321 implements all CHR registers pairing inversion and outer A17",
          "[mapper321]") {
    auto mapper = board();
    constexpr std::array<Address, 6> addresses{0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (unsigned outer = 0U; outer < 2U; ++outer) {
        for (unsigned reg = 0U; reg < 6U; ++reg) {
            for (unsigned value = 0U; value < 256U; ++value) {
                write_outer(*mapper, static_cast<Byte>(outer << 2U));
                select_bank(*mapper, reg, static_cast<Byte>(value));
                const unsigned bank = reg < 2U ? (value & 0x7EU) : (value & 0x7FU);
                const std::size_t base = outer * 128U * 1024U + bank * 1024U;
                for (Address offset = 0U; offset < 1024U; offset += 257U) {
                    CAPTURE(outer, reg, value, offset);
                    REQUIRE(mapper->read_chr(addresses[reg] + offset) == chr_cell(base + offset));
                }
            }
        }
    }

    write_outer(*mapper, 0x04U);
    for (unsigned reg = 0U; reg < 6U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(4U + reg * 4U + 1U));
    }
    constexpr std::array<unsigned, 8> normal{4U, 5U, 8U, 9U, 13U, 17U, 21U, 25U};
    constexpr std::array<unsigned, 8> inverted{13U, 17U, 21U, 25U, 4U, 5U, 8U, 9U};
    for (const bool invert : {false, true}) {
        mapper->write_prg(0x8000U, invert ? 0x80U : 0U);
        for (Address addr = 0U; addr < 8192U; ++addr) {
            const auto bank = (invert ? inverted : normal)[addr >> 10U];
            const std::size_t offset = 128U * 1024U + bank * 1024U + (addr & 0x3FFU);
            REQUIRE(mapper->read_chr(addr) == chr_cell(offset));
        }
    }
}

TEST_CASE("Mapper 321 implements MMC3 mirroring and scanline IRQ controls", "[mapper321]") {
    auto mapper = board();
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0xFEU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(static_cast<Address>(addr), 0xFFU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }

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

    mapper->write_prg(0xC001U, 0xFFU);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 321 leaves unrelated windows floating and owns no persistent hardware",
          "[mapper321]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 321 reset restores deterministic MMC3 defaults", "[mapper321][reset]") {
    auto mapper = board(false);
    write_outer(*mapper, 0x3CU);
    select_bank(*mapper, 6U, 9U, true);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    for (const bool cold : {false, true}) {
        cold ? mapper->power_on() : mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_chr(0U) == chr_cell(0U));
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper) == state(*board(false)));
    }
}

TEST_CASE("Mapper 321 snapshots replay all latches without serializing ROM", "[mapper321][state]") {
    auto mapper = board();
    write_outer(*mapper, 0x34U);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 9U + 3U), true);
    }
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    REQUIRE(mapper->irq_pending());

    auto replay = board(true, kPrgSize, kChrSize, 0x80U);
    StateReader restore(saved);
    replay->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*replay) == saved);
    REQUIRE(replay->read_prg(0x8000U) == prg_cell(128U * 1024U + 14U * 8192U, 0x80U));
    REQUIRE(replay->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(replay->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    replay->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*replay) == saved);
}

TEST_CASE("Mapper 321 rejects every truncated or invalid state without mutation",
          "[mapper321][state]") {
    auto mapper = board();
    write_outer(*mapper, 0x0CU);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 3U + 1U), true);
    }
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    std::vector<std::pair<std::size_t, Byte>> invalid{
        {0U, saved[0U] ^ 1U},
        {1U, saved[1U] ^ 1U},
        {2U, saved[2U] ^ 1U},
        {6U, saved[6U] ^ 1U},
        {10U, 0x01U},
        {10U, 0x40U},
        {10U, 0xFFU},
        {11U, 0x08U},
        {20U, 2U},
        {23U, 0x08U},
        {23U, 0x04U},
        {24U, 0x01U},
        {24U, 0x20U},
    };
    for (std::size_t offset = 12U; offset < 18U; ++offset) {
        invalid.emplace_back(offset, 0x80U);
    }
    for (std::size_t offset = 18U; offset < 20U; ++offset) {
        invalid.emplace_back(offset, 0x10U);
    }
    for (const auto& [offset, value] : invalid) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader reader(corrupt);
        mapper->load_state(reader);
        CAPTURE(offset, value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other_mirror = board(false);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);

    auto other_geometry = board(true, 32768U, 8192U);
    const auto geometry_before = state(*other_geometry);
    StateReader wrong_geometry(saved);
    other_geometry->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other_geometry) == geometry_before);
}

} // namespace
