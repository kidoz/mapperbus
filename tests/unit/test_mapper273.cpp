#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper273.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 262144U;
constexpr std::size_t kChrSize = 131072U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 273U;
    result.is_nes2 = !legacy;
    result.prg_ram_size = legacy ? 8192U : 0U;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper273> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 bool legacy = false,
                                 bool vertical = true) {
    REQUIRE(Mapper273::validate_geometry(header(legacy, vertical), prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t offset = 0U; offset < prg_size; ++offset) {
        prg[offset] = prg_cell(offset / 8192U, offset % 8192U);
    }
    for (std::size_t offset = 0U; offset < chr_size; ++offset) {
        chr[offset] = chr_cell(offset / 1024U, offset % 1024U);
    }
    return std::make_unique<Mapper273>(header(legacy, vertical), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void tick(Mapper273& mapper, unsigned count = 1U) {
    for (unsigned cycle = 0U; cycle < count; ++cycle) {
        mapper.clock_cpu_falling_edge();
    }
}

void select_chr_bank(Mapper273& mapper, unsigned slot, Byte bank) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 8U);
    mapper.write_prg(reg, static_cast<Byte>(bank & 15U));
    mapper.write_prg(reg + 4U, static_cast<Byte>(bank >> 4U));
}

TEST_CASE("Mapper 273 cold state exposes VRC2 windows and no unrelated hardware", "[mapper273]") {
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = board(kPrgSize, kChrSize, legacy, vertical);
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xA000U) == prg_cell(1U, 0U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
            REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(state(*mapper).size() == 25U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
            REQUIRE(mapper->maps_prg(0x8000U));
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
            for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
                REQUIRE_FALSE(mapper->maps_expansion(addr));
            }
            const auto saved = state(*mapper);
            mapper->reset();
            REQUIRE(state(*mapper) == saved);
        }
    }
}

TEST_CASE("Mapper 273 decodes A2 and A3 at every upper CPU register alias", "[mapper273]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), 0xABU);
        const unsigned group = addr >> 12U;
        const unsigned port = (addr >> 2U) & 3U;
        const bool write = addr >= 0x8000U;
        REQUIRE(mapper->read_prg(0x8123U) ==
                prg_cell(write && group == 8U ? 0xABU % 32U : 0U, 0x123U));
        REQUIRE(mapper->read_prg(0xA123U) ==
                prg_cell(write && group == 10U ? 0xABU % 32U : 1U, 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == prg_cell(30U, 0x123U));
        REQUIRE(mapper->read_prg(0xE123U) == prg_cell(31U, 0x123U));
        REQUIRE(mapper->mirror_mode() ==
                (write && group == 9U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            unsigned bank = 0U;
            if (write && group >= 11U && group <= 14U && slot == (group - 11U) * 2U + port / 2U) {
                bank = (port & 1U) == 0U ? 0x0BU : 0xB0U;
            }
            CAPTURE(addr, slot, bank);
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + 23U)) ==
                    chr_cell(bank % 128U, 23U));
        }
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 273 PRG latches accept all bytes and fixed banks stay physical", "[mapper273]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        const unsigned bank = value & 31U;
        for (unsigned offset = 0U; offset < 8192U; ++offset) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(bank, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xA000U + offset)) ==
                    prg_cell(bank, offset));
        }
        REQUIRE(mapper->read_prg(0xC456U) == prg_cell(30U, 0x456U));
        REQUIRE(mapper->read_prg(0xF789U) == prg_cell(31U, 0x1789U));
    }
    for (std::size_t size : {32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
        auto other = board(size);
        const unsigned last = size / 8192U - 1U;
        other->write_prg(0x8000U, 0xFFU);
        other->write_prg(0xA000U, 0xFFU);
        REQUIRE(other->read_prg(0x8000U) == prg_cell(last, 0U));
        REQUIRE(other->read_prg(0xC000U) == prg_cell(last - 1U, 0U));
        REQUIRE(other->read_prg(0xE000U) == prg_cell(last, 0U));
    }
}

