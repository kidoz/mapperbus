#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper268_coolboy.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateHeader = 42U;

Byte cell(std::size_t offset) {
    return static_cast<Byte>((offset >> 13U) * 37U + offset + (offset >> 8U) +
                             ((offset >> 25U) != 0U ? 0x5BU : 0U));
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = cell(i);
    return data;
}

INesHeader header(Byte submapper = 0U,
                  std::size_t prg = 2097152U,
                  std::size_t chr = 262144U,
                  std::size_t wram = 0U,
                  bool battery = false,
                  bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 268U;
    h.submapper = submapper;
    h.is_nes2 = true;
    h.prg_rom_size = prg;
    h.chr_rom_size = chr;
    h.chr_ram_size = chr == 0U ? 8192U : 0U;
    h.prg_ram_size = battery ? 0U : wram;
    h.prg_nvram_size = battery ? wram : 0U;
    h.has_battery = battery;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

std::unique_ptr<Coolboy> board(const INesHeader& h) {
    return std::make_unique<Coolboy>(h, rom(h.prg_rom_size), rom(h.chr_rom_size));
}

unsigned outer_base(Byte submapper) {
    if ((submapper & 1U) != 0U)
        return 0x5000U;
    return submapper == 2U ? 0x7000U : 0x6000U;
}

void outer(Coolboy& m, Byte submapper, unsigned index, Byte value, unsigned alias = 0U) {
    const auto addr = static_cast<Address>(outer_base(submapper) | (alias & 0x0FF8U) | index);
    if ((submapper & 1U) != 0U)
        m.write_expansion(addr, value);
    else
        m.write_prg(addr, value);
}

void reg(Coolboy& m, Byte index, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | mode));
    m.write_prg(0x8001U, value);
}

std::vector<Byte> state(const Coolboy& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

std::size_t expected_normal_prg(Byte submapper,
                                std::size_t prg_size,
                                Address addr,
                                Byte r6,
                                Byte r7,
                                Byte mode,
                                Byte outer0,
                                Byte outer1) {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    unsigned bank = 0U;
    if (slot == 1U)
        bank = r7;
    else if (slot == 3U)
        bank = 0xFFU;
    else if ((slot == 0U) == ((mode & 0x40U) != 0U))
        bank = 0xFEU;
    else
        bank = r6;
    uint32_t offset = bank * 8192U + (addr & 0x1FFFU);
    const auto replace = [&offset](unsigned bit, bool value) {
        offset = (offset & ~(uint32_t{1U} << bit)) | (value ? uint32_t{1U} << bit : 0U);
    };
    if ((outer0 & 0x40U) != 0U)
        replace(17U, (outer0 & 1U) != 0U);
    if ((outer1 & 0x80U) != 0U)
        replace(18U, (outer0 & 2U) != 0U);
    if ((outer1 & 0x40U) == 0U)
        replace(19U, (outer0 & 4U) != 0U);
    if ((outer1 & 0x20U) == 0U) {
        const bool a20 = submapper == 4U   ? (outer0 & 0x10U) != 0U
                         : submapper == 8U ? false
                         : submapper == 2U ? (outer1 & 0x02U) != 0U
                                           : (outer1 & 0x10U) != 0U;
        replace(20U, a20);
    }
    if (submapper == 4U) {
        replace(21U, (outer0 & 0x20U) != 0U);
        replace(22U, false);
        replace(23U, false);
        replace(24U, false);
    } else if (submapper == 8U) {
        replace(21U, false);
        replace(22U, false);
        replace(23U, false);
        replace(24U, false);
    } else {
        replace(21U, (outer1 & 4U) != 0U);
        replace(22U, (outer1 & 8U) != 0U);
        replace(23U, (outer0 & 0x10U) != 0U);
        replace(24U, (outer0 & 0x20U) != 0U);
    }
    return offset % prg_size;
}

TEST_CASE("Mapper 268 decodes every outer-register alias in all submappers", "[mapper268]") {
    for (Byte sub = 0U; sub < 12U; ++sub) {
        const auto h = header(
            sub, sub == 6U || sub == 7U ? 65536U : 2097152U, sub >= 6U && sub <= 9U ? 0U : 8192U);
        auto m = board(h);
        for (unsigned index = 0U; index < 6U; ++index) {
            const auto value = static_cast<Byte>(0x25U + index);
            for (unsigned offset = 0U; offset < 4096U; offset += 8U)
                outer(*m, sub, index, value, offset);
            const auto saved = state(*m);
            REQUIRE(saved[20U + index] == value);
        }
        const auto saved = state(*m);
        const auto other = (sub & 1U) != 0U ? 0x6000U : 0x5000U;
        if ((sub & 1U) != 0U)
            m->write_prg(other, 0xFFU);
        else
            m->write_expansion(other, 0xFFU);
        REQUIRE(state(*m) == saved);
        REQUIRE(m->observes_ppu_address());
        REQUIRE_FALSE(m->has_expansion_audio());
    }
}

TEST_CASE("Mapper 268 PRG mask and offset pins match an independent pin model", "[mapper268]") {
    for (Byte sub : {Byte{0U}, Byte{2U}, Byte{4U}, Byte{8U}, Byte{10U}}) {
        const std::size_t size = sub == 4U    ? 4194304U
                                 : sub == 8U  ? 2097152U
                                 : sub == 10U ? 8388608U
                                              : 16777216U;
        auto m = board(header(sub, size));
        constexpr Byte r6 = 0xA5U, r7 = 0x5AU;
        for (Byte mode : {Byte{0U}, Byte{0x40U}}) {
            reg(*m, 6U, r6, mode);
            reg(*m, 7U, r7, mode);
            for (unsigned a = 0U; a < 256U; ++a)
                for (unsigned b = 0U; b < 256U; ++b) {
                    outer(*m, sub, 0U, static_cast<Byte>(a));
                    outer(*m, sub, 1U, static_cast<Byte>(b));
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto addr = static_cast<Address>(0x8005U + slot * 8192U);
                        INFO("sub=" << +sub << " a=" << a << " b=" << b << " slot=" << slot);
                        REQUIRE(m->read_prg(addr) ==
                                cell(expected_normal_prg(sub,
                                                         size,
                                                         addr,
                                                         r6,
                                                         r7,
                                                         mode,
                                                         static_cast<Byte>(a),
                                                         static_cast<Byte>(b))));
                    }
                }
        }
    }
}

