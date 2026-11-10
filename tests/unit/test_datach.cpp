#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader datach_header(bool external = true) {
    INesHeader h{};
    h.mapper_number = 157U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.chr_ram_size = 8192U;
    h.has_battery = external;
    h.prg_nvram_size = external ? 128U : 0U;
    return h;
}
std::unique_ptr<Mapper> datach_board(bool external = true, unsigned banks = 16U) {
    register_builtin_mappers();
    std::vector<Byte> rom(static_cast<std::size_t>(banks) * 0x4000U);
    for (std::size_t i = 0U; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>(i / 0x4000U);
    auto board = MapperRegistry::instance().create(datach_header(external), std::move(rom), {});
    REQUIRE(board);
    return std::move(*board);
}
struct DatachMaster {
    Mapper& board;
    bool external = false;
    bool clock = false;
    void pins(bool scl, bool sda, bool input = false) {
        const auto clocks = [this](bool level) {
            for (Address reg = 0x8000U; reg < 0x8004U; ++reg)
                board.write_prg(reg, level ? 8U : 0U);
        };
        if (external && !scl)
            clocks(false);
        board.write_prg(0x800DU,
                        static_cast<Byte>((!external && scl ? 0x20U : 0U) | (sda ? 0x40U : 0U) |
                                          (input ? 0x80U : 0U)));
        if (external && scl && !clock)
            clocks(true);
        clock = scl;
    }
    void start() {
        pins(false, true);
        pins(true, true);
        pins(true, false);
        pins(false, false);
    }
    void stop() {
        pins(false, false);
        pins(true, false);
        pins(true, true);
        pins(false, true, true);
    }
    bool send(Byte value) {
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool data = (value & (0x80U >> bit)) != 0U;
            pins(false, data);
            pins(true, data);
        }
        pins(false, true);
        pins(true, false, true);
        const bool ack = (board.read_prg(0x6000U) & 0x10U) == 0U;
        pins(false, true);
        return ack;
    }
    Byte receive(bool ack = false) {
        Byte value = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            pins(false, true);
            pins(true, true, true);
            value =
                static_cast<Byte>((static_cast<unsigned>(value) << 1U) |
                                  ((static_cast<unsigned>(board.read_prg(0x6000U)) >> 4U) & 1U));
        }
        pins(false, !ack);
        pins(true, !ack);
        pins(false, true);
        return value;
    }
    void address(Byte addr) {
        start();
        REQUIRE(send(external ? static_cast<Byte>(addr << 1U) : 0xA0U));
        if (!external)
            REQUIRE(send(addr));
    }
    void write(Byte addr, Byte value) {
        address(addr);
        REQUIRE(send(value));
        stop();
    }
    void read_address(Byte addr) {
        if (!external)
            address(addr);
        start();
        REQUIRE(
            send(external ? static_cast<Byte>((static_cast<unsigned>(addr) << 1U) | 1U) : 0xA1U));
    }
    void settle() {
        for (unsigned i = 0U; i < 9000U; ++i)
            board.clock_cpu();
    }
};

