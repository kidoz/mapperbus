#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::array<uint16_t, 61> kNewBoards = {
    10,  13,  34,  66,  70,  71,  76,  78,  79,  87,  88,  89,  93,  94,  95,  97,
    113, 140, 152, 154, 180, 184, 206, 38,  46,  58,  61,  62,  81,  107, 145, 148,
    149, 193, 200, 201, 203, 212, 213, 225, 226, 229, 231, 232, 240, 21,  22,  23,
    25,  73,  75,  28,  31,  32,  65,  67,  68,  33,  48,  80,  207};

std::vector<Byte> bank_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0; offset < size; ++offset)
        rom[offset] = static_cast<Byte>(offset / bank_size);
    // Conflict-safe register write locations in every 4 KiB page. A separate
    // location intentionally drives all zero bits for the electrical tests.
    for (std::size_t offset = 0; offset < size; offset += 0x1000) {
        rom[offset + 0x100] = 0xFF;
        rom[offset + 0x101] = 0;
    }
    return rom;
}

INesHeader board_header(uint16_t id, uint8_t submapper = 0) {
    INesHeader header{};
    header.mapper_number = id;
    header.submapper = submapper;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}

std::unique_ptr<Mapper> board(uint16_t id, uint8_t submapper = 0, bool ram = false) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        board_header(id, submapper),
        bank_rom(0x40000, 0x4000),
        ram ? std::vector<Byte>{} : bank_rom(id == 67 ? 0x20000 : 0x40000, 0x1000));
    REQUIRE(result);
    return std::move(*result);
}

void set_namco_bank(Mapper& mapper, Byte reg, Byte value) {
    mapper.write_prg(0x8000, reg);
    mapper.write_prg(0x8001, value);
}

Cartridge cartridge(uint16_t id) {
    register_builtin_mappers();
    std::vector<Byte> image(16 + 0x40000 + 0x40000, 0xEA);
    std::fill_n(image.begin(), 16, 0);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1A;
    image[4] = 16;
    image[5] = 32;
    image[6] = static_cast<Byte>((id & 15) << 4);
    image[7] = static_cast<Byte>((id & 0xF0) | 8);
    if (id == 67) {
        image[5] = 16;
        image.resize(16 + 0x40000 + 0x20000);
    }
    if (id == 13 || id == 28 || id == 31 || id == 71 || id == 73 || id == 93 || id == 94 ||
        id == 180 || id == 226 || id == 231 || id == 232) {
        image[5] = 0;
        image.resize(16 + 0x40000);
    }
    // An original JMP $8000 program and vectors in every page let every
    // board's reset bank boot without depending on a commercial image.
    for (std::size_t page = 16; page < 16 + 0x40000; page += 0x1000) {
        image[page] = 0x4C;
        image[page + 1] = 0;
        image[page + 2] = 0x80;
        for (std::size_t vector = 0xFFA; vector < 0x1000; vector += 2) {
            image[page + vector] = 0;
            image[page + vector + 1] = 0x80;
        }
    }
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

} // namespace

TEST_CASE("Discrete boards decode their own PRG and CHR wiring", "[mappers][boards]") {
    struct Wiring {
        uint16_t id;
        Address addr;
        Byte value;
        Byte low_prg;
        Byte high_prg;
        Byte low_chr;
        bool chr_ram;
    };
    const std::array cases = {
        Wiring{34, 0x8100, 2, 4, 5, 0, true},
        Wiring{66, 0x8100, 0x21, 4, 5, 2, false},
        Wiring{70, 0x8100, 0x32, 3, 15, 4, false},
        Wiring{71, 0xC100, 3, 3, 15, 0, true},
        Wiring{78, 0x8100, 0x25, 5, 15, 4, false},
        Wiring{79, 0x4100, 0x0B, 2, 3, 6, false},
        Wiring{87, 0x6000, 1, 0, 1, 4, false},
        Wiring{89, 0x8100, 0xBA, 3, 15, 20, false},
        Wiring{93, 0x8100, 0x31, 3, 15, 0, true},
        Wiring{94, 0x8100, 0x0C, 3, 15, 0, true},
        Wiring{97, 0x8100, 0x83, 15, 3, 0, true},
        Wiring{113, 0x4100, 0xDD, 6, 7, 26, false},
        Wiring{140, 0x6000, 0x23, 4, 5, 6, false},
        Wiring{152, 0x8100, 0xB2, 3, 15, 4, false},
        Wiring{180, 0x8100, 3, 0, 3, 0, true},
        Wiring{184, 0x6000, 0x23, 0, 1, 3, false},
    };
    for (const auto& c : cases) {
        INFO("Mapper " << c.id);
        auto mapper = board(c.id, 0, c.chr_ram);
        if (c.addr < 0x6000)
            mapper->write_expansion(c.addr, c.value);
        else
            mapper->write_prg(c.addr, c.value);
        REQUIRE(mapper->read_prg(0x8000) == c.low_prg);
        REQUIRE(mapper->read_prg(0xC000) == c.high_prg);
        REQUIRE(mapper->read_chr(0) == c.low_chr);
    }
}