TEST_CASE("Mapper 268 GNROM PRG size and page controls apply in every wiring family",
          "[mapper268]") {
    for (Byte sub : {Byte{0U}, Byte{2U}, Byte{4U}, Byte{8U}, Byte{10U}}) {
        const std::size_t size = sub == 4U ? 4194304U : 2097152U;
        auto m = board(header(sub, size));
        for (unsigned q = 0U; q < 4U; ++q)
            for (unsigned r = 0U; r < 2U; ++r)
                for (unsigned l = 0U; l < 2U; ++l) {
                    outer(*m, sub, 0U, 0U);
                    outer(*m, sub, 1U, static_cast<Byte>(sub == 2U ? l << 4U : l << 1U));
                    outer(*m, sub, 3U, static_cast<Byte>(0x20U | (q << 2U) | (r << 1U)));
                    const bool nrom256 = sub == 2U ? l == 0U : l != 0U;
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto addr = static_cast<Address>(0x8003U + slot * 8192U);
                        const std::size_t offset =
                            (q << 14U) | (nrom256 ? addr & 0x7FFFU : (r << 14U) | (addr & 0x3FFFU));
                        INFO("sub=" << +sub << " q=" << q << " r=" << r << " l=" << l
                                    << " slot=" << slot << " addr=" << addr);
                        REQUIRE(m->read_prg(addr) == cell(offset % size));
                    }
                }
    }
}

