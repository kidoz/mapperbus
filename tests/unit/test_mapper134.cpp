#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/t4a54a.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header() {
    INesHeader h{};
    h.mapper_number = 134U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}

std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; i += unit) {
        data[i] = static_cast<Byte>(i / unit);
        data[i + 1U] = static_cast<Byte>((i / unit) >> 8U);
    }
    return data;
}

std::unique_ptr<T4a54a> board(std::size_t prg = 0x100000U,
                              std::size_t chr = 0x100000U,
                              INesHeader h = header()) {
    REQUIRE(T4a54a::validate_geometry(h, prg, chr));
    return std::make_unique<T4a54a>(h, banks(prg, 0x2000U), banks(chr, 0x400U));
}

void select_bank(Mapper& m, Byte index, Byte value) {
    m.write_prg(0x8000U, index);
    m.write_prg(0x8001U, value);
}

unsigned prg_page(Mapper& m, Address addr) {
    return m.read_prg(addr) | (static_cast<unsigned>(m.read_prg(addr + 1U)) << 8U);
}

unsigned chr_page(Mapper& m, Address addr) {
    return m.read_chr(addr) | (static_cast<unsigned>(m.read_chr(addr + 1U)) << 8U);
}

std::vector<Byte> snapshot(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 134 implements MMC3 PRG banking and all register aliases", "[mapper134]") {
    auto m = board();
    REQUIRE(prg_page(*m, 0x8000U) == 0U);
    REQUIRE(prg_page(*m, 0xA000U) == 0U);
    REQUIRE(prg_page(*m, 0xC000U) == 30U);
    REQUIRE(prg_page(*m, 0xE000U) == 31U);

    m->write_prg(0x9FFEU, 6U);
    m->write_prg(0x9FFFU, 0x15U);
    m->write_prg(0x8002U, 7U);
    m->write_prg(0x8003U, 0x2AU);
    REQUIRE(prg_page(*m, 0x8000U) == 0x15U);
    REQUIRE(prg_page(*m, 0xA000U) == 0x0AU);

    m->write_prg(0x9FFEU, 0x46U);
    m->write_prg(0x9FFFU, 0x03U);
    REQUIRE(prg_page(*m, 0x8000U) == 30U);
    REQUIRE(prg_page(*m, 0xA000U) == 0x0AU);
    REQUIRE(prg_page(*m, 0xC000U) == 3U);
    REQUIRE(prg_page(*m, 0xE000U) == 31U);

    m->write_prg(0xA000U, 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0xBFFEU, 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->write_prg(0xA001U, 0U);
    REQUIRE(prg_page(*m, 0xE000U) == 31U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    REQUIRE_FALSE(m->maps_prg(0x7FFFU));
    REQUIRE(m->battery_ram().empty());
}

TEST_CASE("Mapper 134 outer PRG pins precede physical ROM wrapping", "[mapper134]") {
    auto m = board();
    select_bank(*m, 6U, 5U);
    select_bank(*m, 7U, 7U);
    m->write_prg(0x6000U, 0x10U);
    m->write_prg(0x6001U, 7U);
    REQUIRE(prg_page(*m, 0x8000U) == 117U);
    REQUIRE(prg_page(*m, 0xA000U) == 119U);
    REQUIRE(prg_page(*m, 0xC000U) == 126U);
    REQUIRE(prg_page(*m, 0xE000U) == 127U);

    m->write_prg(0x6001U, 2U);
    select_bank(*m, 6U, 0U);
    REQUIRE(prg_page(*m, 0x8000U) == 96U);
    select_bank(*m, 6U, 0x10U);
    REQUIRE(prg_page(*m, 0x8000U) == 112U);
    m->write_prg(0x6000U, 0U);
    REQUIRE(prg_page(*m, 0x8000U) == 48U);

    auto small = board(0x8000U, 0x2000U);
    small->write_prg(0x6001U, 7U);
    REQUIRE(prg_page(*small, 0x8000U) == 0U);
    REQUIRE(prg_page(*small, 0xE000U) == 3U);
}

TEST_CASE("Mapper 134 NROM modes rewire R6 across the full CPU window", "[mapper134]") {
    auto m = board();
    select_bank(*m, 6U, 5U);
    select_bank(*m, 7U, 0x2AU);
    m->write_prg(0x8000U, 0x40U);
    m->write_prg(0x6001U, 0x80U);
    REQUIRE(prg_page(*m, 0x8000U) == 4U);
    REQUIRE(prg_page(*m, 0xA000U) == 5U);
    REQUIRE(prg_page(*m, 0xC000U) == 6U);
    REQUIRE(prg_page(*m, 0xE000U) == 7U);

    m->write_prg(0x6001U, 0x88U);
    REQUIRE(prg_page(*m, 0x8000U) == 4U);
    REQUIRE(prg_page(*m, 0xA000U) == 5U);
    REQUIRE(prg_page(*m, 0xC000U) == 4U);
    REQUIRE(prg_page(*m, 0xE000U) == 5U);

    select_bank(*m, 6U, 6U);
    REQUIRE(prg_page(*m, 0x8000U) == 6U);
    REQUIRE(prg_page(*m, 0xA000U) == 7U);
    REQUIRE(prg_page(*m, 0xC000U) == 6U);
    REQUIRE(prg_page(*m, 0xE000U) == 7U);

    m->write_prg(0x6000U, 0x10U);
    m->write_prg(0x6001U, 0x87U);
    REQUIRE(prg_page(*m, 0x8000U) == 116U);
    REQUIRE(prg_page(*m, 0xA000U) == 117U);
    REQUIRE(prg_page(*m, 0xC000U) == 118U);
    REQUIRE(prg_page(*m, 0xE000U) == 119U);
}

TEST_CASE("Mapper 134 MMC3 CHR pairing, inversion and outer pins are independent", "[mapper134]") {
    auto m = board();
    constexpr std::array<Byte, 6> values = {2U, 6U, 1U, 3U, 5U, 7U};
    for (Byte i = 0U; i < values.size(); ++i)
        select_bank(*m, i, values[i]);
    constexpr std::array<unsigned, 8> normal = {2U, 3U, 6U, 7U, 1U, 3U, 5U, 7U};
    constexpr std::array<unsigned, 8> inverted = {1U, 3U, 5U, 7U, 2U, 3U, 6U, 7U};
    for (unsigned i = 0U; i < normal.size(); ++i)
        REQUIRE(chr_page(*m, static_cast<Address>(i * 0x400U)) == normal[i]);
    m->write_prg(0x8000U, 0x80U);
    for (unsigned i = 0U; i < inverted.size(); ++i)
        REQUIRE(chr_page(*m, static_cast<Address>(i * 0x400U)) == inverted[i]);

    m->write_prg(0x6001U, 0x50U);
    REQUIRE(chr_page(*m, 0U) == 129U);
    m->write_prg(0x6001U, 0x70U);
    REQUIRE(chr_page(*m, 0U) == 385U);
    m->write_prg(0x6000U, 0x40U);
    REQUIRE(chr_page(*m, 0U) == 897U);
    m->write_prg(0x6001U, 0x20U);
    REQUIRE(chr_page(*m, 0U) == 769U);
    m->write_chr(0U, 0xFFU);
    REQUIRE(chr_page(*m, 0U) == 769U);
}

TEST_CASE("Mapper 134 CNROM selection and lock preserve documented writable bits", "[mapper134]") {
    auto m = board();
    m->write_prg(0x7FFCU, 8U);
    m->write_prg(0x7FFEU, 3U);
    REQUIRE(chr_page(*m, 0U) == 24U);
    REQUIRE(chr_page(*m, 0x1C00U) == 31U);
    m->write_prg(0x6006U, 0x1FU);
    REQUIRE(chr_page(*m, 0U) == 248U);
    m->write_prg(0x6000U, 0x48U);
    REQUIRE(chr_page(*m, 0U) == 632U);

    m->write_prg(0x6001U, 0x70U);
    m->write_prg(0x6002U, 3U);
    REQUIRE(chr_page(*m, 0U) == 920U);
    m->write_prg(0x6000U, 0xC8U);
    m->write_prg(0x6001U, 0U);
    REQUIRE(chr_page(*m, 0U) == 920U);
    m->write_prg(0x6000U, 8U);
    REQUIRE(chr_page(*m, 0U) == 920U);
    m->write_prg(0x6002U, 0xFCU);
    REQUIRE(chr_page(*m, 0U) == 896U);
    m->write_prg(0x6002U, 2U);
    REQUIRE(chr_page(*m, 0U) == 912U);
    m->write_prg(0x6003U, 0xFFU);
    m->write_prg(0x7FFFU, 0xFFU);
    REQUIRE(chr_page(*m, 0U) == 912U);

    m->reset();
    m->write_prg(0x6001U, 0U);
    m->write_prg(0x6002U, 5U);
    m->write_prg(0x6000U, 8U);
    REQUIRE(chr_page(*m, 0U) == 40U);
}

TEST_CASE("Mapper 134 solder-pad reads, reset and open bus follow board controls", "[mapper134]") {
    auto m = board();
    REQUIRE(m->configure_board_jumpers(1U));
    REQUIRE_FALSE(m->configure_board_jumpers(2U));
    m->write_prg(0x6000U, 0xA0U);
    REQUIRE(m->read_prg(0x8000U) == 1U);
    REQUIRE(m->read_prg(0xFFFFU) == 1U);
    REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(m->prg_read_mask(0x6000U) == 0xFFU);
    REQUIRE_FALSE(m->maps_expansion(0x6000U));

    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0x6001U, 0U);
    m->write_prg(0x6000U, 0x20U);
    REQUIRE(m->read_prg(0x8000U) == 1U);

    auto horizontal = board(0x8000U, 0x2000U, [] {
        auto h = header();
        h.mirror_mode = MirrorMode::Horizontal;
        return h;
    }());
    REQUIRE(horizontal->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 134 MMC3 IRQ reload, acknowledgement and aliases are deterministic",
          "[mapper134]") {
    auto m = board();
    m->write_prg(0xDFFEU, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xFFFFU, 0U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());

    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
}

TEST_CASE("Mapper 134 rejects unsupported geometry and board memory", "[mapper134]") {
    REQUIRE(T4a54a::validate_geometry(header(), 0x8000U, 0x2000U));
    REQUIRE(T4a54a::validate_geometry(header(), 0x100000U, 0x100000U));
    for (const auto prg : {0U, 0x4000U, 0x8001U, 0x100001U, 0x200000U})
        REQUIRE_FALSE(T4a54a::validate_geometry(header(), prg, 0x2000U));
    for (const auto chr : {0U, 0x1000U, 0x2001U, 0x100001U, 0x200000U})
        REQUIRE_FALSE(T4a54a::validate_geometry(header(), 0x8000U, chr));

    auto h = header();
    h.submapper = 1U;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.has_battery = true;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.prg_ram_size = 0x2000U;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.prg_nvram_size = 0x2000U;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.chr_ram_size = 0x2000U;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.chr_nvram_size = 0x2000U;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
    h = header();
    h.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));

    h = header();
    h.is_nes2 = false;
    h.prg_ram_size = 0x2000U;
    REQUIRE(T4a54a::validate_geometry(h, 0x8000U, 0x2000U));
}

