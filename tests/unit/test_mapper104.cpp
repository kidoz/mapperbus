#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper104_golden_five.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 0x140000U;
constexpr std::size_t kInnerBankSize = 0x4000U;

INesHeader make_header(bool nes2 = true) {
    INesHeader header{};
    header.mapper_number = 104U;
    header.is_nes2 = nes2;
    header.mirror_mode = MirrorMode::Vertical;
    header.prg_ram_size = nes2 ? 0U : 0x2000U;
    header.chr_ram_size = 0x2000U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> rom(kPrgSize);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>((i / kInnerBankSize) ^ (i & 0xFFU));
    return rom;
}

Byte expected_prg(unsigned outer, unsigned inner, Address offset = 0U) {
    const auto page = outer * 16U + inner;
    return static_cast<Byte>(page ^ (offset & 0xFFU));
}

std::unique_ptr<Mapper104GoldenFive> make_board(bool nes2 = true) {
    return std::make_unique<Mapper104GoldenFive>(
        make_header(nes2), make_prg(), std::vector<Byte>{});
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 104 maps every populated outer and inner PRG bank", "[mapper104]") {
    auto mapper = make_board();
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0U, 0U));
    REQUIRE(mapper->read_prg(0xFFFFU) == expected_prg(0U, 15U, 0x3FFFU));

    for (unsigned outer = 0U; outer < 5U; ++outer) {
        mapper->power_on();
        mapper->write_prg(0x8000U, static_cast<Byte>(outer));
        for (unsigned inner = 0U; inner < 16U; ++inner) {
            INFO("outer " << outer << ", inner " << inner);
            mapper->write_prg(0xC000U, static_cast<Byte>(inner));
            for (Address offset : {0x0000U, 0x0001U, 0x0123U, 0x3FFFU}) {
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                        expected_prg(outer, inner, offset));
                REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                        expected_prg(outer, 15U, offset));
            }
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 104 decodes every outer alias and ignores upper data bits", "[mapper104]") {
    auto mapper = make_board();
    for (Address addr = 0x8000U; addr < 0xC000U; ++addr) {
        mapper->power_on();
        mapper->write_prg(addr, 0xF4U);
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(4U, 0U, 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == expected_prg(4U, 15U, 0x123U));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->power_on();
        mapper->write_prg(0x8927U, static_cast<Byte>(value));
        const unsigned outer = value & 7U;
        INFO("value " << value);
        if (outer < 5U) {
            REQUIRE(mapper->read_prg(0x8123U) == expected_prg(outer, 0U, 0x123U));
            REQUIRE(mapper->prg_read_mask(0x8123U) == 0xFFU);
        } else {
            REQUIRE(mapper->read_prg(0x8123U) == 0U);
            REQUIRE(mapper->prg_read_mask(0x8123U) == 0U);
        }
    }
}

TEST_CASE("Mapper 104 decodes every inner alias independently of the outer lock", "[mapper104]") {
    auto mapper = make_board();
    mapper->write_prg(0x8000U, 0x0CU);
    for (Address addr = 0xC000U; addr != 0U; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U);
        mapper->write_prg(addr, value);
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(4U, value & 0x0FU, 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == expected_prg(4U, 15U, 0x123U));
    }
}

TEST_CASE("Mapper 104 outer lock is sticky until cold power", "[mapper104]") {
    auto mapper = make_board();
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        mapper->power_on();
        mapper->write_prg(0x8000U, static_cast<Byte>(outer | 8U));
        const auto locked = save(*mapper);
        for (Address addr = 0x8000U; addr < 0xC000U; ++addr)
            mapper->write_prg(addr, static_cast<Byte>(~addr));
        REQUIRE(save(*mapper) == locked);

        mapper->write_prg(0xFFFFU, 0x06U);
        mapper->reset();
        REQUIRE(save(*mapper) != locked);
        REQUIRE(mapper->read_prg(0x8000U) == (outer < 5U ? expected_prg(outer, 6U) : 0U));
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == (outer < 5U ? expected_prg(outer, 6U) : 0U));

        mapper->power_on();
        mapper->write_prg(0xBFFFU, 0x01U);
        REQUIRE(mapper->read_prg(0x8000U) == expected_prg(1U, 0U));
    }
}

TEST_CASE("Mapper 104 leaves unpopulated outer banks on the full CPU open bus", "[mapper104]") {
    auto mapper = make_board();
    for (unsigned outer = 5U; outer < 8U; ++outer) {
        mapper->power_on();
        mapper->write_prg(0x8000U, static_cast<Byte>(outer));
        for (unsigned inner = 0U; inner < 16U; ++inner) {
            mapper->write_prg(0xC000U, static_cast<Byte>(inner));
            for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->read_prg(addr) == 0U);
                REQUIRE(mapper->prg_read_mask(addr) == 0U);
            }
        }
    }
}

TEST_CASE("Mapper 104 register writes use CPU data without ROM bus conflicts", "[mapper104]") {
    auto mapper = make_board();
    mapper->write_prg(0x8123U, 0x04U);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(4U, 0U));
    mapper->write_prg(0xC123U, 0x01U);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(4U, 1U));
}