TEST_CASE("Mapper 268 weird MMC3 mode changes only documented fixed PRG and paired CHR outputs",
          "[mapper268]") {
    auto m = board(header());
    reg(*m, 6U, 0x12U);
    reg(*m, 7U, 0x23U);
    outer(*m, 0U, 1U, 0x60U); // A18-A20 all come from the inner MMC3 outputs.
    outer(*m, 0U, 3U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == cell(0x12U * 8192U + 0U));
    REQUIRE(m->read_prg(0xA000U) == cell(0x23U * 8192U + 0U));
    REQUIRE(m->read_prg(0xC000U) == cell(0U));
    REQUIRE(m->read_prg(0xE000U) == cell(0U));
    m->write_prg(0x8000U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == cell(0xFEU * 8192U));
    REQUIRE(m->read_prg(0xC000U) == cell(0x12U * 8192U));
    REQUIRE(m->read_prg(0xE000U) == cell(0xFFU * 8192U));

    outer(*m, 0U, 0U, 0U);
    reg(*m, 0U, 5U);
    reg(*m, 1U, 7U);
    REQUIRE(m->read_chr(0x0000U) == cell(5U * 1024U));
    REQUIRE(m->read_chr(0x0400U) == cell(0U));
    REQUIRE(m->read_chr(0x0800U) == cell(7U * 1024U));
    REQUIRE(m->read_chr(0x0C00U) == cell(0U));
    reg(*m, 0U, 5U, 0x80U);
    reg(*m, 1U, 7U, 0x80U);
    REQUIRE(m->read_chr(0x1000U) == cell(5U * 1024U));
    REQUIRE(m->read_chr(0x1400U) == cell(0U));
    REQUIRE(m->read_chr(0x1800U) == cell(7U * 1024U));
    REQUIRE(m->read_chr(0x1C00U) == cell(0U));
}

TEST_CASE("Mapper 268 CHR offset pins and inversion cover ROM and GNROM modes", "[mapper268]") {
    for (Byte sub : {Byte{0U}, Byte{2U}, Byte{4U}, Byte{10U}}) {
        const std::size_t size = sub == 4U ? 4194304U : sub <= 2U ? 1048576U : 262144U;
        auto m = board(header(sub, 2097152U, size));
        constexpr std::array<Byte, 6> banks{0x23U, 0xA5U, 0x47U, 0xC9U, 0x6BU, 0xEDU};
        for (Byte mode : {Byte{0U}, Byte{0x80U}}) {
            for (Byte index = 0U; index < 6U; ++index)
                reg(*m, index, banks[index], mode);
            for (unsigned control = 0U; control < 256U; ++control) {
                outer(*m, sub, 0U, static_cast<Byte>(control));
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 1024U + 3U);
                    const unsigned selected = slot ^ (mode == 0U ? 0U : 4U);
                    const unsigned raw = selected < 4U
                                             ? (banks[selected / 2U] & 0xFEU) | (selected & 1U)
                                             : banks[selected - 2U];
                    uint32_t offset = raw * 1024U + 3U;
                    if ((control & 0x80U) != 0U)
                        offset = (offset & ~0x20000U) | ((control & 8U) << 14U);
                    offset = (offset & ~0xC0000U) | ((control & 0x30U) << 14U);
                    if (sub == 4U)
                        offset = (offset & ~0x300000U) | ((control & 6U) << 19U);
                    REQUIRE(m->read_chr(addr) == cell(offset % size));
                }
            }
        }
    }

    auto m = board(header());
    outer(*m, 0U, 3U, 0x20U);
    for (unsigned mask = 0U; mask < 8U; ++mask)
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            outer(*m, 0U, 2U, static_cast<Byte>((mask << 4U) | bank));
            for (Address addr : {Address{0U}, Address{0x0123U}, Address{0x1FFFU}}) {
                const auto selected = bank & ((mask << 1U) | 1U);
                REQUIRE(m->read_chr(addr) == cell((selected * 8192U + (addr & 0x1FFFU)) % 262144U));
            }
        }
}

TEST_CASE("Mapper 268 mixed CHR uses the seven-bit register comparison and a 2 KiB RAM window",
          "[mapper268]") {
    auto h = header(1U, 2097152U, 262144U);
    h.chr_ram_size = 2048U;
    auto m = board(h);
    for (unsigned selected = 0U; selected < 128U; ++selected) {
        outer(*m, 1U, 4U, static_cast<Byte>((selected << 1U) | 1U));
        reg(*m, 2U, static_cast<Byte>(selected * 2U));
        reg(*m, 3U, static_cast<Byte>(selected * 2U + 1U));
        m->write_chr(0x1005U, static_cast<Byte>(selected ^ 0x5AU));
        m->write_chr(0x1407U, static_cast<Byte>(selected ^ 0xA5U));
        REQUIRE(m->read_chr(0x1005U) == static_cast<Byte>(selected ^ 0x5AU));
        REQUIRE(m->read_chr(0x1407U) == static_cast<Byte>(selected ^ 0xA5U));
        reg(*m, 2U, static_cast<Byte>((selected + 1U) * 2U));
        REQUIRE(m->read_chr(0x1005U) == cell(((selected + 1U) * 2048U + 5U) % 262144U));
    }
    outer(*m, 1U, 4U, 0x02U);
    reg(*m, 2U, 4U);
    m->write_chr(0x1000U, 0xFFU);
    REQUIRE(m->read_chr(0x1000U) == cell(4096U));
}

