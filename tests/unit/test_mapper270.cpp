#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper270.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 270U;
    h.submapper = submapper;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    h.chr_ram_size = submapper == 2U ? 8192U : 0U;
    return h;
}

Byte rom_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) + (offset >> 16U));
}

std::unique_ptr<Mapper270> board(Byte submapper = 0U,
                                 std::size_t prg_size = 0x20000U,
                                 bool jumper = false) {
    REQUIRE(Mapper270::validate_geometry(header(submapper), prg_size, 0U));
    std::vector<Byte> prg(prg_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = rom_cell(i);
    auto mapper =
        std::make_unique<Mapper270>(header(submapper), std::move(prg), std::vector<Byte>{});
    if (jumper)
        REQUIRE(mapper->configure_board_jumpers(1U));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_uio(Mapper270& mapper, Byte value, Byte direction = 0xFFU) {
    mapper.write_expansion(0x412BU, direction);
    mapper.write_expansion(0x412CU, value);
}

void edge(Mapper270& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned i = 0U; i < low_cycles; ++i)
        mapper.clock_cpu();
    mapper.observe_ppu_a12(true);
}

Byte bank_bits(Byte submapper) {
    switch (submapper) {
    case 0U:
        return 0x07U;
    case 1U:
        return 0x02U;
    case 2U:
        return 0x03U;
    default:
        return 0x04U;
    }
}

TEST_CASE("Mapper 270 reset and exact expansion apertures match each submapper", "[mapper270]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto m = board(submapper, 0x8000U);
        const auto initial = state(*m);
        REQUIRE(initial.size() == (submapper == 2U ? 8225U : 33U));
        REQUIRE(m->read_prg(0x8000U) == rom_cell(0U));
        REQUIRE(m->read_prg(0xA000U) == rom_cell(0x2000U));
        REQUIRE(m->read_prg(0xC000U) == rom_cell(0x4000U));
        REQUIRE(m->read_prg(0xE000U) == rom_cell(0x6000U));
        constexpr std::array<unsigned, 8> kChrPages{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U)) ==
                    rom_cell(kChrPages[slot] * 1024U));
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(m->battery_ram().empty());
        REQUIRE_FALSE(m->has_expansion_audio());
        REQUIRE(m->audio_output() == 0.0F);
        REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(m->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(m->maps_prg(0x6000U));

        for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
            const bool native = addr >= 0x4100U && addr <= 0x410BU;
            const bool uio = addr == 0x412BU || addr == 0x412CU;
            const bool chr_ram = submapper == 2U && addr == 0x4242U;
            REQUIRE(m->maps_expansion(static_cast<Address>(addr)) == (native || uio || chr_ram));
            const auto expected_mask =
                static_cast<Byte>(submapper == 2U && addr == 0x412CU ? 0x10U : 0U);
            REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) == expected_mask);
        }
        REQUIRE(m->read_expansion(0x412CU) == 0U);
        REQUIRE(m->configure_board_jumpers(0U).has_value() == (submapper == 2U));
        REQUIRE(m->configure_board_jumpers(1U).has_value() == (submapper == 2U));
        REQUIRE(m->read_expansion(0x412CU) == (submapper == 2U ? 0x10U : 0U));
        REQUIRE_FALSE(m->configure_board_jumpers(2U));
        m->write_expansion(0x412BU, 0x10U);
        REQUIRE(m->expansion_read_mask(0x412CU) == 0U);
        REQUIRE(m->read_expansion(0x412CU) == 0U);
        REQUIRE(state(*m) != initial);
    }
}