TEST_CASE("Discrete register writes obey PRG-ROM bus conflicts", "[mappers][boards]") {
    for (const uint16_t id : {13, 34, 66, 70, 78, 89, 93, 94, 152, 180}) {
        INFO("Mapper " << id);
        auto mapper = board(id, 0, id == 13 || id == 34 || id == 93 || id == 94 || id == 180);
        mapper->write_prg(0x8101, 0xFF);
        REQUIRE(mapper->read_prg(0x8000) == 0);
        if (id != 93)
            REQUIRE(mapper->read_chr(0) == 0);
    }
}

TEST_CASE("CPROM banks writable CHR while preserving its fixed first page", "[mappers][cprom]") {
    auto mapper = board(13, 0, true);
    mapper->write_chr(0, 0x11);
    for (Byte page = 0; page < 4; ++page) {
        mapper->write_prg(0x8100, page);
        mapper->write_chr(0x1000, static_cast<Byte>(0x20 + page));
    }
    for (Byte page = 0; page < 4; ++page) {
        mapper->write_prg(0x8100, page);
        REQUIRE(mapper->read_chr(0x1000) == 0x20 + page);
    }
    // Page zero is physically shared with the fixed window.
    REQUIRE(mapper->read_chr(0) == 0x20);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("NINA-001 register writes also reach its persistent PRG RAM", "[mappers][nina]") {
    auto mapper = board(34, 1);
    REQUIRE(mapper->maps_prg(0x6000));
    mapper->write_prg(0x7FFD, 1);
    mapper->write_prg(0x7FFE, 3);
    mapper->write_prg(0x7FFF, 5);
    REQUIRE(mapper->read_prg(0x8000) == 2);
    REQUIRE(mapper->read_chr(0) == 3);
    REQUIRE(mapper->read_chr(0x1000) == 5);
    REQUIRE(mapper->read_prg(0x7FFE) == 3);
    REQUIRE(mapper->battery_ram().size() == 8192);
    mapper->write_prg(0x8100, 7);
    REQUIRE(mapper->read_prg(0x8000) == 2);
    auto bnrom = board(34, 2);
    REQUIRE_FALSE(bnrom->maps_prg(0x6000));
    REQUIRE(bnrom->battery_ram().empty());
}

TEST_CASE("Low-address latches ignore unrelated CPU writes", "[mappers][boards]") {
    for (const uint16_t id : {79, 87, 113, 140, 184}) {
        INFO(id);
        auto mapper = board(id);
        const auto initial = mapper->read_chr(0);
        mapper->write_prg(0x8100, 0xFF);
        mapper->write_prg(0x5FFF, 0xFF);
        REQUIRE(mapper->read_chr(0) == initial);
        REQUIRE_FALSE(mapper->maps_prg(0x6000));
    }
    auto nina = board(79);
    REQUIRE(nina->maps_expansion(0x4100));
    REQUIRE(nina->maps_expansion(0x5FFF));
    REQUIRE_FALSE(nina->maps_expansion(0x4200));
    nina->write_expansion(0x4200, 0xFF);
    REQUIRE(nina->read_chr(0) == 0);
    nina->write_expansion(0x5FFF, 3);
    REQUIRE(nina->read_chr(0) == 6);
}

TEST_CASE("Discrete mirroring follows board and submapper wiring", "[mappers][boards]") {
    auto cosmo = board(78, 1);
    auto holy = board(78, 3);
    cosmo->write_prg(0x8100, 8);
    holy->write_prg(0x8100, 8);
    REQUIRE(cosmo->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(holy->mirror_mode() == MirrorMode::Vertical);
    holy->write_prg(0x8100, 0);
    REQUIRE(holy->mirror_mode() == MirrorMode::Horizontal);
    auto fire_hawk = board(71, 1, true);
    fire_hawk->write_prg(0x8000, 0x10);
    REQUIRE(fire_hawk->mirror_mode() == MirrorMode::SingleUpper);
    auto camerica = board(71, 0, true);
    camerica->write_prg(0x8000, 0);
    REQUIRE(camerica->mirror_mode() == MirrorMode::Vertical);
    camerica->write_prg(0x9000, 0);
    REQUIRE(camerica->mirror_mode() == MirrorMode::SingleLower);
    auto sunsoft = board(89);
    sunsoft->write_prg(0x8100, 8);
    REQUIRE(sunsoft->mirror_mode() == MirrorMode::SingleUpper);
    auto bandai = board(152);
    bandai->write_prg(0x8100, 0x80);
    REQUIRE(bandai->mirror_mode() == MirrorMode::SingleUpper);
}

TEST_CASE("Sunsoft CHR selection and RAM enable are independent", "[mappers][boards]") {
    auto sunsoft1 = board(184);
    sunsoft1->write_prg(0x7FFF, 0x23);
    REQUIRE(sunsoft1->read_chr(0) == 3);
    REQUIRE(sunsoft1->read_chr(0x1000) == 6);
    auto sunsoft2 = board(93, 0, true);
    sunsoft2->write_chr(0, 0x42);
    sunsoft2->write_prg(0x8100, 1);
    REQUIRE(sunsoft2->read_chr(0) == 0);
    sunsoft2->write_chr(0, 0x55);
    sunsoft2->write_prg(0x8100, 0);
    sunsoft2->write_chr(0, 0x99);
    sunsoft2->write_prg(0x8100, 1);
    REQUIRE(sunsoft2->read_chr(0) == 0x55);
}

TEST_CASE("MMC4 latches switch after the full trigger range is read", "[mappers][mmc4]") {
    auto mapper = board(10);
    mapper->write_prg(0xA000, 3);
    REQUIRE(mapper->read_prg(0x8000) == 3);
    REQUIRE(mapper->read_prg(0xC000) == 15);
    mapper->write_prg(0xB000, 1);
    mapper->write_prg(0xC000, 2);
    mapper->write_prg(0xD000, 3);
    mapper->write_prg(0xE000, 4);
    for (Address half : {Address{0}, Address{0x1000}}) {
        const Byte fd = half == 0 ? 1 : 3;
        const Byte fe = half == 0 ? 2 : 4;
        for (Address low = 0; low < 8; ++low) {
            mapper->read_chr(static_cast<Address>(half + 0x0FD8));
            REQUIRE(mapper->read_chr(static_cast<Address>(half + 0x0FE8 + low)) == fd);
            REQUIRE(mapper->read_chr(half) == fe);
            REQUIRE(mapper->read_chr(static_cast<Address>(half + 0x0FD8 + low)) == fe);
            REQUIRE(mapper->read_chr(half) == fd);
        }
    }
    mapper->write_prg(0xFFFF, 1);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0x6000, 0xAA);
    REQUIRE(mapper->battery_ram()[0] == 0xAA);
}

TEST_CASE("Namco banking variants preserve fixed PRG and hardware CHR windows",
          "[mappers][namco108]") {
    for (const uint16_t id : {76, 88, 95, 154, 206}) {
        auto mapper = board(id);
        set_namco_bank(*mapper, 6, 4);
        set_namco_bank(*mapper, 7, 7);
        REQUIRE(mapper->read_prg(0x8000) == 2);
        REQUIRE(mapper->read_prg(0xA000) == 3);
        REQUIRE(mapper->read_prg(0xC000) == 15);
        REQUIRE(mapper->read_prg(0xE000) == 15);
        mapper->write_prg(0xC000, 9);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->maps_prg(0x6000));
    }
    auto normal = board(206);
    set_namco_bank(*normal, 0, 5);
    REQUIRE(normal->read_chr(0) == 1); // 1 KiB bank 4, low bit ignored.
    set_namco_bank(*normal, 2, 12);
    REQUIRE(normal->read_chr(0x1000) == 3);
    auto n76 = board(76);
    set_namco_bank(*n76, 2, 6);
    REQUIRE(n76->read_chr(0) == 3); // 2 KiB bank 6.
    auto n88 = board(88);
    set_namco_bank(*n88, 2, 12);
    REQUIRE(n88->read_chr(0x1000) == 19); // Forced CHR A16 in the right half.
    auto n154 = board(154);
    n154->write_prg(0xFFFF, 0x40);
    REQUIRE(n154->mirror_mode() == MirrorMode::SingleUpper);
    auto fixed = board(206, 1);
    set_namco_bank(*fixed, 6, 4);
    REQUIRE(fixed->read_prg(0x8000) == 0);
    REQUIRE(fixed->read_prg(0xC000) == 1);
}