TEST_CASE("Mapper 268 lockout and CHR mask lock retain only documented writable fields",
          "[mapper268]") {
    for (Byte sub : {Byte{0U}, Byte{1U}}) {
        auto m = board(header(sub));
        outer(*m, sub, 0U, 0x11U);
        outer(*m, sub, 1U, 0x22U);
        outer(*m, sub, 3U, 0x80U);
        for (unsigned index : {0U, 1U, 3U, 4U, 5U})
            outer(*m, sub, index, 0xFFU);
        auto saved = state(*m);
        REQUIRE(saved[20U] == 0x11U);
        REQUIRE(saved[21U] == 0x22U);
        REQUIRE(saved[23U] == 0x80U);
        outer(*m, sub, 2U, 0xF5U);
        outer(*m, sub, 2U, 0x0AU);
        saved = state(*m);
        REQUIRE(saved[22U] == 0xFAU);
        m->reset();
        REQUIRE(state(*m)[22U] == 0U);

        outer(*m, sub, 3U, 0xA0U);
        outer(*m, sub, 0U, 0x37U);
        outer(*m, sub, 1U, 0x48U);
        outer(*m, sub, 4U, 0x59U);
        outer(*m, sub, 5U, 0x6AU);
        saved = state(*m);
        REQUIRE(saved[20U] == 0x37U);
        REQUIRE(saved[21U] == 0x48U);
        REQUIRE(saved[24U] == 0x59U);
        REQUIRE(saved[25U] == 0x6AU);
    }
}

TEST_CASE("Mapper 268 WRAM overlays are independent and persistence follows the battery flag",
          "[mapper268]") {
    for (Byte sub : {Byte{0U}, Byte{1U}, Byte{2U}})
        for (bool battery : {false, true}) {
            auto m = board(header(sub, sub == 2U ? 2097152U : 2097152U, 8192U, 8192U, battery));
            REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
            if (battery) {
                const std::vector<Byte> initial(8192U, 0xA7U);
                m->set_battery_ram(initial);
                REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            }
            m->write_prg(0xA001U, 0x80U);
            const auto base = static_cast<Address>(outer_base(sub) + 3U);
            if ((sub & 1U) != 0U)
                m->write_expansion(base, 0x10U);
            else
                m->write_prg(base, 0x10U);
            if (sub != 1U)
                REQUIRE(m->read_prg(base) == 0x10U);
            REQUIRE(m->maps_expansion(0x5003U));
            REQUIRE(m->expansion_read_mask(0x5003U) == 0xFFU);
            m->write_expansion(0x5003U, 0x5CU);
            REQUIRE(m->read_expansion(0x5003U) == 0x5CU);
            m->write_prg(0xA001U, 0x00U);
            REQUIRE(m->read_expansion(0x5003U) == 0x5CU);
            if (sub != 1U) {
                REQUIRE(m->maps_prg(base));
                REQUIRE(m->prg_read_mask(base) == 0U);
            }
            m->write_prg(0xA001U, 0xC0U);
            m->write_expansion(0x5003U, 0xFFU);
            REQUIRE(m->read_expansion(0x5003U) == 0x5CU);
            m->write_chr(0U, 0x39U); // ROM must remain immutable.
            REQUIRE(m->read_chr(0U) == cell(0U));
            m->reset();
            REQUIRE(m->read_expansion(0x5003U) == 0U); // P is cleared; reads float.
            m->power_on();
            m->write_prg(0xA001U, 0x80U);
            REQUIRE(m->read_prg(0x6000U) == (battery ? 0xA7U : 0U));
        }

    auto h = header(8U, 2097152U, 0U);
    h.chr_ram_size = 262144U;
    auto protected_ram = board(h);
    protected_ram->write_chr(0x1234U, 0xA6U);
    outer(*protected_ram, 8U, 0U, 0x20U);
    protected_ram->write_chr(0x1234U, 0x59U);
    REQUIRE(protected_ram->read_chr(0x1234U) == 0xA6U);
}

