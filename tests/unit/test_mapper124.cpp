#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/super_game_mega_type_iii.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x390000U;
constexpr std::size_t kChrSize = 0x200000U;
constexpr std::size_t kControlBase = 0x380000U;
constexpr std::size_t kStateSize = 16412U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 124U;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    h.prg_ram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 8192U) * 29U + offset + (offset >> 8U));
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 1024U) * 17U + offset + (offset >> 7U));
}

std::vector<Byte> make_prg() {
    std::vector<Byte> rom(kPrgSize);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = prg_cell(i);
    return rom;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> rom(kChrSize);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = chr_cell(i);
    return rom;
}

std::unique_ptr<SuperGameMegaTypeIII> board(MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    auto prg = make_prg();
    auto chr = make_chr();
    REQUIRE(SuperGameMegaTypeIII::validate_geometry(h, prg.size(), chr.size()));
    return std::make_unique<SuperGameMegaTypeIII>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void control(SuperGameMegaTypeIII& mapper, Byte value, unsigned alias = 0U) {
    mapper.write_expansion(static_cast<Address>(0x5001U + alias * 16U), value);
}

void mapping(SuperGameMegaTypeIII& mapper, Byte value, unsigned alias = 0U) {
    mapper.write_expansion(static_cast<Address>(0x5002U + alias * 16U), value);
}

void mmc1_write(SuperGameMegaTypeIII& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
}

void mmc3_bank(SuperGameMegaTypeIII& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    mapper.write_prg(0x8001U, value);
}

Byte expected_dip(Byte dips, bool coin) {
    const unsigned open = static_cast<unsigned>(~dips);
    return static_cast<Byte>((coin ? 0U : 0x80U) | (((open >> 2U) & 1U) << 6U) |
                             (((open >> 4U) & 1U) << 5U) | (((open >> 6U) & 1U) << 4U) |
                             (((open >> 7U) & 1U) << 3U) | (((open >> 5U) & 1U) << 2U) |
                             (((open >> 3U) & 1U) << 1U) | ((open >> 1U) & 1U));
}

std::vector<Byte> image() {
    std::vector<Byte> bytes(16U, 0U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 228U;
    bytes[5] = 0x54U;
    bytes[6] = 0xC1U;
    bytes[7] = 0x78U;
    bytes[9] = 0xF0U;
    bytes[10] = 7U;
    bytes[11] = 7U;
    auto prg = make_prg();
    const std::size_t program = kControlBase + 0x8000U;
    const std::vector<Byte> code{
        0x78U, 0xA9U, 0xA7U, 0x8DU, 0x00U, 0x60U, 0xA9U, 0x20U,
        0x8DU, 0xF1U, 0x50U, 0xADU, 0x00U, 0x60U, 0xC9U, prg_cell(kControlBase + 0x6000U),
        0xD0U, 0xFEU, 0xA9U, 0x00U, 0x8DU, 0x01U, 0x50U, 0xADU,
        0x00U, 0x60U, 0xC9U, 0xA7U, 0xD0U, 0xFEU, 0xA9U, 0x01U,
        0x8DU, 0xF0U, 0x50U, 0xA9U, 0xC7U, 0x85U, 0x10U, 0xE6U,
        0x11U, 0x4CU, 0x27U, 0x80U};
    std::ranges::copy(code, prg.begin() + static_cast<std::ptrdiff_t>(program));
    for (std::size_t vector = kControlBase + 0xFFFAU; vector < kControlBase + 0x10000U;
         vector += 2U) {
        prg[vector] = 0U;
        prg[vector + 1U] = 0x80U;
    }
    bytes.insert(bytes.end(), prg.begin(), prg.end());
    auto chr = make_chr();
    bytes.insert(bytes.end(), chr.begin(), chr.end());
    return bytes;
}

Cartridge cartridge() {
    MapperRegistry::instance().register_mapper(
        124U, [](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<SuperGameMegaTypeIII>(h, std::move(prg), std::move(chr));
        });
    auto result = Cartridge::from_data(image());
    REQUIRE(result);
    return std::move(*result);
}

void ppu_address(Ppu& ppu, Address addr) {
    (void)ppu.read_register(2U);
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

void ppu_write(Ppu& ppu, Address addr, Byte value) {
    ppu_address(ppu, addr);
    ppu.write_register(7U, value);
}

Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}

TEST_CASE("Mapper 124 validates the documented NES 2.0 population", "[mapper124]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical})
        REQUIRE(SuperGameMegaTypeIII::validate_geometry(header(mirror), kPrgSize, kChrSize));
    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        auto p = kPrgSize;
        auto c = kChrSize;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 4U;
            break;
        case 2U:
            h.submapper = 1U;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.prg_ram_size = 0U;
            break;
        case 5U:
            h.prg_ram_size = 16384U;
            break;
        case 6U:
            h.prg_nvram_size = 8192U;
            break;
        case 7U:
            h.chr_ram_size = 0U;
            break;
        case 8U:
            h.chr_ram_size = 16384U;
            break;
        case 9U:
            h.chr_nvram_size = 8192U;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 11U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 12U:
            p = 0x380000U;
            break;
        case 13U:
            p = 0x3A0000U;
            break;
        case 14U:
            c = 0x1F0000U;
            break;
        default:
            c = 0x210000U;
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(SuperGameMegaTypeIII::validate_geometry(h, p, c));
    }
}

TEST_CASE("Mapper 124 custom aliases control audio mapping DIP and coin inputs", "[mapper124]") {
    auto m = board();
    for (unsigned alias = 0U; alias < 16U; ++alias) {
        m->write_expansion(static_cast<Address>(0x5000U + alias * 16U), 0xFEU);
        REQUIRE_FALSE(m->audio_enabled());
        m->write_expansion(static_cast<Address>(0x5000U + alias * 16U), 1U);
        REQUIRE(m->audio_enabled());
        control(*m, static_cast<Byte>(0x80U | alias), alias);
        mapping(*m, static_cast<Byte>(0x80U | alias), alias);
        REQUIRE(state(*m)[2U] == (0x80U | alias));
        REQUIRE(state(*m)[3U] == alias);
    }
    m->write_expansion(0x5100U, 0U);
    REQUIRE(m->audio_enabled());
    m->write_expansion(0x5101U, 0U);
    REQUIRE(state(*m)[2U] == 0x8FU);
    m->write_expansion(0x5102U, 0U);
    REQUIRE(state(*m)[3U] == 15U);
    for (unsigned dips = 0U; dips < 256U; ++dips) {
        REQUIRE(m->configure_board_jumpers(static_cast<Byte>(dips)));
        for (bool coin : {false, true}) {
            m->set_coin_inserted(coin);
            for (unsigned alias = 0U; alias < 16U; ++alias)
                REQUIRE(m->read_expansion(static_cast<Address>(0x4F0FU + alias * 16U)) ==
                        expected_dip(static_cast<Byte>(dips), coin));
        }
    }
    for (Address addr : {0x4E0FU, 0x4F00U, 0x4F0EU, 0x400FU})
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(addr)));
    for (unsigned offset = 0U; offset < 4096U; ++offset)
        REQUIRE(m->read_expansion(static_cast<Address>(0x5000U + offset)) ==
                prg_cell(kControlBase + 0x5000U + offset));
}

