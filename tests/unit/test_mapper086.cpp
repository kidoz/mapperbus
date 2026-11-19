#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/jaleco_jf13.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBytes = 128U * 1024U;
constexpr std::size_t kChrBytes = 64U * 1024U;
constexpr std::size_t kStateBytes = 6U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 86U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset + (offset >> 8U) + (offset / 32768U) * 47U);
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 3U + (offset >> 7U) + (offset / 8192U) * 29U);
}

std::unique_ptr<JalecoJf13> board(const INesHeader& h = header(), bool zero_rom = false) {
    REQUIRE(JalecoJf13::validate_geometry(h, kPrgBytes, kChrBytes));
    std::vector<Byte> prg(kPrgBytes, 0U), chr(kChrBytes, 0U);
    if (!zero_rom) {
        for (std::size_t i = 0U; i < prg.size(); ++i)
            prg[i] = prg_cell(i);
        for (std::size_t i = 0U; i < chr.size(); ++i)
            chr[i] = chr_cell(i);
    }
    return std::make_unique<JalecoJf13>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_cycles(std::vector<Byte>& bytes, uint16_t cycles) {
    std::memcpy(bytes.data() + 4U, &cycles, sizeof(cycles));
}

TEST_CASE("Mapper 086 accepts only the documented JF-13 population", "[mapper086]") {
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            const auto h = header(legacy, vertical);
            auto mapper = board(h);
            REQUIRE(state(*mapper).size() == kStateBytes);
            REQUIRE(mapper->mirror_mode() == h.mirror_mode);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
            REQUIRE_FALSE(mapper->ciram_address(0x2000U));
            REQUIRE_FALSE(mapper->nametable_address(0x2000U));
            for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
                REQUIRE_FALSE(mapper->maps_prg(addr));
                REQUIRE(mapper->read_prg(addr) == 0U);
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                REQUIRE_FALSE(mapper->maps_expansion(addr));
            }
            REQUIRE(mapper->maps_prg(0x8000U));
            REQUIRE(mapper->maps_prg(0xFFFFU));
        }
    }
}

TEST_CASE("Mapper 086 selects every PRG and CHR bank from the wired data bits", "[mapper086]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg((value & 1U) != 0U ? 0xEABCU : 0x6543U, static_cast<Byte>(value));
        const std::size_t prg_bank = (value >> 4U) & 3U;
        const std::size_t chr_bank = ((value >> 4U) & 4U) | (value & 3U);
        for (Address addr : {0x8000U, 0x9A55U, 0xC000U, 0xFFFFU}) {
            REQUIRE(mapper->read_prg(addr) == prg_cell(prg_bank * 32768U + (addr & 0x7FFFU)));
        }
        for (Address addr : {0x0000U, 0x0123U, 0x1000U, 0x1FFFU})
            REQUIRE(mapper->read_chr(addr) == chr_cell(chr_bank * 8192U + addr));
    }
}

TEST_CASE("Mapper 086 decodes all banking and audio aliases and nothing else", "[mapper086]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        mapper->power_on();
        auto expected = state(*mapper);
        switch (addr & 0xF000U) {
        case 0x6000U:
        case 0xE000U:
            expected[2] = 0x73U;
            break;
        case 0x7000U:
        case 0xF000U:
            expected[3] = 0x77U;
            break;
        default:
            break;
        }
        mapper->write_prg(addr, 0xF7U);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 086 register writes have no bus conflicts and CHR ROM is read-only",
          "[mapper086]") {
    auto mapper = board(header(), true);
    mapper->write_prg(0x6000U, 0x73U);
    REQUIRE(state(*mapper)[2] == 0x73U);
    mapper->write_prg(0xEFFFU, 0x00U);
    REQUIRE(state(*mapper)[2] == 0U);

    mapper->write_prg(0x6000U, 0x42U);
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto value = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(~value));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == value);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 086 times the speech reset input without inventing sample playback",
          "[mapper086]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x73U);
    mapper->write_prg(0x7000U, 0x2FU);
    for (unsigned cycle = 1U; cycle <= 34U; ++cycle) {
        mapper->clock_cpu();
        uint16_t expected = 0U;
        std::memcpy(&expected, state(*mapper).data() + 4U, sizeof(expected));
        REQUIRE(expected == cycle);
    }
    for (unsigned cycle = 0U; cycle < 100U; ++cycle)
        mapper->clock_cpu();
    uint16_t cycles = 0U;
    std::memcpy(&cycles, state(*mapper).data() + 4U, sizeof(cycles));
    REQUIRE(cycles == 34U);
    REQUIRE(state(*mapper)[2] == 0x73U);
    REQUIRE(state(*mapper)[3] == 0x2FU);

    mapper->write_prg(0xFFFFU, 0x60U);
    REQUIRE(state(*mapper)[3] == 0x60U);
    std::memcpy(&cycles, state(*mapper).data() + 4U, sizeof(cycles));
    REQUIRE(cycles == 0U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[2] == 0x73U);
    mapper->write_prg(0x7FFFU, 0xFFU);
    REQUIRE(state(*mapper)[3] == 0x7FU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
}