TEST_CASE("Namco 95 independently wires both nametable pairs", "[mappers][namco108]") {
    auto mapper = board(95);
    set_namco_bank(*mapper, 0, 0x20);
    set_namco_bank(*mapper, 1, 0);
    REQUIRE(mapper->nametable_address(0x2000) == 0x400);
    REQUIRE(mapper->nametable_address(0x2400) == 0x400);
    REQUIRE(mapper->nametable_address(0x2800) == 0);
    REQUIRE(mapper->nametable_address(0x2FFF) == 0x3FF);
}

TEST_CASE("Every new board restores mutable state and rejects truncation", "[mappers][state]") {
    for (const auto id : kNewBoards) {
        INFO(id);
        auto mapper = board(id, 0, id == 13);
        mapper->write_expansion(0x4100, 0x9B);
        for (const Address addr : {Address{0x6000},
                                   Address{0x7FFD},
                                   Address{0x7FFE},
                                   Address{0x8100},
                                   Address{0xA100},
                                   Address{0xB100},
                                   Address{0xC100},
                                   Address{0xD100},
                                   Address{0xE100}})
            mapper->write_prg(addr, 3);
        mapper->write_chr(0x1123, 0x56);
        mapper->read_chr(0x0FEF);
        StateWriter writer;
        mapper->save_state(writer);
        const auto saved = writer.data();
        const auto prg = mapper->read_prg(0x8000);
        const auto chr = mapper->read_chr(0x1123);
        const auto mirror = mapper->mirror_mode();
        mapper->reset();
        mapper->write_chr(0x1123, 0xAA);
        StateReader reader(saved);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0);
        REQUIRE(mapper->read_prg(0x8000) == prg);
        REQUIRE(mapper->read_chr(0x1123) == chr);
        REQUIRE(mapper->mirror_mode() == mirror);
        StateWriter replay;
        mapper->save_state(replay);
        REQUIRE(replay.data() == saved);
        StateReader short_reader(std::span(saved).first(saved.size() - 1));
        mapper->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
    }
}