TEST_CASE("Mapper 124 control ROM and MMC3-gated WRAM retain independent addresses",
          "[mapper124]") {
    auto m = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) == prg_cell(kControlBase + addr));
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 0xA7U);
        m->write_prg(0xA001U, static_cast<Byte>(gate));
        m->write_prg(0x6000U, 0x39U);
        REQUIRE(m->maps_prg(0x6000U) == ((gate & 0x80U) != 0U));
        REQUIRE(m->read_prg(0x6000U) == ((gate & 0x80U) == 0U   ? 0U
                                         : (gate & 0x40U) == 0U ? 0x39U
                                                                : 0xA7U));
        control(*m, 0x20U);
        REQUIRE(m->maps_prg(0x6000U));
        REQUIRE(m->read_prg(0x6000U) == prg_cell(kControlBase + 0x6000U));
        REQUIRE(m->read_prg(0x7FFFU) == prg_cell(kControlBase + 0x7FFFU));
        control(*m, 0U);
    }
    m->write_prg(0xA001U, 0x80U);
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        m->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 7U));
    for (unsigned offset = 0U; offset < 8192U; ++offset)
        REQUIRE(m->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 7U));
    REQUIRE(m->battery_ram().empty());
}

TEST_CASE("Mapper 124 UNROM and AMROM decode every outer bank and latch bit", "[mapper124]") {
    auto m = board();
    for (unsigned p = 0U; p < 32U; ++p) {
        control(*m, static_cast<Byte>(0xA0U | p));
        mapping(*m, 0U);
        for (unsigned value = 0U; value < 256U; ++value) {
            m->write_prg(static_cast<Address>(0x8000U + value * 128U), static_cast<Byte>(value));
            const unsigned a17 = p & 1U;
            const unsigned outer = (p >> 1U) & 15U;
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const unsigned inner = (slot < 2U ? value & 7U : 7U) * 2U + (slot & 1U);
                const std::size_t offset =
                    outer * 0x40000U + a17 * 0x20000U + inner * 8192U + (slot * 8192U & 8191U);
                const auto expected = offset < kControlBase ? prg_cell(offset) : 0U;
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == expected);
            }
            REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        }
        mapping(*m, 0x10U);
        for (unsigned value = 0U; value < 256U; ++value) {
            m->write_prg(0xFFFFU, static_cast<Byte>(value));
            const std::size_t base = ((p >> 1U) & 15U) * 0x40000U +
                                     ((value >> 2U) & 1U) * 0x20000U + (value & 3U) * 0x8000U;
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto offset = base + slot * 8192U;
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        (offset < kControlBase ? prg_cell(offset) : 0U));
            }
            REQUIRE(m->mirror_mode() ==
                    ((value & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower));
        }
    }
}