TEST_CASE("Mapper 104 owns writable volatile CHR RAM and fixed vertical CIRAM", "[mapper104]") {
    for (bool nes2 : {false, true}) {
        auto mapper = make_board(nes2);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
            const auto value = static_cast<Byte>((addr * 13U) ^ (addr >> 8U));
            mapper->write_chr(addr, value);
            REQUIRE(mapper->read_chr(addr) == value);
        }
        const auto populated = save(*mapper);
        mapper->reset();
        REQUIRE(save(*mapper) == populated);
        mapper->power_on();
        REQUIRE(save(*mapper) == save(*make_board(nes2)));
        for (Address addr = 0U; addr < 0x2000U; ++addr)
            REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 104 exposes no CPU RAM, expansion registers, IRQ or audio", "[mapper104]") {
    auto mapper = make_board();
    mapper->write_prg(0x8000U, 0x02U);
    mapper->write_prg(0xC000U, 0x03U);
    const auto before = save(*mapper);
    for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(save(*mapper) == before);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 104 soft reset retains banks, lock and CHR while cold power clears them",
          "[mapper104]") {
    auto mapper = make_board();
    const auto initial = save(*mapper);
    mapper->write_prg(0x8000U, 0x0BU);
    mapper->write_prg(0xC000U, 0x09U);
    mapper->write_chr(0x1234U, 0xA7U);
    const auto running = save(*mapper);
    mapper->reset();
    REQUIRE(save(*mapper) == running);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(3U, 9U));
    REQUIRE(mapper->read_chr(0x1234U) == 0xA7U);
    mapper->write_prg(0x8000U, 0x01U);
    REQUIRE(save(*mapper) == running);

    mapper->power_on();
    REQUIRE(save(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(0U, 15U));
    REQUIRE(mapper->read_chr(0x1234U) == 0U);
}

TEST_CASE("Mapper 104 state replays all latch and CHR combinations without ROM",
          "[mapper104][state]") {
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        auto mapper = make_board();
        mapper->write_prg(0x8000U, static_cast<Byte>(outer | ((outer & 1U) << 3U)));
        mapper->write_prg(0xC000U, static_cast<Byte>(15U - outer));
        for (Address addr = 0U; addr < 0x2000U; ++addr)
            mapper->write_chr(addr, static_cast<Byte>(addr ^ (addr >> 8U) ^ outer));
        const auto saved = save(*mapper);
        REQUIRE(saved.size() == 0x2004U);

        auto restored = make_board(false);
        StateReader reader(saved);
        restored->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(save(*restored) == saved);
        REQUIRE(restored->prg_read_mask(0x8000U) == mapper->prg_read_mask(0x8000U));
        REQUIRE(restored->read_prg(0x8123U) == mapper->read_prg(0x8123U));
        REQUIRE(restored->read_prg(0xC123U) == mapper->read_prg(0xC123U));
        REQUIRE(restored->read_chr(0x1ABCU) == mapper->read_chr(0x1ABCU));
    }
}

TEST_CASE("Mapper 104 malformed state rejection is atomic", "[mapper104][state]") {
    auto mapper = make_board();
    mapper->write_prg(0x8000U, 0x0CU);
    mapper->write_prg(0xC000U, 0x07U);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        mapper->write_chr(addr, static_cast<Byte>(addr * 11U));
    const auto saved = save(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (std::size_t index : {2UZ, 3UZ})
        for (Byte invalid : {0x10U, 0x20U, 0x80U, 0xFFU}) {
            auto bad = saved;
            bad[index] = invalid;
            StateReader reader(bad);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(save(*mapper) == saved);
        }

    auto changed_chr = saved;
    changed_chr[4U] ^= 0xFFU;
    changed_chr.back() ^= 0xFFU;
    StateReader reader(changed_chr);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(save(*mapper) == changed_chr);
}

TEST_CASE("Mapper 104 rejects unsupported geometry and native memory declarations", "[mapper104]") {
    const auto valid = [](const INesHeader& header, std::size_t prg, std::size_t chr) {
        return Mapper104GoldenFive::validate_geometry(header, prg, chr).has_value();
    };
    REQUIRE(valid(make_header(), kPrgSize, 0U));
    REQUIRE(valid(make_header(false), kPrgSize, 0U));
    auto legacy_without_sizes = make_header(false);
    legacy_without_sizes.prg_ram_size = 0U;
    legacy_without_sizes.chr_ram_size = 0U;
    REQUIRE(valid(legacy_without_sizes, kPrgSize, 0U));

    for (std::size_t size : {0UZ,
                             1UZ,
                             0x4000UZ,
                             0x40000UZ,
                             0x100000UZ,
                             0x13FFFFUZ,
                             0x140001UZ,
                             0x180000UZ,
                             0x200000UZ})
        REQUIRE_FALSE(valid(make_header(), size, 0U));
    for (std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ})
        REQUIRE_FALSE(valid(make_header(), kPrgSize, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        REQUIRE_FALSE(valid(header, kPrgSize, 0U));
    }
    for (bool nes2 : {false, true}) {
        auto header = make_header(nes2);
        header.has_battery = true;
        REQUIRE_FALSE(valid(header, kPrgSize, 0U));
        for (MirrorMode mode : {MirrorMode::Horizontal,
                                MirrorMode::FourScreen,
                                MirrorMode::SingleLower,
                                MirrorMode::SingleUpper}) {
            header = make_header(nes2);
            header.mirror_mode = mode;
            REQUIRE_FALSE(valid(header, kPrgSize, 0U));
        }
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.prg_ram_size = 0x2000U;
            break;
        case 1U:
            header.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            header.chr_ram_size = 0U;
            break;
        case 3U:
            header.chr_ram_size = 0x1000U;
            break;
        case 4U:
            header.chr_nvram_size = 0x2000U;
            break;
        default:
            break;
        }
        INFO("field " << field);
        REQUIRE_FALSE(valid(header, kPrgSize, 0U));
    }
}

} // namespace