TEST_CASE("New board factory rejects invalid geometry and ambiguous submappers",
          "[mappers][boards]") {
    register_builtin_mappers();
    for (const uint16_t id : {10, 76, 88, 95, 154, 206})
        REQUIRE_FALSE(MapperRegistry::instance().create(
            board_header(id), std::vector<Byte>(0x4000), std::vector<Byte>(0x2000)));
    REQUIRE_FALSE(MapperRegistry::instance().create(
        board_header(13), std::vector<Byte>(0x8000), std::vector<Byte>(0x2000)));
    REQUIRE_FALSE(MapperRegistry::instance().create(
        board_header(78, 2), std::vector<Byte>(0x8000), std::vector<Byte>(0x2000)));
}

TEST_CASE("Additional latch boards select their documented windows", "[mappers][multicart]") {
    struct Wiring {
        uint16_t id;
        Address addr;
        Byte value;
        Byte low_prg;
        Byte high_prg;
        Byte low_chr;
    };
    const std::array cases = {
        Wiring{38, 0x7000, 0x0D, 2, 3, 6},  Wiring{58, 0x80DD, 0, 5, 5, 6},
        Wiring{61, 0x8A35, 0, 11, 11, 20},  Wiring{62, 0x83A5, 2, 3, 3, 44},
        Wiring{81, 0x800D, 0, 3, 15, 2},    Wiring{107, 0x8100, 0x15, 4, 5, 42},
        Wiring{145, 0x4100, 0x80, 0, 1, 2}, Wiring{148, 0x8100, 0x0B, 2, 3, 6},
        Wiring{149, 0x8100, 0x80, 0, 1, 2}, Wiring{193, 0x6003, 5, 2, 15, 0},
        Wiring{200, 0x800B, 0, 11, 11, 22}, Wiring{201, 0x8003, 0, 6, 7, 6},
        Wiring{203, 0x8100, 0x0D, 3, 3, 2}, Wiring{212, 0xC00B, 0, 2, 3, 6},
        Wiring{213, 0x80DD, 0, 5, 5, 6},    Wiring{225, 0x9483, 0, 2, 2, 6},
        Wiring{226, 0x8000, 0x63, 3, 3, 0}, Wiring{229, 0x8035, 0, 5, 5, 42},
        Wiring{231, 0x80A6, 0, 6, 7, 0},    Wiring{240, 0x4800, 0x23, 4, 5, 6},
    };
    for (const auto& c : cases) {
        INFO(c.id);
        auto mapper = board(c.id);
        if (c.addr < 0x6000)
            mapper->write_expansion(c.addr, c.value);
        else
            mapper->write_prg(c.addr, c.value);
        REQUIRE(mapper->read_prg(0x8000) == c.low_prg);
        REQUIRE(mapper->read_prg(0xC000) == c.high_prg);
        REQUIRE(mapper->read_chr(0) == c.low_chr);
    }
    auto rumble = board(46);
    rumble->write_prg(0x6000, 0x21);
    rumble->write_prg(0x8000, 0x31);
    REQUIRE(rumble->read_prg(0x8000) == 6);
    REQUIRE(rumble->read_prg(0xC000) == 7);
    REQUIRE(rumble->read_chr(0) == 38);
    auto quattro = board(232);
    quattro->write_prg(0x8000, 0x18);
    quattro->write_prg(0xC000, 2);
    REQUIRE(quattro->read_prg(0x8000) == 14);
    REQUIRE(quattro->read_prg(0xC000) == 15);
    auto aladdin = board(232, 1);
    aladdin->write_prg(0x8000, 8);
    aladdin->write_prg(0xC000, 1);
    REQUIRE(aladdin->read_prg(0x8000) == 9);
    REQUIRE(aladdin->read_prg(0xC000) == 11);
}