Cartridge datach_cart(bool external = true,
                      bool program = false,
                      Byte identity = 0U,
                      bool readback = false) {
    std::vector<Byte> image(16U + 0x40000U, 0xEAU);
    std::fill_n(image.begin(), 16U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1A;
    image[4] = 16U;
    image[6] = external ? 0xD2U : 0xD0U;
    image[7] = 0x98U;
    image[10] = external ? 0x10U : 0U;
    image[11] = 7U;
    image[16] = identity;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    if (program) {
        store(0x4017U, 0x40U); // Isolate the mapper IRQ from APU frame IRQs.
        for (const bool chip : {false, true}) {
            if (chip && !external)
                continue;
            bool clock = false;
            const auto pins = [&store, &clock, chip](bool scl, bool data, bool input = false) {
                const auto clocks = [&store](bool level) {
                    for (Address reg = 0x8000U; reg < 0x8004U; ++reg)
                        store(reg, level ? 8U : 0U);
                };
                if (chip && !scl)
                    clocks(false);
                store(0x800DU,
                      static_cast<Byte>((!chip && scl ? 0x20U : 0U) | (data ? 0x40U : 0U) |
                                        (input ? 0x80U : 0U)));
                if (chip && scl && !clock)
                    clocks(true);
                clock = scl;
            };
            const auto send = [&pins](Byte value) {
                for (unsigned bit = 0U; bit < 8U; ++bit) {
                    const bool data = (value & (0x80U >> bit)) != 0U;
                    pins(false, data);
                    pins(true, data);
                }
                pins(false, true);
                pins(true, false, true);
                pins(false, true);
            };
            pins(false, true);
            pins(true, true);
            pins(true, false);
            pins(false, false);
            if (readback) {
                if (!chip) {
                    send(0xA0U);
                    send(0x12U);
                    pins(false, true);
                    pins(true, true);
                    pins(true, false);
                    pins(false, false);
                }
                send(chip ? 0x25U : 0xA1U);
                const Byte target = chip ? 0x11U : 0x10U;
                store(target, 0U);
                for (unsigned bit = 0U; bit < 8U; ++bit) {
                    pins(false, true);
                    pins(true, true, true);
                    code.insert(code.end(),
                                {0xADU, 0U, 0x60U, 0x29U, 0x10U, 0xC9U, 0x10U, 0x26U, target});
                }
                pins(false, true);
                pins(true, true);
                pins(false, true);
            } else {
                send(chip ? 0x24U : 0xA0U);
                if (!chip)
                    send(0x12U);
                send(chip ? 0xA6U : 0x5AU);
            }
            pins(false, false);
            pins(true, false);
            pins(true, true);
            pins(false, true, true);
        }
        store(0x800BU, 30U);
        store(0x800CU, 0U);
        store(0x800AU, 1U);
        code.push_back(0x58U);
    }
    const auto loop = static_cast<Address>(0xC000U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    std::ranges::copy(code, image.begin() + 16U + 0x3C000U);
    const std::array<Byte, 8> handler = {0xE6U, 0x20U, 0xA9U, 0U, 0x8DU, 0x0AU, 0x80U, 0x40U};
    std::ranges::copy(handler, image.begin() + 16U + 0x3D800U);
    for (unsigned vector = 0x3FFFAU; vector <= 0x3FFFEU; vector += 2U) {
        image[16U + vector] = 0U;
        image[16U + vector + 1U] = vector == 0x3FFFCU ? 0xC0U : 0xD8U;
    }
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image);
    REQUIRE(cart);
    return std::move(*cart);
}

TEST_CASE("Datach banks PRG and unbanked CHR RAM with exact memory metadata", "[mappers][datach]") {
    for (const bool external : {false, true}) {
        auto board = datach_board(external, 3U);
        REQUIRE(board->battery_ram().size() == (external ? 128U : 0U));
        REQUIRE(board->shared_nvram().size() == 256U);
        board->write_prg(0x7FF8U, 1U);
        REQUIRE(board->read_prg(0x8000U) == 0U);
        board->write_prg(0xFFF8U, 1U);
        REQUIRE(board->read_prg(0x8000U) == 1U);
        REQUIRE(board->read_prg(0xC000U) ==
                2U); // Fixed last bank, including non-power-of-two ROMs.
        for (unsigned page = 0U; page < 8U; ++page)
            board->write_chr(static_cast<Address>(page * 1024U), static_cast<Byte>(page + 0x30U));
        for (unsigned reg = 0U; reg < 8U; ++reg)
            board->write_prg(static_cast<Address>(0x8000U + reg), 0xFFU);
        for (unsigned page = 0U; page < 8U; ++page)
            REQUIRE(board->read_chr(static_cast<Address>(page * 1024U)) == page + 0x30U);
        const std::array modes = {MirrorMode::Vertical,
                                  MirrorMode::Horizontal,
                                  MirrorMode::SingleLower,
                                  MirrorMode::SingleUpper};
        for (Byte mode = 0U; mode < 4U; ++mode) {
            board->write_prg(0x8009U, mode);
            REQUIRE(board->mirror_mode() == modes[mode]);
        }
    }
    for (unsigned invalid = 0U; invalid < 8U; ++invalid) {
        auto h = datach_header();
        std::size_t chr = 0U;
        switch (invalid) {
        case 0:
            h.submapper = 1U;
            break;
        case 1:
            h.prg_nvram_size = 256U;
            break;
        case 2:
            h.prg_ram_size = 8192U;
            break;
        case 3:
            h.chr_nvram_size = 8192U;
            break;
        case 4:
            h.chr_ram_size = 4096U;
            break;
        case 5:
            h.has_battery = false;
            break;
        case 6:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            chr = 8192U;
            break;
        }
        REQUIRE_FALSE(MapperRegistry::instance().create(
            h, std::vector<Byte>(0x8000U), std::vector<Byte>(chr)));
    }
}

TEST_CASE("Datach independently addresses both EEPROMs and retains programming across reset",
          "[mappers][datach][eeprom]") {
    auto board = datach_board();
    DatachMaster internal{.board = *board};
    DatachMaster external{.board = *board, .external = true};
    internal.write(0x12U, 0x5AU);
    external.write(0x12U, 0xA6U);
    REQUIRE(board->shared_nvram()[0x12U] == 0xFFU);
    REQUIRE(board->battery_ram()[0x12U] == 0xFFU);
    board->reset();
    internal.settle();
    REQUIRE(board->shared_nvram()[0x12U] == 0x5AU);
    REQUIRE(board->battery_ram()[0x12U] == 0xA6U);
    internal.read_address(0x12U);
    REQUIRE(internal.receive() == 0x5AU);
    internal.stop();
    external.read_address(0x12U);
    REQUIRE(external.receive() == 0xA6U);
    external.stop();
}

TEST_CASE("Datach CPU bus gates scanner and EEPROM data while IRQs clock during DMA",
          "[mappers][datach][bus]") {
    auto cart = datach_cart();
    Ppu ppu;
    MemoryBus bus;
    ppu.connect_cartridge(&cart);
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
    bus.write(0x800DU, 0x80U);
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xBDU);
    cart.write_prg(0x800BU, 0U);
    cart.write_prg(0x800CU, 2U);
    cart.write_prg(0x800AU, 1U);
    REQUIRE_FALSE(cart.irq_pending());
    REQUIRE(bus.run_oam_dma(0U, 0xC000U, false) >= 513U);
    REQUIRE(cart.irq_pending());
    cart.write_prg(0x800AU, 0U);
    REQUIRE_FALSE(cart.irq_pending());
}