TEST_CASE("Mapper 268 dual-chip and YH2018A chip-select wiring use live CHR and SC pins",
          "[mapper268]") {
    for (Byte sub : {Byte{6U}, Byte{7U}}) {
        auto m = board(header(sub, 2097152U, 0U));
        outer(*m, sub, 1U, 0xE0U);
        reg(*m, 6U, 0x12U);
        reg(*m, 2U, 0U);
        m->observe_ppu_address(0x1000U);
        REQUIRE(m->read_prg(0x8000U) == cell(0x12U * 8192U));
        reg(*m, 2U, 0x80U);
        REQUIRE(m->read_prg(0x8000U) == cell(1048576U + 0x12U * 8192U));
        outer(*m, sub, 0U, 0x80U);
        REQUIRE(m->read_prg(0x8000U) == cell(0x12U * 8192U));
        outer(*m, sub, 0U, 0x88U);
        REQUIRE(m->read_prg(0x8000U) == cell(1048576U + 0x12U * 8192U));
    }

    for (Byte sub : {Byte{0U}, Byte{1U}}) {
        auto m = board(header(sub, 67108864U));
        outer(*m, sub, 1U, 0xE0U);
        reg(*m, 6U, 0U);
        REQUIRE(m->read_prg(0x8000U) == cell(0U));
        REQUIRE(m->read_prg(0x8001U) == cell(33554432U + 1U));
        outer(*m, sub, 3U, 1U);
        REQUIRE(m->read_prg(0x8000U) == cell(33554432U));
        REQUIRE(m->read_prg(0x8001U) == cell(33554432U + 1U));
        outer(*m, sub, 1U, 0xE1U);
        REQUIRE(m->read_prg(0x8000U) == cell(0U));
        REQUIRE(m->read_prg(0x8001U) == cell(1U));
    }
}

