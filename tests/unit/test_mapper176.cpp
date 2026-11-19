#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/mappers/fk23c.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000U;
constexpr std::size_t kChrSize = 0x200000U;

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 53U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + (bank >> 8U) * 67U + offset + (offset >> 8U));
}

INesHeader header(Byte submapper = 1U, bool wram = true) {
    INesHeader h{};
    h.mapper_number = 176U;
    h.submapper = submapper;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_rom_size = kPrgSize;
    h.chr_rom_size = kChrSize;
    if (submapper == 2U && wram) {
        h.has_battery = true;
        h.prg_ram_size = 0x4000U;
        h.prg_nvram_size = 0x4000U;
    }
    return h;
}

std::vector<Byte> prg_image(std::size_t size = kPrgSize) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = prg_cell(i / 0x2000U, i % 0x2000U);
    return data;
}

std::vector<Byte> chr_image(std::size_t size = kChrSize) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = chr_cell(i / 0x400U, i % 0x400U);
    return data;
}

std::unique_ptr<Fk23c> board(Byte submapper = 1U,
                             bool wram = true,
                             bool mixed_chr = false,
                             std::size_t prg_size = kPrgSize,
                             std::size_t chr_size = kChrSize) {
    auto h = header(submapper, wram);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    h.chr_ram_size = mixed_chr ? 0x2000U : 0U;
    return std::make_unique<Fk23c>(h, prg_image(prg_size), chr_image(chr_size));
}

void outer(Mapper& m, unsigned index, Byte value, Address base = 0x5010U) {
    m.write_expansion(static_cast<Address>(base + index), value);
}

void reg(Mapper& m, Byte index, Byte value, Byte select = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | select));
    m.write_prg(0x8001U, value);
}

std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 176 powers up with documented MMC3 and extended banks", "[mapper176]") {
    for (Byte submapper = 0U; submapper <= 5U; ++submapper) {
        auto m = board(submapper);
        const unsigned last = submapper == 1U || submapper == 3U ? 0xFFU : 0x3FU;
        REQUIRE(m->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(m->read_prg(0xA000U) == prg_cell(1U));
        REQUIRE(m->read_prg(0xC000U) == prg_cell(last - 1U));
        REQUIRE(m->read_prg(0xE000U) == prg_cell(last));
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(m->read_chr(static_cast<Address>(slot * 0x400U)) == chr_cell(slot));
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE_FALSE(m->has_expansion_audio());
        REQUIRE_FALSE(m->maps_expansion(0x5000U));
        REQUIRE(m->battery_ram().size() == (submapper == 2U ? 0x4000U : 0U));
    }

    auto extended = board();
    outer(*extended, 3U, 2U);
    REQUIRE(extended->read_prg(0xC000U) == prg_cell(0xFEU));
    REQUIRE(extended->read_prg(0xE000U) == prg_cell(0xFFU));
    REQUIRE(extended->read_chr(0x0400U) == chr_cell(0xFFU));
    REQUIRE(extended->read_chr(0x0C00U) == chr_cell(0xFFU));
}

TEST_CASE("Mapper 176 decodes every solder-pad alias and rejects unrelated addresses",
          "[mapper176]") {
    auto m = board(0U);
    for (Byte pad = 0U; pad <= 7U; ++pad) {
        REQUIRE(m->configure_board_jumpers(pad));
        const Address selected = static_cast<Address>(0x10U << pad);
        const Address mask = static_cast<Address>(0x5003U | selected);
        const Address base = static_cast<Address>(0x5000U | selected);
        outer(*m, 0U, 3U, base);
        for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
            outer(*m, 1U, 0U, base);
            m->write_expansion(static_cast<Address>(addr), 0x15U);
            const bool decoded = addr >= 0x5000U && addr < 0x6000U && (addr & 3U) == 1U &&
                                 (addr & mask) == (0x5000U | selected | 1U);
            outer(*m, 0U, 3U, base);
            INFO("pad=" << static_cast<unsigned>(pad) << " addr=" << addr);
            REQUIRE(m->read_prg(0x8000U) == prg_cell(decoded ? 0x2AU : 0U));
        }
        outer(*m, 1U, 0U, 0x5FF0U);
        REQUIRE(m->read_prg(0x8000U) == prg_cell(0U));
    }
    REQUIRE_FALSE(m->configure_board_jumpers(8U));
    for (Byte pad = 0U; pad <= 7U; ++pad) {
        const auto profile = "menu-" + std::to_string(pad);
        REQUIRE(m->configure_board_profile(profile));
    }
    REQUIRE_FALSE(m->configure_board_profile("menu-8"));
    REQUIRE_FALSE(m->configure_board_profile("other"));
}