TEST_CASE("Datach barcode waveform matches literal EAN guards digits and quiet zones",
          "[mappers][datach][barcode]") {
    auto board = datach_board(false);
    // EAN-13 4006381333931, GS1 number sets and leading-digit parity.
    const std::string modules = "00000000000"
                                "101000110101001110101111011110100010010110011010101000010100001010"
                                "00010111010010000101100110101"
                                "00000000000";
    REQUIRE(modules.size() == 117U);
    REQUIRE(board->scan_barcode("4006381333931", 3U));
    board->write_prg(0x800DU, 0x80U);
    REQUIRE_FALSE(board->scan_barcode("96385074", 3U));
    for (char bit : modules) {
        for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
            REQUIRE((board->read_prg(0x6000U) & 8U) == (bit == '0' ? 8U : 0U));
            board->clock_cpu();
        }
    }
    REQUIRE((board->read_prg(0x6000U) & 8U) == 8U);
    REQUIRE(board->scan_barcode("96385074", 1U));
    for (unsigned i = 0U; i < 89U; ++i)
        board->clock_cpu();
    REQUIRE(board->scan_barcode("036000291452", 1U));
    for (unsigned i = 0U; i < 117U; ++i)
        board->clock_cpu();
    for (const auto code : {"", "123", "1234567a", "12345678901234"})
        REQUIRE_FALSE(board->scan_barcode(code, 1U));
    REQUIRE_FALSE(board->scan_barcode("96385074", 0U));
    REQUIRE_FALSE(board->scan_barcode("96385074", 1000001U));
}

TEST_CASE("Datach PPU address selects the cartridge EEPROM clock outside pattern memory",
          "[mappers][datach][ppu]") {
    auto cart = datach_cart();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0x8000U, 0U);
    cart.write_prg(0x8001U, 8U);
    cart.write_prg(0x8002U, 0U);
    cart.write_prg(0x8003U, 8U);
    cart.write_prg(0x800DU, 0x40U);
    for (unsigned table = 0U; table < 4U; ++table) {
        ppu.write_register(6U, static_cast<Byte>(0x20U + table * 4U));
        ppu.write_register(6U, 0U);
        StateWriter state;
        cart.save_state(state);
        REQUIRE(state.data()[8192U + 4U + 2U] == table);
        // The second EEPROM's serialized SCL follows selected register bit 3.
        REQUIRE(state.data()[8192U + 4U + 5U + 6U + 275U + 270U] == (table & 1U));
    }
}

struct DatachScratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mapperbus-datach-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    DatachScratch() {
        std::filesystem::create_directories(path);
    }
    ~DatachScratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

