#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/ntdec2746.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 261U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

std::vector<Byte> rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Ntdec2746> board(INesHeader h = header(),
                                 std::size_t prg_size = 0x40000U,
                                 std::size_t chr_size = 0x20000U) {
    auto p = rom(prg_size, 0x4000U);
    auto c = rom(chr_size, 0x2000U);
    REQUIRE(Ntdec2746::validate_geometry(h, p.size(), c.size()));
    return std::make_unique<Ntdec2746>(h, std::move(p), std::move(c));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 261 decodes every PRG mode, CHR bank and CIRAM selection", "[mapper261]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(header(legacy));
        for (unsigned latch = 0U; latch <= 0x3FFU; ++latch) {
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0xA7U);
            if ((latch & 0x20U) != 0U) {
                const auto bank = static_cast<Byte>((latch >> 7U) * 2U);
                REQUIRE(mapper->read_prg(0x8000U) == bank);
                REQUIRE(mapper->read_prg(0xBFFFU) == bank);
                REQUIRE(mapper->read_prg(0xC000U) == bank + 1U);
                REQUIRE(mapper->read_prg(0xFFFFU) == bank + 1U);
            } else {
                const auto bank = static_cast<Byte>((latch >> 6U) & 0x0FU);
                REQUIRE(mapper->read_prg(0x8000U) == bank);
                REQUIRE(mapper->read_prg(0xBFFFU) == bank);
                REQUIRE(mapper->read_prg(0xC000U) == bank);
                REQUIRE(mapper->read_prg(0xFFFFU) == bank);
            }
            REQUIRE(mapper->read_chr(0U) == (latch & 0x0FU));
            REQUIRE(mapper->read_chr(0x1FFFU) == (latch & 0x0FU));
            REQUIRE(mapper->mirror_mode() ==
                    ((latch & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 261 aliases every CPU ROM write and ignores write data", "[mapper261]") {
    auto mapper = board();
    auto expected = board();
    for (unsigned address = 0x8000U; address <= 0xFFFFU; ++address) {
        const auto latch = static_cast<Address>(address & 0x3FFU);
        mapper->write_prg(static_cast<Address>(address), static_cast<Byte>(address));
        expected->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        REQUIRE(state(*mapper) == state(*expected));
        REQUIRE(mapper->read_prg(0x8123U) == expected->read_prg(0x8123U));
        REQUIRE(mapper->read_prg(0xF123U) == expected->read_prg(0xF123U));
        REQUIRE(mapper->read_chr(0x0123U) == expected->read_chr(0x0123U));
        REQUIRE(mapper->mirror_mode() == expected->mirror_mode());
    }
    for (Address address : {0x8000U, 0x8040U, 0x83DFU, 0xFFFFU}) {
        mapper->write_prg(address, 0U);
        const auto before = state(*mapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(address, static_cast<Byte>(value));
            REQUIRE(state(*mapper) == before);
        }
    }
}

TEST_CASE("Mapper 261 applies latch pins before wrapping physical ROM sizes", "[mapper261]") {
    for (bool legacy : {false, true})
        for (std::size_t prg_banks = 1U; prg_banks <= 16U; ++prg_banks)
            for (std::size_t chr_banks = 1U; chr_banks <= 16U; ++chr_banks) {
                auto mapper = board(header(legacy), prg_banks * 0x4000U, chr_banks * 0x2000U);
                for (unsigned latch = 0U; latch <= 0x3FFU; ++latch) {
                    mapper->write_prg(static_cast<Address>(0xFC00U | latch), 0U);
                    const unsigned low_bank =
                        (latch & 0x20U) != 0U ? (latch >> 7U) * 2U : (latch >> 6U) & 0x0FU;
                    const unsigned high_bank = (latch & 0x20U) != 0U ? low_bank + 1U : low_bank;
                    REQUIRE(mapper->read_prg(0x8123U) == low_bank % prg_banks);
                    REQUIRE(mapper->read_prg(0xF123U) == high_bank % prg_banks);
                    REQUIRE(mapper->read_chr(0x0123U) == (latch & 0x0FU) % chr_banks);
                }
            }
}

TEST_CASE("Mapper 261 reset and power-on clear the latch without modifying ROM", "[mapper261]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    REQUIRE(initial.size() == 4U);
    mapper->write_prg(0x83DFU, 0U);
    mapper->write_chr(0x0123U, 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == 15U);
    REQUIRE(mapper->read_prg(0xC000U) == 15U);
    REQUIRE(mapper->read_chr(0x0123U) == 15U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xFFC0U, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0xC000U) == 0U);
}

TEST_CASE("Mapper 261 exposes no RAM, low registers, IRQ or expansion audio", "[mapper261]") {
    auto mapper = board();
    mapper->write_prg(0x81FFU, 0U);
    const auto before = state(*mapper);
    for (unsigned address = 0x4020U; address < 0x8000U; ++address) {
        const auto a = static_cast<Address>(address);
        REQUIRE_FALSE(mapper->maps_prg(a));
        REQUIRE_FALSE(mapper->maps_expansion(a));
        mapper->write_prg(a, 0xFFU);
        mapper->write_expansion(a, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 261 snapshots restore the latch and reject malformed state atomically",
          "[mapper261][state]") {
    auto mapper = board();
    mapper->write_prg(0x83DFU, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 4U);
    auto restored = board(header(true));
    StateReader reader(saved);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0x8000U) == 15U);
    REQUIRE(restored->read_chr(0U) == 15U);
    REQUIRE(restored->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span{saved}.first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset : {0UZ, 1UZ}) {
        auto bad = saved;
        bad[offset] ^= 0xFFU;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned latch = 0x400U; latch <= 0xFFFFU; ++latch) {
        auto bad = saved;
        bad[2] = static_cast<Byte>(latch);
        bad[3] = static_cast<Byte>(latch >> 8U);
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    StateReader valid(saved);
    mapper->load_state(valid);
    REQUIRE(valid.ok());
}

TEST_CASE("Mapper 261 rejects unsupported ROM geometry and cartridge metadata", "[mapper261]") {
    for (unsigned kind = 0U; kind < 20U; ++kind) {
        auto h = header();
        std::size_t p = 0x40000U;
        std::size_t c = 0x20000U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            p = 0U;
            break;
        case 10U:
            p = 0x2000U;
            break;
        case 11U:
            p = 0x42000U;
            break;
        case 12U:
            p = 0x40001U;
            break;
        case 13U:
            c = 0U;
            break;
        case 14U:
            c = 0x1000U;
            break;
        case 15U:
            c = 0x22000U;
            break;
        case 16U:
            c = 0x20001U;
            break;
        case 17U:
            h = header(true);
            h.has_battery = true;
            break;
        case 18U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            p = 0x5000U;
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Ntdec2746::validate_geometry(h, p, c));
    }
    for (bool legacy : {false, true})
        for (MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto h = header(legacy);
            h.mirror_mode = mirror;
            REQUIRE(Ntdec2746::validate_geometry(h, 0x4000U, 0x2000U));
            REQUIRE(Ntdec2746::validate_geometry(h, 0x40000U, 0x20000U));
        }
}

} // namespace