TEST_CASE("Mapper 134 state replay is bounded, exact and atomic on rejection", "[mapper134]") {
    auto m = board();
    REQUIRE(m->configure_board_jumpers(1U));
    m->write_prg(0x6000U, 0x50U);
    m->write_prg(0x6001U, 0xA7U);
    m->write_prg(0x6002U, 0x15U);
    select_bank(*m, 0U, 0x12U);
    select_bank(*m, 6U, 0x15U);
    select_bank(*m, 7U, 0x2AU);
    m->write_prg(0x8000U, 0xC5U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 3U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto state = snapshot(*m);
    REQUIRE(state.size() == 18U);

    auto copy = board();
    REQUIRE(copy->configure_board_jumpers(1U));
    StateReader reader(state);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(snapshot(*copy) == state);
    REQUIRE(prg_page(*copy, 0x8000U) == prg_page(*m, 0x8000U));
    REQUIRE(chr_page(*copy, 0U) == chr_page(*m, 0U));
    for (unsigned i = 0U; i < 4U; ++i) {
        m->clock_irq_counter();
        copy->clock_irq_counter();
        REQUIRE(copy->irq_pending() == m->irq_pending());
    }
    REQUIRE(snapshot(*copy) == snapshot(*m));

    std::vector<std::vector<Byte>> invalid;
    auto wrong_tag = state;
    wrong_tag[0] ^= 1U;
    invalid.push_back(wrong_tag);
    auto bad_mode = state;
    bad_mode[2] |= 1U;
    invalid.push_back(bad_mode);
    auto bad_select = state;
    bad_select[5] |= 8U;
    invalid.push_back(bad_select);
    auto bad_prg = state;
    bad_prg[12] |= 0x40U;
    invalid.push_back(bad_prg);
    auto bad_mirror = state;
    bad_mirror[14] = 2U;
    invalid.push_back(bad_mirror);
    auto bad_irq = state;
    bad_irq[17] = 0x80U;
    invalid.push_back(bad_irq);
    invalid.emplace_back(state.begin(), state.end() - 1);
    invalid.push_back(state);
    invalid.back().push_back(0U);

    for (const auto& bad : invalid) {
        const auto before = snapshot(*copy);
        StateReader rejected(bad);
        copy->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(snapshot(*copy) == before);
    }

    auto other = board();
    StateReader wrong_jumper(state);
    other->load_state(wrong_jumper);
    REQUIRE_FALSE(wrong_jumper.ok());
    other->power_on();
    REQUIRE(snapshot(*other) != state);
}

} // namespace