TEST_CASE("Mapper 176 combines inner masks and complementary PRG base bits", "[mapper176]") {
    auto normal = board(0U);
    outer(*normal, 1U, 0x21U);
    for (Byte select : {0U, 0x40U}) {
        reg(*normal, 6U, 0xC5U, select);
        reg(*normal, 7U, 0x83U, select);
        const std::array<unsigned, 4> pages = {
            select == 0U ? 0x45U : 0x7EU, 0x43U, select == 0U ? 0x7EU : 0x45U, 0x7FU};
        for (unsigned slot = 0U; slot < 4U; ++slot)
            REQUIRE(normal->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                    prg_cell(pages[slot]));
    }

    auto m = board();
    outer(*m, 1U, 0x35U);
    for (Byte mode : {1U, 2U}) {
        outer(*m, 0U, mode);
        const unsigned mask = mode == 1U ? 0x1FU : 0x0FU;
        for (Byte select : {0U, 0x40U}) {
            reg(*m, 6U, 0x97U, select);
            reg(*m, 7U, 0x63U, select);
            const unsigned r6 = (0x35U * 2U & ~mask) | (0x97U & mask);
            const unsigned r7 = (0x35U * 2U & ~mask) | (0x63U & mask);
            const unsigned fixed = (0x35U * 2U & ~mask) | mask;
            const std::array<unsigned, 4> pages = {
                select == 0U ? r6 : fixed - 1U, r7, select == 0U ? fixed - 1U : r6, fixed};
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        prg_cell(pages[slot]));
        }
    }
}

TEST_CASE("Mapper 176 implements NROM and UNROM modes across the whole CPU window", "[mapper176]") {
    auto m = board();
    outer(*m, 1U, 0x15U);

    outer(*m, 0U, 3U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                prg_cell(0x2AU + ((addr & 0x2000U) != 0U ? 1U : 0U), addr & 0x1FFFU));

    outer(*m, 0U, 4U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                prg_cell(0x28U + ((addr & 0x7FFFU) >> 13U), addr & 0x1FFFU));

    outer(*m, 0U, 5U);
    for (Byte latch = 0U; latch < 8U; ++latch) {
        m->write_prg(0x9FFFU, latch);
        REQUIRE(m->read_prg(0x8000U) == prg_cell((0x10U | latch) * 2U));
        REQUIRE(m->read_prg(0xBFFFU) == prg_cell((0x10U | latch) * 2U + 1U, 0x1FFFU));
        REQUIRE(m->read_prg(0xC000U) == prg_cell(0x2EU));
        REQUIRE(m->read_prg(0xFFFFU) == prg_cell(0x2FU, 0x1FFFU));
    }
}

TEST_CASE("Mapper 176 extended mode selects four PRG banks and rejects false MMC3 aliases",
          "[mapper176]") {
    auto m = board();
    outer(*m, 0U, 5U);
    outer(*m, 3U, 2U);
    for (Byte select : {0U, 0x40U}) {
        reg(*m, 6U, 0x81U, select);
        reg(*m, 7U, 0x42U, select);
        reg(*m, 8U, 0x73U, select);
        reg(*m, 9U, 0x24U, select);
        const std::array<unsigned, 4> pages = {
            select == 0U ? 0x81U : 0x73U, 0x42U, select == 0U ? 0x73U : 0x81U, 0x24U};
        for (unsigned slot = 0U; slot < 4U; ++slot)
            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                    prg_cell(pages[slot]));
    }

    reg(*m, 7U, 0x42U);
    m->write_prg(0x8002U, 6U);
    m->write_prg(0x8001U, 0x11U);
    REQUIRE(m->read_prg(0xA000U) == prg_cell(0x11U));
    reg(*m, 6U, 0x81U);
    m->write_prg(0x9FFFU, 0x22U);
    m->write_prg(0x8003U, 0x33U);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0x81U));
    for (Byte index = 12U; index < 16U; ++index)
        reg(*m, index, 0x99U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0x73U));
}