TEST_CASE("Mapper 124 MMC1 serial banking covers all PRG modes and CHR modes", "[mapper124]") {
    auto m = board();
    control(*m, 0xBBU);
    mapping(*m, 0x20U);
    for (Byte mode : {0U, 4U, 8U, 12U}) {
        for (unsigned bank = 0U; bank < 32U; ++bank) {
            mmc1_write(*m, 0x9FFFU, mode);
            mmc1_write(*m, 0xFFFFU, static_cast<Byte>(bank));
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                unsigned inner = 0U;
                if (mode < 8U)
                    inner = ((bank & 14U) | (slot >> 1U)) * 2U + (slot & 1U);
                else if (mode == 8U)
                    inner = slot < 2U ? slot : (bank & 15U) * 2U + (slot & 1U);
                else
                    inner = slot < 2U ? (bank & 15U) * 2U + slot : 12U + slot;
                const std::size_t offset = 13U * 0x40000U + 0x20000U + inner * 8192U;
                INFO("mode=" << +mode << " bank=" << bank << " slot=" << slot);
                REQUIRE(m->read_prg(addr) == (offset < kControlBase ? prg_cell(offset) : 0U));
            }
        }
    }
    constexpr std::array<MirrorMode, 4> mirrors{MirrorMode::SingleLower,
                                                MirrorMode::SingleUpper,
                                                MirrorMode::Vertical,
                                                MirrorMode::Horizontal};
    for (unsigned mirror = 0U; mirror < 4U; ++mirror) {
        mmc1_write(*m, 0x8000U, static_cast<Byte>(mirror | 0x0CU));
        REQUIRE(m->mirror_mode() == mirrors[mirror]);
    }
    const std::array<std::pair<Address, unsigned>, 4> registers{
        {{0x8000U, 7U}, {0xA000U, 8U}, {0xC000U, 9U}, {0xE000U, 10U}}};
    for (const auto& [base, offset] : registers)
        for (Address addr :
             {base, static_cast<Address>(base + 0x1234U), static_cast<Address>(base + 0x1FFFU)}) {
            m->reset();
            control(*m, 0xA0U);
            mapping(*m, 0x20U);
            mmc1_write(*m, addr, 0x15U);
            REQUIRE(state(*m)[offset] == 0x15U);
        }
}

TEST_CASE("Mapper 124 MMC1 consecutive filtering does not filter other banking cores",
          "[mapper124]") {
    auto m = board();
    control(*m, 0xA0U);
    mapping(*m, 0x20U);
    for (unsigned bit = 0U; bit < 5U; ++bit)
        m->write_prg_cpu(0xE000U, static_cast<Byte>((7U >> bit) & 1U), bit != 0U);
    REQUIRE(state(*m)[10U] == 0U);
    REQUIRE(state(*m)[4U] == 0U);
    m->write_prg_cpu(0xE000U, 0x80U, true);
    for (unsigned bit = 0U; bit < 5U; ++bit)
        m->write_prg_cpu(0xE000U, static_cast<Byte>((7U >> bit) & 1U), false);
    REQUIRE(state(*m)[10U] == 7U);
    m->write_prg_cpu(0x8000U, 0x80U, true);
    REQUIRE(state(*m)[6U] == 0x10U);
    REQUIRE((state(*m)[7U] & 0x0CU) == 0x0CU);
    m->write_prg_cpu(0xC000U, 2U, true);
    REQUIRE(state(*m)[23U] == 2U);
}

