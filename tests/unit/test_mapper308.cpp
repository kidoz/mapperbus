#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper308.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 128U * 1024U;
constexpr std::size_t kChrSize = 128U * 1024U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 308U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper308> board(bool vertical = true) {
    REQUIRE(Mapper308::validate_geometry(header(vertical), kPrgSize, kChrSize));
    std::vector<Byte> prg(kPrgSize), chr(kChrSize);
    for (std::size_t offset = 0U; offset < prg.size(); ++offset) {
        prg[offset] = prg_cell(offset / 8192U, offset % 8192U);
    }
    for (std::size_t offset = 0U; offset < chr.size(); ++offset) {
        chr[offset] = chr_cell(offset / 1024U, offset % 1024U);
    }
    return std::make_unique<Mapper308>(header(vertical), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void tick(Mapper308& mapper, unsigned count = 1U) {
    for (unsigned cycle = 0U; cycle < count; ++cycle) {
        mapper.clock_cpu();
    }
}

void select_chr_bank(Mapper308& mapper, unsigned slot, Byte bank) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 2U);
    mapper.write_prg(reg, static_cast<Byte>(bank & 0x0FU));
    mapper.write_prg(static_cast<Address>(reg + 1U), static_cast<Byte>(bank >> 4U));
}

TEST_CASE("Mapper 308 cold state exposes only documented ROM windows", "[mapper308]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(vertical);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->read_prg(0xA000U) == prg_cell(1U, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(14U, 0U));
        REQUIRE(mapper->read_prg(0xE000U) == prg_cell(15U, 0U));
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(state(*mapper).size() == 25U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());

        const auto saved = state(*mapper);
        for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x6000U, 0x6ABCu, 0x7FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_prg(addr) == 0U);
            mapper->write_prg(addr, 0xA5U);
            mapper->write_expansion(addr, 0x5AU);
        }
        REQUIRE(state(*mapper) == saved);
        for (Address addr : {0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        const std::array<Byte, 4> imported{1U, 2U, 3U, 4U};
        mapper->set_battery_ram(imported);
        REQUIRE(mapper->battery_ram().empty());
        mapper->reset();
        REQUIRE(state(*mapper) == saved);
    }
}

TEST_CASE("Mapper 308 decodes CPU A0 and A1 at every VRC2 register alias", "[mapper308]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), 0xABU);
        const unsigned group = addr >> 12U;
        const unsigned port = addr & 3U;
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(group == 8U ? 0x0BU : 0U, 0x123U));
        REQUIRE(mapper->read_prg(0xA123U) == prg_cell(group == 10U ? 0x0BU : 1U, 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == prg_cell(14U, 0x123U));
        REQUIRE(mapper->read_prg(0xE123U) == prg_cell(15U, 0x123U));
        REQUIRE(mapper->mirror_mode() ==
                (group == 9U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            unsigned bank = 0U;
            if (group >= 11U && group <= 14U && slot == (group - 11U) * 2U + port / 2U) {
                bank = (port & 1U) == 0U ? 0x0BU : 0xB0U;
            }
            CAPTURE(addr, slot, bank);
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 23U)) ==
                    chr_cell(bank % 128U, 23U));
        }
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 308 PRG latches accept all bytes and fixed banks stay physical", "[mapper308]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8003U, static_cast<Byte>(value));
        mapper->write_prg(0xA002U, static_cast<Byte>(value));
        const unsigned bank = (value & 31U) % 16U;
        for (unsigned offset : {0U, 1U, 0x123U, 0x1FFFU}) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(bank, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xA000U + offset)) ==
                    prg_cell(bank, offset));
        }
        REQUIRE(mapper->read_prg(0xC456U) == prg_cell(14U, 0x456U));
        REQUIRE(mapper->read_prg(0xF789U) == prg_cell(15U, 0x1789U));
    }
}