TEST_CASE("Mapper 176 maps standard and extended CHR banks with outer masks", "[mapper176]") {
    auto m = board();
    outer(*m, 1U, 0U);
    outer(*m, 2U, 0x15U);
    for (Byte size : {0U, 0x10U}) {
        outer(*m, 0U, size);
        const unsigned mask = size == 0U ? 0xFFU : 0x7FU;
        for (Byte inversion : {0U, 0x80U}) {
            std::array<Byte, 6> values = {0xF3U, 0xA6U, 0xD5U, 0x4AU, 0x89U, 0xBCU};
            for (Byte index = 0U; index < 6U; ++index)
                reg(*m, index, values[index], inversion);
            const unsigned outer_base = 0x15U * 8U & ~mask;
            const std::array<unsigned, 8> inner = {values[0] & 0xFEU,
                                                   (values[0] & 0xFEU) + 1U,
                                                   values[1] & 0xFEU,
                                                   (values[1] & 0xFEU) + 1U,
                                                   values[2],
                                                   values[3],
                                                   values[4],
                                                   values[5]};
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const unsigned bank =
                    outer_base | (inner[slot ^ (inversion == 0U ? 0U : 4U)] & mask);
                REQUIRE(m->read_chr(static_cast<Address>(slot * 0x400U)) == chr_cell(bank));
            }
        }
    }

    outer(*m, 3U, 2U);
    outer(*m, 0U, 0U);
    const std::array<Byte, 12> values = {3U, 5U, 7U, 9U, 11U, 13U, 0U, 0U, 0U, 0U, 15U, 17U};
    for (Byte inversion : {0U, 0x80U}) {
        for (Byte index = 0U; index < 12U; ++index)
            if (index < 6U || index >= 10U)
                reg(*m, index, values[index], inversion);
        const std::array<unsigned, 8> pages = {3U, 15U, 5U, 17U, 7U, 9U, 11U, 13U};
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(m->read_chr(static_cast<Address>(slot * 0x400U)) ==
                    chr_cell(pages[slot ^ (inversion == 0U ? 0U : 4U)]));
    }
}

TEST_CASE("Mapper 176 FK23C implements CNROM and direct NROM CHR modes", "[mapper176]") {
    auto m = board();
    outer(*m, 2U, 0x15U);
    for (Byte size : {0U, 0x10U}) {
        outer(*m, 0U, static_cast<Byte>(0x40U | size));
        const unsigned mask = size == 0U ? 3U : 1U;
        for (Byte latch = 0U; latch < 4U; ++latch) {
            m->write_prg(0xC000U, latch);
            const unsigned bank = (0x15U & ~mask) | (latch & mask);
            for (unsigned addr = 0U; addr < 0x2000U; ++addr)
                REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                        chr_cell(bank * 8U + addr / 0x400U, addr & 0x3FFU));
        }
    }
    outer(*m, 0U, 0x60U);
    for (unsigned addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                chr_cell(0x15U * 8U + addr / 0x400U, addr & 0x3FFU));
}

TEST_CASE("Mapper 176 selects mixed CHR RAM without making ROM writable", "[mapper176]") {
    for (Byte submapper : {0U, 1U}) {
        auto m = board(submapper, false, true, kPrgSize, 0x2000U);
        outer(*m, 0U, 0x20U);
        for (unsigned addr = 0U; addr < 0x2000U; ++addr)
            m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
        for (unsigned addr = 0U; addr < 0x2000U; ++addr)
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ 0xA5U));
        outer(*m, 0U, 0U);
        for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
            m->write_chr(static_cast<Address>(addr), 0U);
            REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                    chr_cell(addr / 0x400U, addr & 0x3FFU));
        }
    }

    auto waixing = board(2U, true, true, kPrgSize, 0x4000U);
    waixing->write_prg(0xA001U, 0xE4U);
    reg(*waixing, 0U, 0U);
    waixing->write_chr(0U, 0xA7U);
    REQUIRE(waixing->read_chr(0U) == 0xA7U);
    reg(*waixing, 0U, 8U);
    REQUIRE(waixing->read_chr(0U) == chr_cell(8U));
    waixing->write_chr(0U, 0U);
    REQUIRE(waixing->read_chr(0U) == chr_cell(8U));
    waixing->write_prg(0xA001U, 0x84U);
    reg(*waixing, 0U, 0U);
    REQUIRE(waixing->read_chr(0U) == chr_cell(0U));
}