TEST_CASE("Mapper 273 CHR nibble writes preserve the opposite nibble and ROM bytes",
          "[mapper273]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->power_on();
            const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 8U);
            mapper->write_prg(reg, static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) ==
                    chr_cell(value & 15U, 0U));
            mapper->write_prg(reg + 4U, static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) ==
                    chr_cell(((value & 15U) | ((value & 15U) << 4U)) % 128U, 0U));
        }
    }
    for (unsigned bank = 0U; bank < 128U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            select_chr_bank(*mapper, slot, static_cast<Byte>(bank));
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            const auto data = static_cast<Byte>(addr ^ (addr >> 8U));
            mapper->write_chr(static_cast<Address>(addr), data);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(bank, addr & 1023U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 8192U)) ==
                    chr_cell(bank, addr & 1023U));
        }
    }
}

TEST_CASE("Mapper 273 mirroring uses only D0 on all four VRC aliases", "[mapper273]") {
    auto mapper = board();
    for (unsigned port = 0U; port < 4U; ++port) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0x9000U + port * 4U), static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 273 IRQ register masks cover every F-page address and data byte",
          "[mapper273][irq]") {
    auto mapper = board();
    for (unsigned addr = 0xF000U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto saved = state(*mapper);
        const unsigned reg = addr & 0xF00CU;
        REQUIRE(saved[21U] == (reg == 0xF000U ? 0xA5U : 0U));
        REQUIRE(saved[23U] == (reg == 0xF008U ? 1U : 0U));
        REQUIRE(saved[24U] == (reg == 0xF000U ? 1U : 0U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->power_on();
        mapper->write_prg(0xF008U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[23U] == (value & 1U));
        mapper->write_prg(0xF004U, 0U);
        mapper->write_prg(0xF00CU, 0U);
        REQUIRE(state(*mapper)[23U] == (value & 1U));
    }
}

TEST_CASE("Mapper 273 IRQ overflow timing and 128-cycle self acknowledgement match the PCB",
          "[mapper273][irq]") {
    auto mapper = board();
    for (unsigned initial = 0U; initial < 256U; ++initial) {
        mapper->power_on();
        mapper->write_prg(0xF000U, static_cast<Byte>(initial));
        mapper->write_prg(0xF008U, 1U);
        if (initial == 255U) {
            REQUIRE(mapper->irq_pending());
            tick(*mapper, 127U);
            REQUIRE(mapper->irq_pending());
            tick(*mapper);
            REQUIRE_FALSE(mapper->irq_pending());
            continue;
        }
        const unsigned until_irq = (255U - initial) * 256U;
        REQUIRE_FALSE(mapper->irq_pending());
        tick(*mapper, until_irq - 1U);
        REQUIRE_FALSE(mapper->irq_pending());
        tick(*mapper);
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE(mapper->irq_pending());
        tick(*mapper, 127U);
        REQUIRE(mapper->irq_pending());
        tick(*mapper);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 273 counter loads preserve phase and override a simultaneous count",
          "[mapper273][irq]") {
    auto mapper = board();
    mapper->write_prg(0xF008U, 1U);
    tick(*mapper, 127U);
    mapper->write_prg(0xF000U, 0xFEU);
    tick(*mapper);
    auto saved = state(*mapper);
    REQUIRE(saved[21U] == 0xFEU);
    REQUIRE(saved[22U] == 0x80U);
    REQUIRE_FALSE(mapper->irq_pending());
    tick(*mapper, 255U);
    REQUIRE_FALSE(mapper->irq_pending());
    tick(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    tick(*mapper, 127U);
    REQUIRE_FALSE(mapper->irq_pending());
    tick(*mapper);
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xF008U, 0U);
    saved = state(*mapper);
    REQUIRE(saved[21U] == 0xFFU);
    REQUIRE(saved[22U] == 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF000U, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF008U, 1U);
    REQUIRE(mapper->irq_pending());
    tick(*mapper, 1000U);
    const auto before_other_clocks = state(*mapper);
    mapper->clock_cpu();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    REQUIRE(state(*mapper) == before_other_clocks);
}

TEST_CASE("Mapper 273 halting clears only the prescaler and reset has no undocumented effect",
          "[mapper273][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8003U, 7U);
    mapper->write_prg(0xA002U, 9U);
    select_chr_bank(*mapper, 3U, 0xA7U);
    mapper->write_prg(0x9001U, 0U);
    mapper->write_prg(0xF000U, 0xFEU);
    mapper->write_prg(0xF008U, 1U);
    tick(*mapper, 77U);
    const auto running = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == running);
    mapper->write_prg(0xF008U, 0U);
    const auto halted = state(*mapper);
    REQUIRE(halted[21U] == running[21U]);
    REQUIRE(halted[22U] == 0U);
    REQUIRE(halted[23U] == 0U);
    tick(*mapper, 1000U);
    REQUIRE(state(*mapper) == halted);
    mapper->reset();
    REQUIRE(state(*mapper) == halted);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
}

TEST_CASE("Mapper 273 validates ROM extents and board memory metadata", "[mapper273]") {
    for (bool legacy : {false, true}) {
        for (std::size_t size : {32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
            for (std::size_t chr : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
                REQUIRE(Mapper273::validate_geometry(header(legacy), size, chr));
            }
        }
        for (std::size_t size : {0UZ,
                                 1UZ,
                                 8192UZ,
                                 16384UZ,
                                 32767UZ,
                                 32769UZ,
                                 49152UZ,
                                 262145UZ,
                                 524288UZ,
                                 1048576UZ,
                                 2097152UZ}) {
            REQUIRE_FALSE(Mapper273::validate_geometry(header(legacy), size, kChrSize));
        }
        for (std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 262145UZ, 524288UZ}) {
            REQUIRE_FALSE(Mapper273::validate_geometry(header(legacy), kPrgSize, size));
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto invalid = header(legacy);
            invalid.mirror_mode = mode;
            REQUIRE_FALSE(Mapper273::validate_geometry(invalid, kPrgSize, kChrSize));
        }
        for (unsigned field = 0U; field < 4U; ++field) {
            auto invalid = header(legacy);
            if (field == 0U) {
                invalid.has_battery = true;
            } else if (field == 1U) {
                invalid.prg_nvram_size = 8192U;
            } else if (field == 2U) {
                invalid.chr_ram_size = 8192U;
            } else {
                invalid.chr_nvram_size = 8192U;
            }
            REQUIRE_FALSE(Mapper273::validate_geometry(invalid, kPrgSize, kChrSize));
        }
    }
    auto native_ram = header();
    native_ram.prg_ram_size = 8192U;
    REQUIRE_FALSE(Mapper273::validate_geometry(native_ram, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper273::validate_geometry(invalid, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 273 snapshots replay partial IRQ phase and reject atomically",
          "[mapper273][irq][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x93U);
    mapper->write_prg(0xA000U, 0xD7U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        select_chr_bank(*mapper, slot, static_cast<Byte>(slot * 29U + 3U));
    }
    mapper->write_prg(0x9000U, 0U);
    mapper->write_prg(0xF000U, 0x80U);
    mapper->write_prg(0xF008U, 1U);
    tick(*mapper, 173U);
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
    for (unsigned offset : {2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<unsigned, Byte>, 6>{
             {{10U, 32U}, {11U, 32U}, {20U, 2U}, {23U, 2U}, {24U, 2U}, {24U, 0xFFU}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    auto disabled_phase = saved;
    disabled_phase[23U] = 0U;
    StateReader phase_reader(disabled_phase);
    copy->load_state(phase_reader);
    REQUIRE_FALSE(phase_reader.ok());
    REQUIRE(state(*copy) == saved);

    for (bool vertical : {false, true}) {
        for (std::size_t size : {131072UZ, 262144UZ}) {
            if (vertical && size == kPrgSize) {
                continue;
            }
            auto other = board(size, kChrSize, false, vertical);
            const auto before = state(*other);
            StateReader wrong(saved);
            other->load_state(wrong);
            REQUIRE_FALSE(wrong.ok());
            REQUIRE(state(*other) == before);
        }
    }

    mapper->write_prg(0xF000U, 0x12U);
    const auto pending_load = state(*mapper);
    REQUIRE(pending_load[24U] == 1U);
    StateReader load_reader(pending_load);
    copy->load_state(load_reader);
    REQUIRE(load_reader.ok());
    REQUIRE(state(*copy) == pending_load);
}

} // namespace