TEST_CASE("Mapper 308 CHR nibble writes preserve the opposite nibble and ROM bytes",
          "[mapper308]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->power_on();
            const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 2U);
            mapper->write_prg(reg, static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) ==
                    chr_cell(value & 15U, 0U));
            mapper->write_prg(static_cast<Address>(reg + 1U), static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) ==
                    chr_cell(((value & 15U) | ((value & 15U) << 4U)) % 128U, 0U));
        }
    }
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            select_chr_bank(*mapper, slot, static_cast<Byte>(bank));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 0x155U)) ==
                    chr_cell(bank % 128U, 0x155U));
        }
    }
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto original = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == original);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 8192U)) == original);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 308 mirroring uses only D0 on all four VRC2 aliases", "[mapper308]") {
    auto mapper = board();
    for (unsigned port = 0U; port < 4U; ++port) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0x9000U + port), static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 308 IRQ register masks cover every F-page alias and data byte",
          "[mapper308][irq]") {
    auto mapper = board();
    for (unsigned addr = 0xF000U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->write_prg(0xF003U, 0x30U);
        mapper->write_prg(0xF001U, 0U);
        tick(*mapper, 17U);
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto saved = state(*mapper);
        const unsigned reg = addr & 0xF003U;
        REQUIRE(saved[21U] == (reg == 0xF000U ? 0U : 17U));
        REQUIRE(saved[22U] == 0U);
        REQUIRE(saved[23U] == (reg == 0xF003U ? 0x0AU : 0x03U));
        REQUIRE(saved[24U] == (reg == 0xF000U ? 0U : 1U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->power_on();
        mapper->write_prg(0xF003U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[23U] == value >> 4U);

        mapper->write_prg(0xF003U, 0x50U);
        mapper->write_prg(0xF001U, 0U);
        tick(*mapper, 9U);
        const auto running = state(*mapper);
        mapper->write_prg(0xF002U, static_cast<Byte>(value));
        REQUIRE(state(*mapper) == running);
        mapper->acknowledge_irq();
        REQUIRE(state(*mapper) == running);
        mapper->clock_irq_counter();
        mapper->on_ppu_hblank();
        mapper->clock_cpu_falling_edge();
        REQUIRE(state(*mapper) == running);

        mapper->write_prg(0xF000U, static_cast<Byte>(value));
        const auto halted = state(*mapper);
        REQUIRE(halted[21U] == 0U);
        REQUIRE(halted[22U] == 0U);
        REQUIRE(halted[23U] == 5U);
        REQUIRE(halted[24U] == 0U);
        mapper->write_prg(0xF001U, static_cast<Byte>(value));
        const auto restarted = state(*mapper);
        REQUIRE(restarted[21U] == 0U);
        REQUIRE(restarted[23U] == 5U);
        REQUIRE(restarted[24U] == 1U);
    }
}

TEST_CASE("Mapper 308 IRQ level follows both counters and the 2048-cycle boundaries",
          "[mapper308][irq]") {
    auto mapper = board();
    for (unsigned initial = 0U; initial < 16U; ++initial) {
        CAPTURE(initial);
        mapper->power_on();
        mapper->write_prg(0xF003U, static_cast<Byte>(initial << 4U));
        mapper->write_prg(0xF001U, 0U);
        if (initial == 0U) {
            REQUIRE(mapper->irq_pending());
            tick(*mapper, 2047U);
            REQUIRE(mapper->irq_pending());
            tick(*mapper);
            REQUIRE_FALSE(mapper->irq_pending());
            auto saved = state(*mapper);
            REQUIRE(saved[21U] == 0U);
            REQUIRE(saved[22U] == 8U);
            REQUIRE(saved[23U] == 15U);
            tick(*mapper, 63487U);
            REQUIRE_FALSE(mapper->irq_pending());
            tick(*mapper);
            REQUIRE(mapper->irq_pending());
            continue;
        }

        const unsigned until_irq = initial * 4096U;
        REQUIRE_FALSE(mapper->irq_pending());
        tick(*mapper, until_irq - 1U);
        REQUIRE_FALSE(mapper->irq_pending());
        tick(*mapper);
        REQUIRE(mapper->irq_pending());
        const auto saved = state(*mapper);
        REQUIRE(saved[21U] == 0U);
        REQUIRE(saved[22U] == 0U);
        REQUIRE(saved[23U] == 0U);
        tick(*mapper, 2047U);
        REQUIRE(mapper->irq_pending());
        tick(*mapper);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 308 enable and acknowledge preserve high count while F000 clears low count",
          "[mapper308][irq]") {
    auto mapper = board();
    mapper->write_prg(0xF003U, 0x20U);
    mapper->write_prg(0xF001U, 0U);
    tick(*mapper, 123U);
    const auto running = state(*mapper);
    mapper->write_prg(0xF001U, 0xFFU);
    REQUIRE(state(*mapper) == running);

    mapper->write_prg(0xF003U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 0x10U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF000U, 0xFFU);
    const auto halted = state(*mapper);
    REQUIRE(halted[21U] == 0U);
    REQUIRE(halted[23U] == 1U);
    REQUIRE(halted[24U] == 0U);
    tick(*mapper, 10000U);
    REQUIRE(state(*mapper) == halted);
    mapper->write_prg(0xF001U, 0U);
    tick(*mapper, 4095U);
    REQUIRE_FALSE(mapper->irq_pending());
    tick(*mapper);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 308 soft reset is inert and cold power restores deterministic defaults",
          "[mapper308][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0xA000U, 9U);
    select_chr_bank(*mapper, 3U, 0xA7U);
    mapper->write_prg(0x9001U, 0U);
    mapper->write_prg(0xF003U, 0x40U);
    mapper->write_prg(0xF001U, 0U);
    tick(*mapper, 777U);
    const auto running = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == running);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
}

TEST_CASE("Mapper 308 validates the documented population and board metadata", "[mapper308]") {
    for (bool vertical : {false, true}) {
        REQUIRE(Mapper308::validate_geometry(header(vertical), kPrgSize, kChrSize));
        for (std::size_t size :
             {0UZ, 1UZ, 8192UZ, 32768UZ, 65536UZ, 131071UZ, 131072UZ, 131073UZ, 262144UZ}) {
            if (size != kPrgSize) {
                REQUIRE_FALSE(Mapper308::validate_geometry(header(vertical), size, kChrSize));
            }
            if (size != kChrSize) {
                REQUIRE_FALSE(Mapper308::validate_geometry(header(vertical), kPrgSize, size));
            }
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto invalid = header(vertical);
            invalid.mirror_mode = mode;
            REQUIRE_FALSE(Mapper308::validate_geometry(invalid, kPrgSize, kChrSize));
        }
    }

    auto invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper308::validate_geometry(invalid, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper308::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = header();
        if (field == 0U) {
            invalid.has_battery = true;
        } else if (field == 1U) {
            invalid.prg_ram_size = 8192U;
        } else if (field == 2U) {
            invalid.prg_nvram_size = 8192U;
        } else if (field == 3U) {
            invalid.chr_ram_size = 8192U;
        } else {
            invalid.chr_nvram_size = 8192U;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper308::validate_geometry(invalid, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 308 snapshots replay partial IRQ phase and reject atomically",
          "[mapper308][irq][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x13U);
    mapper->write_prg(0xA000U, 0x17U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        select_chr_bank(*mapper, slot, static_cast<Byte>(slot * 29U + 3U));
    }
    mapper->write_prg(0x9001U, 1U);
    mapper->write_prg(0xF003U, 0x50U);
    mapper->write_prg(0xF001U, 0U);
    tick(*mapper, 0x923U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 25U);

    auto copy = board();
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    tick(*mapper, 700U);
    tick(*copy, 700U);
    REQUIRE(state(*copy) == state(*mapper));
    REQUIRE(copy->irq_pending() == mapper->irq_pending());
    StateReader restore(saved);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*copy) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated(std::span(saved).first(length));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned offset = 2U; offset < 10U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<unsigned, Byte>, 5>{
             {{10U, 32U}, {11U, 32U}, {20U, 2U}, {23U, 16U}, {24U, 2U}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    auto invalid_low = saved;
    invalid_low[21U] = 0U;
    invalid_low[22U] = 0x10U;
    StateReader low_reader(invalid_low);
    copy->load_state(low_reader);
    REQUIRE_FALSE(low_reader.ok());
    REQUIRE(state(*copy) == saved);

    auto disabled_phase = saved;
    disabled_phase[24U] = 0U;
    StateReader phase_reader(disabled_phase);
    copy->load_state(phase_reader);
    REQUIRE_FALSE(phase_reader.ok());
    REQUIRE(state(*copy) == saved);

    auto other = board(false);
    const auto before = state(*other);
    StateReader wrong_mirror(saved);
    other->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
