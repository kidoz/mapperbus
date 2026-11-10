#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "core/binary_file.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
// Written pins D0..D7 reach these ordinary MMC3 data pins (NESdev r7642).
constexpr std::array<unsigned, 8> kActualBits = {2U, 0U, 5U, 3U, 4U, 1U, 6U, 7U};
constexpr Byte actual(Byte written) {
    unsigned value = 0U;
    for (unsigned pin = 0U; pin < 8U; ++pin)
        value |= ((static_cast<unsigned>(written) >> pin) & 1U) << kActualBits[pin];
    return static_cast<Byte>(value);
}
constexpr Byte encode(Byte logical) {
    unsigned written = 0U;
    for (unsigned pin = 0U; pin < 8U; ++pin)
        written |= ((static_cast<unsigned>(logical) >> kActualBits[pin]) & 1U) << pin;
    return static_cast<Byte>(written);
}
INesHeader header(uint16_t id = 263U) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x80000U,
                              std::size_t c = 0x20000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x8FFFU, encode(static_cast<Byte>(reg | mode)));
    m.write_prg(0x9FFFU, encode(value));
}
TEST_CASE("KOF97 all data inputs match MMC3 registers after the published pin permutation",
          "[mappers][kof97]") {
    auto m = board();
    // TQROM uses the same compact register/IRQ payload. No CHR writes are
    // involved here, so its ROM/RAM chip-selection wiring is immaterial.
    auto reference = board(header(119U));
    constexpr std::array<Address, 8> kPorts = {
        0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
    for (unsigned port = 0U; port < 8U; ++port) {
        for (unsigned input = 0U; input < 256U; ++input) {
            m->reset();
            reference->reset();
            bank(*m, 2U, 9U);
            reference->write_prg(0x8000U, 2U);
            reference->write_prg(0x8001U, 9U);
            m->write_prg(static_cast<Address>(0x8000U + port * 4096U), static_cast<Byte>(input));
            reference->write_prg(kPorts[port], actual(static_cast<Byte>(input)));
            REQUIRE((state(*m) == state(*reference)));
        }
    }
}
TEST_CASE("KOF97 ignores low address bits including A0 at every register port",
          "[mappers][kof97]") {
    auto m = board();
    auto reference = board(header(119U));
    constexpr std::array<Address, 8> kPorts = {
        0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
    for (unsigned port = 0U; port < 8U; ++port) {
        for (unsigned low : {0U, 1U, 0x2A5U, 0xFFEU, 0xFFFU}) {
            m->reset();
            reference->reset();
            m->write_prg(static_cast<Address>(0x8000U + port * 4096U + low), encode(0xC7U));
            reference->write_prg(kPorts[port], 0xC7U);
            REQUIRE((state(*m) == state(*reference)));
        }
    }
    m->reset();
    m->write_prg(0x8001U, encode(3U)); // Still bank select, not bank data.
    m->write_prg(0x9000U, encode(9U));
    REQUIRE(m->read_chr(0x1400U) == 9U);
}
std::vector<Byte> image(bool battery = false) {
    auto p = banks(0x40000U, 8192U);
    const auto c = banks(0x40000U, 1024U);
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 16U;
    rom[5] = 32U;
    rom[6] = battery ? 0x73U : 0x71U;
    rom[7] = 8U;
    rom[8] = 1U;
    rom[10] = battery ? 0x70U : 0U;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto mapper_write = [&store](Address a, Byte v) { store(a, encode(v)); };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&mapper_write](Byte r, Byte v) {
        mapper_write(0x8FFFU, r);
        mapper_write(0x9FFFU, v);
    };
    const auto address = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&address, &store](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &address, &expect](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U); // Inhibit APU frame IRQ; only the mapper may trigger the handler.
    store(0x11U, 0U);
    store(0x12U, 0U);
    mapper_write(0xB000U, 0x80U);
    if (battery) {
        store(0x6000U, 0U); // Clear the prior success marker before each launch.
        code.insert(code.end(), {0xEEU, 2U, 0x60U});
        store(0x6003U, 0x35U);
        expect(0x6003U, 0x35U);
    }
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    expect(0x8000U, 3U);
    expect(0xA000U, 5U);
    expect(0xC000U, 30U);
    mapper_write(0x8001U, 0x46U); // A0 is ignored: this is bank select.
    expect(0x8000U, 30U);
    expect(0xC000U, 3U);
    constexpr std::array<Byte, 6> kRegs = {0x83U, 5U, 0xE5U, 0xA6U, 0x37U, 0x6BU};
    for (Byte r = 0U; r < 6U; ++r)
        set_bank(r, kRegs[r]);
    pread(0U, 0x82U);
    pread(0x400U, 0x83U);
    pread(0x800U, 4U);
    pread(0xC00U, 5U);
    pread(0x1000U, 0xE5U);
    pread(0x1400U, 0xA6U);
    pread(0x1800U, 0x37U);
    pread(0x1C00U, 0x6BU);
    pwrite(0x1000U, 0x11U);
    pread(0x1000U, 0xE5U);
    mapper_write(0x8000U, 0x80U);
    pread(0U, 0xE5U);
    pread(0x1000U, 0x82U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    mapper_write(0xAFFFU, 1U);
    pread(0x2400U, 0x19U);
    mapper_write(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    mapper_write(0xCFFFU, 2U);
    mapper_write(0xD000U, 0U);
    mapper_write(0xFFFFU, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xADU, 0x11U, 0U, 0xC9U, 0xD3U, 0xD0U, 0xF9U});
    store(0x12U, 0xC7U);
    if (battery)
        store(0x6000U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x800U);
    std::ranges::copy(code, p.begin() + 0x3E000U);
    constexpr std::array<Byte, 12> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0xA9U, 0xD3U, 0x85U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(kIrq, p.begin() + 0x3E800U);
    for (std::size_t v = 0x3FFFAU; v < 0x40000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x3FFFCU ? 0xE0U : 0xE8U;
    }
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool battery = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(battery));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("KOF97 banks full CHR bytes, rounds paired banks and masks six PRG pins",
          "[mappers][kof97]") {
    auto m = board(header(), 0x80000U, 0x40000U);
    bank(*m, 6U, 0xC3U);
    bank(*m, 7U, 0xC5U);
    bank(*m, 0U, 0x83U);
    bank(*m, 2U, 0xE5U);
    for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U}) {
        m->write_prg(0x8000U, encode(mode));
        REQUIRE(m->read_prg(0x8000U) == ((mode & 0x40U) != 0U ? 62U : 3U));
        REQUIRE(m->read_prg(0xA000U) == 5U);
        REQUIRE(m->read_prg(0xC000U) == ((mode & 0x40U) != 0U ? 3U : 62U));
        REQUIRE(m->read_prg(0xE000U) == 63U);
        const Address pair = (mode & 0x80U) != 0U ? 0x1000U : 0U;
        REQUIRE(m->read_chr(pair) == 0x82U);
        REQUIRE(m->read_chr(static_cast<Address>(pair + 1024U)) == 0x83U);
        REQUIRE(m->read_chr(static_cast<Address>(pair ^ 0x1000U)) == 0xE5U);
        m->write_chr(pair, 0U);
        REQUIRE(m->read_chr(pair) == 0x82U);
    }
    auto small = board(header(), 0x18000U, 0x18000U);
    bank(*small, 6U, 11U);
    bank(*small, 2U, 255U);
    REQUIRE(small->read_prg(0x8000U) == 11U);
    REQUIRE(small->read_chr(0x1000U) == 63U);
}
TEST_CASE("KOF97 WRAM gates use remapped B ports while memory data stays unpermuted",
          "[mappers][kof97]") {
    auto h = header();
    h.prg_ram_size = 4096U;
    h.prg_nvram_size = 4096U;
    h.has_battery = true;
    auto m = board(h);
    m->write_prg(0x6001U, 1U);
    m->write_prg(0x7005U, 0x35U);
    REQUIRE(m->read_prg(0x6001U) == 1U);
    REQUIRE(m->read_prg(0x7005U) == 0x35U);
    REQUIRE(m->battery_ram().size() == 4096U);
    REQUIRE(m->battery_ram()[5U] == 0x35U);
    m->write_prg(0xBFFFU, encode(0xC0U));
    m->write_prg(0x7005U, 0x22U);
    REQUIRE(m->read_prg(0x7005U) == 0x35U);
    m->write_prg(0xB000U, 0U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    m->write_prg(0x6001U, 2U);
    m->write_prg(0xB000U, encode(0x80U));
    REQUIRE(m->read_prg(0x6001U) == 1U);
    auto copy = board(h);
    copy->set_battery_ram(m->battery_ram());
    REQUIRE(copy->read_prg(0x7005U) == 0x35U);
    REQUIRE(copy->read_prg(0x6001U) == 0U);
    REQUIRE_FALSE(m->maps_expansion(0x4100U));
    auto absent = board();
    absent->write_prg(0xB000U, encode(0x80U));
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->battery_ram().empty());
}
TEST_CASE("KOF97 IRQ reload and acknowledgement survive snapshots and reject malformed state",
          "[mappers][kof97]") {
    auto m = board();
    bank(*m, 2U, 0x65U);
    m->write_prg(0xA000U, encode(1U));
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0xC000U, encode(2U));
    m->write_prg(0xDFFFU, 0U);
    m->write_prg(0xF001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(copy->read_chr(0x1000U) == 0x65U);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    copy->write_prg(0xEFFFU, 0U);
    REQUIRE_FALSE(copy->irq_pending());
    for (std::size_t offset : {16384UZ, 16385UZ, 16392UZ, 16394UZ, 16397UZ, 16398UZ}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
    StateReader truncated(std::span{saved}.first(saved.size() - 1U));
    copy->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
}
TEST_CASE("KOF97 metadata bounds the standard MMC3 ROM and header-selected WRAM profile",
          "[mappers][kof97]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 15U; ++kind) {
        auto h = header();
        std::size_t p = 0x40000U, c = 0x20000U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            p = 16384U;
            break;
        case 3U:
            p = 0x82000U;
            break;
        case 4U:
            c = 0U;
            break;
        case 5U:
            c = 0x40400U;
            break;
        case 6U:
            c = 8193U;
            break;
        case 7U:
            h.chr_ram_size = 8192U;
            break;
        case 8U:
            h.chr_nvram_size = 8192U;
            break;
        case 9U:
            h.prg_ram_size = 16384U;
            break;
        case 10U:
            h.prg_ram_size = 8192U;
            h.prg_nvram_size = 8192U;
            h.has_battery = true;
            break;
        case 11U:
            h.prg_ram_size = 3072U;
            break;
        case 12U:
            h.has_battery = true;
            break;
        case 13U:
            h.prg_nvram_size = 8192U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    for (std::size_t ram : {0U, 2048U, 4096U, 8192U}) {
        auto h = header();
        h.prg_ram_size = ram;
        REQUIRE(MapperRegistry::instance().create(h, banks(32768U, 8192U), banks(8192U, 1024U)));
    }
}
TEST_CASE("KOF97 original CPU program verifies native registers, PPUDATA and IRQ before replay",
          "[mappers][kof97]") {
    for (bool battery : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(battery), ""));
        emulator.reset();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
        REQUIRE(emulator.read_cpu(0x11U) == 0xD3U);
        if (battery) {
            REQUIRE(emulator.read_cpu(0x6000U) == 0xC7U);
            REQUIRE(emulator.read_cpu(0x6003U) == 0x35U);
        }
        const auto early = emulator.save_state();
        emulator.step_frame();
        const auto future = emulator.save_state();
        REQUIRE(emulator.load_state(early));
        emulator.step_frame();
        REQUIRE((emulator.save_state() == future));
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        auto bad = early;
        bad[bad.size() - tail.data().size() - 1U] = 0xFFU;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
struct Scratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mapperbus-263-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(path);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
TEST_CASE("KOF97 battery bytes reload without applying the register permutation",
          "[mappers][kof97][battery]") {
    Scratch scratch;
    const auto path = (scratch.path / "263.sav").string();
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(cartridge(true), path));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.read_cpu(0x12U) == 0xC7U);
        REQUIRE(writer.flush_persistent_data());
    }
    auto bytes = read_binary_file_capped(path, 8192U, "263 save");
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 8192U);
    REQUIRE((*bytes)[0U] == 0xC7U);
    REQUIRE((*bytes)[2U] == 1U);
    REQUIRE((*bytes)[3U] == 0x35U);
    Emulator reader;
    REQUIRE(reader.load_cartridge(cartridge(true), path));
    reader.reset();
    reader.step_frame();
    REQUIRE(reader.read_cpu(0x6000U) == 0xC7U);
    REQUIRE(reader.read_cpu(0x6002U) == 2U);
    REQUIRE(write_binary_file_atomic(path, std::vector<Byte>(8193U), "oversize 263 save"));
    const auto before = reader.save_state();
    REQUIRE_FALSE(reader.load_battery_ram(path));
    REQUIRE((reader.save_state() == before));
}
} // namespace
