#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper091.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateBytes = 23U;

INesHeader header(Byte submapper = 0U, bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 91U;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 8192U) * 37U + offset + (offset >> 8U));
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 2048U) * 29U + offset + (offset >> 8U));
}

std::pair<std::size_t, std::size_t> default_sizes(Byte submapper) {
    return submapper == 0U ? std::pair{524288U, 1048576U} : std::pair{2097152U, 524288U};
}

std::unique_ptr<Mapper091> board(Byte submapper = 0U,
                                 bool legacy = false,
                                 bool vertical = true,
                                 std::size_t prg_size = 0U,
                                 std::size_t chr_size = 0U) {
    const auto sizes = default_sizes(submapper);
    prg_size = prg_size == 0U ? sizes.first : prg_size;
    chr_size = chr_size == 0U ? sizes.second : chr_size;
    const auto h = header(submapper, legacy, vertical);
    REQUIRE(Mapper091::validate_geometry(h, prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i);
    return std::make_unique<Mapper091>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void require_prg(Mapper& mapper, Address addr, std::size_t offset) {
    INFO("PRG addr=" << addr << " offset=" << offset);
    REQUIRE(mapper.read_prg(addr) == prg_cell(offset));
}

void require_chr(Mapper& mapper, Address addr, std::size_t offset) {
    INFO("CHR addr=" << addr << " offset=" << offset);
    REQUIRE(mapper.read_chr(addr) == chr_cell(offset));
}

TEST_CASE("Mapper 091 validates documented submappers and ROM-only geometry", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        const auto sizes = default_sizes(submapper);
        for (std::size_t prg : {32768U, 65536U, 131072U, 262144U, 524288U})
            REQUIRE(Mapper091::validate_geometry(header(submapper), prg, 8192U));
        for (std::size_t chr : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U, 524288U})
            REQUIRE(Mapper091::validate_geometry(header(submapper), 32768U, chr));
        REQUIRE(Mapper091::validate_geometry(header(submapper), sizes.first, sizes.second));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), 0U, sizes.second));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), 16384U, sizes.second));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), 49152U, sizes.second));
        REQUIRE_FALSE(
            Mapper091::validate_geometry(header(submapper), sizes.first * 2U, sizes.second));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), sizes.first, 0U));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), sizes.first, 4096U));
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), sizes.first, 20480U));
        REQUIRE_FALSE(
            Mapper091::validate_geometry(header(submapper), sizes.first, sizes.second * 2U));
        for (MirrorMode mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto h = header(submapper);
            h.mirror_mode = mirror;
            REQUIRE_FALSE(Mapper091::validate_geometry(h, sizes.first, sizes.second));
        }
        for (unsigned field = 0U; field < 5U; ++field) {
            auto h = header(submapper);
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
            default:
                h.chr_nvram_size = 8192U;
                break;
            }
            REQUIRE_FALSE(Mapper091::validate_geometry(h, sizes.first, sizes.second));
        }
    }
    REQUIRE(Mapper091::validate_geometry(header(1U), 1048576U, 8192U));
    REQUIRE(Mapper091::validate_geometry(header(1U), 2097152U, 8192U));
    REQUIRE(Mapper091::validate_geometry(header(0U), 32768U, 1048576U));
    REQUIRE_FALSE(Mapper091::validate_geometry(header(0U), 1048576U, 8192U));
    REQUIRE_FALSE(Mapper091::validate_geometry(header(1U), 32768U, 1048576U));
    REQUIRE(Mapper091::validate_geometry(header(0U, true), 32768U, 8192U));
    REQUIRE_FALSE(Mapper091::validate_geometry(header(1U, true), 32768U, 8192U));
    for (Byte submapper = 2U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Mapper091::validate_geometry(header(submapper), 32768U, 8192U));
    auto wrong_id = header();
    wrong_id.mapper_number = 90U;
    REQUIRE_FALSE(Mapper091::validate_geometry(wrong_id, 32768U, 8192U));
}

