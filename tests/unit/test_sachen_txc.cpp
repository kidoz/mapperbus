#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::array<uint16_t, 16> kBoards = {
    36, 132, 133, 135, 136, 137, 138, 139, 141, 143, 146, 147, 150, 172, 173, 243};
bool txc_id(uint16_t id) {
    return id == 36U || id == 132U || id == 136U || id == 147U || id == 172U || id == 173U;
}
std::size_t prg_size(uint16_t id) {
    if (id == 143U || id == 172U || id == 173U)
        return 32768U;
    if (id == 132U || id == 133U || id == 136U || id == 146U)
        return 65536U;
    return id == 36U || id == 147U || id == 150U || id == 243U ? 131072U : 262144U;
}
std::size_t chr_size(uint16_t id) {
    if (id == 143U)
        return 8192U;
    if (id == 132U || id == 133U || id == 137U || id == 172U)
        return 32768U;
    if (id == 136U || id == 146U || id == 150U || id == 173U)
        return 65536U;
    if (id == 139U)
        return 524288U;
    return id == 135U || id == 141U ? 262144U : 131072U;
}
INesHeader header(uint16_t id) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; i += unit) {
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(i), unit, static_cast<Byte>(i / unit));
        data[i + 1U] = static_cast<Byte>((i / unit) >> 8U);
    }
    return data;
}
std::unique_ptr<Mapper> board(uint16_t id, std::size_t prg = 0U, std::size_t chr = 0U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(header(id),
                                               banks(prg == 0U ? prg_size(id) : prg, 32768U),
                                               banks(chr == 0U ? chr_size(id) : chr, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
void reg(Mapper& m, Byte index, Byte value, Address port = 0x4100U) {
    m.write_expansion(port, index);
    m.write_expansion(port + 1U, value);
}
unsigned chr_page(Mapper& m, Address addr) {
    return m.read_chr(addr) | (static_cast<unsigned>(m.read_chr(addr + 1U)) << 8U);
}
std::vector<Byte> snapshot(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void ppu_addr(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_addr(ppu, addr);
    static_cast<void>(ppu.read_register(7U));
    return ppu.read_register(7U);
}
void ppu_write(Ppu& ppu, Address addr, Byte v) {
    ppu_addr(ppu, addr);
    ppu.write_register(7U, v);
}

TEST_CASE("Sachen 133 and 146 decode independent latch bit assignments without ROM conflicts",
          "[mappers][sachen]") {
    for (const uint16_t id : {133U, 146U}) {
        auto m = board(id);
        for (unsigned v = 0U; v < 256U; ++v) {
            m->write_expansion(0x5FFFU, static_cast<Byte>(v));
            REQUIRE(m->read_prg(0x8000U) == ((v >> (id == 133U ? 2U : 3U)) & 1U));
            REQUIRE(chr_page(*m, 0U) == (v & (id == 133U ? 3U : 7U)) * 8U);
            m->write_prg(0xFFFFU, 0U);
            m->write_expansion(0x4000U, 0U);
            REQUIRE(chr_page(*m, 0U) == (v & (id == 133U ? 3U : 7U)) * 8U);
        }
        REQUIRE(m->expansion_read_mask(0x4100U) == 0U);
        REQUIRE_FALSE(m->maps_expansion(0x6000U));
    }
}
TEST_CASE("Sachen 8259 rewired CHR units preserve PPU address leakage in simple mode",
          "[mappers][sachen]") {
    struct Row {
        uint16_t id;
        std::array<unsigned, 4> normal;
        std::array<unsigned, 4> simple;
    };
    const std::array rows = {
        Row{.id = 135U, .normal = {164U, 170U, 172U, 178U}, .simple = {164U, 166U, 164U, 166U}},
        Row{.id = 138U, .normal = {82U, 84U, 86U, 88U}, .simple = {82U, 82U, 82U, 82U}},
        Row{.id = 139U, .normal = {328U, 338U, 348U, 358U}, .simple = {328U, 330U, 332U, 334U}},
        Row{.id = 141U, .normal = {164U, 170U, 172U, 178U}, .simple = {164U, 166U, 164U, 166U}}};
    for (const auto& row : rows) {
        auto m = board(row.id);
        for (Byte i = 0U; i < 4U; ++i)
            reg(*m, i, i + 1U);
        reg(*m, 4U, 5U);
        reg(*m, 5U, 7U);
        REQUIRE(m->read_prg(0x8000U) == 7U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto a = static_cast<Address>(slot * 2048U);
            REQUIRE(chr_page(*m, a) == row.normal[slot]);
            REQUIRE(chr_page(*m, a + 1024U) == row.normal[slot] + 1U);
        }
        reg(*m, 7U, 7U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            REQUIRE(chr_page(*m, static_cast<Address>(slot * 2048U)) == row.simple[slot]);
        m->write_prg(0x6100U, 5U);
        m->write_prg(0x6101U, 3U);
        REQUIRE(m->read_prg(0x8000U) == 3U);
        m->write_prg(0x8101U, 0U);
        REQUIRE(m->read_prg(0x8000U) == 3U);
        REQUIRE(m->expansion_read_mask(0x4101U) == 0U);
    }
}
TEST_CASE("Sachen 8259D has independent top bits and a fixed final four KiB", "[mappers][sachen]") {
    auto m = board(137U);
    for (Byte i = 0U; i < 4U; ++i)
        reg(*m, i, 7U - i);
    reg(*m, 4U, 7U);
    reg(*m, 6U, 1U);
    constexpr std::array<unsigned, 8> kNormal = {7, 22, 21, 28, 28, 29, 30, 31};
    constexpr std::array<unsigned, 8> kSimple = {7, 23, 23, 31, 28, 29, 30, 31};
    for (unsigned i = 0U; i < 8U; ++i)
        REQUIRE(chr_page(*m, static_cast<Address>(i * 1024U)) == kNormal[i]);
    reg(*m, 7U, 7U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned i = 0U; i < 8U; ++i)
        REQUIRE(chr_page(*m, static_cast<Address>(i * 1024U)) == kSimple[i]);
    reg(*m, 7U, 2U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
}
TEST_CASE("Sachen asymmetric CIRAM arrangements differ between 8259 and 74LS374 boards",
          "[mappers][sachen]") {
    for (const uint16_t id : {135U, 137U, 138U, 139U, 141U, 150U, 243U}) {
        auto m = board(id);
        const bool readable = id == 150U || id == 243U;
        for (Byte mode = 0U; mode < 4U; ++mode) {
            reg(*m, 7U, mode << 1U);
            for (unsigned t = 0U; t < 4U; ++t) {
                unsigned page = 0U;
                if (readable)
                    page = mode == 0U   ? (t == 3U ? 1U : 0U)
                           : mode == 1U ? t / 2U
                           : mode == 2U ? t % 2U
                                        : 1U;
                else
                    page = mode == 2U                     ? (t == 0U ? 0U : 1U)
                           : mode == 3U                   ? 0U
                           : (mode == 0U) != (id == 137U) ? t % 2U
                                                          : t / 2U;
                REQUIRE(m->nametable_address(static_cast<Address>(0x2003U + t * 1024U)) ==
                        page * 1024U + 3U);
            }
        }
    }
}
TEST_CASE("Sachen readable registers, CHR wiring and D2 solder pad survive CPU bus aliases",
          "[mappers][sachen]") {
    for (const uint16_t id : {150U, 243U}) {
        auto m = board(id);
        for (Byte i = 0U; i < 8U; ++i) {
            reg(*m, i, static_cast<Byte>(i ^ 7U));
            REQUIRE(m->read_expansion(0x5FFFU) == (i ^ 7U));
        }
        reg(*m, 2U, 1U);
        reg(*m, 4U, 1U);
        reg(*m, 5U, 3U);
        reg(*m, 6U, 3U);
        REQUIRE(m->read_prg(0x8000U) == 3U);
        REQUIRE(chr_page(*m, 0U) == (id == 150U ? 56U : 120U));
        REQUIRE(m->expansion_read_mask(0x4100U) == 0U);
        REQUIRE(m->expansion_read_mask(0x4101U) == 7U);
        REQUIRE(m->maps_prg(0x6101U));
        REQUIRE(m->prg_read_mask(0x6101U) == 7U);
    }
    auto m = board(150U);
    REQUIRE(m->configure_board_jumpers(1U));
    reg(*m, 2U, 1U);
    REQUIRE(m->read_expansion(0x4101U) == 5U);
    REQUIRE(m->expansion_read_mask(0x4101U) == 3U);
    REQUIRE_FALSE(m->configure_board_jumpers(2U));
    REQUIRE(m->configure_board_jumpers(0U));
    m->write_expansion(0x4100U, 6U);
    REQUIRE(m->read_expansion(0x4101U) == 5U);
}
TEST_CASE(
    "TXC protection stages input, counts with chip width and commits output only on high writes",
    "[mappers][txc]") {
    auto m = board(36U);
    m->write_expansion(0x4102U, 0x20U);
    REQUIRE(m->read_expansion(0x4100U) == 0U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_expansion(0x4100U) == 0x20U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 0U);
    REQUIRE(m->read_prg(0x8000U) == 2U);
    m->write_expansion(0x4200U, 5U);
    REQUIRE(chr_page(*m, 0U) == 40U);
    m->write_expansion(0x4302U, 0x31U); // Both CHR and protection input decoders.
    REQUIRE(chr_page(*m, 0U) == 8U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_expansion(0x4100U) == 0x30U);
    m->write_expansion(0x4103U, 0x10U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_expansion(0x4100U) == 0U);
    REQUIRE(m->read_prg(0x8000U) == 2U);
    auto a = board(132U);
    a->write_expansion(0x4102U, 14U);
    REQUIRE(a->read_expansion(0x4100U) == 8U);
    a->write_expansion(0x4101U, 1U);
    REQUIRE(a->read_expansion(0x4100U) == 0U);
    a->write_expansion(0x4100U, 0U);
    REQUIRE(a->read_expansion(0x4100U) == 1U);
    a->write_prg(0xFFFFU, 0U);
    REQUIRE(chr_page(*a, 0U) == 8U);
    a->write_expansion(0x4101U, 0U);
    a->write_expansion(0x4102U, 5U);
    a->write_expansion(0x4100U, 0U);
    a->write_prg(0x8000U, 0U);
    REQUIRE(a->read_prg(0x8000U) == 1U);
    REQUIRE(chr_page(*a, 0U) == 8U);
}
TEST_CASE("JV001 increments only its low nibble and applies separate read and load inversion",
          "[mappers][txc]") {
    for (const uint16_t id : {136U, 147U}) {
        auto m = board(id);
        const unsigned shift = id == 147U ? 2U : 0U;
        m->write_expansion(0x4102U, static_cast<Byte>(63U << shift));
        m->write_expansion(0x4100U, 0U);
        REQUIRE(m->read_expansion(0x4103U) == (63U << shift));
        m->write_prg(0x8000U, 0U);
        REQUIRE(m->read_prg(0x8000U) == (id == 147U ? 3U : 1U));
        REQUIRE(chr_page(*m, 0U) == (id == 147U ? 120U : 56U));
        m->write_expansion(0x4103U, static_cast<Byte>(1U << shift));
        m->write_expansion(0x4100U, 0U);
        REQUIRE(m->read_expansion(0x4100U) == (48U << shift));
        m->write_expansion(0x4101U, static_cast<Byte>(1U << shift));
        REQUIRE(m->read_expansion(0x4100U) == 0U);
        m->write_expansion(0x4103U, 0U);
        m->write_expansion(0x4102U, static_cast<Byte>(0x12U << shift));
        m->write_expansion(0x4100U, 0U);
        REQUIRE(m->read_expansion(0x4100U) == (0x2DU << shift));
    }
}
TEST_CASE("Mapper 147 small latch PCB decodes low and high aliases with high-ROM conflicts",
          "[mappers][txc]") {
    register_builtin_mappers();
    auto prg = banks(65536U, 32768U);
    for (std::size_t b = 0U; b < prg.size(); b += 32768U) {
        prg[b + 0x4102U] = 0xFFU;
        prg[b + 0x4106U] = 0U;
    }
    auto result =
        MapperRegistry::instance().create(header(147U), std::move(prg), banks(131072U, 1024U));
    REQUIRE(result);
    auto& m = **result;
    m.write_expansion(0x4102U, 0x7CU);
    REQUIRE(m.read_prg(0x8000U) == 1U);
    REQUIRE(chr_page(m, 0U) == 120U);
    m.write_prg(0x8000U, 0U);
    REQUIRE(m.read_prg(0x8000U) == 1U);
    m.write_prg(0xC102U, 0x04U);
    REQUIRE(m.read_prg(0x8000U) == 1U);
    REQUIRE(chr_page(m, 0U) == 0U);
    m.write_prg(0xC106U, 0xFCU);
    REQUIRE(m.read_prg(0x8000U) == 0U);
    m.write_prg(0x6102U, 0x7CU);
    REQUIRE(chr_page(m, 0U) == 120U);
    REQUIRE(m.expansion_read_mask(0x4102U) == 0U);
}
TEST_CASE("Mapper 172 reverses six data pins and latches mirroring with output", "[mappers][txc]") {
    auto m = board(172U);
    m->write_expansion(0x4102U, 1U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_expansion(0x4100U) == 1U);
    m->write_prg(0xFFFFU, 0U);
    REQUIRE(chr_page(*m, 0U) == 8U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_expansion(0x4101U, 0x20U);
    REQUIRE(m->read_expansion(0x4100U) == 2U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0x8000U, 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->write_expansion(0x4101U, 0U);
    m->write_expansion(0x4102U, 3U);
    m->write_expansion(0x4100U, 0U);
    m->write_expansion(0x4103U, 0x20U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_expansion(0x4100U) == 35U);
}
TEST_CASE("Mapper 173 uses an immediate inverted CHR pin and models its UVEPROM disable",
          "[mappers][txc]") {
    auto m = board(173U);
    m->write_expansion(0x4102U, 2U);
    m->write_expansion(0x4100U, 0U);
    m->write_prg(0x8000U, 0U);
    REQUIRE(chr_page(*m, 0U) == 48U);
    m->write_expansion(0x4101U, 1U);
    REQUIRE(chr_page(*m, 0U) == 32U);
    auto small = board(173U, 32768U, 8192U);
    REQUIRE(small->read_chr(0U) == 0U);
    small->write_expansion(0x4101U, 1U);
    REQUIRE(small->read_chr(0U) == 0xFFU);
    small->write_expansion(0x4101U, 0U);
    REQUIRE(small->read_chr(0U) == 0U);
}
TEST_CASE("Sachen and TXC state rejects invalid flags and resumes staged operations",
          "[mappers][sachen][txc]") {
    for (auto id : kBoards) {
        auto m = board(id);
        m->write_expansion(0x4102U, 0x15U);
        m->write_expansion(0x4100U, 3U);
        if (id == 150U)
            REQUIRE(m->configure_board_jumpers(1U));
        const auto state = snapshot(*m);
        auto copy = board(id);
        StateReader r(state);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        copy->reset();
        REQUIRE((snapshot(*copy) == state));
        for (auto* x : {m.get(), copy.get()}) {
            x->write_expansion(0x4100U, 0U);
            x->write_prg(0xFFFFU, 0U);
        }
        REQUIRE((snapshot(*m) == snapshot(*copy)));
        REQUIRE(m->read_prg(0x8000U) == copy->read_prg(0x8000U));
        auto bad = state;
        bad[txc_id(id) ? 5U : 8U] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        StateReader short_state(std::span{state}.first(state.size() - 1U));
        copy->load_state(short_state);
        REQUIRE_FALSE(short_state.ok());
    }
}
std::vector<Byte> image_for(uint16_t id, std::span<const Byte> code = {}, bool ram = false) {
    const auto prg = prg_size(id), chr = ram ? 0U : chr_size(id);
    std::vector<Byte> image(16U + prg + chr);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = static_cast<Byte>(prg / 16384U);
    image[5] = static_cast<Byte>(chr / 8192U);
    image[6] = static_cast<Byte>((id & 15U) << 4U);
    image[7] = static_cast<Byte>((id & 0xF0U) | 8U);
    if (ram)
        image[11] = 7U;
    const auto p = banks(prg, 32768U), c = banks(chr, 1024U);
    std::ranges::copy(p, image.begin() + 16);
    std::ranges::copy(c, image.begin() + static_cast<std::ptrdiff_t>(16U + prg));
    constexpr std::array<Byte, 3> kLoop = {0x4CU, 0U, 0xE0U};
    if (code.empty())
        code = kLoop;
    for (std::size_t bank = 16U; bank < 16U + prg; bank += 32768U) {
        std::ranges::copy(code, image.begin() + static_cast<std::ptrdiff_t>(bank + 0x6000U));
        for (std::size_t v = 0x7FFAU; v < 0x8000U; v += 2U) {
            image[bank + v] = 0U;
            image[bank + v + 1U] = 0xE0U;
        }
    }
    return image;
}
Cartridge cart_for(uint16_t id, bool ram = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image_for(id, {}, ram));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Sachen physical CIRAM routing respects asymmetric modes and the erroneous 137 header",
          "[mappers][sachen]") {
    auto cart = cart_for(150U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    ppu_write(ppu, 0x2000U, 0x11U);
    ppu_write(ppu, 0x2C00U, 0x22U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x11U);
    REQUIRE(ppu_read(ppu, 0x2800U) == 0x11U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x22U);
    cart.write_expansion(0x4100U, 7U);
    cart.write_expansion(0x4101U, 4U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x22U);
    REQUIRE(ppu_read(ppu, 0x3000U) == 0x11U);
    auto d = image_for(137U);
    d[6] |= 8U;
    auto parsed = Cartridge::from_data(d);
    REQUIRE(parsed);
    auto great_wall = std::move(*parsed);
    Ppu other;
    other.connect_cartridge(&great_wall);
    great_wall.write_expansion(0x4100U, 7U);
    great_wall.write_expansion(0x4101U, 4U);
    ppu_write(other, 0x2000U, 0x33U);
    ppu_write(other, 0x2400U, 0x44U);
    REQUIRE(ppu_read(other, 0x2800U) == 0x44U);
    REQUIRE(ppu_read(other, 0x2C00U) == 0x44U);
    REQUIRE(ppu_read(other, 0x3000U) == 0x33U);
}
TEST_CASE("Sachen Q-Boy has unbanked volatile CHR RAM with snapshot persistence",
          "[mappers][sachen]") {
    auto cart = cart_for(141U, true);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    ppu_write(ppu, 0x0010U, 0x5AU);
    ppu_write(ppu, 0x1FFFU, 0xA5U);
    for (Byte i = 0U; i < 8U; ++i) {
        cart.write_expansion(0x4100U, i);
        cart.write_expansion(0x4101U, 7U);
    }
    REQUIRE(ppu_read(ppu, 0x0010U) == 0x5AU);
    REQUIRE(ppu_read(ppu, 0x1FFFU) == 0xA5U);
    StateWriter w;
    cart.save_state(w);
    auto copy = cart_for(141U, true);
    StateReader r(w.data());
    copy.load_state(r);
    REQUIRE(r.ok());
    REQUIRE(copy.read_chr(0x10U) == 0x5AU);
    REQUIRE(copy.battery_ram().empty());
    REQUIRE_FALSE(copy.has_battery());
    auto truncated = w.data();
    truncated.pop_back();
    StateReader bad(truncated);
    copy.load_state(bad);
    REQUIRE_FALSE(bad.ok());
}
TEST_CASE("Protection reads merge only driven CPU data bits, including low PRG-window aliases",
          "[mappers][sachen][txc]") {
    auto cart = cart_for(150U);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0x6100U, 6U);
    bus.write(0x6101U, 5U);
    bus.write(0x10U, 0xA0U);
    REQUIRE(bus.read(0x6101U) == 0xA5U);
    REQUIRE(cart.configure_board_jumpers(1U));
    bus.write(0x10U, 0xA0U);
    REQUIRE(bus.read(0x6101U) == 0xA1U);
    auto protection = cart_for(143U);
    bus.connect_cartridge(&protection);
    bus.write(0x10U, 0xC0U);
    REQUIRE(bus.read(0x4115U) == 0xEAU);
    REQUIRE_FALSE(protection.maps_prg(0x6000U));
    for (const uint16_t id : {36U, 132U, 136U, 147U, 172U, 173U}) {
        auto c = cart_for(id);
        bus.connect_cartridge(&c);
        bus.write(0x4102U, 0U);
        bus.write(0x4100U, 0U);
        bus.write(0x10U, 0xFFU);
        const Byte mask = id == 36U                  ? 0x30U
                          : id == 147U               ? 0xFCU
                          : id == 136U || id == 172U ? 0x3FU
                                                     : 0x0FU;
        REQUIRE(bus.read(0x4100U) == static_cast<Byte>(~mask));
    }
}
TEST_CASE("Sachen and TXC metadata rejects unsupported memory and submapper declarations",
          "[mappers][sachen][txc]") {
    register_builtin_mappers();
    for (const auto id : kBoards) {
        for (unsigned kind = 0U; kind < 6U; ++kind) {
            auto h = header(id);
            auto prg = prg_size(id), chr = chr_size(id);
            switch (kind) {
            case 0U:
                h.submapper = 1U;
                break;
            case 1U:
                h.has_battery = true;
                break;
            case 2U:
                h.prg_ram_size = 8192U;
                break;
            case 3U:
                h.chr_nvram_size = 8192U;
                break;
            case 4U:
                prg += 16384U;
                break;
            default:
                chr += 1024U;
                break;
            }
            REQUIRE_FALSE(MapperRegistry::instance().create(
                h, std::vector<Byte>(prg), std::vector<Byte>(chr)));
        }
    }
}
TEST_CASE("Original Sachen and TXC CPU programs exercise banking, protection and state rollback",
          "[mappers][sachen][txc]") {
    register_builtin_mappers();
    for (const auto id : kBoards) {
        INFO("Mapper " << id);
        std::vector<Byte> code{0x78U};
        unsigned prg_bank = 0U, chr_bank_page = 0U;
        const auto store = [&code](Address a, Byte v) {
            code.insert(code.end(),
                        {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
        };
        const auto expect = [&code](Address a, Byte mask, Byte v) {
            code.insert(code.end(),
                        {0xADU,
                         static_cast<Byte>(a),
                         static_cast<Byte>(a >> 8U),
                         0x29U,
                         mask,
                         0xC9U,
                         v,
                         0xD0U,
                         0xFEU});
        };
        const auto set_reg = [&store](Byte r, Byte v) {
            store(0x4100U, r);
            store(0x4101U, v);
        };
        if (id == 133U || id == 146U) {
            store(0x4100U, id == 133U ? 7U : 15U);
            prg_bank = 1U;
            chr_bank_page = id == 133U ? 24U : 56U;
        } else if (id == 143U)
            expect(0x4115U, 0x3FU, 0x2AU);
        else if (id == 150U || id == 243U) {
            set_reg(2U, 1U);
            set_reg(4U, 1U);
            set_reg(5U, 2U);
            set_reg(6U, 3U);
            expect(0x4101U, 7U, 3U);
            prg_bank = 2U;
            chr_bank_page = id == 150U ? 56U : 120U;
        } else if (!txc_id(id)) {
            set_reg(0U, 1U);
            set_reg(4U, 0U);
            set_reg(5U, 2U);
            prg_bank = 2U;
            chr_bank_page = id == 137U ? 1U : id == 138U ? 2U : id == 139U ? 8U : 4U;
        } else {
            const Byte data = id == 36U    ? 0x20U
                              : id == 132U ? 6U
                              : id == 136U ? 0x13U
                              : id == 147U ? 0xFCU
                              : id == 172U ? 3U
                                           : 1U;
            store(0x4102U, data);
            store(0x4100U, 0U);
            store(0x8000U, 0U);
            const Byte mask = id == 36U                  ? 0x30U
                              : id == 147U               ? 0xFCU
                              : id == 136U || id == 172U ? 0x3FU
                                                         : 0x0FU;
            expect(0x4100U, mask, data);
            prg_bank = id == 36U ? 2U : id == 132U || id == 136U ? 1U : id == 147U ? 3U : 0U;
            chr_bank_page = id == 132U ? 16U : id == 147U ? 120U : 24U;
            if (id == 36U)
                store(0x4200U, 3U);
        }
        expect(0x8000U, 0xFFU, static_cast<Byte>(prg_bank));
        store(0x2006U, 0U);
        store(0x2006U, 0U);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, 0xFFU, static_cast<Byte>(chr_bank_page));
        code.insert(code.end(), {0xA9U, 0x5AU, 0x85U, 0x10U});
        const auto loop = static_cast<Address>(0xE000U + code.size());
        code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
        auto parsed = Cartridge::from_data(image_for(id, code));
        REQUIRE(parsed);
        Emulator emulator;
        const auto loaded = emulator.load_cartridge(std::move(*parsed), "");
        REQUIRE(loaded);
        emulator.reset();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x10U) == 0x5AU);
        const auto early = emulator.save_state();
        emulator.step_frame();
        const auto future = emulator.save_state();
        REQUIRE(emulator.load_state(early));
        emulator.step_frame();
        REQUIRE((emulator.save_state() == future));
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        const std::size_t payload = txc_id(id) ? 8U : 11U;
        auto bad = early;
        bad[bad.size() - tail.data().size() - payload + (txc_id(id) ? 5U : 8U)] = 0xFFU;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
} // namespace