TEST_CASE("Mapper 270 native and UIO registers decode only their documented addresses",
          "[mapper270]") {
    auto m = board(2U);
    const auto initial = state(*m);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        m->reset();
        m->write_expansion(static_cast<Address>(addr), 0xA5U);
        auto expected = initial;
        if (addr == 0x4100U)
            expected[10] = 0xA5U;
        if (addr == 0x4101U)
            expected[26] = 0xA5U;
        if (addr == 0x4104U)
            expected[28] = 1U;
        if (addr == 0x4105U)
            expected[11] = 0xA5U;
        if (addr == 0x4106U)
            expected[12] = 1U;
        if (addr >= 0x4107U && addr <= 0x4109U)
            expected[13U + addr - 0x4107U] = 0xA5U;
        if (addr == 0x410AU)
            expected[16] = 0xA5U;
        if (addr == 0x410BU)
            expected[17] = 0x85U;
        if (addr == 0x412BU)
            expected[30] = 0xA5U;
        if (addr == 0x4242U)
            expected[32] = 1U;
        REQUIRE(state(*m) == expected);
    }
    for (unsigned addr = 0x2000U; addr < 0x4000U; ++addr) {
        m->reset();
        const bool native = addr >= 0x2010U && addr <= 0x201FU;
        REQUIRE(m->write_ppu_register(static_cast<Address>(addr), 0xD5U) == native);
        REQUIRE(m->read_ppu_register(static_cast<Address>(addr), 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto expected = initial;
        if (addr >= 0x2012U && addr <= 0x2017U)
            expected[18U + addr - 0x2012U] = 0xD5U;
        if (addr == 0x2018U)
            expected[24] = 0x50U;
        if (addr == 0x201AU)
            expected[25] = 0xD5U;
        REQUIRE(state(*m) == expected);
    }
}

TEST_CASE("Mapper 270 UIO direction gates every writable submapper bank bit", "[mapper270]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto m = board(submapper);
        for (unsigned direction = 0U; direction < 256U; ++direction) {
            for (unsigned value = 0U; value < 256U; ++value) {
                set_uio(*m, static_cast<Byte>(value), static_cast<Byte>(direction));
                REQUIRE(state(*m)[31] == (value & direction & bank_bits(submapper)));
            }
        }
        m->reset();
        m->write_expansion(0x412CU, 0xFFU);
        REQUIRE(state(*m)[31] == 0U);
    }
}

TEST_CASE("Mapper 270 UIO pins replace A24 and extend shared PRG and CHR with A25", "[mapper270]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        const std::size_t size = submapper == 0U || submapper == 2U ? 0x4000000UZ : 0x2000000UZ;
        auto m = board(submapper, size);
        for (unsigned native_outer : {0U, 0xFFU}) {
            m->write_expansion(0x4100U, static_cast<Byte>(native_outer));
            for (unsigned value = 0U; value < 256U; ++value) {
                set_uio(*m, static_cast<Byte>(value));
                const bool a24 = ((submapper == 0U || submapper == 1U || submapper == 2U) &&
                                  (value & 0x02U) != 0U) ||
                                 ((submapper == 0U || submapper == 3U) && (value & 0x04U) != 0U);
                const bool a25 = (submapper == 0U || submapper == 2U) && (value & 0x01U) != 0U;
                const std::size_t outer = (a24 ? 0x1000000UZ : 0UZ) | (a25 ? 0x2000000UZ : 0UZ);
                const std::size_t base = native_outer == 0U ? 0UZ : 0xE00000UZ;
                INFO("submapper=" << +submapper << " native=" << native_outer
                                  << " value=" << value);
                REQUIRE(m->read_prg(0x8000U) == rom_cell(base | outer));
                REQUIRE(m->read_chr(0x1000U) == rom_cell(base | 0x1000UZ | outer));
            }
        }
    }
}

