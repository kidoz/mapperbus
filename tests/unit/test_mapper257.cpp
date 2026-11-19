#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper257.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 2U) {
    INesHeader h{};
    h.mapper_number = 257U;
    h.submapper = submapper;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) * 11U + (offset >> 16U));
}

std::vector<Byte> rom(std::size_t size = 524288U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = cell(i);
    return data;
}

std::unique_ptr<Mapper257> board(INesHeader h = header(),
                                 std::size_t prg_size = 524288U,
                                 std::size_t chr_size = 0U) {
    REQUIRE(Mapper257::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper257>(h, rom(prg_size), std::vector<Byte>(chr_size));
}

std::vector<Byte> state(const Mapper257& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper257& mapper, std::span<const Byte> data) {
    StateReader reader(data);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg_offset(Byte mode, Address addr) {
    if ((mode & 0x10U) != 0U)
        return static_cast<std::size_t>(mode & 7U) * 32768U + (addr & 0x7FFFU);
    if ((mode & 0x50U) == 0x40U && addr < 0xA000U) {
        const auto bank = static_cast<std::size_t>((mode & 0x1FU) | ((mode & 0x20U) >> 1U));
        return 0x40000UZ + bank * 8192U + (addr & 0x1FFFU);
    }
    const auto slot = static_cast<std::size_t>((addr - 0x8000U) >> 10U);
    return 0x40000UZ + slot * 8192U + 0x1C00UZ + (addr & 0x03FFU);
}

TEST_CASE("Mapper 257 decodes every mode and scattered 1 KiB window", "[mapper257]") {
    auto m = board();
    for (unsigned mode = 0U; mode < 256U; ++mode) {
        m->write_expansion(0x5000U, static_cast<Byte>(mode));
        for (unsigned slot = 0U; slot < 32U; ++slot) {
            const auto addr = static_cast<Address>(0x8000U + slot * 1024U + 0x02BCU);
            INFO("mode=" << mode << " slot=" << slot);
            REQUIRE(m->read_prg(addr) == cell(expected_prg_offset(static_cast<Byte>(mode), addr)));
        }
    }
    m->write_expansion(0x5000U, 0U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) == cell(expected_prg_offset(0U, addr)));
}

TEST_CASE("Mapper 257 decodes register aliases and leaves tape input floating", "[mapper257]") {
    auto m = board();
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const auto reg = addr & 0xF700U;
        const bool mode = reg == 0x5000U;
        const bool tape = reg == 0x5100U;
        const bool input = reg == 0x5300U;
        m->reset();
        REQUIRE(m->maps_expansion(static_cast<Address>(addr)) == (mode || tape || input));
        REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) == 0U);
        m->write_expansion(static_cast<Address>(addr), 0x0AU);
        REQUIRE(m->mirror_mode() == (mode ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(m->tape_output() == tape);
        REQUIRE(m->read_expansion(static_cast<Address>(addr)) == 0U);
    }
    REQUIRE_FALSE(m->maps_expansion(0x5400U));
    REQUIRE_FALSE(m->maps_expansion(0x5500U));
    REQUIRE_FALSE(m->maps_expansion(0x5700U));
}

TEST_CASE("Mapper 257 provides volatile unbanked PRG and CHR RAM", "[mapper257]") {
    auto m = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 13U));
        REQUIRE(m->maps_prg(static_cast<Address>(addr)));
    }
    for (unsigned addr = 0U; addr < 0x2000U; ++addr)
        m->write_chr(static_cast<Address>(addr), cell(addr));
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) == static_cast<Byte>(addr * 13U));
    for (unsigned addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == cell(addr));
    REQUIRE(m->battery_ram().empty());
    m->set_battery_ram(std::array<Byte, 2>{1U, 2U});
    REQUIRE(m->battery_ram().empty());

    m->write_expansion(0x5000U, 0xD5U);
    m->write_expansion(0x5100U, 2U);
    m->reset();
    REQUIRE(m->read_prg(0x6000U) == static_cast<Byte>(0x6000U * 13U));
    REQUIRE(m->read_chr(0x0123U) == cell(0x0123U));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(m->tape_output());
    m->power_on();
    REQUIRE(m->read_prg(0x6000U) == 0U);
    REQUIRE(m->read_chr(0x0123U) == 0U);
    for (Address addr : {0x0000U, 0x4020U, 0x5000U, 0x5FFFU})
        REQUIRE_FALSE(m->maps_prg(addr));
}