TEST_CASE("Mapper 268 mega-UNROM and single-screen controls follow current pins", "[mapper268]") {
    auto m = board(header());
    outer(*m, 0U, 1U, 0xE0U);
    reg(*m, 6U, 0U);
    reg(*m, 2U, 0x00U);
    m->observe_ppu_address(0x1000U);
    outer(*m, 0U, 5U, 0x10U);
    REQUIRE(m->read_prg(0x8000U) == cell(0U));
    reg(*m, 2U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == cell(0x40000U));
    reg(*m, 2U, 0x80U);
    REQUIRE(m->read_prg(0x8000U) == cell(0x80000U));
    m->observe_ppu_address(0x0000U);
    REQUIRE(m->read_prg(0x8000U) == cell(0U));

    for (Byte sub : {Byte{10U}, Byte{11U}}) {
        auto one_screen = board(header(sub, 8388608U));
        REQUIRE(one_screen->mirror_mode() == MirrorMode::SingleLower);
        outer(*one_screen, sub, 0U, 0x10U);
        REQUIRE(one_screen->mirror_mode() == MirrorMode::SingleUpper);
        outer(*one_screen, sub, 0U, 0x50U);
        REQUIRE(one_screen->mirror_mode() == MirrorMode::Vertical);
        one_screen->write_prg(0xA000U, 1U);
        REQUIRE(one_screen->mirror_mode() == MirrorMode::Horizontal);
        one_screen->write_prg(0xA000U, 0U);
        REQUIRE(one_screen->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE("Mapper 268 NEC IRQs never fire for a zero latch and snapshots retain partial counts",
          "[mapper268]") {
    auto m = board(header());
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    const auto partial = state(*m);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    StateReader restore(partial);
    m->load_state(restore);
    REQUIRE(restore.ok());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 1000U; ++count) {
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
    }
    m->reset();
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 268 bounded state replays and rejects corruption or geometry changes atomically",
          "[mapper268]") {
    auto h = header(1U, 2097152U, 262144U, 8192U, true);
    h.chr_ram_size = 2048U;
    auto m = board(h);
    outer(*m, 1U, 0U, 0x37U);
    outer(*m, 1U, 1U, 0x48U);
    outer(*m, 1U, 2U, 0x65U);
    outer(*m, 1U, 3U, 0x10U);
    outer(*m, 1U, 4U, 0x09U);
    outer(*m, 1U, 5U, 0x1AU);
    reg(*m, 5U, 0x9AU, 0xC0U);
    reg(*m, 2U, 8U, 0xC0U);
    m->write_prg(0xC000U, 3U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->observe_ppu_address(0x1234U);
    m->write_prg(0x6000U, 0xA7U);
    m->write_chr(0x1000U, 0x5CU);
    const auto saved = state(*m);
    REQUIRE(saved.size() == kStateHeader + 16U + 8192U + 2048U);

    m->power_on();
    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*m) == saved);

    for (std::size_t length :
         std::array<std::size_t, 8>{0U, 19U, 20U, 41U, 42U, 49U, 50U, saved.size() - 1U}) {
        StateReader reader(std::span(saved).first(length));
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<unsigned, Byte>, 11>{{{0U, 0U},
                                                                                 {6U, 0U},
                                                                                 {10U, 0U},
                                                                                 {13U, 0U},
                                                                                 {17U, 0U},
                                                                                 {26U, 8U},
                                                                                 {35U, 4U},
                                                                                 {38U, 8U},
                                                                                 {38U, 4U},
                                                                                 {39U, 1U},
                                                                                 {41U, 0x40U}}}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }

    const auto mismatch = [&saved](const INesHeader& other_header) {
        auto other = board(other_header);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    mismatch(header(0U, 2097152U, 262144U, 8192U, true));
    mismatch(header(1U, 1048576U, 262144U, 8192U, true));
    mismatch(header(1U, 2097152U, 131072U, 8192U, true));
    mismatch(header(1U, 2097152U, 262144U, 8192U, false));
    mismatch(header(1U, 2097152U, 262144U, 8192U, true, false));
}

TEST_CASE("Mapper 268 geometry validation enforces board wiring and loader bounds", "[mapper268]") {
    for (Byte sub = 0U; sub < 12U; ++sub) {
        auto h = header(sub);
        if (sub == 4U || sub == 5U) {
            h.prg_rom_size = 4194304U;
            h.chr_rom_size = 4194304U;
        } else if (sub == 6U || sub == 7U) {
            h.prg_rom_size = 65536U;
            h.chr_rom_size = 0U;
            h.chr_ram_size = 131072U;
        } else if (sub == 8U || sub == 9U) {
            h.chr_rom_size = 0U;
            h.chr_ram_size = 262144U;
        } else if (sub == 10U || sub == 11U) {
            h.prg_rom_size = 8388608U;
        }
        REQUIRE(Coolboy::validate_geometry(h, h.prg_rom_size, h.chr_rom_size));
    }
    auto h = header(3U);
    h.chr_ram_size = 2048U;
    REQUIRE(Coolboy::validate_geometry(h, h.prg_rom_size, h.chr_rom_size));

    const auto reject = [](INesHeader bad) {
        REQUIRE_FALSE(Coolboy::validate_geometry(bad, bad.prg_rom_size, bad.chr_rom_size));
    };
    h = header();
    h.is_nes2 = false;
    reject(h);
    h = header();
    h.mapper_number = 4U;
    reject(h);
    h = header();
    h.submapper = 12U;
    reject(h);
    for (std::size_t size : {0U, 16384U, 98304U, 33554432U}) {
        h = header();
        h.prg_rom_size = size;
        reject(h);
    }
    h = header(6U);
    h.prg_rom_size = 32768U;
    reject(h);
    h = header(4U);
    h.prg_rom_size = 8388608U;
    reject(h);
    h = header(8U);
    h.prg_rom_size = 4194304U;
    reject(h);
    h = header(10U);
    h.prg_rom_size = 16777216U;
    reject(h);
    for (std::size_t size : {4096U, 12288U, 2097152U}) {
        h = header();
        h.chr_rom_size = size;
        h.chr_ram_size = size == 0U ? 8192U : 0U;
        reject(h);
    }
    h = header();
    h.chr_ram_size = 8192U;
    reject(h);
    h = header(4U);
    h.chr_ram_size = 2048U;
    reject(h);
    h = header(6U);
    h.chr_rom_size = 0U;
    h.chr_ram_size = 262144U;
    reject(h);
    h = header();
    h.chr_rom_size = 0U;
    h.chr_ram_size = 0U;
    reject(h);
    h = header();
    h.chr_nvram_size = 8192U;
    reject(h);
    h = header(0U, 2097152U, 8192U, 4096U);
    reject(h);
    h = header(0U, 2097152U, 8192U, 0U, true);
    reject(h);
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        h = header();
        h.mirror_mode = mirror;
        reject(h);
    }
}

} // namespace