TEST_CASE("Mapper 176 FS005 banks volatile and persistent WRAM and exposes its protection window",
          "[mapper176]") {
    auto m = board(2U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        m->write_prg(0xA001U, static_cast<Byte>(0xE0U | bank));
        m->write_prg(0x6000U, static_cast<Byte>(0x40U + bank));
        REQUIRE(m->read_prg(0x6000U) == 0x40U + bank);
    }
    REQUIRE(m->battery_ram()[0U] == 0x41U);
    REQUIRE(m->battery_ram()[0x2000U] == 0x43U);

    m->write_prg(0xA001U, 0xA0U);
    for (Address addr : {0x5000U, 0x5010U, 0x5013U, 0x5FFFU}) {
        REQUIRE(m->maps_expansion(addr));
        m->write_expansion(addr, static_cast<Byte>(addr));
        REQUIRE(m->read_expansion(addr) == static_cast<Byte>(addr));
    }
    m->write_prg(0xA001U, 0xE2U);
    REQUIRE(m->read_prg(0x7000U) == 0U);
    REQUIRE(m->read_prg(0x7010U) == 0x10U);
    REQUIRE(m->read_prg(0x7013U) == 0x13U);
    REQUIRE(m->read_prg(0x7FFFU) == 0xFFU);
    REQUIRE_FALSE(m->maps_expansion(0x5000U));
    REQUIRE(m->expansion_read_mask(0x5000U) == 0xFFU);

    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6000U, 0x66U);
    m->write_prg(0xA001U, 0xC0U);
    m->write_prg(0x6000U, 0U);
    REQUIRE(m->read_prg(0x6000U) == 0x66U);
    m->write_prg(0xA001U, 0x20U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));

    for (unsigned bank = 0U; bank < 4U; ++bank) {
        m->write_prg(0xA001U, static_cast<Byte>(0xE0U | bank));
        REQUIRE(m->read_prg(0x6000U) == (bank == 0U ? 0x66U : 0x40U + bank));
    }
    m->reset();
    m->write_prg(0xA001U, 0xE1U);
    REQUIRE(m->read_prg(0x6000U) == 0x41U);
    m->power_on();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        m->write_prg(0xA001U, static_cast<Byte>(0xE0U | bank));
        REQUIRE(m->read_prg(0x6000U) == ((bank & 1U) != 0U ? 0x40U + bank : bank == 0U ? 0U : 0U));
    }

    std::array<Byte, 0x4000> imported{};
    imported[7U] = 0x99U;
    m->set_battery_ram(imported);
    REQUIRE(m->battery_ram()[7U] == 0x99U);
    const auto before = state(*m);
    m->set_battery_ram(std::span(imported).first(1U));
    REQUIRE(state(*m) == before);
}

