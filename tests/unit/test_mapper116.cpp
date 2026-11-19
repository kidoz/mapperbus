#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/somari_p_116.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(Byte submapper = 0U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 116U;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

std::vector<Byte> patterned_rom(std::size_t size, std::size_t page) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / page) >> (i % page == 1U ? 8U : 0U));
    return data;
}

std::unique_ptr<Mapper> board(Byte submapper = 0U,
                              bool legacy = false,
                              std::size_t prg_size = 524288U,
                              std::size_t chr_size = 524288U) {
    const auto h = header(submapper, legacy);
    REQUIRE(SomariP116::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<SomariP116>(
        h, patterned_rom(prg_size, 8192U), patterned_rom(chr_size, 1024U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void supervisor(Mapper& mapper, Byte value) {
    mapper.write_expansion(0x4100U, value);
}

void mmc3_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void mmc1_write(Mapper& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((static_cast<unsigned>(value) >> bit) & 1U));
}

unsigned prg_marker(Mapper& mapper, Address addr) {
    return mapper.read_prg(addr) | (static_cast<unsigned>(mapper.read_prg(addr + 1U)) << 8U);
}

unsigned chr_marker(Mapper& mapper, Address addr) {
    return mapper.read_chr(addr) | (static_cast<unsigned>(mapper.read_chr(addr + 1U)) << 8U);
}

TEST_CASE("Mapper 116 powers into MMC3 with independent VRC2 defaults", "[mapper116]") {
    for (Byte submapper : {0U, 1U, 2U}) {
        auto mapper = board(submapper);
        REQUIRE(prg_marker(*mapper, 0x8000U) == 0U);
        REQUIRE(prg_marker(*mapper, 0xA000U) == 0U);
        REQUIRE(prg_marker(*mapper, 0xC000U) == 62U);
        REQUIRE(prg_marker(*mapper, 0xE000U) == 63U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(state(*mapper).size() == 43U);
        supervisor(*mapper, 0U);
        REQUIRE(prg_marker(*mapper, 0x8000U) == 0U);
        REQUIRE(prg_marker(*mapper, 0xA000U) == 1U);
        REQUIRE(prg_marker(*mapper, 0xC000U) == 62U);
        REQUIRE(prg_marker(*mapper, 0xE000U) == 63U);
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(chr_marker(*mapper, static_cast<Address>(slot * 1024U)) == 255U);
        supervisor(*mapper, 2U);
        REQUIRE(prg_marker(*mapper, 0x8000U) == 0U);
        REQUIRE(prg_marker(*mapper, 0xC000U) == 62U);
        REQUIRE(chr_marker(*mapper, 0U) == 0U);
        REQUIRE(chr_marker(*mapper, 0x1000U) == 4U);
    }
}

TEST_CASE("Mapper 116 supervisor decodes only its documented aliases", "[mapper116]") {
    auto mapper = board();
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        const bool decoded = (a & 0xE100U) == 0x4100U;
        REQUIRE(mapper->maps_expansion(addr) == decoded);
        REQUIRE(mapper->maps_prg(addr) == (decoded && a >= 0x6000U));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        mapper->write_expansion(addr, 0U);
        REQUIRE(mapper->read_prg(0xA000U) == (decoded ? 1U : 0U));
        supervisor(*mapper, 1U);
    }
    for (Address addr : {0x4100U, 0x4101U, 0x41FFU}) {
        CAPTURE(addr);
        supervisor(*mapper, 1U);
        if (addr < 0x6000U)
            mapper->write_expansion(addr, 2U);
        else
            mapper->write_prg(addr, 2U);
        REQUIRE(prg_marker(*mapper, 0xC000U) == 62U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    }
}

TEST_CASE("Mapper 116 VRC2 mode implements all ports mirrors banking and outer CHR",
          "[mapper116]") {
    auto mapper = board();
    auto aliases = board();
    supervisor(*mapper, 0U);
    supervisor(*aliases, 0U);
    for (unsigned group = 8U; group < 16U; ++group)
        for (unsigned port = 0U; port < 4U; ++port)
            for (unsigned data = 0U; data < 256U; ++data) {
                const auto canonical = static_cast<Address>((group << 12U) | port);
                mapper->write_prg(canonical, static_cast<Byte>(data));
                for (Address alias : {static_cast<Address>(canonical | 0x0FFCU),
                                      static_cast<Address>(canonical | 0x0100U),
                                      static_cast<Address>(canonical | 0x0200U)}) {
                    aliases->write_prg(alias, static_cast<Byte>(data));
                    REQUIRE(state(*aliases) == state(*mapper));
                }
            }
    for (Byte outer : {0U, 4U}) {
        supervisor(*mapper, outer);
        for (unsigned low = 0U; low < 32U; ++low) {
            mapper->write_prg(0x8003U, static_cast<Byte>(low | 0xE0U));
            mapper->write_prg(0xA002U, static_cast<Byte>(31U - low));
            REQUIRE(prg_marker(*mapper, 0x8000U) == low);
            REQUIRE(prg_marker(*mapper, 0xA000U) == 31U - low);
            REQUIRE(prg_marker(*mapper, 0xC000U) == 62U);
            REQUIRE(prg_marker(*mapper, 0xE000U) == 63U);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto group = static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U);
            const auto low = static_cast<Address>(group + (slot % 2U) * 2U);
            const auto high = static_cast<Address>(low + 1U);
            for (unsigned bank = 0U; bank < 256U; ++bank) {
                mapper->write_prg(low, static_cast<Byte>(bank));
                mapper->write_prg(high, static_cast<Byte>(bank >> 4U));
                for (unsigned other = 0U; other < 8U; ++other) {
                    const auto expected = other == slot ? bank + outer * 64U : 255U + outer * 64U;
                    REQUIRE(chr_marker(*mapper, static_cast<Address>(other * 1024U)) == expected);
                }
            }
        }
    }
    for (unsigned data = 0U; data < 256U; ++data) {
        mapper->write_prg(0x9000U, static_cast<Byte>(data));
        REQUIRE(mapper->mirror_mode() ==
                ((data & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 116 MMC3 mode preserves stock bank inversion and IRQ behavior",
          "[mapper116][irq]") {
    auto mapper = board();
    for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U})
        for (unsigned data = 0U; data < 256U; ++data) {
            mmc3_bank(*mapper, 6U, static_cast<Byte>(data), mode);
            mmc3_bank(*mapper, 7U, static_cast<Byte>(data ^ 0x35U), mode);
            const std::array<unsigned, 4> expected{(mode & 0x40U) == 0U ? data % 64U : 62U,
                                                   (data ^ 0x35U) % 64U,
                                                   (mode & 0x40U) == 0U ? 62U : data % 64U,
                                                   63U};
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(prg_marker(*mapper, static_cast<Address>(0x8000U + slot * 8192U)) ==
                        expected[slot]);
            for (Byte reg = 0U; reg < 6U; ++reg)
                mmc3_bank(*mapper, reg, static_cast<Byte>(data + reg * 29U), mode);
            std::array<unsigned, 8> banks{data & 0xFEU,
                                          (data & 0xFEU) + 1U,
                                          (data + 29U) & 0xFEU,
                                          ((data + 29U) & 0xFEU) + 1U,
                                          (data + 58U) & 0xFFU,
                                          (data + 87U) & 0xFFU,
                                          (data + 116U) & 0xFFU,
                                          (data + 145U) & 0xFFU};
            for (unsigned slot = 0U; slot < 8U; ++slot)
                REQUIRE(chr_marker(*mapper, static_cast<Address>(slot * 1024U)) ==
                        banks[slot ^ ((mode & 0x80U) != 0U ? 4U : 0U)]);
        }
    supervisor(*mapper, 5U);
    REQUIRE(chr_marker(*mapper, 0U) != 0xFFFFU);
    for (unsigned data = 0U; data < 256U; ++data) {
        mapper->write_prg(0xA000U, static_cast<Byte>(data));
        REQUIRE(mapper->mirror_mode() ==
                ((data & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 116 MMC1 serial registers cover all modes and Huang bank units", "[mapper116]") {
    for (Byte submapper : {0U, 1U, 2U}) {
        auto mapper = board(submapper);
        supervisor(*mapper, 2U);
        for (Byte mirror = 0U; mirror < 4U; ++mirror) {
            mmc1_write(*mapper, 0x9FFFU, static_cast<Byte>(0x0CU | mirror));
            constexpr std::array kModes = {MirrorMode::SingleLower,
                                           MirrorMode::SingleUpper,
                                           MirrorMode::Vertical,
                                           MirrorMode::Horizontal};
            REQUIRE(mapper->mirror_mode() == kModes[mirror]);
        }
        for (Byte mode = 0U; mode < 4U; ++mode)
            for (Byte bank = 0U; bank < 16U; ++bank) {
                mmc1_write(*mapper, 0x8000U, static_cast<Byte>(mode << 2U));
                mmc1_write(*mapper, 0xE000U, bank);
                const auto unit = submapper == 2U ? 1U : 2U;
                const auto switched = bank * unit;
                const auto pair = (bank & (submapper == 2U ? 0x0CU : 0x0EU)) * unit;
                const std::array<unsigned, 2> expected =
                    mode < 2U    ? std::array<unsigned, 2>{pair, pair + 2U}
                    : mode == 2U ? std::array<unsigned, 2>{0U, switched}
                                 : std::array<unsigned, 2>{switched, 62U};
                REQUIRE(prg_marker(*mapper, 0x8000U) == expected[0]);
                REQUIRE(prg_marker(*mapper, 0xC000U) == expected[1]);
            }
        for (Byte outer : {0U, 4U}) {
            supervisor(*mapper, static_cast<Byte>(2U | outer));
            mmc1_write(*mapper, 0x8000U, 0x0CU);
            for (Byte bank = 0U; bank < 32U; ++bank) {
                mmc1_write(*mapper, 0xA000U, bank);
                mmc1_write(*mapper, 0xC000U, static_cast<Byte>(31U - bank));
                REQUIRE(chr_marker(*mapper, 0U) == (bank & 0x1EU) * 4U + outer * 64U);
                REQUIRE(chr_marker(*mapper, 0x1000U) == (bank & 0x1EU) * 4U + 4U + outer * 64U);
            }
            mmc1_write(*mapper, 0x8000U, 0x1CU);
            for (Byte bank = 0U; bank < 32U; ++bank) {
                mmc1_write(*mapper, 0xA000U, bank);
                mmc1_write(*mapper, 0xC000U, static_cast<Byte>(31U - bank));
                REQUIRE(chr_marker(*mapper, 0U) == bank * 4U + outer * 64U);
                REQUIRE(chr_marker(*mapper, 0x1000U) == (31U - bank) * 4U + outer * 64U);
            }
        }
    }
}

TEST_CASE("Mapper 116 PAL variants differ only in MMC1 mode-entry serial reset", "[mapper116]") {
    for (Byte submapper : {0U, 1U, 2U}) {
        auto mapper = board(submapper);
        supervisor(*mapper, 2U);
        mapper->write_prg(0x8000U, 1U);
        mapper->write_prg(0x8000U, 0U);
        const auto partial = state(*mapper);
        supervisor(*mapper, 1U);
        supervisor(*mapper, 2U);
        REQUIRE((state(*mapper) == partial) == (submapper == 1U));
        if (submapper != 1U) {
            mapper->write_prg(0x8000U, 1U);
            mapper->write_prg(0x8000U, 0U);
            mapper->write_prg(0x8000U, 1U);
            mapper->write_prg(0x8000U, 0x80U);
            mmc1_write(*mapper, 0x8000U, 0x1FU);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        }
        supervisor(*mapper, 1U);
        supervisor(*mapper, 3U);
        const auto reset_mirror =
            submapper == 1U ? MirrorMode::SingleLower : MirrorMode::Horizontal;
        REQUIRE(mapper->mirror_mode() == reset_mirror);
        mapper->write_prg(0x8000U, 0x80U);
        REQUIRE(mapper->mirror_mode() == reset_mirror);
        mapper->write_prg_cpu(0x8000U, 1U, false);
        mapper->write_prg_cpu(0x8000U, 0U, true);
        mapper->write_prg_cpu(0x8000U, 0x80U, true);
        REQUIRE(mapper->mirror_mode() == reset_mirror);
    }
}

TEST_CASE("Mapper 116 isolates low-bus and IRQ state by active ASIC", "[mapper116][irq]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        mapper->write_prg(addr, 1U);
    }
    supervisor(*mapper, 0U);
    for (Address addr = 0x6000U; addr < 0x7000U; ++addr) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 1U);
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_prg(addr) == (addr & 1U));
    }
    for (Address addr = 0x7000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->maps_prg(addr) == ((addr & 0xE100U) == 0x4100U));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    supervisor(*mapper, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    supervisor(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    supervisor(*mapper, 1U);
    REQUIRE(mapper->irq_pending());
    supervisor(*mapper, 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    supervisor(*mapper, 1U);
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 116 reset restores power-on registers without touching ROM", "[mapper116]") {
    auto mapper = board();
    supervisor(*mapper, 4U);
    mapper->write_prg(0x8001U, 3U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    supervisor(*mapper, 2U);
    mmc1_write(*mapper, 0x8000U, 0x1FU);
    mmc1_write(*mapper, 0xA000U, 0x12U);
    const auto initial = state(*board());
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(prg_marker(*mapper, 0xE000U) == 63U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_chr(0U, 0xAAU);
    REQUIRE(chr_marker(*mapper, 0U) == 0U);
}

TEST_CASE("Mapper 116 state replays all ASICs and rejects corruption atomically",
          "[mapper116][state]") {
    auto mapper = board(1U);
    auto copy = board(1U);
    supervisor(*mapper, 0U);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0xA000U, 9U);
    mapper->write_prg(0xB000U, 3U);
    mapper->write_prg(0x6000U, 1U);
    supervisor(*mapper, 5U);
    mmc3_bank(*mapper, 6U, 11U, 0xC0U);
    mmc3_bank(*mapper, 2U, 47U, 0xC0U);
    mapper->write_prg(0xA001U, 0x40U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    supervisor(*mapper, 2U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0x8000U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 43U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6000U) == 0U);
    supervisor(*copy, 0U);
    REQUIRE(copy->read_prg(0x6000U) == 1U);
    REQUIRE(prg_marker(*copy, 0x8000U) == 7U);
    REQUIRE(chr_marker(*copy, 0U) == 243U);
    supervisor(*copy, 4U);
    REQUIRE(chr_marker(*copy, 0U) == 499U);
    supervisor(*copy, 1U);
    REQUIRE(copy->irq_pending());
    for (std::size_t n = 0U; n < saved.size(); ++n) {
        StateReader truncated(std::span(saved).first(n));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 20>{
             {{0U, 0U},  {2U, 1U},   {6U, 1U},   {10U, 8U},  {11U, 32U}, {21U, 2U}, {22U, 2U},
              {23U, 8U}, {30U, 64U}, {31U, 64U}, {32U, 4U},  {35U, 8U},  {35U, 4U}, {36U, 1U},
              {37U, 0U}, {38U, 5U},  {39U, 32U}, {40U, 32U}, {41U, 32U}, {42U, 32U}}}) {
        auto bad = saved;
        CAPTURE(offset, value);
        bad[offset] = value;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto fresh = board(1U);
    auto bad_empty_shift = state(*fresh);
    bad_empty_shift[37U] = 0x11U;
    StateReader invalid_empty_shift(bad_empty_shift);
    fresh->load_state(invalid_empty_shift);
    REQUIRE_FALSE(invalid_empty_shift.ok());
    REQUIRE(state(*fresh) == state(*board(1U)));
    auto other_profile = board(0U);
    StateReader wrong_profile(saved);
    other_profile->load_state(wrong_profile);
    REQUIRE_FALSE(wrong_profile.ok());
    REQUIRE(state(*other_profile) == state(*board(0U)));
}

TEST_CASE("Mapper 116 validates geometry and explicitly rejects unspecified multicart wiring",
          "[mapper116]") {
    for (Byte submapper : {0U, 1U, 2U})
        for (const auto [p, c] : std::array<std::pair<std::size_t, std::size_t>, 4>{
                 {{32768U, 8192U}, {262144U, 262144U}, {524288U, 524288U}, {32768U, 524288U}}})
            REQUIRE(SomariP116::validate_geometry(header(submapper), p, c));
    REQUIRE(SomariP116::validate_geometry(header(0U, true), 262144U, 262144U));
    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto h = header();
        std::size_t p = 262144U, c = 262144U;
        switch (kind) {
        case 0U:
            h.submapper = 3U;
            break;
        case 1U:
            h.submapper = 4U;
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
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 9U:
            p = 16384U;
            break;
        case 10U:
            p = 1048576U;
            break;
        case 11U:
            p = 98304U;
            break;
        case 12U:
            p = 0U;
            break;
        case 13U:
            c = 0U;
            break;
        case 14U:
            c = 4096U;
            break;
        case 15U:
            c = 1048576U;
            break;
        case 16U:
            c = 24576U;
            break;
        default:
            h.mapper_number = 115U;
            break;
        }
        REQUIRE_FALSE(SomariP116::validate_geometry(h, p, c));
    }
}
} // namespace