TEST_CASE("Mapper 091 decodes every low register alias for both submappers", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper);
        for (unsigned raw = 0x0000U; raw <= 0x7FFFU; ++raw) {
            m->reset();
            auto expected = state(*m);
            const auto addr = static_cast<Address>(raw);
            const Address mask = submapper == 0U ? 0xF003U : 0xF007U;
            const Address reg = addr >= 0x6000U ? addr & mask : 0U;
            if (reg >= 0x6000U && reg <= 0x6003U)
                expected[12U + (reg & 3U)] = 0xA5U;
            else if (submapper == 1U && (reg == 0x6004U || reg == 0x6005U))
                expected[22] = reg == 0x6004U ? 0U : 1U;
            else if (submapper == 1U && reg == 0x6006U)
                expected[17] = 0xA5U;
            else if (submapper == 1U && reg == 0x6007U)
                expected[18] = 0xA5U;
            else if (reg == 0x7000U || reg == 0x7001U)
                expected[10U + (reg & 1U)] = 0xA5U;
            else if (reg == (0x7007U & mask))
                expected[21] = 1U;
            m->write_prg(addr, 0xA5U);
            REQUIRE(state(*m) == expected);
        }
    }
}

TEST_CASE("Mapper 091 outer register covers only submapper 0 CPU 8000-9FFF", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            m->reset();
            auto expected = state(*m);
            if (submapper == 0U && raw < 0xA000U)
                expected[16] = 5U;
            m->write_prg(static_cast<Address>(raw), 0xFDU);
            REQUIRE(state(*m) == expected);
        }
    }
}

TEST_CASE("Mapper 091 maps inner and outer PRG banks with fixed last 16 KiB", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper);
        const auto prg_size = default_sizes(submapper).first;
        for (Byte outer : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
            m->write_prg(0x8000U, outer);
            const std::size_t outer_base =
                submapper == 0U ? static_cast<std::size_t>((outer >> 1U) & 3U) << 17U : 0U;
            for (unsigned bank = 0U; bank < 256U; ++bank) {
                for (unsigned slot = 0U; slot < 2U; ++slot)
                    m->write_prg(static_cast<Address>(0x7000U + slot), static_cast<Byte>(bank));
                const std::size_t inner = submapper == 0U ? bank & 15U : bank;
                for (Address addr : {0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xBFFFU})
                    require_prg(
                        *m, addr, (outer_base + inner * 8192U + (addr & 0x1FFFU)) % prg_size);
                if (submapper == 0U) {
                    require_prg(*m, 0xC123U, (outer_base + 14U * 8192U + 0x123U) % prg_size);
                    require_prg(*m, 0xFFFFU, (outer_base + 15U * 8192U + 0x1FFFU) % prg_size);
                } else {
                    require_prg(*m, 0xC123U, prg_size - 16384U + 0x123U);
                    require_prg(*m, 0xFFFFU, prg_size - 1U);
                }
            }
        }
    }
}