TEST_CASE("Mapper 086 reset and power-on restore deterministic zero latches", "[mapper086]") {
    auto mapper = board();
    for (bool cold : {false, true}) {
        mapper->write_prg(0xE000U, 0x73U);
        mapper->write_prg(0xF000U, 0x3FU);
        mapper->clock_cpu();
        if (cold)
            mapper->power_on();
        else
            mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_chr(0U) == chr_cell(0U));
        const auto current = state(*mapper);
        REQUIRE(current[2] == 0U);
        REQUIRE(current[3] == 0U);
        uint16_t cycles = 1U;
        std::memcpy(&cycles, current.data() + 4U, sizeof(cycles));
        REQUIRE(cycles == 0U);
    }
}

TEST_CASE("Mapper 086 rejects unsupported sizes memory submappers and CIRAM", "[mapper086]") {
    const auto reject = [](const INesHeader& h,
                           std::size_t prg_size = kPrgBytes,
                           std::size_t chr_size = kChrBytes) {
        REQUIRE_FALSE(JalecoJf13::validate_geometry(h, prg_size, chr_size));
    };
    for (std::size_t size : {0U, 16384U, 32768U, 65536U, 131071U, 131073U, 262144U})
        reject(header(), size);
    for (std::size_t size : {0U, 8192U, 32768U, 65535U, 65537U, 131072U})
        reject(header(), kPrgBytes, size);
    for (uint16_t id : {0U, 72U, 85U, 87U, 92U}) {
        auto h = header();
        h.mapper_number = id;
        reject(h);
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        reject(h);
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mode;
            reject(h);
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0:
            h.prg_ram_size = 8192U;
            break;
        case 1:
            h.prg_nvram_size = 8192U;
            break;
        case 2:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        reject(h);
    }
}

TEST_CASE("Mapper 086 state replay and rejection are bounded and atomic", "[mapper086]") {
    auto mapper = board();
    mapper->write_prg(0xE000U, 0x73U);
    mapper->write_prg(0xF000U, 0x0FU);
    for (unsigned cycle = 0U; cycle < 17U; ++cycle)
        mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateBytes);

    mapper->power_on();
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(3U * 32768U));
    REQUIRE(mapper->read_chr(0U) == chr_cell(7U * 8192U));

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        auto candidate = saved;
        candidate[2] = static_cast<Byte>(value);
        StateReader reader(candidate);
        mapper->load_state(reader);
        if ((value & ~0x73U) == 0U) {
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == candidate);
        } else {
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        StateReader reset(saved);
        mapper->load_state(reset);
        REQUIRE(reset.ok());
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        auto candidate = saved;
        candidate[3] = static_cast<Byte>(value);
        if ((value & 0x40U) != 0U)
            set_cycles(candidate, 0U);
        StateReader reader(candidate);
        mapper->load_state(reader);
        if ((value & 0x80U) == 0U) {
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == candidate);
        } else {
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        StateReader reset(saved);
        mapper->load_state(reset);
        REQUIRE(reset.ok());
    }
    for (uint16_t cycles : {uint16_t{35U}, uint16_t{100U}, uint16_t{65535U}}) {
        auto bad = saved;
        set_cycles(bad, cycles);
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto bad_phase = saved;
    bad_phase[3] = 0x40U;
    StateReader phase_reader(bad_phase);
    mapper->load_state(phase_reader);
    REQUIRE_FALSE(phase_reader.ok());
    REQUIRE(state(*mapper) == saved);

    auto other = board(header(false, false));
    const auto before = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