TEST_CASE("Multicart outer address lines reach distinct banks", "[mappers][multicart]") {
    register_builtin_mappers();
    auto make_large = [](uint16_t id) {
        auto result = MapperRegistry::instance().create(
            board_header(id), bank_rom(0x200000, 0x4000), bank_rom(0x100000, 0x2000));
        REQUIRE(result);
        return std::move(*result);
    };
    auto mapper225 = make_large(225);
    mapper225->write_prg(0xD183, 0);
    REQUIRE(mapper225->read_prg(0x8000) == 70);
    REQUIRE(mapper225->read_prg(0xC000) == 70);
    REQUIRE(mapper225->read_chr(0) == 67);
    mapper225->write_prg(0xC1C3, 0);
    REQUIRE(mapper225->read_prg(0x8000) == 70);
    REQUIRE(mapper225->read_prg(0xC000) == 71);
    auto mapper226 = make_large(226);
    mapper226->write_prg(0x8000, 0x85);
    mapper226->write_prg(0x8001, 1);
    REQUIRE(mapper226->read_prg(0x8000) == 100);
    REQUIRE(mapper226->read_prg(0xC000) == 101);
    auto mapper62 = make_large(62);
    mapper62->write_prg(0xC241, 3);
    REQUIRE(mapper62->read_prg(0x8000) == 66);
    REQUIRE(mapper62->read_prg(0xC000) == 67);
    REQUIRE(mapper62->read_chr(0) == 7);
}

TEST_CASE("Data bus masks preserve undriven bits", "[mappers][bus]") {
    auto cart225 = cartridge(225);
    MemoryBus bus;
    bus.connect_cartridge(&cart225);
    bus.write(0x5800, 0x3C);
    bus.write(0, 0xA0);
    REQUIRE(bus.read(0x5804) == 0xAC);
    auto cart212 = cartridge(212);
    bus.connect_cartridge(&cart212);
    bus.write(0, 0x7F);
    REQUIRE(bus.read(0x6000) == 0xF0);
    REQUIRE(bus.read(0x6010) == 0x70);
    auto cart79 = cartridge(79);
    bus.connect_cartridge(&cart79);
    bus.write(0, 0xA5);
    REQUIRE(bus.read(0x4100) == 0xA5);
}

TEST_CASE("Multicart CHR write protection and RAM survive resets", "[mappers][state]") {
    auto mapper = board(226, 0, true);
    mapper->write_chr(0, 0x42);
    mapper->write_prg(0x8001, 2);
    mapper->write_chr(0, 0x99);
    REQUIRE(mapper->read_chr(0) == 0x42);
    mapper->reset();
    REQUIRE(mapper->read_chr(0) == 0x42);
    mapper->write_chr(0, 0x11);
    REQUIRE(mapper->read_chr(0) == 0x11);
}

TEST_CASE("Namco 95 routes nametables through the production PPU", "[mappers][ppu]") {
    auto cart = cartridge(95);
    cart.write_prg(0x8000, 0);
    cart.write_prg(0x8001, 0x20);
    cart.write_prg(0x8000, 1);
    cart.write_prg(0x8001, 0);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    auto address = [&](Address addr) {
        ppu.write_register(6, static_cast<Byte>(addr >> 8));
        ppu.write_register(6, static_cast<Byte>(addr));
    };
    address(0x2000);
    ppu.write_register(7, 0xAA);
    address(0x2800);
    ppu.write_register(7, 0xBB);
    address(0x2400);
    ppu.read_register(7);
    REQUIRE(ppu.read_register(7) == 0xAA);
    address(0x2C00);
    ppu.read_register(7);
    REQUIRE(ppu.read_register(7) == 0xBB);
    REQUIRE(ppu.vram()[0x400] == 0xAA);
    REQUIRE(ppu.vram()[0] == 0xBB);
}

TEST_CASE("VRC2 and VRC4 use exact submapper address lines", "[mappers][vrc]") {
    struct Wiring {
        uint16_t id;
        Byte submapper;
        std::array<Address, 4> offsets;
    };
    const std::array wiring = {
        Wiring{21, 1, {0, 2, 4, 6}},
        Wiring{21, 2, {0, 0x40, 0x80, 0xC0}},
        Wiring{22, 0, {0, 2, 1, 3}},
        Wiring{23, 1, {0, 1, 2, 3}},
        Wiring{23, 2, {0, 4, 8, 12}},
        Wiring{23, 3, {0, 1, 2, 3}},
        Wiring{25, 1, {0, 2, 1, 3}},
        Wiring{25, 2, {0, 8, 4, 12}},
        Wiring{25, 3, {0, 2, 1, 3}},
    };
    register_builtin_mappers();
    for (const auto& pins : wiring) {
        INFO(pins.id << ":" << static_cast<int>(pins.submapper));
        auto result = MapperRegistry::instance().create(board_header(pins.id, pins.submapper),
                                                        bank_rom(0x40000, 0x2000),
                                                        bank_rom(0x40000, 0x400));
        REQUIRE(result);
        auto& mapper = **result;
        mapper.write_prg(0x8000, 5);
        mapper.write_prg(0xA000, 7);
        REQUIRE(mapper.read_prg(0x8000) == 5);
        REQUIRE(mapper.read_prg(0xA000) == 7);
        mapper.write_prg(static_cast<Address>(0xB000 + pins.offsets[0]), 3);
        mapper.write_prg(static_cast<Address>(0xB000 + pins.offsets[1]), 2);
        mapper.write_prg(static_cast<Address>(0xB000 + pins.offsets[2]), 6);
        mapper.write_prg(static_cast<Address>(0xB000 + pins.offsets[3]), 1);
        REQUIRE(mapper.read_chr(0) == (pins.id == 22 ? 17 : 35));
        REQUIRE(mapper.read_chr(0x400) == (pins.id == 22 ? 11 : 22));
        const bool vrc2 = pins.id == 22 || pins.submapper == 3;
        mapper.write_prg(static_cast<Address>(0x9000 + pins.offsets[2]), 3);
        REQUIRE(mapper.read_prg(0x8000) == (vrc2 ? 5 : 30));
        REQUIRE(mapper.read_prg(0xC000) == (vrc2 ? 30 : 5));
        mapper.write_prg(0x9000, 0xFF);
        REQUIRE(mapper.mirror_mode() == (vrc2 ? MirrorMode::Horizontal : MirrorMode::SingleUpper));
    }
}