TEST_CASE("Mapper 270 PRG masks middle banks third bank and COMR6 survive outer banking",
          "[mapper270]") {
    constexpr std::size_t kSize = 0x800000UZ;
    auto m = board(0U, kSize);
    constexpr std::array<unsigned, 8> kKeep{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
    for (unsigned mode = 0U; mode < 8U; ++mode)
        for (unsigned value = 0U; value < 256U; ++value)
            for (unsigned outer : {0U, 3U})
                for (Byte middle : {0U, 0x55U, 0xAAU, 0xFFU})
                    for (bool third : {false, true})
                        for (bool swap : {false, true}) {
                            m->write_expansion(0x4100U, static_cast<Byte>(outer << 4U));
                            m->write_expansion(0x4105U, swap ? 0x40U : 0U);
                            m->write_expansion(0x4107U, static_cast<Byte>(value));
                            m->write_expansion(0x4108U, 0x59U);
                            m->write_expansion(0x4109U, 0xA6U);
                            m->write_expansion(0x410AU, middle);
                            m->write_expansion(0x410BU,
                                               static_cast<Byte>(mode | (third ? 0x40U : 0U)));
                            const unsigned pages[4] = {swap ? (third ? 0xA6U : 254U) : value,
                                                       0x59U,
                                                       swap    ? value
                                                       : third ? 0xA6U
                                                               : 254U,
                                                       255U};
                            for (unsigned slot = 0U; slot < 4U; ++slot) {
                                const unsigned bank = (outer * 256U + (pages[slot] & kKeep[mode]) +
                                                       (middle & (255U ^ kKeep[mode]))) %
                                                      (kSize / 8192U);
                                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U +
                                                                         0x123U)) ==
                                        rom_cell(bank * 8192U + 0x123U));
                            }
                        }
}

TEST_CASE("Mapper 270 CHR masks pairing inversion intermediate and native outer banks",
          "[mapper270]") {
    constexpr std::size_t kSize = 0x400000UZ;
    auto m = board(0U, kSize);
    constexpr std::array<unsigned, 8> kKeep{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
    for (unsigned mode = 0U; mode < 8U; ++mode)
        for (unsigned value = 0U; value < 256U; ++value)
            for (unsigned intermediate = 0U; intermediate < 8U; ++intermediate)
                for (unsigned outer : {0U, 1U, 7U, 15U})
                    for (bool flip : {false, true}) {
                        m->write_expansion(0x4100U, static_cast<Byte>(outer));
                        m->write_expansion(0x4105U, flip ? 0x80U : 0U);
                        const auto middle = static_cast<Byte>((value & 248U) | mode);
                        m->write_ppu_register(0x201AU, middle);
                        m->write_ppu_register(0x2018U, static_cast<Byte>(intermediate << 4U));
                        std::array<unsigned, 6> banks{};
                        for (unsigned r = 0U; r < 6U; ++r) {
                            banks[r] = (value + r * 17U) & 255U;
                            m->write_ppu_register(static_cast<Address>(0x2012U + r),
                                                  static_cast<Byte>(banks[r]));
                        }
                        const unsigned pages[8] = {banks[4] & 254U,
                                                   banks[4] | 1U,
                                                   banks[5] & 254U,
                                                   banks[5] | 1U,
                                                   banks[0],
                                                   banks[1],
                                                   banks[2],
                                                   banks[3]};
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const unsigned bank = (outer * 2048U + intermediate * 256U +
                                                   (pages[slot ^ (flip ? 4U : 0U)] & kKeep[mode]) +
                                                   (middle & (255U ^ kKeep[mode]) & 248U)) %
                                                  (kSize / 1024U);
                            const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                            m->write_chr(addr, 0xFFU);
                            REQUIRE(m->read_chr(addr) == rom_cell(bank * 1024U + 0x123U));
                        }
                    }
}

TEST_CASE("Mapper 270 forwards every MMC3 alias and FWEN disables only forwarding", "[mapper270]") {
    auto m = board();
    m->write_expansion(0x4105U, 0x20U);
    const auto initial = state(*m);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        m->write_expansion(0x4105U, 0x20U);
        m->write_prg(static_cast<Address>(addr), 0xA3U);
        auto expected = initial;
        if (addr >= 0x8000U)
            switch (addr & 0xE001U) {
            case 0x8000U:
                expected[11] = 0xA3U;
                break;
            case 0x8001U:
                expected[22] = 0xA3U;
                break;
            case 0xA000U:
                expected[12] = 1U;
                break;
            case 0xC000U:
                expected[26] = 0xA3U;
                break;
            case 0xE001U:
                expected[28] = 1U;
                break;
            default:
                break;
            }
        REQUIRE(state(*m) == expected);
    }
    m->write_expansion(0x410BU, 8U);
    const auto disabled = state(*m);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(state(*m) == disabled);
    }
    m->write_expansion(0x4107U, 9U);
    REQUIRE(state(*m)[13] == 9U);
}