TEST_CASE("Mapper 176 FS005 gates single-screen mirroring by RAM configuration", "[mapper176]") {
    auto m = board(2U);
    for (Byte value = 0U; value < 4U; ++value) {
        m->write_prg(0xA000U, value);
        REQUIRE(m->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    m->write_prg(0xA001U, 0xE0U);
    const std::array<MirrorMode, 4> modes = {MirrorMode::Vertical,
                                             MirrorMode::Horizontal,
                                             MirrorMode::SingleLower,
                                             MirrorMode::SingleUpper};
    for (Byte value = 0U; value < 4U; ++value) {
        m->write_prg(0xA000U, value);
        REQUIRE(m->mirror_mode() == modes[value]);
    }
    m->write_prg(0xA001U, 0x80U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 176 implements submapper-specific high outer pins", "[mapper176]") {
    auto jx = board(3U, false, false, 0x1000000U, 0x1000000U);
    outer(*jx, 0U, 3U, 0x5FF0U);
    outer(*jx, 1U, 0U, 0x5FF0U);
    outer(*jx, 5U, 1U, 0x5FF0U);
    REQUIRE(jx->read_prg(0x8000U) == prg_cell(0x100U));
    outer(*jx, 0U, 0x40U, 0x5FF0U);
    outer(*jx, 2U, 0U, 0x5FF0U);
    outer(*jx, 6U, 1U, 0x5FF0U);
    REQUIRE(jx->read_chr(0U) == chr_cell(0x800U));

    auto genius = board(4U, false, false, 0x400000U);
    outer(*genius, 0U, 3U);
    outer(*genius, 1U, 0U);
    outer(*genius, 2U, 0x80U);
    REQUIRE(genius->read_prg(0x8000U) == prg_cell(0x100U));

    auto hst = board(5U);
    outer(*hst, 0U, 3U);
    outer(*hst, 1U, 3U);
    hst->write_expansion(0x4800U, 1U);
    hst->write_expansion(0x4801U, 3U);
    REQUIRE(hst->read_prg(0x8000U) == prg_cell(0x46U));
    REQUIRE(hst->maps_expansion(0x4800U));
    REQUIRE(hst->expansion_read_mask(0x4800U) == 0U);
}

TEST_CASE("Mapper 176 retains MMC3 IRQ progress and repeating zero-latch interrupts",
          "[mapper176]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
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

    StateReader reader(partial);
    m->load_state(reader);
    REQUIRE(reader.ok());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        m->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 176 reset restores registers and cold power clears only volatile memory",
          "[mapper176]") {
    auto m = board(2U, true, true);
    m->write_prg(0xA001U, 0xE4U);
    m->write_chr(0U, 0xA7U);
    m->write_prg(0x6000U, 0x66U);
    outer(*m, 0U, 4U);
    outer(*m, 1U, 0x12U);
    outer(*m, 3U, 2U);
    reg(*m, 6U, 9U, 0xC0U);
    m->write_prg(0xA000U, 3U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());

    m->reset();
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U));
    REQUIRE(m->read_prg(0xC000U) == prg_cell(0x3EU));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xA001U, 0xE4U);
    REQUIRE(m->read_chr(0U) == 0xA7U);
    REQUIRE(m->read_prg(0x6000U) == 0x66U);
    m->power_on();
    m->write_prg(0xA001U, 0xE4U);
    REQUIRE(m->read_chr(0U) == 0U);
    REQUIRE(m->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 176 bounded state replay is byte-exact and rejection is atomic", "[mapper176]") {
    auto m = board(2U, true, true);
    REQUIRE(m->configure_board_jumpers(6U));
    m->write_prg(0xA001U, 0xE4U);
    m->write_chr(0U, 0xA7U);
    m->write_prg(0x6000U, 0x66U);
    outer(*m, 0U, 5U, 0x5FF0U);
    outer(*m, 1U, 0x23U, 0x5FF0U);
    outer(*m, 2U, 0x45U, 0x5FF0U);
    outer(*m, 3U, 2U, 0x5FF0U);
    reg(*m, 6U, 9U, 0xC0U);
    reg(*m, 10U, 12U, 0xC0U);
    m->write_prg(0xA000U, 3U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);

    m->power_on();
    m->write_chr(0U, 0U);
    StateReader replay(saved);
    m->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    REQUIRE(saved[44U] == 0xA7U);
    REQUIRE(m->read_chr(0U) == chr_cell(0x204U));
    REQUIRE(m->read_prg(0x6000U) == 0x66U);
    REQUIRE(m->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(m->irq_pending() == false);

    const std::array<std::pair<std::size_t, Byte>, 13> bad_fields = {{{0U, 0xFFU},
                                                                      {2U, 1U},
                                                                      {11U, 0x80U},
                                                                      {13U, 1U},
                                                                      {14U, 1U},
                                                                      {15U, 4U},
                                                                      {16U, 8U},
                                                                      {17U, 4U},
                                                                      {18U, 0x10U},
                                                                      {31U, 4U},
                                                                      {35U, 8U},
                                                                      {35U, 4U},
                                                                      {36U, 0xFFU}}};
    for (const auto [offset, value] : bad_fields) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    for (std::size_t length : {std::size_t{0U},
                               std::size_t{1U},
                               std::size_t{9U},
                               std::size_t{35U},
                               std::size_t{36U},
                               std::size_t{43U},
                               saved.size() - 1U}) {
        StateReader reader(std::span(saved).first(length));
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    auto trailing = saved;
    trailing.push_back(0U);
    StateReader extra(trailing);
    m->load_state(extra);
    REQUIRE_FALSE(extra.ok());
    REQUIRE(state(*m) == saved);

    for (Byte submapper = 0U; submapper <= 5U; ++submapper) {
        auto other = board(submapper);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
    auto other_pad = board(2U, true, true);
    REQUIRE(other_pad->configure_board_jumpers(5U));
    const auto before = state(*other_pad);
    StateReader mismatch(saved);
    other_pad->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_pad) == before);

    auto other_geometry = board(2U, true, true, 0x400000U);
    REQUIRE(other_geometry->configure_board_jumpers(6U));
    const auto geometry_before = state(*other_geometry);
    StateReader geometry_mismatch(saved);
    other_geometry->load_state(geometry_mismatch);
    REQUIRE_FALSE(geometry_mismatch.ok());
    REQUIRE(state(*other_geometry) == geometry_before);
}

TEST_CASE("Mapper 176 validates every documented memory population", "[mapper176]") {
    for (Byte submapper = 0U; submapper <= 5U; ++submapper) {
        auto h = header(submapper);
        REQUIRE(Fk23c::validate_geometry(h, kPrgSize, kChrSize));
        if (submapper <= 2U) {
            h.chr_ram_size = 0x2000U;
            REQUIRE(Fk23c::validate_geometry(h, kPrgSize, kChrSize));
            h.chr_ram_size = 0x10000U;
            h.chr_rom_size = 0U;
            REQUIRE(Fk23c::validate_geometry(h, kPrgSize, 0U));
        }
        if (submapper == 2U) {
            h = header(2U, false);
            REQUIRE(Fk23c::validate_geometry(h, kPrgSize, kChrSize));
        }
    }
    for (std::size_t chr_size : {0U, 0x2000U}) {
        auto legacy = header(0U, false);
        legacy.is_nes2 = false;
        legacy.prg_ram_size = 0x2000U;
        legacy.chr_ram_size = chr_size == 0U ? 0x2000U : 0U;
        REQUIRE(Fk23c::validate_geometry(legacy, kPrgSize, chr_size));
    }

    const auto reject = [](INesHeader h, std::size_t prg = kPrgSize, std::size_t chr = kChrSize) {
        REQUIRE_FALSE(Fk23c::validate_geometry(h, prg, chr));
    };
    reject(header(), 0U);
    reject(header(), 0x4000U);
    reject(header(), 0x30000U);
    reject(header(), 0x2000000U);
    reject(header(), kPrgSize, 0x1000U);
    reject(header(), kPrgSize, 0x18000U);
    auto h = header();
    h.mapper_number = 175U;
    reject(h);
    h = header();
    h.submapper = 6U;
    reject(h);
    h = header();
    h.mirror_mode = MirrorMode::FourScreen;
    reject(h);
    h = header();
    h.has_battery = true;
    reject(h);
    h = header();
    h.chr_nvram_size = 0x2000U;
    reject(h);
    h = header(3U);
    h.chr_ram_size = 0x2000U;
    reject(h);
    h = header(3U);
    h.chr_rom_size = 0U;
    h.chr_ram_size = 0x2000U;
    reject(h, kPrgSize, 0U);
    h = header(2U);
    h.prg_ram_size = 0x2000U;
    reject(h);
    h = header(2U, false);
    h.chr_rom_size = 0U;
    reject(h, kPrgSize, 0U);
    h = header();
    h.is_nes2 = false;
    h.has_battery = true;
    reject(h);
}

} // namespace