TEST_CASE("VRC4 has nine CHR bits while VRC2 has eight", "[mappers][vrc]") {
    register_builtin_mappers();
    std::vector<Byte> chr(0x80000, 0x11);
    std::fill(chr.begin() + 0x40000, chr.end(), 0x22);
    for (const Byte submapper : {Byte{1}, Byte{3}}) {
        auto result = MapperRegistry::instance().create(
            board_header(23, submapper), bank_rom(0x40000, 0x2000), chr);
        REQUIRE(result);
        auto& mapper = **result;
        mapper.write_prg(0xB001, 0x10);
        REQUIRE(mapper.read_chr(0) == (submapper == 1 ? 0x22 : 0x11));
    }
}

TEST_CASE("VRC2 latch drives only D0 and does not map the upper RAM window",
          "[mappers][vrc][bus]") {
    auto cart = cartridge(23); // Legacy combined definition uses VRC4.
    // Explicitly identify the VRC2b board in the ROM header.
    std::vector<Byte> image(16 + 0x8000 + 0x2000, 0);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1A;
    image[4] = 2;
    image[5] = 1;
    image[6] = 0x70;
    image[7] = 0x18;
    image[8] = 0x30;
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    cart = std::move(*result);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0x6000, 1);
    bus.write(0, 0xA0);
    REQUIRE(bus.read(0x6100) == 0xA1);
    bus.write(0, 0x42);
    REQUIRE(bus.read(0x7000) == 0x42);
    REQUIRE_FALSE(cart.maps_prg(0x7000));
    cart.write_prg(0xF000, 15);
    cart.write_prg(0xF001, 15);
    cart.write_prg(0xF002, 7);
    for (int i = 0; i < 1000; ++i)
        cart.clock_cpu();
    REQUIRE_FALSE(cart.irq_pending());
}

