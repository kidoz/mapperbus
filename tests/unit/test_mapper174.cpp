#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper174.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 174U;
    h.is_nes2 = !legacy;
    h.mirror_mode = mirror;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

std::vector<Byte> rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Ntdec174> board(bool legacy = false) {
    return std::make_unique<Ntdec174>(
        header(legacy), rom(128U * 1024U, 16U * 1024U), rom(64U * 1024U, 8U * 1024U));
}

std::vector<Byte> state(const Ntdec174& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_low_prg(Byte latch) {
    return (latch & 0x80U) != 0U ? static_cast<Byte>((latch >> 4U) & 6U)
                                 : static_cast<Byte>((latch >> 4U) & 7U);
}

TEST_CASE("Mapper 174 decodes every data bit into PRG CHR and CIRAM controls", "[mapper174]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(legacy);
        for (unsigned value = 0U; value < 256U; ++value) {
            const auto latch = static_cast<Byte>(value);
            mapper->power_on();
            mapper->write_prg(0xFFFFU, latch);
            const Byte low = expected_low_prg(latch);
            const Byte high = (latch & 0x80U) != 0U ? static_cast<Byte>(low + 1U) : low;
            REQUIRE(mapper->read_prg(0x8000U) == low);
            REQUIRE(mapper->read_prg(0xBFFFU) == low);
            REQUIRE(mapper->read_prg(0xC000U) == high);
            REQUIRE(mapper->read_prg(0xFFFFU) == high);
            REQUIRE(mapper->read_chr(0U) == ((latch >> 1U) & 7U));
            REQUIRE(mapper->read_chr(0x1FFFU) == ((latch >> 1U) & 7U));
            REQUIRE(mapper->mirror_mode() ==
                    ((latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 174 accepts the full CPU register range and ignores other writes",
          "[mapper174]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto saved = state(*mapper);
        REQUIRE(saved[2] == (addr >= 0x8000U ? value : 0U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
        mapper->write_expansion(static_cast<Address>(addr), value ^ 0xFFU);
        REQUIRE(state(*mapper) == saved);
    }
}

TEST_CASE("Mapper 174 maps every ROM cell and protects CHR ROM", "[mapper174]") {
    auto mapper = board();
    const auto prg = rom(128U * 1024U, 16U * 1024U);
    const auto chr = rom(64U * 1024U, 8U * 1024U);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->write_prg(0x8000U, static_cast<Byte>(latch));
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const std::size_t offset = (latch & 0x80U) != 0U
                                           ? ((latch >> 5U) & 3U) * 0x8000U + (addr & 0x7FFFU)
                                           : ((latch >> 4U) & 7U) * 0x4000U + (addr & 0x3FFFU);
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == prg[offset]);
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            const auto expected = chr[((latch >> 1U) & 7U) * 0x2000U + addr];
            mapper->write_chr(static_cast<Address>(addr), expected ^ 0xFFU);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
        }
    }
}

TEST_CASE("Mapper 174 has no RAM persistence IRQ audio or extra bus hardware", "[mapper174]") {
    auto mapper = board();
    REQUIRE(mapper->read_prg(0x4020U) == 0U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->expansion_read_mask(0x4020U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 174 powers on at zero and soft reset retains the selected game", "[mapper174]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    REQUIRE(initial == std::vector<Byte>{0x00U, 0xAEU, 0x00U});
    mapper->write_prg(0xFF00U, 0xFFU);
    const auto selected = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == selected);
    REQUIRE(mapper->read_prg(0x8000U) == 6U);
    REQUIRE(mapper->read_prg(0xC000U) == 7U);
    REQUIRE(mapper->read_chr(0U) == 7U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 0U);
    REQUIRE(mapper->read_chr(0U) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 174 validates the recorded ROM-only population and metadata", "[mapper174]") {
    for (bool legacy : {false, true})
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical})
            REQUIRE(Ntdec174::validate_geometry(header(legacy, mirror), 131072U, 65536U));

    for (auto size : {0UZ, 16384UZ, 32768UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ})
        REQUIRE_FALSE(Ntdec174::validate_geometry(header(), size, 65536U));
    for (auto size : {0UZ, 8192UZ, 32768UZ, 65535UZ, 65537UZ, 131072UZ})
        REQUIRE_FALSE(Ntdec174::validate_geometry(header(), 131072U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Ntdec174::validate_geometry(h, 131072U, 65536U));
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        REQUIRE_FALSE(Ntdec174::validate_geometry(h, 131072U, 65536U));
        for (auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mirror;
            REQUIRE_FALSE(Ntdec174::validate_geometry(h, 131072U, 65536U));
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 8192U;
            break;
        case 2U:
            h.chr_ram_size = 8192U;
            break;
        case 3U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(Ntdec174::validate_geometry(h, 131072U, 65536U));
    }
}

TEST_CASE("Mapper 174 snapshots replay the latch and reject malformed tags atomically",
          "[mapper174]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->write_prg(0x8000U, static_cast<Byte>(latch));
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == 3U);
        auto copy = board();
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
        REQUIRE(copy->read_prg(0xFFFFU) == mapper->read_prg(0xFFFFU));
        REQUIRE(copy->read_chr(0x1FFFU) == mapper->read_chr(0x1FFFU));
        REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    }

    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
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
    mapper->power_on();
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
