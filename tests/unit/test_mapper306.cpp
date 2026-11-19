#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper306.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header() {
    INesHeader h{};
    h.mapper_number = 306U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.chr_ram_size = 8192U;
    return h;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) * 11U + (offset >> 16U));
}

std::vector<Byte> rom() {
    std::vector<Byte> data(131072U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = cell(i);
    return data;
}

std::unique_ptr<Mapper306> board() {
    REQUIRE(Mapper306::validate_geometry(header(), 131072U, 0U));
    return std::make_unique<Mapper306>(header(), rom(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper306& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper306& mapper, std::span<const Byte> data) {
    StateReader reader(data);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 306 decodes every address-latched bank and ignores write data", "[mapper306]") {
    auto m = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        const bool selected = (addr & 0xD903U) == 0xD903U;
        Byte bank = 0U;
        if (selected) {
            bank = (addr & 0x0040U) == 0U ? static_cast<Byte>(0x08U | ((addr >> 2U) & 0x03U))
                                          : static_cast<Byte>((addr >> 2U) & 0x0FU);
            if (bank >= 0x0CU)
                bank = 0x0BU;
        }
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
        INFO("write address=" << addr);
        for (Address read : {0x6000U, 0x6123U, 0x7FFFU}) {
            const auto offset = static_cast<std::size_t>(bank) * 8192U + (read & 0x1FFFU);
            REQUIRE(m->read_prg(read) == cell(offset));
        }
        for (Address read : {0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU})
            REQUIRE(m->read_prg(read) == cell(0x18000U + (read & 0x7FFFU)));
    }
}

TEST_CASE("Mapper 306 provides CHR RAM, vertical CIRAM, open bus and no persistence",
          "[mapper306]") {
    auto m = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        m->write_chr(addr, static_cast<Byte>(cell(addr) ^ 0x5AU));
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(m->read_chr(addr) == static_cast<Byte>(cell(addr) ^ 0x5AU));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(m->battery_ram().empty());
    m->set_battery_ram(std::array<Byte, 3>{1U, 2U, 3U});
    REQUIRE(m->battery_ram().empty());

    for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        REQUIRE(m->read_prg(addr) == 0U);
    }
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xD903U, 0xFFFFU}) {
        REQUIRE(m->maps_prg(addr));
        REQUIRE(m->prg_read_mask(addr) == 0xFFU);
    }

    m->write_prg(0xD903U, 0U);
    const auto selected = state(*m);
    m->reset();
    REQUIRE(state(*m) != selected);
    REQUIRE(m->read_chr(0x0123U) == static_cast<Byte>(cell(0x0123U) ^ 0x5AU));
    REQUIRE(m->read_prg(0x6000U) == cell(0U));
    m->power_on();
    REQUIRE(m->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 306 rejects undocumented metadata and non-board geometry", "[mapper306]") {
    REQUIRE(Mapper306::validate_geometry(header(), 131072U, 0U));
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t prg = 131072U;
        std::size_t chr = 0U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 305U;
            break;
        case 2U:
            h.submapper = 1U;
            break;
        case 3U:
            prg = 0U;
            break;
        case 4U:
            prg = 98304U;
            break;
        case 5U:
            prg = 262144U;
            break;
        case 6U:
            chr = 8192U;
            break;
        case 7U:
            h.has_battery = true;
            break;
        case 8U:
            h.prg_ram_size = 8192U;
            break;
        case 9U:
            h.prg_nvram_size = 8192U;
            break;
        case 10U:
            h.chr_ram_size = 0U;
            break;
        case 11U:
            h.chr_ram_size = 4096U;
            break;
        case 12U:
            h.chr_ram_size = 16384U;
            break;
        case 13U:
            h.chr_nvram_size = 8192U;
            break;
        case 14U:
            h.mirror_mode = MirrorMode::Horizontal;
            break;
        case 15U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        INFO("kind=" << kind);
        REQUIRE_FALSE(Mapper306::validate_geometry(h, prg, chr));
    }
}

TEST_CASE("Mapper 306 snapshots are fixed-size, ROM-free and reject atomically",
          "[mapper306][state]") {
    auto m = board();
    m->write_prg(0xD903U, 0xFFU);
    for (Address addr = 0U; addr < 0x2000U; addr += 97U)
        m->write_chr(addr, static_cast<Byte>(addr * 13U));
    const auto saved = state(*m);
    REQUIRE(saved.size() == 8195U);

    Mapper306 different_rom(header(), std::vector<Byte>(131072U, 0xA5U), {});
    REQUIRE(state(different_rom) == state(*board()));

    m->power_on();
    load(*m, saved);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_prg(0x6123U) == cell(8U * 8192U + 0x0123U));
    REQUIRE(m->read_chr(0x0061U) == static_cast<Byte>(0x0061U * 13U));
    REQUIRE(m->read_prg(0xFFFFU) == cell(0x1FFFFU));

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span{saved}.first(size));
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
             {{0U, 0U}, {1U, 0U}, {2U, 0x0CU}, {2U, 0x0FU}, {2U, 0xFFU}}}) {
        auto bad = saved;
        bad[offset] = value;
        bad.back() ^= 0xFFU;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    load(*m, saved);
}

TEST_CASE("Mapper 306 has no cartridge IRQ or expansion audio", "[mapper306]") {
    auto m = board();
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE_FALSE(m->has_expansion_audio());
    REQUIRE(m->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
}

} // namespace