TEST_CASE("Mapper 270 submapper 2 CHR RAM is unbanked writable and volatile", "[mapper270]") {
    auto m = board(2U, 0x20000U, true);
    m->write_chr(0x123U, 0xA7U);
    REQUIRE(m->read_chr(0x123U) == rom_cell(0x123U));
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_expansion(0x4242U, static_cast<Byte>(value));
        REQUIRE(state(*m)[32] == (value & 1U));
    }
    m->write_expansion(0x4242U, 1U);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        m->write_chr(static_cast<Address>(addr), value);
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ (addr >> 8U)));
    m->write_ppu_register(0x2012U, 0xFFU);
    REQUIRE(m->read_chr(0x1000U) == static_cast<Byte>(0x1000U ^ (0x1000U >> 8U)));
    m->write_expansion(0x4242U, 0U);
    REQUIRE(m->read_chr(0x123U) == rom_cell(0x123U));
    m->write_expansion(0x4242U, 1U);
    REQUIRE(m->read_chr(0x123U) == 0x22U);
    m->reset();
    REQUIRE(state(*m)[32] == 0U);
    REQUIRE(m->configure_board_jumpers(1U));
    m->write_expansion(0x4242U, 1U);
    REQUIRE(m->read_chr(0x123U) == 0x22U);
    m->power_on();
    REQUIRE(m->read_expansion(0x412CU) == 0x10U);
    m->write_expansion(0x4242U, 1U);
    REQUIRE(m->read_chr(0x123U) == 0U);
    for (Byte submapper : {0U, 1U, 3U}) {
        auto other = board(submapper);
        other->write_expansion(0x4242U, 1U);
        other->write_chr(0U, 0xFFU);
        REQUIRE(state(*other)[32] == 0U);
        REQUIRE(other->read_chr(0U) == rom_cell(0U));
    }
}

TEST_CASE("Mapper 270 IRQ filters A12 or selects HSYNC and preserves partial state",
          "[mapper270]") {
    auto m = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        m->reset();
        m->write_expansion(0x4101U, static_cast<Byte>(latch));
        m->write_expansion(0x4102U, 0U);
        m->write_expansion(0x4104U, 0U);
        m->on_ppu_hblank();
        REQUIRE_FALSE(m->irq_pending());
        edge(*m);
        REQUIRE(m->irq_pending() == (latch == 0U));
        for (unsigned n = 0U; n < latch; ++n) {
            edge(*m);
            REQUIRE(m->irq_pending() == (n + 1U == latch));
        }
        m->write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(m->irq_pending());
    }
    for (unsigned low = 0U; low <= 3U; ++low) {
        m->reset();
        m->write_expansion(0x4104U, 0U);
        edge(*m, low);
        REQUIRE(m->irq_pending() == (low == 3U));
    }
    m->reset();
    m->write_expansion(0x4101U, 2U);
    m->write_expansion(0x4104U, 0U);
    edge(*m);
    m->observe_ppu_a12(false);
    m->clock_cpu();
    m->clock_cpu();
    const auto partial = state(*m);
    m->reset();
    StateReader restore(partial);
    m->load_state(restore);
    REQUIRE(restore.ok());
    m->clock_cpu();
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    edge(*m);
    REQUIRE(m->irq_pending());
    m->write_expansion(0x4103U, 0U);
    m->write_expansion(0x410BU, 0x80U);
    m->write_expansion(0x4101U, 0U);
    m->write_expansion(0x4104U, 0U);
    edge(*m);
    REQUIRE_FALSE(m->irq_pending());
    m->on_ppu_hblank();
    REQUIRE(m->irq_pending());
    m->reset();
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 270 rejects unsupported geometry and metadata", "[mapper270]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        const std::size_t maximum = submapper == 0U || submapper == 2U ? 0x4000000UZ : 0x2000000UZ;
        for (std::size_t size = 0x8000UZ; size <= maximum; size <<= 1U)
            REQUIRE(Mapper270::validate_geometry(header(submapper), size, 0U));
        for (std::size_t size : {0UZ, 0x4000UZ, 0xC000UZ, maximum << 1U})
            REQUIRE_FALSE(Mapper270::validate_geometry(header(submapper), size, 0U));
        REQUIRE_FALSE(Mapper270::validate_geometry(header(submapper), 0x20000U, 0x2000U));
        auto h = header(submapper);
        h.chr_ram_size = submapper == 2U ? 0U : 8192U;
        REQUIRE_FALSE(Mapper270::validate_geometry(h, 0x20000U, 0U));
        if (submapper == 1U || submapper == 3U)
            REQUIRE_FALSE(Mapper270::validate_geometry(header(submapper), 0x4000000U, 0U));
    }
    for (Byte submapper = 4U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Mapper270::validate_geometry(header(submapper), 0x20000U, 0U));
    for (unsigned variant = 0U; variant < 8U; ++variant) {
        auto h = header();
        switch (variant) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 256U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        REQUIRE_FALSE(Mapper270::validate_geometry(h, 0x20000U, 0U));
    }
}

