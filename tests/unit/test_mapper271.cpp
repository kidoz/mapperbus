#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper271.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader h{};
    h.mapper_number = 271U;
    h.is_nes2 = !legacy;
    h.mirror_mode = mirror;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

std::vector<Byte> prg_image(std::size_t size, bool all_ff = false) {
    std::vector<Byte> image(size);
    for (std::size_t i = 0U; i < size; ++i) {
        image[i] = static_cast<Byte>(all_ff || (i & 0x7FFFU) == 0U ? 0xFFU : i >> 14U);
    }
    return image;
}

std::vector<Byte> chr_image(std::size_t size) {
    std::vector<Byte> image(size);
    for (std::size_t i = 0U; i < size; ++i)
        image[i] = static_cast<Byte>(i >> 13U);
    return image;
}

std::unique_ptr<Bmc22026> board(INesHeader h = header(),
                                std::size_t prg_size = 0x20000U,
                                std::size_t chr_size = 0x20000U,
                                bool all_ff = false) {
    REQUIRE(Bmc22026::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Bmc22026>(h, prg_image(prg_size, all_ff), chr_image(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 271 latch selects PRG CHR and vertical-or-horizontal CIRAM", "[mapper271]") {
    auto mapper = board();
    REQUIRE(state(*mapper) == std::vector<Byte>{0x0FU, 0x01U, 0x00U});
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    for (Byte latch = 0U; latch < 64U; ++latch) {
        mapper->write_prg(0x8000U, latch);
        const Byte prg_bank = static_cast<Byte>((latch >> 4U) & 3U);
        REQUIRE(mapper->read_prg(0x8123U) == prg_bank * 2U);
        REQUIRE(mapper->read_prg(0xF123U) == prg_bank * 2U + 1U);
        REQUIRE(mapper->read_chr(0x0123U) == (latch & 0x0FU));
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 0x10U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(state(*mapper)[2] == latch);
    }

    auto all_ff = board(header(), 0x20000U, 0x20000U, true);
    for (Byte latch = 0U; latch < 64U; ++latch) {
        for (Byte reserved : {0x00U, 0x40U, 0x80U, 0xC0U}) {
            all_ff->write_prg(0x8000U, static_cast<Byte>(latch | reserved));
            REQUIRE(state(*all_ff)[2] == latch);
        }
    }
}

TEST_CASE("Mapper 271 aliases every CPU ROM write and ignores lower writes", "[mapper271]") {
    auto mapper = board(header(), 0x20000U, 0x20000U, true);
    auto expected = board(header(), 0x20000U, 0x20000U, true);
    for (unsigned address = 0x8000U; address <= 0xFFFFU; ++address) {
        const auto value = static_cast<Byte>(address ^ (address >> 8U));
        mapper->write_prg(static_cast<Address>(address), value);
        expected->write_prg(0x8000U, value);
        REQUIRE(state(*mapper) == state(*expected));
        REQUIRE(mapper->read_prg(0x8123U) == expected->read_prg(0x8123U));
        REQUIRE(mapper->read_chr(0x0123U) == expected->read_chr(0x0123U));
        REQUIRE(mapper->mirror_mode() == expected->mirror_mode());
    }

    const auto before = state(*mapper);
    for (unsigned address = 0x4020U; address < 0x8000U; ++address) {
        const auto addr = static_cast<Address>(address);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    mapper->write_prg_cpu(0xFFFFU, 0x2DU, false);
    REQUIRE(state(*mapper)[2] == 0x2DU);
}

TEST_CASE("Mapper 271 bus conflicts AND write data with the old PRG mapping", "[mapper271]") {
    auto mapper = board();
    for (Byte initial = 0U; initial < 64U; ++initial) {
        for (unsigned data = 0U; data < 256U; ++data) {
            for (Address address : {0x8123U, 0xF123U}) {
                mapper->write_prg(0x8000U, initial);
                const Byte old_rom = static_cast<Byte>(
                    address < 0xC000U ? (initial >> 4U) * 2U : (initial >> 4U) * 2U + 1U);
                const Byte expected = static_cast<Byte>(data & old_rom & 0x3FU);
                mapper->write_prg(address, static_cast<Byte>(data));
                REQUIRE(state(*mapper)[2] == expected);
                REQUIRE(mapper->read_chr(0x0123U) == (expected & 0x0FU));
                REQUIRE(mapper->mirror_mode() ==
                        ((expected & 0x10U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
            }
        }
    }
}

TEST_CASE("Mapper 271 decodes bank pins before wrapping fitted ROM sizes", "[mapper271]") {
    for (bool legacy : {false, true}) {
        for (std::size_t prg_banks = 1U; prg_banks <= 4U; ++prg_banks) {
            for (std::size_t chr_banks = 1U; chr_banks <= 16U; ++chr_banks) {
                auto mapper = board(header(legacy), prg_banks * 0x8000U, chr_banks * 0x2000U);
                for (Byte latch = 0U; latch < 64U; ++latch) {
                    mapper->write_prg(0x8000U, latch);
                    const std::size_t prg_bank = ((latch >> 4U) & 3U) % prg_banks;
                    REQUIRE(mapper->read_prg(0x8123U) == prg_bank * 2U);
                    REQUIRE(mapper->read_prg(0xF123U) == prg_bank * 2U + 1U);
                    REQUIRE(mapper->read_chr(0x0123U) == (latch & 0x0FU) % chr_banks);
                }
            }
        }
    }
}

TEST_CASE("Mapper 271 maps every CHR cell as read-only ROM", "[mapper271]") {
    auto mapper = board(header(), 0x20000U, 0x20000U, true);
    for (Byte bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(0x8000U, bank);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            REQUIRE(mapper->read_chr(addr) == bank);
            mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xA7U));
            REQUIRE(mapper->read_chr(addr) == bank);
        }
    }
}

TEST_CASE("Mapper 271 reset clears the latch and exposes no other hardware", "[mapper271]") {
    auto mapper = board(header(false, MirrorMode::Vertical));
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0x3FU);
    REQUIRE(state(*mapper) != initial);
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8123U) == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);

    mapper->write_prg(0x8000U, 0x2FU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 271 rejects unsupported ROM geometry and cartridge metadata", "[mapper271]") {
    for (bool legacy : {false, true}) {
        for (MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (std::size_t prg_banks = 1U; prg_banks <= 4U; ++prg_banks) {
                for (std::size_t chr_banks = 1U; chr_banks <= 16U; ++chr_banks) {
                    REQUIRE(Bmc22026::validate_geometry(
                        header(legacy, mirror), prg_banks * 0x8000U, chr_banks * 0x2000U));
                }
            }
        }
    }

    for (std::size_t size :
         {0U, 0x2000U, 0x4000U, 0x7FFFU, 0x8001U, 0xC000U, 0x20001U, 0x28000U, 0x40000U}) {
        REQUIRE_FALSE(Bmc22026::validate_geometry(header(), size, 0x20000U));
    }
    for (std::size_t size :
         {0U, 0x1000U, 0x1FFFU, 0x2001U, 0x3000U, 0x20001U, 0x22000U, 0x40000U}) {
        REQUIRE_FALSE(Bmc22026::validate_geometry(header(), 0x20000U, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Bmc22026::validate_geometry(h, 0x20000U, 0x20000U));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
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
        INFO(field);
        REQUIRE_FALSE(Bmc22026::validate_geometry(h, 0x20000U, 0x20000U));
    }
    auto legacy = header(true);
    legacy.has_battery = true;
    REQUIRE_FALSE(Bmc22026::validate_geometry(legacy, 0x20000U, 0x20000U));
    legacy = header(true);
    legacy.chr_ram_size = 8192U;
    REQUIRE_FALSE(Bmc22026::validate_geometry(legacy, 0x20000U, 0x20000U));
}

TEST_CASE("Mapper 271 snapshots replay and reject malformed state atomically",
          "[mapper271][state]") {
    auto mapper = board(header(), 0x20000U, 0x20000U, true);
    mapper->write_prg(0xFFFFU, 0x2DU);
    const auto saved = state(*mapper);
    REQUIRE(saved == std::vector<Byte>{0x0FU, 0x01U, 0x2DU});

    auto restored = board(header(true, MirrorMode::Vertical));
    StateReader valid(saved);
    restored->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0x8123U) == 4U);
    REQUIRE(restored->read_chr(0x0123U) == 0x0DU);
    REQUIRE(restored->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span{saved}.first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte latch : {0x40U, 0x80U, 0xC0U, 0xFFU}) {
        auto bad = saved;
        bad[2] = latch;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte latch = 0U; latch < 64U; ++latch) {
        auto bytes = saved;
        bytes[2] = latch;
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == bytes);
    }
}

} // namespace
