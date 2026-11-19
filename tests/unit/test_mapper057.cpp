#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper057.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 131072U;
constexpr std::size_t kChrSize = 131072U;

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 57U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte prg_pattern(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset % 256U);
}

Byte chr_pattern(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 31U + offset % 256U);
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t i = 0U; i < bytes.size(); ++i)
        bytes[i] =
            prg_pattern(static_cast<unsigned>(i / 16384U), static_cast<unsigned>(i % 16384U));
    return bytes;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> bytes(kChrSize);
    for (std::size_t i = 0U; i < bytes.size(); ++i)
        bytes[i] = chr_pattern(static_cast<unsigned>(i / 8192U), static_cast<unsigned>(i % 8192U));
    return bytes;
}

std::unique_ptr<Mapper> board(const INesHeader& h = header()) {
    auto p = prg_rom();
    auto c = chr_rom();
    REQUIRE(Mapper057::validate_geometry(h, p.size(), c.size()));
    return std::make_unique<Mapper057>(h, std::move(p), std::move(c));
}

std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

unsigned prg_page(Byte control, bool upper) {
    const unsigned bank = (control >> 5U) & 7U;
    return (control & 0x10U) != 0U && upper ? (bank + 1U) & 7U : bank;
}

unsigned chr_page(Byte chr_control, Byte prg_control) {
    const unsigned low = (chr_control & 0x40U) != 0U ? prg_control & 3U : chr_control & 3U;
    return ((chr_control & 0x80U) >> 4U) | (prg_control & 4U) | low;
}

TEST_CASE("Mapper 057 exhaustively decodes both data latches and all banking modes",
          "[mapper057]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        for (unsigned c = 0U; c < 256U; ++c) {
            m->write_prg(0x8000U, static_cast<Byte>(c));
            for (unsigned p = 0U; p < 256U; ++p) {
                m->write_prg(0x8800U, static_cast<Byte>(p));
                INFO("CHR control=" << c << ", PRG control=" << p);
                REQUIRE(m->read_prg(0x8123U) ==
                        prg_pattern(prg_page(static_cast<Byte>(p), false), 0x123U));
                REQUIRE(m->read_prg(0xBFFFU) ==
                        prg_pattern(prg_page(static_cast<Byte>(p), false), 0x3FFFU));
                REQUIRE(m->read_prg(0xC123U) ==
                        prg_pattern(prg_page(static_cast<Byte>(p), true), 0x123U));
                REQUIRE(m->read_prg(0xFFFFU) ==
                        prg_pattern(prg_page(static_cast<Byte>(p), true), 0x3FFFU));
                REQUIRE(m->read_chr(0x0123U) ==
                        chr_pattern(chr_page(static_cast<Byte>(c), static_cast<Byte>(p)), 0x123U));
                REQUIRE(m->read_chr(0x1FFFU) ==
                        chr_pattern(chr_page(static_cast<Byte>(c), static_cast<Byte>(p)), 0x1FFFU));
                REQUIRE(m->mirror_mode() ==
                        ((p & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
        }
    }
}

TEST_CASE("Mapper 057 decodes every address alias and ignores writes below PRG space",
          "[mapper057]") {
    auto m = board();
    Byte chr_control = 0U;
    Byte prg_control = 0U;
    for (unsigned a = 0U; a <= 0xFFFFU; ++a) {
        const auto value = static_cast<Byte>(a * 37U + a / 256U);
        m->write_prg(static_cast<Address>(a), value);
        if (a >= 0x8000U) {
            if ((a & 0x8800U) == 0x8000U)
                chr_control = value & 0xC3U;
            else
                prg_control = value;
        }
        const auto saved = state(*m);
        REQUIRE(saved[2U] == chr_control);
        REQUIRE(saved[3U] == prg_control);
        REQUIRE(m->read_prg(0x9123U) == prg_pattern(prg_page(prg_control, false), 0x1123U));
        REQUIRE(m->read_chr(0x0321U) == chr_pattern(chr_page(chr_control, prg_control), 0x321U));
    }
}

TEST_CASE("Mapper 057 retains the inactive CHR low-bank source across mode switches",
          "[mapper057]") {
    auto m = board();
    m->write_prg(0x8000U, 0x81U);
    m->write_prg(0x8800U, 0x06U);
    REQUIRE(m->read_chr(0U) == chr_pattern(13U, 0U));
    m->write_prg(0x8000U, 0xC1U);
    REQUIRE(m->read_chr(0U) == chr_pattern(14U, 0U));
    m->write_prg(0x8800U, 0x05U);
    REQUIRE(m->read_chr(0U) == chr_pattern(13U, 0U));
    m->write_prg(0x8000U, 0x81U);
    REQUIRE(m->read_chr(0U) == chr_pattern(13U, 0U));
}

TEST_CASE("Mapper 057 reset and power-on select deterministic zeroed latches", "[mapper057]") {
    auto m = board();
    const auto initial = state(*m);
    REQUIRE(initial.size() == 4U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->write_prg(0x8000U, 0xFFU);
    m->write_prg(0x8800U, 0xFFU);
    REQUIRE(state(*m) != initial);
    m->reset();
    REQUIRE(state(*m) == initial);
    REQUIRE(m->read_prg(0x8000U) == prg_pattern(0U, 0U));
    REQUIRE(m->read_prg(0xC000U) == prg_pattern(0U, 0U));
    REQUIRE(m->read_chr(0U) == chr_pattern(0U, 0U));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->write_prg(0xFFFFU, 0xA7U);
    m->power_on();
    REQUIRE(state(*m) == initial);
}

TEST_CASE("Mapper 057 exposes only read-only PRG/CHR ROM without other hardware", "[mapper057]") {
    auto m = board();
    m->write_prg(0x8000U, 0xC3U);
    m->write_prg(0x8800U, 0xE7U);
    const auto saved = state(*m);
    for (unsigned a = 0U; a < 8192U; ++a) {
        const auto addr = static_cast<Address>(a);
        const auto expected = m->read_chr(addr);
        m->write_chr(addr, static_cast<Byte>(a * 11U));
        REQUIRE(m->read_chr(addr) == expected);
    }
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*m) == saved);
    REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
    m->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->audio_output() == 0.0F);
    const auto before_reads = state(*m);
    for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a)
        m->read_prg(static_cast<Address>(a));
    for (unsigned a = 0U; a < 8192U; ++a)
        m->read_chr(static_cast<Address>(a));
    REQUIRE(state(*m) == before_reads);
}