TEST_CASE("VRC4 clocks cycle IRQs independently of audio and preserves prescaler state",
          "[mappers][vrc][irq]") {
    auto mapper = board(23, 1);
    mapper->write_prg(0xF000, 14);
    mapper->write_prg(0xF001, 15);
    mapper->write_prg(0xF002, 6); // Latch FE; CPU-cycle mode; disable after ACK.
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003, 0);
    for (int i = 0; i < 1000; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xF000, 15);
    mapper->write_prg(0xF002, 3); // Scanline prescaler, repeat after ACK.
    for (int i = 0; i < 100; ++i)
        mapper->clock_cpu();
    StateWriter writer;
    mapper->save_state(writer);
    for (int i = 0; i < 13; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    StateReader reader(writer.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE_FALSE(mapper->irq_pending());
    for (int i = 0; i < 14; ++i)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003, 0);
    for (int i = 0; i < 113; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("CPU bus cycles advance non-audio VRC IRQs", "[mappers][vrc][bus]") {
    auto cart = cartridge(23);
    cart.write_prg(0xF000, 15);
    cart.write_prg(0xF001, 15);
    cart.write_prg(0xF002, 6);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    REQUIRE_FALSE(cart.has_expansion_audio());
    bus.clock_cpu_bus(0, 1);
    REQUIRE(cart.irq_pending());
}

TEST_CASE("H3001 IRQ counts CPU and OAM DMA cycles with rendering disabled",
          "[mappers][irem][bus][irq]") {
    auto cart = cartridge(65U);
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    REQUIRE_FALSE(cart.has_expansion_audio());
    // No PPU tick or audio callback may clock the CPU timer.
    cart.write_prg(0x9005U, 2U);
    cart.write_prg(0x9006U, 2U);
    cart.write_prg(0x9004U, 0U);
    cart.write_prg(0x9003U, 0x80U);
    for (unsigned i = 0U; i < 600U; ++i) {
        cart.clock_irq_counter();
        cart.clock_audio();
    }
    REQUIRE_FALSE(bus.poll_irq());
    const auto cycles = bus.run_oam_dma(0U, 0x8000U, false);
    REQUIRE(cycles == 513U);
    REQUIRE_FALSE(bus.poll_irq());
    bus.clock_cpu_bus(0U);
    REQUIRE(bus.poll_irq());
    cart.acknowledge_irq(); // CPU servicing the level does not clear the ASIC.
    REQUIRE(bus.poll_irq());
    bus.write(0x9003U, 0U);
    REQUIRE_FALSE(bus.poll_irq());
}

TEST_CASE("H3001 running IRQ snapshots replay through the CPU and reject corrupt state",
          "[mappers][irem][state]") {
    register_builtin_mappers();
    std::vector<Byte> image(16U + 0x8000U, 0xEAU);
    std::fill_n(image.begin(), 16U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 2U;
    image[6U] = 0x10U;
    image[7U] = 0x48U;                                           // NES 2.0 mapper 65.
    const std::array<Byte, 27U> program = {0x78U,                // SEI
                                           0xA9U, 0x00U,         // LDA #0
                                           0x85U, 0x00U,         // STA $00
                                           0x8DU, 0x05U, 0x90U,  // STA $9005
                                           0xA9U, 0x20U,         // LDA #32
                                           0x8DU, 0x06U, 0x90U,  // STA $9006
                                           0xA9U, 0x00U,         // LDA #0
                                           0x8DU, 0x04U, 0x90U,  // STA $9004
                                           0xA9U, 0x80U,         // LDA #$80
                                           0x8DU, 0x03U, 0x90U,  // STA $9003
                                           0x58U,                // CLI
                                           0x4CU, 0x18U, 0x80U}; // JMP $8018
    const std::array<Byte, 8U> handler = {0xE6U,
                                          0x00U, // INC $00
                                          0xA9U,
                                          0x00U, // LDA #0
                                          0x8DU,
                                          0x03U,
                                          0x90U,  // STA $9003 (acknowledge and disable)
                                          0x40U}; // RTI
    std::copy(program.begin(), program.end(), image.begin() + 16U);
    std::copy(handler.begin(), handler.end(), image.begin() + 16U + 0x100U);
    for (std::size_t offset = image.size() - 6U; offset < image.size(); offset += 2U) {
        image[offset] = 0U;
        image[offset + 1U] = offset == image.size() - 2U ? 0x81U : 0x80U;
    }
    auto cart = Cartridge::from_data(image);
    REQUIRE(cart);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cart), {}));
    emulator.reset();
    REQUIRE(emulator.cpu().pc() == 0x8000U);
    for (unsigned i = 0U; i < 10U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 0U);
    const auto saved = emulator.save_state();
    for (unsigned i = 0U; i < 30U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 1U);
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(saved));
    REQUIRE(emulator.read_cpu(0U) == 0U);
    for (unsigned i = 0U; i < 30U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 1U);
    REQUIRE(emulator.save_state() == expected);
    REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
    REQUIRE(emulator.save_state() == expected);
}

