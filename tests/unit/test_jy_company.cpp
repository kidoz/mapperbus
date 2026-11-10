#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>

#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader jy_header(uint16_t id = 209U) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> jy_banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; i += unit) {
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(i), unit, static_cast<Byte>(i / unit));
        data[i + 1U] = static_cast<Byte>((i / unit) >> 8U);
    }
    return data;
}
std::unique_ptr<Mapper> jy_board(INesHeader h = jy_header(), bool ram = false) {
    register_builtin_mappers();
    const std::size_t prg_size =
        h.mapper_number == 387U
            ? 524288U
            : (h.mapper_number > 255U && h.mapper_number != 282U ? 1048576U : 2097152U);
    const std::size_t chr_size = h.mapper_number > 255U ? 1048576U : 2097152U;
    auto r = MapperRegistry::instance().create(
        h, jy_banks(prg_size, 8192U), ram ? std::vector<Byte>{} : jy_banks(chr_size, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
unsigned chr_index(Mapper& m, Address addr) {
    return m.read_chr(addr) | (static_cast<unsigned>(m.read_chr(addr + 1U)) << 8U);
}
void jy_irq(Mapper& m, Byte mode, Byte prescaler, Byte counter) {
    m.write_prg(0xC002U, 0U);
    m.write_prg(0xC001U, mode);
    m.write_prg(0xC006U, 0U);
    m.write_prg(0xC004U, prescaler);
    m.write_prg(0xC005U, counter);
    m.write_prg(0xC003U, 0U);
}
std::vector<Byte> jy_image(uint16_t id = 209U, bool nvram = false) {
    std::vector<Byte> image(16U + 32768U + 8192U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 2U;
    image[5] = 1U;
    image[6] = static_cast<Byte>((id & 15U) << 4U) | (nvram ? 2U : 0U);
    image[7] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[8] = static_cast<Byte>(id >> 8U);
    image[10] = nvram ? 0x70U : 0U;
    // Stable original program in every 8 KiB bank, booting in fixed last 32 KiB.
    for (std::size_t bank = 16U; bank < 16U + 32768U; bank += 8192U) {
        image[bank] = 0x4CU;
        image[bank + 1U] = 0U;
        image[bank + 2U] = 0x80U;
        for (std::size_t v = 0x1FFAU; v < 0x2000U; v += 2U) {
            image[bank + v] = 0U;
            image[bank + v + 1U] = 0x80U;
        }
    }
    return image;
}
Cartridge jy_cart(uint16_t id = 209U, bool nvram = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(jy_image(id, nvram));
    REQUIRE(r);
    return std::move(*r);
}
void set_ppu_addr(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
void write_ppu(Ppu& ppu, Address addr, Byte value) {
    set_ppu_addr(ppu, addr);
    ppu.write_register(7U, value);
}
Byte read_ppu(Ppu& ppu, Address addr) {
    set_ppu_addr(ppu, addr);
    static_cast<void>(ppu.read_register(7U));
    return ppu.read_register(7U);
}
std::size_t jy_controller_tail_size() {
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    return tail.data().size();
}
TEST_CASE("JY all PRG modes use the correct register, last bank and outer mask", "[mappers][jy]") {
    auto m = jy_board();
    for (unsigned reg = 0U; reg < 4U; ++reg)
        m->write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg + 1U));
    for (Byte outer = 0U; outer < 4U; ++outer) {
        m->write_prg(0xD003U, outer << 1U);
        for (Byte mode = 0U; mode < 4U; ++mode) {
            const unsigned shift = mode < 2U ? 2U - mode : 0U;
            const unsigned unit = 8192U << shift;
            for (Byte switch_last : {0U, 4U}) {
                m->write_prg(0xD000U, static_cast<unsigned>(mode) | switch_last | 0x80U);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const unsigned reg = mode == 0U   ? 3U
                                         : mode == 1U ? (slot / 2U) * 2U + 1U
                                                      : slot;
                    unsigned bank = reg == 3U && switch_last == 0U ? 127U : reg + 1U;
                    if (mode == 3U) {
                        unsigned reverse = 0U;
                        for (unsigned bit = 0U; bit < 7U; ++bit)
                            reverse |= ((bank >> bit) & 1U) << (6U - bit);
                        bank = reverse;
                    }
                    const unsigned expected =
                        outer * 64U + ((bank * (unit / 8192U) + slot % (unit / 8192U)) & 63U);
                    REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == expected);
                }
                const unsigned low = mode == 0U ? 19U : mode == 1U ? 9U : mode == 2U ? 4U : 16U;
                REQUIRE(m->read_prg(0x6000U) == outer * 64U + low);
            }
        }
    }
    // Banking aliases ignore A11, IRQ registers deliberately do not.
    m->write_prg(0xD000U, 6U);
    m->write_prg(0xD003U, 0U);
    m->write_prg(0x87FCU, 7U);
    m->write_prg(0x8800U, 8U);
    m->write_prg(0xD800U, 0U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
}
TEST_CASE("JY CHR modes, high bytes, outer halves and MMC4 physical-read latches",
          "[mappers][jy]") {
    auto m = jy_board();
    for (unsigned i = 0U; i < 8U; ++i) {
        m->write_prg(static_cast<Address>(0x9000U + i), static_cast<Byte>(i + 2U));
        m->write_prg(static_cast<Address>(0xA000U + i), 1U);
    }
    for (Byte mode = 0U; mode < 4U; ++mode) {
        m->write_prg(0xD000U, mode << 3U);
        for (const Byte outer : {0U, 1U, 8U, 0x10U, 0x19U, 0x20U, 0x21U, 0x38U}) {
            m->write_prg(0xD003U, outer);
            const unsigned unit = 8192U >> mode;
            const unsigned mask = (outer & 0x20U) != 0U ? 511U : 255U;
            const unsigned base = ((static_cast<unsigned>(outer) >> 3U) & 3U) * 512U +
                                  ((outer & 0x20U) == 0U ? (outer & 1U) * 256U : 0U);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const unsigned reg = (slot * 1024U / unit) << (3U - mode);
                const unsigned expected =
                    base | (((258U + reg) * (unit / 1024U) + slot % (unit / 1024U)) & mask);
                REQUIRE(chr_index(*m, static_cast<Address>(slot * 1024U)) == expected);
            }
        }
    }
    m->write_prg(0xD000U, 8U);
    m->write_prg(0xD003U, 0x80U);
    REQUIRE(chr_index(*m, 0U) == 8U);
    // Cached data reads never toggle latches or clock read-source IRQs.
    static_cast<void>(m->read_chr(0x0FE8U));
    REQUIRE(chr_index(*m, 0U) == 8U);
    m->observe_ppu_read(0x0FEFU);
    REQUIRE(chr_index(*m, 0U) == 16U);
    m->observe_ppu_read(0x1FE8U);
    REQUIRE(chr_index(*m, 0x1000U) == 32U);
    m->observe_ppu_read(0x0FD8U);
    REQUIRE(chr_index(*m, 0U) == 8U);
    m->write_prg(0x9800U, 33U);
    m->write_prg(0xA800U, 33U);
    REQUIRE(chr_index(*m, 0U) == 8U);
}
TEST_CASE("JY ROM nametables, mixed CIRAM, extended arrangements and mapper 90 inhibit",
          "[mappers][jy]") {
    for (const uint16_t id :
         {35U, 90U, 209U, 211U, 281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        auto m = jy_board(jy_header(id));
        for (unsigned i = 0U; i < 4U; ++i) {
            m->write_prg(static_cast<Address>(0xB000U + i), static_cast<Byte>(0x80U + i));
            m->write_prg(static_cast<Address>(0xB004U + i), 1U);
        }
        m->write_prg(0xD000U, 0x60U);
        for (unsigned i = 0U; i < 4U; ++i) {
            const auto addr = static_cast<Address>(0x2000U + i * 1024U);
            REQUIRE(m->read_nametable(addr).has_value() == ((id != 90U && id != 388U)));
            REQUIRE(m->write_nametable(addr, 0U) == ((id != 90U && id != 388U)));
            if ((id != 90U && id != 388U)) {
                REQUIRE(m->read_nametable(addr) == 0x80U + i);
                REQUIRE(m->read_nametable(addr + 1U) == 1U);
            }
        }
        m->write_prg(0xD000U, 0x20U);
        m->write_prg(0xB001U, 3U);
        REQUIRE_FALSE(m->read_nametable(0x2400U));
        if ((id != 90U && id != 388U))
            REQUIRE(m->nametable_address(0x2403U) == 1027U);
        m->write_prg(0xD002U, 0x80U);
        REQUIRE(m->read_nametable(0x2400U).has_value() == ((id != 90U && id != 388U)));
        REQUIRE_FALSE(m->read_nametable(0x2000U));
        m->write_prg(0xD000U, 0U);
        m->write_prg(0xD001U, 8U);
        REQUIRE(m->nametable_address(0x2403U).has_value() == ((id != 90U && id != 388U)));
        for (Byte mode = 0U; mode < 4U; ++mode) {
            m->write_prg(0xD001U, mode);
            const std::array modes = {MirrorMode::Vertical,
                                      MirrorMode::Horizontal,
                                      MirrorMode::SingleLower,
                                      MirrorMode::SingleUpper};
            REQUIRE(m->mirror_mode() == modes[mode]);
            REQUIRE_FALSE(m->nametable_address(0x2400U));
        }
    }
    auto cart = jy_cart();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0xD001U, 8U);
    cart.write_prg(0xB000U, 1U);
    cart.write_prg(0xB001U, 0U);
    cart.write_prg(0xB002U, 1U);
    write_ppu(ppu, 0x2000U, 0x53U);
    write_ppu(ppu, 0x2400U, 0xA4U);
    REQUIRE(read_ppu(ppu, 0x2800U) == 0x53U);
    REQUIRE(read_ppu(ppu, 0x3000U) == 0x53U);
    cart.write_prg(0xD000U, 0x60U);
    REQUIRE(read_ppu(ppu, 0x2000U) == 0U);
    write_ppu(ppu, 0x2000U, 0xFFU);
    cart.write_prg(0xD000U, 0U);
    REQUIRE(read_ppu(ppu, 0x2000U) == 0x53U);
}
TEST_CASE("JY exact solder pads, open bus, eight-cycle multiplication and accumulator",
          "[mappers][jy]") {
    auto m = jy_board();
    for (Byte j = 0U; j < 4U; ++j) {
        REQUIRE(m->configure_board_jumpers(j));
        for (const Address a : {0x5000U, 0x5400U, 0x5C00U}) {
            REQUIRE(m->maps_expansion(a));
            REQUIRE(m->expansion_read_mask(a) == 0xC0U);
            REQUIRE(m->read_expansion(a) == j * 64U);
        }
    }
    REQUIRE_FALSE(m->configure_board_jumpers(4U));
    REQUIRE_FALSE(m->maps_expansion(0x5001U));
    REQUIRE_FALSE(m->maps_expansion(0x5401U));
    for (unsigned a = 0U; a < 256U; a += 17U) {
        for (unsigned b = 0U; b < 256U; b += 15U) {
            m->write_expansion(0x5800U, static_cast<Byte>(a));
            m->write_expansion(0x5FFDU, static_cast<Byte>(b));
            for (unsigned c = 0U; c < 8U; ++c)
                m->clock_cpu();
            REQUIRE(static_cast<unsigned>(
                        m->read_expansion(0x5800U) |
                        (static_cast<unsigned>(m->read_expansion(0x5801U)) << 8U)) == a * b);
        }
    }
    m->write_expansion(0x5802U, 250U);
    m->write_expansion(0x5FFEU, 11U);
    REQUIRE(m->read_expansion(0x5802U) == 5U);
    m->write_expansion(0x5FFFU, 0xA5U);
    REQUIRE(m->read_expansion(0x5802U) == 0U);
    REQUIRE(m->read_expansion(0x5803U) == 0xA5U);
    auto cart = jy_cart();
    REQUIRE(cart.configure_board_jumpers(2U));
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0x10U, 0x35U);
    REQUIRE(bus.read(0x5000U) == 0xB5U);
    bus.write(0x10U, 0x35U);
    REQUIRE(bus.read(0x5001U) == 0x35U);
}
TEST_CASE("JY IRQ sources, directions, masks, XOR loads, enable and sticky acknowledgement",
          "[mappers][jy]") {
    auto m = jy_board();
    for (Byte source = 0U; source < 4U; ++source) {
        for (Byte direction : {0x40U, 0x80U}) {
            for (Byte narrow : {0U, 4U}) {
                jy_irq(*m,
                       static_cast<unsigned>(direction) | narrow | source,
                       direction == 0x40U ? 0xFEU : 1U,
                       direction == 0x40U ? 0xFFU : 0U);
                const auto tick = [&] {
                    switch (source) {
                    case 0U:
                        m->clock_cpu();
                        break;
                    case 1U:
                        m->observe_ppu_a12(false);
                        m->observe_ppu_a12(true);
                        break;
                    case 2U:
                        m->observe_ppu_read(0x2000U);
                        break;
                    default:
                        m->observe_cpu_write(0x10U, 0U);
                        break;
                    }
                };
                tick();
                REQUIRE_FALSE(m->irq_pending());
                tick();
                REQUIRE(m->irq_pending());
                tick();
                REQUIRE(m->irq_pending());
                m->write_prg(0xC802U, 0xFFU);
                REQUIRE_FALSE(m->irq_pending());
            }
        }
    }
    for (Byte direction : {0U, 0xC0U}) {
        jy_irq(*m, direction, 0xFFU, 0xFFU);
        m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
    }
    jy_irq(*m, 0x44U, 5U, 0xFFU);
    m->clock_cpu();
    m->write_prg(0xC803U, 0U);
    m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_cpu();
    REQUIRE(m->irq_pending());
    jy_irq(*m, 0x44U, 5U, 0xFFU);
    m->clock_cpu();
    m->write_prg(0xC800U, 0U);
    m->write_prg(0xC803U, 0U);
    for (unsigned i = 0U; i < 7U; ++i)
        m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_cpu();
    REQUIRE(m->irq_pending());
    m->write_prg(0xC002U, 0U);
    m->write_prg(0xC001U, 0x40U);
    m->write_prg(0xC006U, 0xA5U);
    m->write_prg(0xC804U, 0x5AU);
    m->write_prg(0xC805U, 0x5AU);
    m->write_prg(0xC003U, 0U);
    m->clock_cpu();
    REQUIRE(m->irq_pending());
    jy_irq(*m, 0x41U, 0xFEU, 0xFFU);
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE(m->irq_pending());
}
TEST_CASE("JY PPU IRQs count physical fetches and PPUDATA, and CPU IRQs see RAM IO and DMA writes",
          "[mappers][jy]") {
    auto cart = jy_cart();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    const auto arm = [&](Byte mode, Byte pre, Byte count) {
        cart.write_prg(0xC002U, 0U);
        cart.write_prg(0xC001U, mode);
        cart.write_prg(0xC004U, pre);
        cart.write_prg(0xC005U, count);
        cart.write_prg(0xC003U, 0U);
    };
    ppu.write_register(1U, 0x18U);
    ppu.step_dots(341U);
    arm(0x42U, 86U, 0xFFU); // 256 - 170 = 86: one complete scanline.
    for (unsigned i = 0U; i < 1000U; ++i)
        static_cast<void>(cart.read_chr(0U));
    REQUIRE_FALSE(cart.irq_pending());
    ppu.step_dots(338U);
    REQUIRE_FALSE(cart.irq_pending());
    ppu.step_dots(1U);
    REQUIRE(cart.irq_pending());
    ppu.step_dots(2U);
    arm(0x41U, 248U, 0xFFU);
    ppu.write_register(0U, 8U); // BG $0000 / sprites $1000: eight unfiltered rises.
    ppu.step_dots(315U);
    REQUIRE_FALSE(cart.irq_pending());
    ppu.step_dots(1U);
    REQUIRE(cart.irq_pending());
    ppu.write_register(1U, 0U);
    ppu.step_dots(341U);
    arm(0x42U, 0xFFU, 0xFFU);
    ppu.step_dots(341U);
    REQUIRE_FALSE(cart.irq_pending());
    set_ppu_addr(ppu, 0x3F00U);
    static_cast<void>(ppu.read_register(7U));
    REQUIRE(cart.irq_pending());
    // Buffered non-palette read is also exactly one external /RD.
    arm(0x42U, 0xFEU, 0xFFU);
    set_ppu_addr(ppu, 0U);
    static_cast<void>(ppu.read_register(7U));
    REQUIRE_FALSE(cart.irq_pending());
    static_cast<void>(ppu.read_register(7U));
    REQUIRE(cart.irq_pending());
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    arm(0x43U, 0xFDU, 0xFFU);
    bus.write_cpu(0x10U, 1U, false);
    bus.write_cpu(0x4016U, 0U, false);
    REQUIRE_FALSE(cart.irq_pending());
    bus.write_cpu(0x4014U, 0U, false);
    REQUIRE(cart.irq_pending());
    arm(0x43U, 0U, 0xFFU);
    REQUIRE(bus.run_oam_dma(2U, 0x8000U, false) == 513U);
    REQUIRE(cart.irq_pending()); // 256 OAM puts, not just the $4014 request.
}
TEST_CASE("JY metadata, legacy 35 WRAM, ROM overlay and CHR RAM write gate", "[mappers][jy]") {
    for (const uint16_t id : {35U, 90U, 209U, 211U}) {
        auto h = jy_header(id);
        h.is_nes2 = false;
        auto legacy = jy_board(h);
        REQUIRE(legacy->maps_prg(0x6000U) == (id == 35U));
        auto exact = jy_board(jy_header(id));
        REQUIRE_FALSE(exact->maps_prg(0x6000U));
    }
    auto h = jy_header();
    h.prg_nvram_size = 8192U;
    h.has_battery = true;
    h.chr_ram_size = 16384U;
    auto m = jy_board(h, true);
    m->write_prg(0x6000U, 0xA5U);
    REQUIRE(m->battery_ram()[0] == 0xA5U);
    m->write_prg(0xD000U, 0x82U);
    m->write_prg(0x8003U, 5U);
    REQUIRE(m->read_prg(0x6000U) == 5U);
    m->write_prg(0x6000U, 0U);
    m->write_prg(0xD000U, 0U);
    REQUIRE(m->read_prg(0x6000U) == 0xA5U);
    m->write_chr(0U, 0xFFU);
    REQUIRE(m->read_chr(0U) == 0U);
    m->write_prg(0xD002U, 0x40U);
    m->write_chr(0U, 0x55U);
    m->write_prg(0x9000U, 1U);
    m->write_chr(0U, 0xAAU);
    m->write_prg(0x9000U, 0U);
    REQUIRE(m->read_chr(0U) == 0x55U);
    m->write_prg(0xD000U, 0x60U);
    m->write_prg(0xB000U, 0U);
    REQUIRE(m->write_nametable(0x2000U, 0x88U));
    REQUIRE(m->read_chr(0U) == 0x88U);
    for (unsigned bad = 0U; bad < 8U; ++bad) {
        auto invalid = jy_header();
        std::size_t prg = 32768U, chr = 8192U;
        switch (bad) {
        case 0U:
            invalid.submapper = 1U;
            break;
        case 1U:
            prg = 2097152U + 8192U;
            break;
        case 2U:
            chr = 1025U;
            break;
        case 3U:
            invalid.prg_ram_size = 16384U;
            break;
        case 4U:
            invalid.has_battery = true;
            break;
        case 5U:
            invalid.chr_ram_size = 8192U;
            break;
        case 6U:
            invalid.chr_nvram_size = 8192U;
            break;
        default:
            chr = 0U;
            break;
        }
        REQUIRE_FALSE(MapperRegistry::instance().create(
            invalid, std::vector<Byte>(prg), std::vector<Byte>(chr)));
    }
}
TEST_CASE("JY snapshots resume multiplication, latches and partial IRQs and reject corrupt fields",
          "[mappers][jy]") {
    auto m = jy_board();
    m->write_prg(0xD003U, 0x80U);
    m->observe_ppu_read(0xFE8U);
    REQUIRE(m->configure_board_jumpers(3U));
    m->write_expansion(0x5800U, 23U);
    m->write_expansion(0x5801U, 17U);
    jy_irq(*m, 0x40U, 250U, 255U);
    for (unsigned i = 0U; i < 3U; ++i)
        m->clock_cpu();
    StateWriter w;
    m->save_state(w);
    auto clone = jy_board();
    StateReader r(w.data());
    clone->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    for (unsigned i = 0U; i < 5U; ++i) {
        clone->clock_cpu();
        m->clock_cpu();
        REQUIRE(clone->irq_pending() == m->irq_pending());
        StateWriter a, b;
        clone->save_state(a);
        m->save_state(b);
        REQUIRE((a.data() == b.data()));
    }
    REQUIRE(clone->irq_pending());
    REQUIRE(clone->read_expansion(0x5800U) == 135U);
    REQUIRE(clone->read_expansion(0x5801U) == 1U);
    const auto valid = w.data();
    for (const std::size_t offset : {0U, 32U, 33U, 34U, 41U, 50U, 51U, 52U}) {
        auto bad = valid;
        bad[offset] = 0xFFU;
        auto rejected = jy_board();
        StateReader invalid(bad);
        rejected->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
    for (const std::size_t length : std::array<std::size_t, 4>{0U, 4U, 35U, valid.size() - 1U}) {
        auto rejected = jy_board();
        StateReader short_state(std::span{valid}.first(length));
        rejected->load_state(short_state);
        REQUIRE_FALSE(short_state.ok());
    }
}
TEST_CASE("JY machine state replay and malformed mapper rollback for every ID", "[mappers][jy]") {
    for (const uint16_t id :
         {35U, 90U, 209U, 211U, 281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(jy_cart(id), ""));
        emulator.reset();
        emulator.step_frame();
        const auto early = emulator.save_state();
        emulator.step_frame();
        const auto future = emulator.save_state();
        REQUIRE(emulator.load_state(early));
        emulator.step_frame();
        REQUIRE((emulator.save_state() == future));
        // Mapper precedes the two expansion controller payloads. Corrupt its RAM length.
        auto bad = early;
        bad[bad.size() - jy_controller_tail_size() - 1U] = 0xFFU;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
TEST_CASE("JY battery files contain exactly WRAM and reload through the cartridge",
          "[mappers][jy][battery]") {
    const auto root = std::filesystem::temp_directory_path() / "mapperbus-jy-battery";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "jy.sav";
    auto cart = jy_cart(35U, true);
    cart.write_prg(0x6000U, 0x6DU);
    cart.write_prg(0x7FFFU, 0xB2U);
    Emulator writer;
    const auto installed = writer.load_cartridge(std::move(cart), path.string());
    REQUIRE(installed);
    REQUIRE(writer.flush_persistent_data());
    REQUIRE(std::filesystem::file_size(path) == 8192U);
    Emulator restored;
    REQUIRE(restored.load_cartridge(jy_cart(35U, true), path.string()));
    REQUIRE(restored.read_cpu(0x6000U) == 0x6DU);
    REQUIRE(restored.read_cpu(0x7FFFU) == 0xB2U);
    restored.unload_cartridge();
    writer.unload_cartridge();
    std::filesystem::remove_all(root);
}
TEST_CASE("JY PPUDATA latch trigger fetch uses the old bank before switching", "[mappers][jy]") {
    auto image = jy_image();
    for (unsigned bank = 0U; bank < 8U; ++bank)
        std::fill_n(image.begin() + 16 + 32768 + static_cast<std::ptrdiff_t>(bank) * 1024,
                    1024U,
                    static_cast<Byte>(bank));
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    auto cart = std::move(*result);
    cart.write_prg(0xD000U, 8U);
    cart.write_prg(0xD003U, 0x80U);
    cart.write_prg(0x9002U, 1U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    set_ppu_addr(ppu, 0x0FE8U);
    static_cast<void>(ppu.read_register(7U));
    REQUIRE(ppu.read_register(7U) == 3U);
    REQUIRE(ppu.read_register(7U) == 7U);
}
TEST_CASE("JY banked CHR RAM and WRAM snapshot sizes remain bounded", "[mappers][jy]") {
    auto h = jy_header();
    h.chr_ram_size = 16384U;
    h.prg_ram_size = 8192U;
    auto m = jy_board(h, true);
    m->write_prg(0xD002U, 0x40U);
    m->write_chr(0U, 0x55U);
    m->write_prg(0x9000U, 1U);
    m->write_chr(0U, 0xAAU);
    m->write_prg(0x6000U, 0x6DU);
    StateWriter w;
    m->save_state(w);
    auto clone = jy_board(h, true);
    StateReader r(w.data());
    clone->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(clone->read_prg(0x6000U) == 0x6DU);
    REQUIRE(clone->read_chr(0U) == 0xAAU);
    clone->write_prg(0x9000U, 0U);
    REQUIRE(clone->read_chr(0U) == 0x55U);
    REQUIRE(clone->battery_ram().empty());
    for (const std::size_t pos : {52U, 52U + 8U + 8192U}) {
        auto bad = w.data();
        std::fill_n(bad.begin() + static_cast<std::ptrdiff_t>(pos), 8U, 0xFFU);
        StateReader invalid(bad);
        clone->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        static_cast<void>(clone->read_chr(0U));
        static_cast<void>(clone->read_prg(0x6000U));
    }
}
TEST_CASE("JY multicart pin assignments compose with every PRG and CHR mode",
          "[mappers][jy][multicart]") {
    struct Wiring {
        uint16_t id;
        Byte outer;
        unsigned prg_base;
        unsigned prg_mask;
        unsigned chr_base;
        unsigned chr_mask;
    };
    // Expected physical pages (8 KiB PRG / 1 KiB CHR), independently tabulated
    // from the PCB diagrams. High inner bits must never leak into outer banks.
    const std::array cases = {
        Wiring{.id = 281U,
               .outer = 0xFFU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 768U,
               .chr_mask = 255U},
        Wiring{.id = 282U,
               .outer = 0x0FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 256U,
               .chr_mask = 255U},
        Wiring{.id = 282U,
               .outer = 0x1FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 768U,
               .chr_mask = 255U},
        Wiring{.id = 282U,
               .outer = 0x2FU,
               .prg_base = 192U,
               .prg_mask = 63U,
               .chr_base = 0U,
               .chr_mask = 511U},
        Wiring{.id = 282U,
               .outer = 0x3FU,
               .prg_base = 192U,
               .prg_mask = 63U,
               .chr_base = 512U,
               .chr_mask = 511U},
        Wiring{.id = 295U,
               .outer = 0xFFU,
               .prg_base = 112U,
               .prg_mask = 15U,
               .chr_base = 896U,
               .chr_mask = 127U},
        Wiring{.id = 358U,
               .outer = 0x07U,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 768U,
               .chr_mask = 255U},
        Wiring{.id = 358U,
               .outer = 0x27U,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 512U,
               .chr_mask = 511U},
        Wiring{.id = 386U,
               .outer = 0x02U,
               .prg_base = 32U,
               .prg_mask = 31U,
               .chr_base = 0U,
               .chr_mask = 255U},
        Wiring{.id = 386U,
               .outer = 0x04U,
               .prg_base = 0U,
               .prg_mask = 31U,
               .chr_base = 512U,
               .chr_mask = 255U},
        Wiring{.id = 386U,
               .outer = 0x08U,
               .prg_base = 64U,
               .prg_mask = 31U,
               .chr_base = 0U,
               .chr_mask = 255U},
        Wiring{.id = 386U,
               .outer = 0x1FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 768U,
               .chr_mask = 255U},
        Wiring{.id = 386U,
               .outer = 0x3FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 512U,
               .chr_mask = 511U},
        Wiring{.id = 387U,
               .outer = 0x02U,
               .prg_base = 16U,
               .prg_mask = 15U,
               .chr_base = 0U,
               .chr_mask = 255U},
        Wiring{.id = 387U,
               .outer = 0x04U,
               .prg_base = 0U,
               .prg_mask = 15U,
               .chr_base = 512U,
               .chr_mask = 255U},
        Wiring{.id = 387U,
               .outer = 0x08U,
               .prg_base = 32U,
               .prg_mask = 15U,
               .chr_base = 0U,
               .chr_mask = 255U},
        Wiring{.id = 387U,
               .outer = 0x3FU,
               .prg_base = 48U,
               .prg_mask = 15U,
               .chr_base = 512U,
               .chr_mask = 511U},
        Wiring{.id = 388U,
               .outer = 0x02U,
               .prg_base = 0U,
               .prg_mask = 31U,
               .chr_base = 512U,
               .chr_mask = 255U},
        Wiring{.id = 388U,
               .outer = 0x04U,
               .prg_base = 32U,
               .prg_mask = 31U,
               .chr_base = 0U,
               .chr_mask = 255U},
        Wiring{.id = 388U,
               .outer = 0x0FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 768U,
               .chr_mask = 255U},
        Wiring{.id = 388U,
               .outer = 0x2FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 512U,
               .chr_mask = 511U},
        Wiring{.id = 397U,
               .outer = 0x07U,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 896U,
               .chr_mask = 127U},
        Wiring{.id = 397U,
               .outer = 0x3FU,
               .prg_base = 96U,
               .prg_mask = 31U,
               .chr_base = 896U,
               .chr_mask = 127U},
    };
    constexpr std::array<std::array<unsigned, 4>, 4> kPrgPages = {{{300U, 301U, 302U, 303U},
                                                                   {202U, 203U, 150U, 151U},
                                                                   {83U, 101U, 54U, 75U},
                                                                   {101U, 83U, 54U, 105U}}};
    constexpr std::array<unsigned, 4> kLowPages = {303U, 151U, 75U, 105U};
    constexpr std::array<unsigned, 8> kChrRegisters = {
        723U, 314U, 71U, 764U, 373U, 428U, 512U, 511U};
    constexpr std::array<std::array<unsigned, 8>, 4> kChrPages = {
        {{5784U, 5785U, 5786U, 5787U, 5788U, 5789U, 5790U, 5791U},
         {2892U, 2893U, 2894U, 2895U, 1492U, 1493U, 1494U, 1495U},
         {1446U, 1447U, 142U, 143U, 746U, 747U, 1024U, 1025U},
         kChrRegisters}};
    for (const auto& c : cases) {
        INFO(c.id << " outer " << static_cast<unsigned>(c.outer));
        auto m = jy_board(jy_header(c.id));
        for (unsigned reg = 0U; reg < 4U; ++reg)
            m->write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(kPrgPages[2][reg]));
        for (unsigned reg = 0U; reg < 8U; ++reg) {
            m->write_prg(static_cast<Address>(0x9000U + reg),
                         static_cast<Byte>(kChrRegisters[reg]));
            m->write_prg(static_cast<Address>(0xA000U + reg),
                         static_cast<Byte>(kChrRegisters[reg] >> 8U));
        }
        m->write_prg(0xD003U, c.outer);
        for (unsigned mode = 0U; mode < 4U; ++mode) {
            m->write_prg(0xD000U, static_cast<Byte>(0x84U | mode | (mode << 3U)));
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        (c.prg_base | (kPrgPages[mode][slot] & c.prg_mask)));
            REQUIRE(m->read_prg(0x6000U) == (c.prg_base | (kLowPages[mode] & c.prg_mask)));
            for (unsigned slot = 0U; slot < 8U; ++slot)
                REQUIRE(chr_index(*m, static_cast<Address>(slot * 1024U)) ==
                        (c.chr_base | (kChrPages[mode][slot] & c.chr_mask)));
            m->write_prg(0xD000U, static_cast<Byte>(mode));
            const unsigned first_fixed = mode == 0U ? 0U : mode == 1U ? 2U : 3U;
            for (unsigned slot = first_fixed; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        (c.prg_base | (c.prg_mask - 3U + slot)));
        }
    }
}
TEST_CASE("JY multicart outer register aliases preserve common ASIC register decoding",
          "[mappers][jy][multicart]") {
    for (const uint16_t id : {281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        auto m = jy_board(jy_header(id));
        m->write_prg(0xD003U, 0U);
        StateWriter before;
        m->save_state(before);
        m->write_prg(0xDFFFU, 0x27U);
        StateWriter after;
        m->save_state(after);
        REQUIRE((before.data() == after.data()) == (id == 358U));
        auto exact = jy_board(jy_header(id));
        exact->write_prg(0xD003U, id == 358U ? 0U : 0x27U);
        StateWriter expected;
        exact->save_state(expected);
        REQUIRE((after.data() == expected.data()));
        m->write_prg(0xD800U, 0xFFU);
        m->write_prg(0x8800U, 0xFFU);
        m->write_prg(0x9800U, 0xFFU);
        m->write_prg(0xA800U, 0xFFU);
        m->write_prg(0xB800U, 0xFFU);
        StateWriter ignored;
        m->save_state(ignored);
        REQUIRE((ignored.data() == after.data()));
    }
}
TEST_CASE("JY 397 retains PRG A17 while 295 masks it, and large 282 banking changes the window",
          "[mappers][jy][multicart]") {
    auto small = jy_board(jy_header(295U));
    auto wide = jy_board(jy_header(397U));
    for (auto* m : {small.get(), wide.get()}) {
        m->write_prg(0xD000U, 6U);
        m->write_prg(0x8000U, 16U);
        m->write_prg(0xD003U, 2U);
    }
    REQUIRE(small->read_prg(0x8000U) == 32U);
    REQUIRE(wide->read_prg(0x8000U) == 48U);
    auto large = jy_board(jy_header(282U));
    large->write_prg(0xD000U, 6U);
    large->write_prg(0x8000U, 32U);
    large->write_prg(0xD003U, 0U);
    REQUIRE(large->read_prg(0x8000U) == 0U);
    large->write_prg(0xD003U, 0x20U);
    REQUIRE(large->read_prg(0x8000U) == 32U);
}
TEST_CASE("JY multicart IRQs and snapshots retain independent outer, latch and timer state",
          "[mappers][jy][multicart]") {
    for (const uint16_t id : {281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        for (Byte source = 0U; source < 4U; ++source) {
            auto m = jy_board(jy_header(id));
            m->write_prg(0xD000U, 0x0EU);
            m->write_prg(0xD003U, 0x9FU);
            m->write_prg(0x8000U, 0x36U);
            m->write_prg(0x9002U, 5U);
            m->observe_ppu_read(0xFE8U);
            jy_irq(*m, 0x40U | source, 0xFEU, 0xFFU);
            const auto tick = [source](Mapper& mapper) {
                if (source == 0U)
                    mapper.clock_cpu();
                else if (source == 1U) {
                    mapper.observe_ppu_a12(false);
                    mapper.observe_ppu_a12(true);
                } else if (source == 2U)
                    mapper.observe_ppu_read(0x2000U);
                else
                    mapper.observe_cpu_write(0x4016U, 0U);
            };
            tick(*m);
            REQUIRE_FALSE(m->irq_pending());
            StateWriter w;
            m->save_state(w);
            auto clone = jy_board(jy_header(id));
            StateReader r(w.data());
            clone->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(r.remaining() == 0U);
            REQUIRE(clone->read_prg(0x8000U) == m->read_prg(0x8000U));
            REQUIRE(chr_index(*clone, 0U) == chr_index(*m, 0U));
            clone->reset();
            tick(*clone);
            tick(*m);
            REQUIRE(clone->irq_pending());
            StateWriter a, b;
            clone->save_state(a);
            m->save_state(b);
            REQUIRE((a.data() == b.data()));
        }
    }
}
TEST_CASE("JY multicart metadata bounds follow the decoded PCB address pins",
          "[mappers][jy][multicart]") {
    for (const uint16_t id : {281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        auto h = jy_header(id);
        const std::size_t prg_max = id == 387U ? 524288U : id == 282U ? 2097152U : 1048576U;
        for (unsigned bad = 0U; bad < 5U; ++bad) {
            auto invalid = h;
            std::size_t prg = 32768U, chr = 8192U;
            switch (bad) {
            case 0U:
                invalid.is_nes2 = false;
                break;
            case 1U:
                invalid.submapper = 1U;
                break;
            case 2U:
                prg = prg_max + 8192U;
                break;
            case 3U:
                chr = 1048576U + 1024U;
                break;
            default:
                invalid.chr_ram_size = 8192U;
                break;
            }
            REQUIRE_FALSE(MapperRegistry::instance().create(
                invalid, std::vector<Byte>(prg), std::vector<Byte>(chr)));
        }
    }
}
TEST_CASE("JY multicart CPU outer switches and IRQ handlers replay full-machine state",
          "[mappers][jy][multicart]") {
    for (const uint16_t id : {281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U}) {
        const Byte outer = id == 281U                               ? 3U
                           : id == 388U                             ? 15U
                           : id == 282U || id == 386U || id == 387U ? 31U
                                                                    : 7U;
        const std::size_t prg_size = id == 387U ? 524288U : 1048576U;
        std::vector<Byte> image(16U + prg_size + 1048576U);
        const auto header = jy_image(id);
        std::copy_n(header.begin(), 16U, image.begin());
        image[4] = static_cast<Byte>(prg_size / 16384U);
        image[5] = 128U;
        image[10] = 7U;
        std::vector<Byte> code = {0x78U};
        const auto store = [&code](Address addr, Byte value) {
            code.insert(
                code.end(),
                {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
        };
        store(0xD000U, 2U);
        store(0x8000U, 16U);
        store(id == 358U ? 0xD003U : 0xD803U, outer);
        code.insert(code.end(), {0xADU, 0U, 0x80U, 0x8DU, 0U, 0x60U});
        store(0xC001U, 0x40U);
        store(0xC004U, 0xF0U);
        store(0xC005U, 0xFFU);
        store(0xC003U, 1U);
        code.push_back(0x58U);
        const auto loop = static_cast<Address>(0xE000U + code.size());
        code.insert(
            code.end(),
            {0xEEU, 1U, 0x60U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
        constexpr std::array<Byte, 12> kIrq = {
            0x48U, 0xA9U, 0U, 0x8DU, 2U, 0xC0U, 0xEEU, 2U, 0x60U, 0x68U, 0x40U, 0xEAU};
        for (std::size_t page = 0U; page < prg_size; page += 8192U)
            image[16U + page] = static_cast<Byte>(page / 8192U);
        for (std::size_t block = 16U; block < 16U + prg_size; block += 131072U) {
            const auto boot = image.begin() + static_cast<std::ptrdiff_t>(block + 0x1E000U);
            std::ranges::copy(code, boot);
            std::ranges::copy(kIrq, boot + 256);
            for (std::size_t vector = 0x1FFFAU; vector < 0x20000U; vector += 2U) {
                image[block + vector] = 0U;
                image[block + vector + 1U] = vector == 0x1FFFCU ? 0xE0U : 0xE1U;
            }
        }
        register_builtin_mappers();
        auto cart = Cartridge::from_data(image);
        REQUIRE(cart);
        Emulator emulator;
        const auto loaded = emulator.load_cartridge(std::move(*cart), "");
        REQUIRE(loaded);
        emulator.reset();
        emulator.step_frame();
        INFO("Mapper " << id);
        REQUIRE(emulator.read_cpu(0x6000U) == (id == 387U ? 48U : 112U));
        REQUIRE(emulator.read_cpu(0x6002U) == 1U);
        const auto early = emulator.save_state();
        emulator.step_frame();
        const auto future = emulator.save_state();
        REQUIRE(emulator.load_state(early));
        emulator.step_frame();
        REQUIRE((emulator.save_state() == future));
        auto bad = early;
        bad[bad.size() - jy_controller_tail_size() - 8192U - 1U] =
            0xFFU; // Oversized WRAM length, before its bytes.
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
} // namespace