TEST_CASE("Mapper 091 maps four 2 KiB CHR windows and protects ROM", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper);
        const auto chr_size = default_sizes(submapper).second;
        for (Byte outer : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
            m->write_prg(0x9FFFU, outer);
            const std::size_t outer_base =
                submapper == 0U ? static_cast<std::size_t>(outer & 1U) << 19U : 0U;
            for (unsigned bank = 0U; bank < 256U; ++bank) {
                for (unsigned slot = 0U; slot < 4U; ++slot)
                    m->write_prg(static_cast<Address>(0x6000U + slot), static_cast<Byte>(bank));
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    for (unsigned offset : {0U, 0x123U, 0x7FFU}) {
                        const auto addr = static_cast<Address>(slot * 2048U + offset);
                        const auto mapped = (outer_base + bank * 2048U + offset) % chr_size;
                        require_chr(*m, addr, mapped);
                        m->write_chr(addr, static_cast<Byte>(bank ^ 0xFFU));
                        require_chr(*m, addr, mapped);
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 091 submapper 0 counts exactly 64 unfiltered PA12 rises", "[mapper091]") {
    auto m = board();
    REQUIRE(m->observes_ppu_a12());
    m->write_prg(0x7007U, 0U);
    for (unsigned edge = 0U; edge < 63U; ++edge) {
        m->observe_ppu_a12(false);
        m->observe_ppu_a12(true);
        m->observe_ppu_a12(true);
        REQUIRE_FALSE(m->irq_pending());
    }
    REQUIRE(state(*m)[20] == 63U);
    const auto partial = state(*m);
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE(m->irq_pending());
    REQUIRE(state(*m)[20] == 0U);
    m->acknowledge_irq();
    REQUIRE(m->irq_pending());
    for (unsigned edge = 0U; edge < 64U; ++edge) {
        m->observe_ppu_a12(false);
        m->observe_ppu_a12(true);
    }
    REQUIRE(m->irq_pending());
    m->write_prg(0x7006U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    for (unsigned edge = 0U; edge < 64U; ++edge) {
        m->observe_ppu_a12(false);
        m->observe_ppu_a12(true);
    }
    REQUIRE(state(*m)[20] == 0U);
    m->write_prg(0x7007U, 0U);
    m->observe_ppu_a12(true);
    REQUIRE(state(*m)[20] == 0U);
    for (unsigned cycle = 0U; cycle < 10000U; ++cycle)
        m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());

    auto replay = board();
    StateReader reader(partial);
    replay->load_state(reader);
    REQUIRE(reader.ok());
    replay->observe_ppu_a12(true);
    REQUIRE_FALSE(replay->irq_pending());
    replay->observe_ppu_a12(false);
    replay->observe_ppu_a12(true);
    REQUIRE(replay->irq_pending());
}

TEST_CASE("Mapper 091 submapper 1 decrements by five every fourth M2 cycle", "[mapper091]") {
    auto m = board(1U);
    REQUIRE_FALSE(m->observes_ppu_a12());
    m->write_prg(0x6006U, 10U);
    m->write_prg(0x6007U, 0U);
    m->write_prg(0x7007U, 0U);
    for (unsigned clock = 0U; clock < 3U; ++clock) {
        m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
    }
    m->clock_cpu();
    REQUIRE(state(*m)[17] == 5U);
    REQUIRE_FALSE(m->irq_pending());
    for (unsigned clock = 0U; clock < 4U; ++clock)
        m->clock_cpu();
    REQUIRE(state(*m)[17] == 0U);
    REQUIRE_FALSE(m->irq_pending());
    const auto before_borrow = state(*m);
    for (unsigned clock = 0U; clock < 4U; ++clock)
        m->clock_cpu();
    REQUIRE(m->irq_pending());
    REQUIRE(state(*m)[17] == 0xFBU);
    REQUIRE(state(*m)[18] == 0xFFU);
    m->acknowledge_irq();
    REQUIRE(m->irq_pending());
    m->write_prg(0x6006U, 20U);
    REQUIRE(m->irq_pending());
    m->write_prg(0x7006U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0x6007U, 0U);
    auto stopped = before_borrow;
    stopped[17] = 20U;
    stopped[21] = 0U;
    REQUIRE(state(*m) == stopped);
    for (unsigned clock = 0U; clock < 16U; ++clock)
        m->clock_cpu();
    REQUIRE(state(*m) == stopped);

    m->write_prg(0x7007U, 0U);
    m->clock_cpu();
    m->clock_cpu();
    REQUIRE(state(*m)[19] == 2U);
    m->write_prg(0x7007U, 0U);
    REQUIRE(state(*m)[19] == 0U);
    REQUIRE(state(*m)[17] == 20U);
    for (unsigned clock = 0U; clock < 3U; ++clock)
        m->clock_cpu();
    REQUIRE(state(*m)[17] == 20U);
    m->clock_cpu();
    REQUIRE(state(*m)[17] == 15U);
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 091 mirroring reset and open-bus routing match submapper wiring", "[mapper091]") {
    for (Byte submapper : {0U, 1U})
        for (bool vertical : {false, true}) {
            auto m = board(submapper, false, vertical);
            const auto initial = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
            REQUIRE(m->mirror_mode() == initial);
            REQUIRE(m->battery_ram().empty());
            REQUIRE_FALSE(m->has_expansion_audio());
            for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7000U, 0x7FFFU}) {
                REQUIRE_FALSE(m->maps_prg(addr));
                REQUIRE(m->prg_read_mask(addr) == 0xFFU);
                REQUIRE_FALSE(m->maps_expansion(addr));
            }
            REQUIRE(m->maps_prg(0x8000U));
            REQUIRE(m->maps_prg(0xFFFFU));
            m->write_prg(0x6004U, 0U);
            if (submapper == 1U) {
                REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
                m->write_prg(0x6005U, 0U);
                REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            } else {
                REQUIRE(m->mirror_mode() == initial);
            }
            m->write_prg(0x7000U, 0xA5U);
            m->write_prg(0x7007U, 0U);
            m->observe_ppu_a12(true);
            m->reset();
            REQUIRE(m->mirror_mode() == initial);
            REQUIRE(state(*m) == state(*board(submapper, false, vertical)));
            m->write_prg(0x7000U, 0x5AU);
            m->power_on();
            REQUIRE(state(*m) == state(*board(submapper, false, vertical)));
        }
}