TEST_CASE("Mapper 124 MMC3 PRG windows aliases and shared A17 follow the Verilog", "[mapper124]") {
    auto m = board();
    control(*m, 0xA0U);
    mapping(*m, 0x30U);
    for (Byte prg_mode : {0U, 0x40U})
        for (unsigned r6 = 0U; r6 < 64U; ++r6) {
            mmc3_bank(*m, 6U, static_cast<Byte>(r6), prg_mode);
            mmc3_bank(*m, 7U, static_cast<Byte>(63U - r6), prg_mode);
            const std::array<unsigned, 4> inner{prg_mode == 0U ? r6 & 15U : 14U,
                                                (63U - r6) & 15U,
                                                prg_mode == 0U ? 14U : r6 & 15U,
                                                15U};
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        prg_cell(inner[slot] * 8192U));
        }
    for (Byte mapper_mode : {0U, 0x10U, 0x20U, 0x30U})
        for (Byte prg_mode : {0U, 0x40U}) {
            control(*m, 0x80U);
            mapping(*m, mapper_mode);
            m->write_prg(0xE000U, 0x80U);
            mmc3_bank(*m, 6U, 0x10U, prg_mode);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                const bool switch_r6 = prg_mode == 0U ? slot == 0U : slot == 2U;
                const std::size_t a17 = switch_r6 ? 0x20000U : 0U;
                unsigned inner = slot;
                if (mapper_mode == 0U)
                    inner = (slot < 2U ? 0U : 7U) * 2U + (slot & 1U);
                else if (mapper_mode == 0x10U)
                    inner = slot;
                else if (mapper_mode == 0x20U)
                    inner = slot < 2U ? slot : 12U + slot;
                else
                    inner = switch_r6 ? 0U : (slot == 1U ? 0U : (slot == 3U ? 15U : 14U));
                REQUIRE(m->read_prg(addr) == prg_cell(a17 + inner * 8192U));
            }
        }
    constexpr std::array<Address, 8> ports{
        0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
    for (unsigned port = 0U; port < 8U; ++port) {
        const Address canonical = ports[port];
        for (Address alias : {canonical,
                              static_cast<Address>(canonical + 2U),
                              static_cast<Address>(canonical + 0x1FFEU)}) {
            m->reset();
            control(*m, 0xA0U);
            mapping(*m, 0x30U);
            if (port == 1U)
                m->write_prg(0x8000U, 5U);
            m->write_prg(alias, 0xA5U);
            const auto saved = state(*m);
            if (port == 0U)
                REQUIRE(saved[11U] == 5U);
            if (port == 1U)
                REQUIRE(saved[13U + 5U] == 0xA5U);
            if (port == 2U)
                REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
            if (port == 3U)
                REQUIRE(saved[25U] == 0x80U);
            if (port == 4U)
                REQUIRE(saved[23U] == 0xA5U);
            if (port == 5U)
                REQUIRE((saved[24U] & 1U) != 0U);
            if (port == 6U)
                REQUIRE(saved[24U] == 0U);
            if (port == 7U)
                REQUIRE((saved[24U] & 2U) != 0U);
        }
    }
}