TEST_CASE("Mapper 257 1 bpp mode preserves A8 and latches A0/A9 only on A13 rises", "[mapper257]") {
    auto m = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        m->write_chr(addr, cell(addr));
    m->write_expansion(0x5000U, 0x80U);
    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2001U);
    m->observe_ppu_address(0x2200U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto mapped = static_cast<Address>((addr & 0x0FF7U) | 0x0008U);
        REQUIRE(m->read_chr(addr) == cell(mapped));
    }

    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2200U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto mapped = static_cast<Address>((addr & 0x0FF7U) | 0x1000U);
        REQUIRE(m->read_chr(addr) == cell(mapped));
    }
    m->write_chr(0x0000U, 0x39U);
    REQUIRE(m->read_chr(0x1000U) == 0x39U);
    REQUIRE(m->read_chr(0x0000U) == 0x39U);
    REQUIRE(m->read_chr(0x0100U) == cell(0x1100U));

    m->write_expansion(0x5000U, 0U);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(m->read_chr(addr) == (addr == 0x1000U ? 0x39U : cell(addr)));
}

TEST_CASE("Mapper 257 snapshots restore registers, tape and RAM atomically", "[mapper257][state]") {
    auto m = board();
    m->write_prg(0x6123U, 0xA7U);
    m->write_chr(0x145EU, 0x6CU);
    m->write_expansion(0x5000U, 0xEAU);
    m->write_expansion(0x5100U, 2U);
    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2201U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 16389U);

    m->power_on();
    load(*m, saved);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->tape_output());
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(m->read_prg(0x6123U) == 0xA7U);
    REQUIRE(m->read_chr(0x0456U) == 0x6CU);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span{saved}.first(size));
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
             {{0U, 0U}, {1U, 0U}, {3U, 2U}, {4U, 8U}, {4U, 0xFFU}}}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    load(*m, saved);
}

TEST_CASE("Mapper 257 rejects undocumented variants and incorrect geometry", "[mapper257]") {
    REQUIRE(Mapper257::validate_geometry(header(0U), 524288U, 0U));
    REQUIRE(Mapper257::validate_geometry(header(2U), 524288U, 0U));
    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto h = header();
        std::size_t prg = 524288U;
        std::size_t chr = 0U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 164U;
            break;
        case 2U:
            h.submapper = 1U;
            break;
        case 3U:
            h.submapper = 3U;
            break;
        case 4U:
            prg = 262144U;
            break;
        case 5U:
            prg = 1048576U;
            break;
        case 6U:
            chr = 8192U;
            break;
        case 7U:
            h.has_battery = true;
            break;
        case 8U:
            h.prg_ram_size = 0U;
            break;
        case 9U:
            h.prg_ram_size = 2048U;
            break;
        case 10U:
            h.prg_nvram_size = 8192U;
            break;
        case 11U:
            h.chr_ram_size = 0U;
            break;
        case 12U:
            h.chr_ram_size = 16384U;
            break;
        case 13U:
            h.chr_nvram_size = 8192U;
            break;
        case 14U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 15U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 16U:
            h = header(0U);
            prg = 262144U;
            break;
        default:
            prg = 0U;
            break;
        }
        INFO("kind=" << kind);
        REQUIRE_FALSE(Mapper257::validate_geometry(h, prg, chr));
    }
}

TEST_CASE("Mapper 257 has no IRQ, expansion audio or bus conflicts", "[mapper257]") {
    auto m = board();
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE_FALSE(m->has_expansion_audio());
    REQUIRE(m->audio_output() == 0.0F);
    REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
}

} // namespace