TEST_CASE("VRC3 supports full and low-byte counter modes", "[mappers][vrc][irq]") {
    auto mapper = board(73, 0, true);
    mapper->write_prg(0xF000, 3);
    REQUIRE(mapper->read_prg(0x8000) == 3);
    for (const Byte control : {Byte{2}, Byte{6}}) {
        mapper->write_prg(0x8000, 14);
        mapper->write_prg(0x9000, 15);
        mapper->write_prg(0xA000, 15);
        mapper->write_prg(0xB000, control == 2 ? 15 : 1);
        mapper->write_prg(0xC000, control);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xD000, 0);
        mapper->clock_cpu();
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("VRC1 CHR high bits are independent of lower bank writes", "[mappers][vrc]") {
    auto mapper = board(75);
    mapper->write_prg(0xE000, 3);
    mapper->write_prg(0xF000, 5);
    mapper->write_prg(0x9000, 7);
    REQUIRE(mapper->read_chr(0) == 19);
    REQUIRE(mapper->read_chr(0x1000) == 21);
    mapper->write_prg(0xE000, 2);
    REQUIRE(mapper->read_chr(0) == 18);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("NES 2.0 memory sizes distinguish absent, volatile and persistent RAM",
          "[mappers][header]") {
    std::vector<Byte> image(16, 0);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1A;
    image[4] = 2;
    image[7] = 8;
    image[10] = 0x75;
    image[11] = 0x87;
    auto header = parse_ines_header(image);
    REQUIRE(header);
    REQUIRE(header->prg_ram_size == 2048);
    REQUIRE(header->prg_nvram_size == 8192);
    REQUIRE(header->chr_ram_size == 8192);
    REQUIRE(header->chr_nvram_size == 16384);
    image[10] = 0;
    image[11] = 0;
    header = parse_ines_header(image);
    REQUIRE(header->prg_ram_size == 0);
    REQUIRE(header->prg_nvram_size == 0);
    REQUIRE(header->chr_ram_size == 0);
}

TEST_CASE("VRC4 small WRAM mirrors only while enabled and preserves NVRAM",
          "[mappers][vrc][state]") {
    auto header = board_header(23, 1);
    header.has_battery = true;
    header.prg_nvram_size = 2048;
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(header, bank_rom(0x40000, 0x2000), {});
    REQUIRE(result);
    auto& mapper = **result;
    REQUIRE_FALSE(mapper.maps_prg(0x6000));
    mapper.write_prg(0x9002, 1);
    mapper.write_prg(0x6000, 0x42);
    REQUIRE(mapper.read_prg(0x6800) == 0x42);
    REQUIRE_FALSE(mapper.maps_prg(0x7000));
    REQUIRE(mapper.battery_ram().size() == 2048);
    mapper.reset();
    mapper.write_prg(0x9002, 1);
    REQUIRE(mapper.read_prg(0x6000) == 0x42);
}

TEST_CASE("Action 53 mode and outer size combinations follow the hardware truth table",
          "[mappers][homebrew][action53]") {
    auto mapper = board(28, 0, true);
    auto reg = [&](Byte index, Byte value) {
        mapper->write_expansion(0x5000, index);
        mapper->write_prg(0x8000, value);
    };
    const std::array<std::array<Byte, 6>, 4> expected = {{
        {10, 11, 10, 10, 10, 11},
        {8, 9, 10, 10, 10, 11},
        {12, 13, 10, 14, 14, 11},
        {12, 13, 10, 6, 6, 11},
    }};
    reg(0x81, 5);
    reg(1, 6);
    for (Byte size = 0; size < 4; ++size) {
        for (Byte mode = 0; mode < 4; ++mode) {
            INFO("Size " << static_cast<int>(size) << " mode " << static_cast<int>(mode));
            reg(0x80, static_cast<Byte>((size << 4) | (mode << 2) | 2));
            const auto pair = mode < 2 ? 0 : (mode == 2 ? 2 : 4);
            REQUIRE(mapper->read_prg(0x8000) == expected[size][pair]);
            REQUIRE(mapper->read_prg(0xC000) == expected[size][pair + 1]);
        }
    }
    reg(0x80, 0);
    reg(0, 0x12);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    mapper->write_chr(0, 0x42);
    reg(0, 0);
    REQUIRE(mapper->read_chr(0) == 0);
    reg(0, 2);
    REQUIRE(mapper->read_chr(0) == 0x42);
    reg(0x80, 2);
    reg(1, 0x10);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    StateWriter before;
    mapper->save_state(before);
    mapper->reset();
    StateWriter after;
    mapper->save_state(after);
    REQUIRE(after.data() == before.data());
}

TEST_CASE("Mapper 31 banks all eight 4 KiB windows and retains banks on reset",
          "[mappers][homebrew]") {
    auto mapper = board(31, 0, true);
    REQUIRE(mapper->read_prg(0xF000) == 15);
    for (Byte slot = 0; slot < 8; ++slot)
        mapper->write_expansion(static_cast<Address>(0x5000 + slot), static_cast<Byte>(slot * 4));
    for (Byte slot = 0; slot < 8; ++slot)
        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000 + slot * 0x1000)) == slot);
    mapper->reset();
    REQUIRE(mapper->read_prg(0xF000) == 7);
    mapper->write_chr(0x1234, 0x56);
    REQUIRE(mapper->read_chr(0x1234) == 0x56);
    REQUIRE_FALSE(mapper->has_expansion_audio());
}

TEST_CASE("Every new cartridge boots and replays a full machine snapshot",
          "[mappers][state][integration]") {
    for (const auto id : kNewBoards) {
        INFO(id);
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(id), ""));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0x8000U);
        for (int instruction = 0; instruction < 50; ++instruction)
            emulator.step_instruction();
        const auto before = emulator.save_state();
        for (int instruction = 0; instruction < 100; ++instruction)
            emulator.step_instruction();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(before));
        for (int instruction = 0; instruction < 100; ++instruction)
            emulator.step_instruction();
        REQUIRE(emulator.save_state() == expected);
        auto truncated = expected;
        truncated.resize(truncated.size() / 2);
        REQUIRE_FALSE(emulator.load_state(truncated));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("MMC4 CHR RAM uses the same bank output pins as CHR ROM", "[mappers][mmc4]") {
    auto mapper = board(10, 0, true);
    mapper->write_prg(0xB000, 0);
    mapper->write_prg(0xC000, 1);
    mapper->write_prg(0xD000, 0);
    mapper->write_prg(0xE000, 1);
    mapper->write_chr(0, 0x11);
    mapper->read_chr(0x0FE8);
    mapper->write_chr(0, 0x22);
    REQUIRE(mapper->read_chr(0x1000) == 0x11);
    mapper->read_chr(0x1FEF);
    REQUIRE(mapper->read_chr(0x1000) == 0x22);
}