TEST_CASE("Mapper 270 bounded snapshots replay and reject corruption atomically", "[mapper270]") {
    auto m = board(2U, 0x20000U, true);
    m->write_expansion(0x4100U, 0xA7U);
    m->write_expansion(0x4107U, 0x95U);
    m->write_ppu_register(0x2012U, 0xFFU);
    set_uio(*m, 0x03U);
    m->write_expansion(0x4242U, 1U);
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 11U));
    m->write_expansion(0x4101U, 2U);
    m->write_expansion(0x4104U, 0U);
    edge(*m);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 8225U);

    for (std::size_t length : {0UZ, 1UZ, 2UZ, 10UZ, 29UZ, 30UZ, 31UZ, 32UZ, 33UZ, 34UZ, 8224UZ}) {
        StateReader reader(std::span(saved).first(length));
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    constexpr std::array<std::pair<unsigned, Byte>, 13> kInvalid{{
        {0U, 0U},
        {1U, 0U},
        {2U, 1U},
        {6U, 1U},
        {11U, 8U},
        {12U, 2U},
        {17U, 0x10U},
        {24U, 0x80U},
        {28U, 2U},
        {29U, 3U},
        {29U, 8U},
        {31U, 0x80U},
        {32U, 2U},
    }};
    for (const auto [offset, value] : kInvalid) {
        auto bad = saved;
        bad[10] ^= 0xFFU;
        bad[offset] = value;
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    auto different_size = board(2U, 0x40000U, true);
    const auto size_state = state(*different_size);
    StateReader wrong_size(saved);
    different_size->load_state(wrong_size);
    REQUIRE_FALSE(wrong_size.ok());
    REQUIRE(state(*different_size) == size_state);
    auto different_submapper = board(1U, 0x20000U);
    const auto submapper_state = state(*different_submapper);
    StateReader wrong_submapper(saved);
    different_submapper->load_state(wrong_submapper);
    REQUIRE_FALSE(wrong_submapper.ok());
    REQUIRE(state(*different_submapper) == submapper_state);
    auto different_jumper = board(2U, 0x20000U);
    const auto jumper_state = state(*different_jumper);
    StateReader wrong_jumper(saved);
    different_jumper->load_state(wrong_jumper);
    REQUIRE_FALSE(wrong_jumper.ok());
    REQUIRE(state(*different_jumper) == jumper_state);

    m->power_on();
    StateReader replay(saved);
    m->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_prg(0x8000U) == rom_cell(5U * 8192U));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 11U));
    edge(*m);
    REQUIRE_FALSE(m->irq_pending());
    edge(*m);
    REQUIRE(m->irq_pending());
}

TEST_CASE("Mapper 270 state output masks and reset defaults are submapper-specific",
          "[mapper270]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto m = board(submapper);
        set_uio(*m, 0xFFU);
        m->write_expansion(0x4105U, 0xE0U);
        m->write_expansion(0x4106U, 1U);
        m->write_expansion(0x4104U, 0U);
        const auto active = state(*m);
        auto bad = active;
        bad[31] = static_cast<Byte>(~bank_bits(submapper) & 0xFFU);
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == active);
        m->reset();
        REQUIRE(state(*m) == state(*board(submapper)));
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        REQUIRE_FALSE(m->irq_pending());
    }
}

} // namespace