TEST_CASE("Mapper 057 state replay is bounded and malformed loads are atomic",
          "[mapper057][state]") {
    auto m = board();
    m->write_prg(0x8000U, 0xC2U);
    m->write_prg(0x8800U, 0xF7U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 4U);
    auto copy = board(header(true));
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == m->read_prg(0x8000U));
    REQUIRE(copy->read_prg(0xC000U) == m->read_prg(0xC000U));
    REQUIRE(copy->read_chr(0x1FFFU) == m->read_chr(0x1FFFU));
    REQUIRE(copy->mirror_mode() == m->mirror_mode());

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    for (std::size_t offset : {0UZ, 1UZ}) {
        auto bad = saved;
        bad[offset] ^= 0xFFU;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    for (Byte reserved : {0x04U, 0x08U, 0x10U, 0x20U, 0x3CU}) {
        auto bad = saved;
        bad[2U] |= reserved;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
    auto canonical = saved;
    canonical[2U] = 0xFFU;
    StateReader invalid(canonical);
    m->load_state(invalid);
    REQUIRE_FALSE(invalid.ok());
    REQUIRE(state(*m) == saved);
}

TEST_CASE("Mapper 057 rejects unsupported ROM geometry and memory metadata", "[mapper057]") {
    for (bool legacy : {false, true}) {
        REQUIRE(Mapper057::validate_geometry(header(legacy), kPrgSize, kChrSize));
        for (unsigned kind = 0U; kind < 17U; ++kind) {
            auto h = header(legacy);
            auto prg_size = kPrgSize;
            auto chr_size = kChrSize;
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
                prg_size = 0U;
                break;
            case 10U:
                prg_size = 65536U;
                break;
            case 11U:
                prg_size = 262144U;
                break;
            case 12U:
                prg_size = 131071U;
                break;
            case 13U:
                chr_size = 0U;
                break;
            case 14U:
                chr_size = 65536U;
                break;
            case 15U:
                chr_size = 262144U;
                break;
            case 16U:
                chr_size = 131071U;
                break;
            default:
                break;
            }
            INFO("legacy=" << legacy << ", kind=" << kind);
            if (legacy && (kind == 0U || (kind >= 2U && kind <= 5U)))
                REQUIRE(Mapper057::validate_geometry(h, prg_size, chr_size));
            else
                REQUIRE_FALSE(Mapper057::validate_geometry(h, prg_size, chr_size));
        }
    }
}
} // namespace