TEST_CASE("Mapper 124 CHR RAM and both CHR cores use outer and A17 pins", "[mapper124]") {
    auto m = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 11U));
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 11U));
    }
    control(*m, 0xE0U);
    for (unsigned outer = 0U; outer < 8U; ++outer)
        for (unsigned forced_a17 = 0U; forced_a17 < 2U; ++forced_a17)
            for (Byte mapper_mode : {0U, 0x20U}) {
                mapping(*m, static_cast<Byte>(0x40U | (outer << 1U) | forced_a17 | mapper_mode));
                for (unsigned chr0 = 0U; chr0 < 32U; ++chr0) {
                    const unsigned chr1 = 31U - chr0;
                    mmc1_write(*m, 0x8000U, 0x1CU);
                    mmc1_write(*m, 0xA000U, static_cast<Byte>(chr0));
                    mmc1_write(*m, 0xC000U, static_cast<Byte>(chr1));
                    for (unsigned half = 0U; half < 2U; ++half) {
                        const unsigned bank = half == 0U ? chr0 : chr1;
                        const std::size_t offset =
                            outer * 0x40000U + forced_a17 * 0x20000U + bank * 4096U + 0x800U;
                        INFO("outer=" << outer << " a17=" << forced_a17 << " chr0=" << chr0
                                      << " half=" << half << " control=" << +state(*m)[2U]);
                        REQUIRE(m->read_chr(static_cast<Address>(half * 4096U + 0x800U)) ==
                                chr_cell(offset));
                    }
                }
                mmc1_write(*m, 0x8000U, 0x0CU);
                mmc1_write(*m, 0xA000U, 0x15U);
                for (unsigned addr = 0U; addr < 8192U; ++addr) {
                    const unsigned bank = (0x14U | (addr >> 12U)) * 4096U + (addr & 4095U);
                    REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                            chr_cell(outer * 0x40000U + forced_a17 * 0x20000U + bank));
                }
            }
    for (unsigned outer = 0U; outer < 8U; ++outer)
        for (unsigned forced_a17 = 0U; forced_a17 < 2U; ++forced_a17)
            for (Byte inversion : {0U, 0x80U}) {
                mapping(*m, static_cast<Byte>(0x50U | (outer << 1U) | forced_a17));
                std::array<Byte, 6> banks{};
                for (Byte reg = 0U; reg < 6U; ++reg) {
                    banks[reg] = static_cast<Byte>(reg * 37U + outer + forced_a17 * 128U);
                    mmc3_bank(*m, reg, banks[reg], inversion);
                }
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const unsigned selected = slot ^ (inversion != 0U ? 4U : 0U);
                    const unsigned bank = selected < 4U
                                              ? (banks[selected / 2U] & 0xFEU) | (selected & 1U)
                                              : banks[selected - 2U];
                    for (unsigned byte : {0U, 1023U}) {
                        const std::size_t offset = outer * 0x40000U + forced_a17 * 0x20000U +
                                                   (bank & 0x7FU) * 1024U + byte;
                        INFO("outer=" << outer << " a17=" << forced_a17
                                      << " inversion=" << +inversion << " slot=" << slot
                                      << " byte=" << byte << " bank=" << bank);
                        REQUIRE(m->read_chr(static_cast<Address>(slot * 1024U + byte)) ==
                                chr_cell(offset));
                    }
                }
            }
    mapping(*m, 0x10U);
    mmc3_bank(*m, 2U, 0x80U);
    REQUIRE(m->read_chr(0x1000U) == chr_cell(0x20000U));
    m->write_chr(0U, 0xA7U);
    REQUIRE(m->read_chr(0U) != 0xA7U);
}

TEST_CASE("Mapper 124 MMC3 IRQs progress in every mode but are visible only in MMC3 mode",
          "[mapper124]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (Byte mode : {0U, 0x10U, 0x20U}) {
        mapping(*m, mode);
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
    }
    mapping(*m, 0x30U);
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xC000U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(m->irq_pending());
        m->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 124 reset follows the custom latch and discrete-core reset wiring",
          "[mapper124]") {
    auto m = board();
    m->configure_board_jumpers(0xA5U);
    m->set_coin_inserted(true);
    control(*m, 0xFFU);
    mapping(*m, 0x7FU);
    m->write_expansion(0x5000U, 1U);
    m->write_prg(0x8000U, 0x17U);
    m->reset();
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(kControlBase + 0x8000U));
    REQUIRE(m->read_chr(0U) == 0U);
    REQUIRE_FALSE(m->audio_enabled());
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->read_expansion(0x4FFFU) == expected_dip(0xA5U, true));
    control(*m, 0xA0U);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(7U * 16384U));
    mapping(*m, 0x10U);
    REQUIRE(m->mirror_mode() == MirrorMode::SingleUpper);
    m->write_prg(0xA001U, 0x80U);
    control(*m, 0U);
    m->write_prg(0x6000U, 0xA7U);
    m->write_chr(0x123U, 0x39U);
    m->power_on();
    REQUIRE(m->read_prg(0x6000U) == 0U);
    REQUIRE(m->read_chr(0x123U) == 0U);
    control(*m, 0xA0U);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U));
    mapping(*m, 0x10U);
    REQUIRE(m->mirror_mode() == MirrorMode::SingleLower);
}