TEST_CASE(
    "Datach CPU execution replays mid-swipe and serial state with atomic malformed-state rejection",
    "[mappers][datach][state][integration]") {
    for (const bool external : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(datach_cart(external, true), {}));
        emulator.reset();
        REQUIRE(emulator.scan_barcode("4006381333931", 1000U));
        for (unsigned i = 0U; i < 95U; ++i)
            emulator.step_instruction();
        const auto partial = emulator.save_state();
        const auto finish = [&] {
            for (unsigned i = 0U; i < 5U; ++i)
                emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x20U) == 1U);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(partial));
        finish();
        REQUIRE(emulator.save_state() == expected);
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        StateWriter mapper;
        datach_cart(external).save_state(mapper);
        const auto start = expected.size() - tail.data().size() - mapper.data().size();
        REQUIRE(mapper.data().size() == 8895U);
        for (const std::size_t field :
             {8192U,        8196U,        8197U,        8198U,        8199U,        8200U,
              8205U,        8206U,        8207U + 261U, 8207U + 263U, 8207U + 265U, 8207U + 267U,
              8207U + 270U, 8207U + 274U, 8482U + 261U, 8482U + 263U, 8482U + 267U, 8482U + 274U,
              8757U,        8885U,        8886U,        8890U,        8894U}) {
            auto bad = expected;
            bad[start + field] = 0xFFU;
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE(emulator.save_state() == expected);
        }
        auto short_state = expected;
        short_state.resize(short_state.size() - 1U);
        REQUIRE_FALSE(emulator.load_state(short_state));
        REQUIRE(emulator.save_state() == expected);
        Emulator other;
        const auto loaded = other.load_cartridge(datach_cart(external, false, 7U), {});
        REQUIRE(loaded);
        REQUIRE_FALSE(other.load_state(expected));
    }
}

TEST_CASE("Datach device and cartridge saves stay separate and shared device memory survives game "
          "changes",
          "[mappers][datach][battery][integration]") {
    DatachScratch scratch;
    const auto unit = (scratch.path / "unit.sav").string();
    const auto game = (scratch.path / "battle-rush.sav").string();
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(datach_cart(true, true), game, unit));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.flush_persistent_data());
        const auto unit_data = read_binary_file_capped(unit, 256U, "unit");
        const auto game_data = read_binary_file_capped(game, 128U, "game");
        REQUIRE(unit_data);
        REQUIRE(game_data);
        REQUIRE(unit_data->size() == 256U);
        REQUIRE(game_data->size() == 128U);
        REQUIRE((*unit_data)[0x12U] == 0x5AU);
        REQUIRE((*game_data)[0x12U] == 0xA6U);
        // A differently spelled path still names the live device. Its newer
        // memory must take precedence over an older file at that same path.
        REQUIRE(write_binary_file_atomic(unit, std::vector<Byte>(256U, 0U), "older unit image"));
        REQUIRE(writer.load_cartridge(
            datach_cart(false, true, 2U, true), {}, (scratch.path / "." / "unit.sav").string()));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.read_cpu(0x10U) == 0x5AU);
        writer.unload_cartridge();
        REQUIRE(writer.load_cartridge(datach_cart(true, true, 3U, true), game, unit));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.read_cpu(0x10U) == 0x5AU);
        REQUIRE(writer.read_cpu(0x11U) == 0xA6U);
    }
    {
        Emulator reader;
        REQUIRE(reader.load_cartridge(datach_cart(true, true, 4U, true), game, unit));
        reader.reset();
        reader.step_frame();
        REQUIRE(reader.read_cpu(0x10U) == 0x5AU);
        REQUIRE(reader.read_cpu(0x11U) == 0xA6U);
        const auto before = reader.save_state();
        REQUIRE_FALSE(reader.load_cartridge(datach_cart(true), unit, unit));
        REQUIRE(reader.save_state() == before);
        REQUIRE_FALSE(reader.load_cartridge(
            datach_cart(true), game, (scratch.path / "." / "battle-rush.sav").string()));
        REQUIRE(reader.save_state() == before);
        for (const unsigned size : {255U, 257U}) {
            const auto bad = (scratch.path / (std::to_string(size) + ".sav")).string();
            REQUIRE(write_binary_file_atomic(bad, std::vector<Byte>(size), "bad"));
            REQUIRE_FALSE(reader.load_cartridge(datach_cart(false), {}, bad));
            REQUIRE(reader.save_state() == before);
        }
    }
    {
        Emulator memory;
        REQUIRE(memory.load_cartridge(datach_cart(false, true), {}));
        memory.reset();
        memory.step_frame();
        memory.unload_cartridge();
        REQUIRE(memory.load_cartridge(datach_cart(false, true, 9U, true), {}));
        memory.reset();
        memory.step_frame();
        REQUIRE(memory.read_cpu(0x10U) == 0x5AU);
    }
    {
        const auto staged = (scratch.path / "staged.sav").string();
        const auto staged_game = (scratch.path / "staged-game.sav").string();
        Emulator candidate;
        candidate.set_persistence_enabled(false);
        REQUIRE(candidate.load_cartridge(datach_cart(true, true), staged_game, staged));
        candidate.reset();
        candidate.step_frame();
        REQUIRE(candidate.flush_persistent_data());
        candidate.unload_cartridge();
        REQUIRE_FALSE(std::filesystem::exists(staged));
        REQUIRE_FALSE(std::filesystem::exists(staged_game));
    }
}