TEST_CASE("Mapper 091 snapshots replay all state and reject damage atomically", "[mapper091]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper);
        m->write_prg(0x7000U, 0xA7U);
        m->write_prg(0x7001U, 0x5AU);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            m->write_prg(static_cast<Address>(0x6000U + slot), static_cast<Byte>(0x40U + slot));
        m->write_prg(0x8000U, 5U);
        if (submapper == 1U) {
            m->write_prg(0x6004U, 0U);
            m->write_prg(0x6006U, 0x34U);
            m->write_prg(0x6007U, 0x12U);
        }
        m->write_prg(0x7007U, 0U);
        if (submapper == 0U) {
            for (unsigned edge = 0U; edge < 17U; ++edge) {
                m->observe_ppu_a12(false);
                m->observe_ppu_a12(true);
            }
        } else {
            m->clock_cpu();
            m->clock_cpu();
        }
        const auto saved = state(*m);
        REQUIRE(saved.size() == kStateBytes);

        for (std::size_t length = 0U; length < saved.size(); ++length) {
            StateReader reader(std::span(saved).first(length));
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned bit = 0U; bit < 16U; ++bit) {
            auto bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned offset = 2U; offset <= 9U; ++offset) {
            auto bad = saved;
            bad[offset] ^= 0xFFU;
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        std::vector<std::pair<unsigned, Byte>> invalid = {
            {16U, 0xFFU}, {19U, 0xFFU}, {20U, 0xFFU}, {21U, 0x80U}, {22U, 0xFFU}};
        if (submapper == 0U)
            invalid.emplace_back(17U, 1U);
        else
            invalid.emplace_back(21U, static_cast<Byte>(saved[21] | 4U));
        for (const auto [offset, value] : invalid) {
            auto bad = saved;
            bad[offset] = value;
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }

        m->reset();
        StateReader reader(saved);
        m->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*m) == saved);
        auto replay = board(submapper);
        StateReader replay_reader(saved);
        replay->load_state(replay_reader);
        REQUIRE(replay_reader.ok());
        for (unsigned clock = 0U; clock < 8U; ++clock) {
            m->clock_cpu();
            replay->clock_cpu();
            m->observe_ppu_a12(false);
            replay->observe_ppu_a12(false);
            m->observe_ppu_a12(true);
            replay->observe_ppu_a12(true);
        }
        REQUIRE(state(*replay) == state(*m));

        auto other = board(submapper == 0U ? 1U : 0U);
        const auto before = state(*other);
        StateReader wrong_profile(saved);
        other->load_state(wrong_profile);
        REQUIRE_FALSE(wrong_profile.ok());
        REQUIRE(state(*other) == before);
        auto other_mirror = board(submapper, false, false);
        const auto mirror_before = state(*other_mirror);
        StateReader wrong_mirror(saved);
        other_mirror->load_state(wrong_mirror);
        REQUIRE_FALSE(wrong_mirror.ok());
        REQUIRE(state(*other_mirror) == mirror_before);
        auto other_size = board(submapper, false, true, 32768U, 8192U);
        const auto size_before = state(*other_size);
        StateReader wrong_size(saved);
        other_size->load_state(wrong_size);
        REQUIRE_FALSE(wrong_size.ok());
        REQUIRE(state(*other_size) == size_before);
    }
}

} // namespace