TEST_CASE("Mapper 124 state replay is bounded complete and rejection is atomic", "[mapper124]") {
    auto m = board();
    control(*m, 0xFFU);
    mapping(*m, 0x7FU);
    m->write_expansion(0x5000U, 1U);
    m->configure_board_jumpers(0xA5U);
    m->set_coin_inserted(true);
    m->write_prg(0x8000U, 7U);
    mmc1_write(*m, 0x8000U, 0x1FU);
    mmc1_write(*m, 0xA000U, 0x12U);
    mmc1_write(*m, 0xC000U, 0x0DU);
    mmc1_write(*m, 0xE000U, 0x1BU);
    m->write_prg(0x8000U, 1U);
    m->write_prg(0x8000U, 0U);
    for (Byte reg = 0U; reg < 6U; ++reg)
        mmc3_bank(*m, reg, static_cast<Byte>(reg * 31U + 0x80U), 0xC0U);
    mmc3_bank(*m, 6U, 0x23U, 0xC0U);
    mmc3_bank(*m, 7U, 0x31U, 0xC0U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0xC000U, 3U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    control(*m, 0U);
    for (unsigned offset = 0U; offset < 8192U; ++offset) {
        m->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 3U));
        m->write_chr(static_cast<Address>(offset), static_cast<Byte>(offset * 5U));
    }
    control(*m, 0xFFU);
    mapping(*m, 0x7FU);
    const auto saved = state(*m);
    REQUIRE(saved.size() == kStateSize);
    auto copy = board();
    StateReader restore(saved);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_expansion(0x4FFFU) == expected_dip(0xA5U, true));
    REQUIRE(copy->audio_enabled());
    control(*copy, 0U);
    for (unsigned offset = 0U; offset < 8192U; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 3U));
        REQUIRE(copy->read_chr(static_cast<Address>(offset)) == static_cast<Byte>(offset * 5U));
    }
    control(*copy, 0xFFU);
    mapping(*copy, 0x7FU);
    const std::array<std::pair<std::size_t, Byte>, 16> invalid{{{0U, 1U},
                                                                {3U, 0x80U},
                                                                {4U, 8U},
                                                                {5U, 0x10U},
                                                                {6U, 0U},
                                                                {7U, 0x20U},
                                                                {11U, 8U},
                                                                {12U, 1U},
                                                                {19U, 0x40U},
                                                                {21U, 2U},
                                                                {24U, 8U},
                                                                {24U, 5U},
                                                                {25U, 1U},
                                                                {26U, 4U},
                                                                {1U, 0U},
                                                                {6U, 0x20U}}};
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader rejected(bad);
        copy->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (std::size_t length : {0UZ, 1UZ, 27UZ, 28UZ, 8219UZ, 8220UZ, saved.size() - 1U}) {
        StateReader truncated{std::span(saved).first(length)};
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
}

TEST_CASE("Mapper 124 CPU and PPU buses preserve open bus CHR and dynamic CIRAM", "[mapper124]") {
    auto cart = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0xA001U, 0U);
    for (Address addr : {0x4020U, 0x4F00U, 0x6000U, 0x7FFFU}) {
        bus.write(0x10U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x4FFFU, 0U);
    REQUIRE(bus.read(0x4FFFU) == 0xFFU);
    REQUIRE(bus.read(0x5000U) == prg_cell(kControlBase + 0x5000U));
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    ppu.reset();
    ppu_write(ppu, 0x0123U, 0xA7U);
    REQUIRE(ppu_read(ppu, 0x0123U) == 0xA7U);
    ppu_write(ppu, 0x2000U, 0x19U);
    ppu_write(ppu, 0x2800U, 0x27U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x19U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x27U);
    bus.write(0x5002U, 0x10U);
    bus.write(0xFFFFU, 0U);
    ppu_write(ppu, 0x2000U, 0x39U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x39U);
    REQUIRE(ppu_read(ppu, 0x2800U) == 0x39U);
    bus.write(0x5002U, 0x30U);
    bus.write(0xA000U, 0U);
    ppu_write(ppu, 0x2000U, 0x51U);
    ppu_write(ppu, 0x2400U, 0x73U);
    REQUIRE(ppu_read(ppu, 0x2800U) == 0x51U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x73U);
}

TEST_CASE("Mapper 124 original control firmware boots and machine states replay deterministically",
          "[mapper124]") {
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(cartridge(), ""));
    emulator.power_on();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x10U) == 0xC7U);
    const auto early = emulator.save_state();
    emulator.step_frame();
    const auto future = emulator.save_state();
    REQUIRE(emulator.load_state(early));
    emulator.step_frame();
    REQUIRE(emulator.save_state() == future);
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    auto bad = early;
    bad[bad.size() - tail.data().size() - kStateSize] ^= 1U;
    const auto before = emulator.save_state();
    REQUIRE_FALSE(emulator.load_state(bad));
    REQUIRE(emulator.save_state() == before);
    emulator.reset();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x10U) == 0xC7U);
}

} // namespace