TEST_CASE("Datach shared SDA observes the active peer and partial read transfers replay",
          "[mappers][datach][eeprom][state]") {
    auto board = datach_board();
    board->set_shared_nvram(std::vector<Byte>(256U, 0xF0U));
    board->set_battery_ram(std::vector<Byte>(128U, 0x0FU));
    DatachMaster internal{.board = *board};
    DatachMaster external{.board = *board, .external = true};
    internal.read_address(0U); // Hold the internal read's high MSB with SCL low.
    external.read_address(0U); // The external low MSB must dominate shared SDA.
    REQUIRE((board->read_prg(0x6000U) & 0x10U) == 0U);
    Byte value = 0U;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        external.pins(false, true);
        external.pins(true, true, true);
        value = static_cast<Byte>((static_cast<unsigned>(value) << 1U) |
                                  ((static_cast<unsigned>(board->read_prg(0x6000U)) >> 4U) & 1U));
        StateWriter checkpoint;
        board->save_state(checkpoint);
        auto clone = datach_board();
        StateReader reader(checkpoint.data());
        clone->load_state(reader);
        REQUIRE(reader.ok());
        StateWriter replay;
        clone->save_state(replay);
        REQUIRE(replay.data() == checkpoint.data());
        if (bit < 7U) {
            DatachMaster resumed{.board = *clone, .external = true, .clock = true};
            resumed.pins(false, true);
            resumed.pins(true, true, true);
            REQUIRE(((static_cast<unsigned>(clone->read_prg(0x6000U)) >> 4U) & 1U) ==
                    ((0x0FU >> (6U - bit)) & 1U));
        }
    }
    REQUIRE(value == 0x0FU); // AND with the internal chip's held high output.
    external.pins(false, true);
    external.pins(true, true);
    external.pins(false, true);
    external.stop();
    REQUIRE(internal.receive() == 0xF0U);
    internal.stop();
}

TEST_CASE("Datach failed unit-save writes preserve the active machine during replacement",
          "[mappers][datach][battery]") {
    DatachScratch scratch;
    const auto parent = scratch.path / "device";
    std::filesystem::create_directories(parent);
    const auto unit = (parent / "unit.sav").string();
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(datach_cart(false, true), {}, unit));
    emulator.reset();
    emulator.step_frame();
    const auto before = emulator.save_state();
    std::filesystem::remove(parent);
    REQUIRE(write_binary_file_atomic(parent.string(), std::vector<Byte>{0U}, "blocking file"));
    REQUIRE_FALSE(emulator.flush_persistent_data());
    REQUIRE_FALSE(emulator.load_cartridge(datach_cart(false), {}));
    REQUIRE(emulator.save_state() == before);
    emulator.set_persistence_enabled(false);
}

TEST_CASE("Datach snapshots retain both busy EEPROM pages and their independent ACK polling",
          "[mappers][datach][eeprom][state]") {
    auto board = datach_board();
    DatachMaster internal{.board = *board};
    DatachMaster external{.board = *board, .external = true};
    internal.write(0x21U, 0x96U);
    external.write(0x21U, 0x69U);
    internal.start();
    REQUIRE_FALSE(internal.send(0xA0U));
    internal.stop();
    external.start();
    REQUIRE_FALSE(external.send(0x42U));
    external.stop();
    for (unsigned cycle = 0U; cycle < 1375U; ++cycle)
        board->clock_cpu();
    StateWriter checkpoint;
    board->save_state(checkpoint);
    auto clone = datach_board();
    StateReader reader(checkpoint.data());
    clone->load_state(reader);
    REQUIRE(reader.ok());
    clone->reset();
    for (unsigned cycle = 0U; cycle < 7624U; ++cycle)
        clone->clock_cpu();
    REQUIRE(clone->shared_nvram()[0x21U] == 0xFFU);
    REQUIRE(clone->battery_ram()[0x21U] == 0xFFU);
    clone->clock_cpu();
    REQUIRE(clone->shared_nvram()[0x21U] == 0x96U);
    REQUIRE(clone->battery_ram()[0x21U] == 0x69U);
}

} // namespace
