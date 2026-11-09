#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/serial_eeprom.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader fcg_header(uint16_t id, Byte sub = 0U, std::size_t nvram = 0U) {
    INesHeader h{};
    h.mapper_number = id;
    h.submapper = sub;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_nvram_size = nvram;
    h.has_battery = nvram != 0U;
    if (id == 153U || (id == 16U && sub == 3U))
        h.chr_ram_size = 8192U;
    return h;
}
std::vector<Byte> fcg_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank);
    return data;
}
std::unique_ptr<Mapper> fcg_board(INesHeader h) {
    register_builtin_mappers();
    const bool ram = h.mapper_number == 153U || (h.mapper_number == 16U && h.submapper == 3U);
    auto result =
        MapperRegistry::instance().create(h,
                                          fcg_rom(ram ? 0x80000U : 0x40000U, 0x4000U),
                                          ram ? std::vector<Byte>{} : fcg_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}

struct EepromMaster {
    Mapper& mapper;
    bool small;
    void pins(bool clock, bool data, bool input = false) {
        mapper.write_prg(
            0x800DU,
            static_cast<Byte>((clock ? 0x20U : 0U) | (data ? 0x40U : 0U) | (input ? 0x80U : 0U)));
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
        const bool ack = (mapper.read_prg(0x6000U) & 0x10U) == 0U;
        pins(false, true);
        return ack;
    }
    Byte receive(bool ack) {
        Byte value = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            pins(false, true);
            pins(true, true, true);
            value =
                static_cast<Byte>((static_cast<unsigned>(value) << 1U) |
                                  ((static_cast<unsigned>(mapper.read_prg(0x6000U)) >> 4U) & 1U));
        }
        pins(false, !ack);
        pins(true, !ack);
        pins(false, true);
        return value;
    }
    void write_address(Byte address) {
        start();
        REQUIRE(send(small ? static_cast<Byte>(address << 1U) : 0xA0U));
        if (!small)
            REQUIRE(send(address));
    }
    void read_address(Byte address) {
        if (!small)
            write_address(address);
        start();
        REQUIRE(
            send(small ? static_cast<Byte>((static_cast<unsigned>(address) << 1U) | 1U) : 0xA1U));
    }
    void settle() {
        for (unsigned i = 0U; i < 9000U; ++i)
            mapper.clock_cpu();
    }
};

Cartridge fcg_cartridge(INesHeader h, bool program = true) {
    const bool ram = h.mapper_number == 153U || (h.mapper_number == 16U && h.submapper == 3U);
    const auto prg_size = ram ? 0x80000U : 0x40000U;
    const auto chr_size = ram ? 0U : 0x40000U;
    std::vector<Byte> image(16U + prg_size + chr_size, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = static_cast<Byte>(prg_size / 0x4000U);
    image[5] = static_cast<Byte>(chr_size / 0x2000U);
    image[6] = static_cast<Byte>(((h.mapper_number & 15U) << 4U) | (h.has_battery ? 2U : 0U));
    image[7] = static_cast<Byte>((h.mapper_number & 0xF0U) | 8U);
    image[8] = static_cast<Byte>(h.submapper << 4U);
    image[10] = h.prg_nvram_size == 128U   ? 0x10U
                : h.prg_nvram_size == 256U ? 0x20U
                : h.prg_nvram_size != 0U   ? 0x70U
                                           : 0U;
    image[11] = ram ? 7U : 0U;
    auto prg = fcg_rom(prg_size, 0x4000U);
    if (program) {
        std::vector<Byte> code = {0x78U}; // Original program in each fixed bank.
        const Address base = h.submapper == 4U ? 0x6000U : 0x8000U;
        const auto store = [&code](Address addr, Byte value) {
            code.insert(
                code.end(),
                {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
        };
        store(base + 10U, 0U);
        store(base + 8U, 2U);
        if (ram) {
            for (unsigned reg = 0U; reg < 4U; ++reg)
                store(static_cast<Address>(base + reg), 1U);
            store(0x800DU, 0x20U);
            store(0x6005U, 0x5AU);
        } else if (h.prg_nvram_size != 0U) {
            const bool small = h.prg_nvram_size == 128U;
            const auto pins = [&store](bool clock, bool data, bool input = false) {
                store(0x800DU,
                      static_cast<Byte>((clock ? 0x20U : 0U) | (data ? 0x40U : 0U) |
                                        (input ? 0x80U : 0U)));
            };
            const auto start = [&pins] {
                pins(false, true);
                pins(true, true);
                pins(true, false);
                pins(false, false);
            };
            const auto stop = [&pins] {
                pins(false, false);
                pins(true, false);
                pins(true, true);
                pins(false, true, true);
            };
            const auto send = [&pins](Byte b) {
                for (unsigned bit = 0U; bit < 8U; ++bit) {
                    const bool data = (b & (0x80U >> bit)) != 0U;
                    pins(false, data);
                    pins(true, data);
                }
                pins(false, true);
                pins(true, false, true);
                pins(false, true);
            };
            start();
            send(small ? 0x24U : 0xA0U);
            if (!small)
                send(0x12U);
            send(0x5AU);
            stop();
            // More than the deterministic EEPROM programming interval.
            code.insert(code.end(),
                        {0xA0U, 10U, 0xA2U, 0U, 0xCAU, 0xD0U, 0xFDU, 0x88U, 0xD0U, 0xF8U});
            if (!small) {
                start();
                send(0xA0U);
                send(0x12U);
            }
            start();
            send(small ? 0x25U : 0xA1U);
            for (unsigned bit = 0U; bit < 8U; ++bit) {
                pins(false, true);
                pins(true, true, true);
                code.insert(code.end(),
                            {0xADU, 0U, 0x60U, 0x29U, 0x10U, 0xC9U, 0x10U, 0x26U, 0x10U});
            }
            pins(false, true);
            pins(true, true);
            pins(false, true);
            stop();
        }
        store(base, ram ? 1U : 7U);
        store(0x2006U, 0U);
        store(0x2006U, 0U);
        if (ram)
            store(0x2007U, 0xA5U);
        store(0x2006U, 0U);
        store(0x2006U, 0U);
        code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x11U});
        store(base + 11U, 30U);
        store(base + 12U, 0U);
        store(base + 10U, 1U);
        code.push_back(0x58U);
        const auto loop = static_cast<Address>(0xC000U + code.size());
        code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
        const std::array<Byte, 8> handler = {0xE6U,
                                             0x20U,
                                             0xA9U,
                                             0U,
                                             0x8DU,
                                             static_cast<Byte>(base + 10U),
                                             static_cast<Byte>((base + 10U) >> 8U),
                                             0x40U};
        for (std::size_t outer = 0U; outer < prg_size; outer += 0x40000U) {
            REQUIRE(code.size() < 0x800U);
            std::ranges::copy(code, prg.begin() + static_cast<std::ptrdiff_t>(outer + 0x3C000U));
            std::ranges::copy(handler, prg.begin() + static_cast<std::ptrdiff_t>(outer + 0x3C800U));
            for (unsigned vector = 0x3FFFAU; vector <= 0x3FFFEU; vector += 2U) {
                prg[outer + vector] = 0U;
                prg[outer + vector + 1U] = vector == 0x3FFFCU ? 0xC0U : 0xC8U;
            }
        }
    }
    std::ranges::copy(prg, image.begin() + 16U);
    std::ranges::copy(fcg_rom(chr_size, 0x400U), image.begin() + 16U + prg_size);
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE(
    "Bandai CPU EEPROM and IRQ execution replays snapshots and rejects corrupt state atomically",
    "[mappers][fcg][integration][state]") {
    const std::array headers = {fcg_header(16U, 4U),
                                fcg_header(16U, 5U),
                                fcg_header(16U, 5U, 256U),
                                fcg_header(153U, 0U, 8192U),
                                fcg_header(159U, 0U, 128U),
                                fcg_header(16U, 1U, 128U),
                                fcg_header(16U, 3U, 8192U)};
    for (const auto& h : headers) {
        INFO(h.mapper_number << ':' << static_cast<unsigned>(h.submapper));
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(fcg_cartridge(h), {}));
        emulator.reset();
        for (unsigned instruction = 0U; instruction < 40U; ++instruction)
            emulator.step_instruction();
        const auto partial = emulator.save_state();
        const auto finish = [&] {
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x11U) == (h.chr_ram_size != 0U ? 0xA5U : 7U));
            REQUIRE(emulator.read_cpu(0x20U) == 1U);
            if (h.prg_nvram_size == 128U || h.prg_nvram_size == 256U)
                REQUIRE(emulator.read_cpu(0x10U) == 0x5AU);
            if (h.chr_ram_size != 0U)
                REQUIRE(emulator.read_cpu(0x6005U) == 0x5AU);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(partial));
        finish();
        REQUIRE(emulator.save_state() == expected);
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        StateWriter board_state;
        fcg_cartridge(h, false).save_state(board_state);
        const auto start = expected.size() - tail.data().size() - board_state.data().size();
        for (const std::size_t field : {8U,
                                        9U,
                                        10U,
                                        11U,
                                        12U,
                                        17U,
                                        18U,
                                        16403U + 261U,
                                        16403U + 262U,
                                        16403U + 263U,
                                        16403U + 264U,
                                        16403U + 265U,
                                        16403U + 267U,
                                        16403U + 268U,
                                        16403U + 269U,
                                        16403U + 270U,
                                        16403U + 271U,
                                        16403U + 272U,
                                        16403U + 274U}) {
            auto corrupt = expected;
            corrupt[start + field] = 0xFFU;
            REQUIRE_FALSE(emulator.load_state(corrupt));
            REQUIRE(emulator.save_state() == expected);
        }
    }
}

TEST_CASE("Bandai EEPROM and WRAM saves contain exactly their physical persistent bytes",
          "[mappers][fcg][battery]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-bandai-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    for (const auto h :
         {fcg_header(16U, 5U, 256U), fcg_header(159U, 0U, 128U), fcg_header(153U, 0U, 8192U)}) {
        const auto path = root / (std::to_string(h.mapper_number) + ".sav");
        {
            Emulator writer;
            REQUIRE(writer.load_cartridge(fcg_cartridge(h), path.string()));
            writer.reset();
            writer.step_frame();
            writer.flush_battery_ram();
        }
        const auto data = read_binary_file_capped(path.string(), h.prg_nvram_size, "battery");
        REQUIRE(data);
        REQUIRE(data->size() == h.prg_nvram_size);
        const auto index = h.chr_ram_size != 0U ? 5U : 0x12U;
        REQUIRE((*data)[index] == 0x5AU);
        Emulator reader;
        REQUIRE(reader.load_cartridge(fcg_cartridge(h), path.string()));
        const auto restored_path = root / "restored.sav";
        REQUIRE(reader.save_battery_ram(restored_path.string()));
        const auto restored =
            read_binary_file_capped(restored_path.string(), h.prg_nvram_size, "battery");
        REQUIRE(restored);
        REQUIRE(*restored == *data);
        const auto before = reader.save_state();
        REQUIRE(write_binary_file_atomic(
            path.string(), std::vector<Byte>(h.prg_nvram_size + 1U), "battery"));
        REQUIRE_FALSE(reader.load_battery_ram(path.string()));
        REQUIRE(reader.save_state() == before);
    }
}

TEST_CASE("Mapper 153 PPU pins select outer banks through nametables and cached background fetches",
          "[mappers][fcg][ppu]") {
    auto cart = fcg_cartridge(fcg_header(153U, 0U, 8192U), false);
    cart.write_prg(0x8008U, 3U);
    cart.write_prg(0x8001U, 1U);
    cart.write_prg(0x8003U, 1U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    const auto address = [&ppu](Address addr) {
        ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
        ppu.write_register(6U, static_cast<Byte>(addr));
    };
    for (unsigned table = 0U; table < 4U; ++table) {
        address(static_cast<Address>(0x2000U + table * 0x400U));
        REQUIRE(cart.read_prg(0x8000U) == ((table & 1U) != 0U ? 19U : 3U));
        REQUIRE(cart.read_prg(0xC000U) == ((table & 1U) != 0U ? 31U : 15U));
    }
    address(0x2000U);
    for (unsigned tile = 0U; tile < 32U; ++tile)
        ppu.write_register(7U, 0x40U); // Pattern tile addresses PPU A10 = 1.
    address(0U);
    ppu.write_register(1U, 8U);
    ppu.step_dots(19U);                    // Allow the renderer's delayed PPUMASK to settle.
    REQUIRE(cart.read_prg(0x8000U) == 3U); // Attribute fetch.
    ppu.step_dots(1U);
    REQUIRE(cart.read_prg(0x8000U) == 19U); // Pattern fetch.
    ppu.step_dots(3U);
    REQUIRE(cart.read_prg(0x8000U) == 19U); // Pixel cache cannot suppress pins.
    ppu.step_dots(1U);
    REQUIRE(cart.read_prg(0x8000U) == 3U); // Next nametable fetch.
}

TEST_CASE("Bandai IRQ counter distinguishes direct writes from latched reloads",
          "[mappers][fcg][timing]") {
    for (const Byte sub : {4U, 5U}) {
        auto mapper = fcg_board(fcg_header(16U, sub));
        const Address base = sub == 4U ? 0x6000U : 0x8000U;
        mapper->write_prg(base + 11U, 2U);
        mapper->write_prg(base + 12U, 0U);
        mapper->write_prg(base + 10U, 1U);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(base + 11U, 5U);
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending() == (sub == 5U));
        if (sub == 4U) {
            for (unsigned i = 0U; i < 3U; ++i)
                mapper->clock_cpu();
            REQUIRE_FALSE(mapper->irq_pending());
            mapper->clock_cpu();
            REQUIRE(mapper->irq_pending());
        }
        mapper->write_prg(base + 10U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(base + 11U, 0U);
        mapper->write_prg(base + 12U, 0U);
        mapper->write_prg(base + 10U, 1U);
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(base + 10U, 0U);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    auto mapper = fcg_board(fcg_header(16U));
    mapper->write_prg(0x600BU, 3U);
    mapper->write_prg(0x600AU, 1U);
    mapper->write_prg(0x800BU, 9U);
    for (unsigned i = 0U; i < 3U; ++i)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending()); // High writes only changed the latch.
    mapper->write_prg(0x800AU, 1U);
    for (unsigned i = 0U; i < 8U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Bandai CHR registers and all four mirroring modes are independent", "[mappers][fcg]") {
    auto mapper = fcg_board(fcg_header(159U, 0U, 128U));
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xFFF0U + slot), static_cast<Byte>(0xF8U + slot));
        const auto addr = static_cast<Address>(slot * 0x400U);
        REQUIRE(mapper->read_chr(addr) == 0xF8U + slot);
        mapper->write_chr(addr, 0U);
        REQUIRE(mapper->read_chr(addr) == 0xF8U + slot);
    }
    const std::array<MirrorMode, 4> modes = {MirrorMode::Vertical,
                                             MirrorMode::Horizontal,
                                             MirrorMode::SingleLower,
                                             MirrorMode::SingleUpper};
    for (unsigned mode = 0U; mode < modes.size(); ++mode) {
        mapper->write_prg(0xFFF9U, static_cast<Byte>(0xFCU | mode));
        REQUIRE(mapper->mirror_mode() == modes[mode]);
    }
    REQUIRE_FALSE(mapper->maps_prg(0x6000U)); // Direction must release SDA for CPU reads.
    mapper->write_prg(0x800DU, 0x80U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0x10U);
}

TEST_CASE("Xicor EEPROMs write on STOP, NACK while busy and serialize physical MSB-first bytes",
          "[mappers][fcg][eeprom]") {
    for (const bool small : {false, true}) {
        auto mapper =
            fcg_board(fcg_header(small ? 159U : 16U, small ? 0U : 5U, small ? 128U : 256U));
        EepromMaster ee{.mapper = *mapper, .small = small};
        ee.write_address(0x41U);
        REQUIRE(ee.send(0x96U));
        REQUIRE(mapper->battery_ram()[0x41U] == 0xFFU);
        ee.stop();
        ee.start();
        REQUIRE_FALSE(ee.send(small ? 0x82U : 0xA0U));
        ee.stop();
        for (unsigned i = 0U; i < 8999U; ++i)
            mapper->clock_cpu();
        REQUIRE(mapper->battery_ram()[0x41U] == 0xFFU);
        mapper->clock_cpu();
        REQUIRE(mapper->battery_ram()[0x41U] == 0x96U);
        REQUIRE(mapper->battery_ram().size() == (small ? 128U : 256U));
        ee.read_address(0x41U);
        REQUIRE(ee.receive(false) == 0x96U);
        ee.stop();
        REQUIRE(mapper->battery_ram()[0x02U] == 0xFFU); // No legacy game bit-reversal heuristic.
    }
}

TEST_CASE("Xicor four-byte pages wrap locally and sequential reads wrap the whole device",
          "[mappers][fcg][eeprom]") {
    for (const bool small : {false, true}) {
        auto mapper =
            fcg_board(fcg_header(small ? 159U : 16U, small ? 0U : 5U, small ? 128U : 256U));
        EepromMaster ee{.mapper = *mapper, .small = small};
        ee.write_address(3U);
        for (Byte b = 0x10U; b < 0x16U; ++b)
            REQUIRE(ee.send(b));
        ee.stop();
        ee.settle();
        const std::array<Byte, 4> expected = {0x15U, 0x12U, 0x13U, 0x14U};
        REQUIRE(std::ranges::equal(mapper->battery_ram().first(4U), expected));
        REQUIRE(mapper->battery_ram()[4U] == 0xFFU);
        auto data = std::vector<Byte>(small ? 128U : 256U);
        for (unsigned i = 0U; i < data.size(); ++i)
            data[i] = static_cast<Byte>(i);
        mapper->set_battery_ram(data);
        ee.read_address(static_cast<Byte>(data.size() - 1U));
        REQUIRE(ee.receive(true) == data.size() - 1U);
        REQUIRE(ee.receive(false) == 0U);
        ee.stop();
        if (!small) {
            ee.start();
            REQUIRE(ee.send(0xA1U));
            REQUIRE(ee.receive(false) == 1U);
            ee.stop(); // Current-address read after NACK.
            ee.start();
            REQUIRE_FALSE(ee.send(0xA2U));
            ee.stop(); // Unwired slave address.
        }
    }
}

TEST_CASE("EEPROM repeated START and incomplete bytes do not program an abandoned page",
          "[mappers][fcg][eeprom]") {
    auto mapper = fcg_board(fcg_header(16U, 5U, 256U));
    EepromMaster ee{.mapper = *mapper, .small = false};
    ee.write_address(8U);
    REQUIRE(ee.send(0x55U));
    ee.start();
    REQUIRE(ee.send(0xA1U));
    REQUIRE(ee.receive(false) == 0xFFU);
    ee.stop();
    ee.settle();
    REQUIRE(mapper->battery_ram()[8U] == 0xFFU);
    ee.write_address(8U);
    for (unsigned i = 0U; i < 4U; ++i) {
        ee.pins(false, false);
        ee.pins(true, false);
    }
    ee.stop();
    ee.settle();
    REQUIRE(mapper->battery_ram()[8U] == 0xFFU);
}

TEST_CASE("Mapper 153 selects both PRG windows from the current PPU A10/A11 and gates real WRAM",
          "[mappers][fcg][wram]") {
    auto mapper = fcg_board(fcg_header(153U, 0U, 8192U));
    mapper->write_prg(0x8008U, 3U);
    for (unsigned reg = 0U; reg < 4U; ++reg)
        mapper->write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg & 1U));
    for (unsigned page = 0U; page < 8U; ++page) {
        mapper->write_chr(static_cast<Address>(page * 0x400U), static_cast<Byte>(0xA0U + page));
        REQUIRE(mapper->read_prg(0x8000U) == ((page & 1U) != 0U ? 19U : 3U));
        REQUIRE(mapper->read_prg(0xC000U) == ((page & 1U) != 0U ? 31U : 15U));
    }
    for (unsigned page = 0U; page < 8U; ++page)
        REQUIRE(mapper->read_chr(static_cast<Address>(page * 0x400U)) == 0xA0U + page);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x800DU, 0x20U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6005U) == 0xFFU);
    mapper->write_prg(0x6005U, 0x5AU);
    mapper->write_prg(0x7FFFU, 0x66U);
    REQUIRE(mapper->battery_ram().size() == 8192U);
    REQUIRE(mapper->battery_ram()[5U] == 0x5AU);
    mapper->write_prg(0x800DU, 0U);
    mapper->write_prg(0x6005U, 0x99U);
    mapper->reset();
    mapper->write_prg(0x800DU, 0x20U);
    REQUIRE(mapper->read_prg(0x6005U) == 0x5AU);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0x66U);
}

TEST_CASE("Bandai FCG variants decode their own register windows", "[mappers][fcg]") {
    for (const Byte sub : {0U, 4U, 5U}) {
        auto mapper = fcg_board(fcg_header(16U, sub));
        mapper->write_prg(0x7FF8U, 3U);
        REQUIRE(mapper->read_prg(0x8000U) == (sub == 5U ? 0U : 3U));
        mapper->write_prg(0xFFF8U, 7U);
        REQUIRE(mapper->read_prg(0x8000U) == (sub == 4U ? 3U : 7U));
        REQUIRE(mapper->read_prg(0xC000U) == 15U);
    }
}

TEST_CASE("Bandai metadata selects physical memories and rejects unsupported variants",
          "[mappers][fcg][geometry]") {
    register_builtin_mappers();
    const auto create = [](INesHeader h, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(h, fcg_rom(prg, 0x4000U), fcg_rom(chr, 0x400U));
    };
    for (const auto h : {fcg_header(16U),
                         fcg_header(16U, 4U),
                         fcg_header(16U, 5U),
                         fcg_header(16U, 5U, 256U),
                         fcg_header(16U, 1U, 128U),
                         fcg_header(16U, 3U, 8192U),
                         fcg_header(153U, 0U, 8192U),
                         fcg_header(159U, 0U, 128U)}) {
        const bool ram = h.chr_ram_size != 0U;
        REQUIRE(create(h, ram ? 0x80000U : 0x40000U, ram ? 0U : 0x40000U));
        REQUIRE_FALSE(create(h, 0x4000U, ram ? 0U : 0x40000U));
        REQUIRE_FALSE(create(h, 0x84000U, ram ? 0U : 0x40000U));
        REQUIRE_FALSE(create(h, 0x8000U, ram ? 0x2000U : 0U));
        for (unsigned field = 0U; field < 6U; ++field) {
            auto bad = h;
            switch (field) {
            case 0:
                bad.submapper = 2U;
                break; // Datach is a separate device.
            case 1:
                bad.prg_nvram_size = 512U;
                break;
            case 2:
                bad.prg_ram_size = 8192U;
                break;
            case 3:
                bad.chr_nvram_size = 8192U;
                break;
            case 4:
                bad.has_battery = !h.has_battery;
                break;
            default:
                bad.mirror_mode = MirrorMode::FourScreen;
                break;
            }
            REQUIRE_FALSE(create(bad, 0x40000U, ram ? 0U : 0x40000U));
        }
    }
    for (const uint16_t id : {16U, 153U, 159U}) {
        auto legacy = fcg_header(id);
        legacy.is_nes2 = false;
        legacy.has_battery = true;
        legacy.prg_ram_size = 8192U; // Legacy implicit RAM is not an EEPROM size.
        auto result = create(legacy, 0x40000U, id == 153U ? 0U : 0x40000U);
        REQUIRE(result);
        REQUIRE((*result)->battery_ram().size() == (id == 153U ? 8192U : id == 159U ? 128U : 256U));
    }
}

TEST_CASE("Bandai serial reads preserve CPU open bus and IRQs run during OAM DMA",
          "[mappers][fcg][bus]") {
    for (const auto h : {fcg_header(16U, 4U),
                         fcg_header(16U, 5U, 256U),
                         fcg_header(153U, 0U, 8192U),
                         fcg_header(159U, 0U, 128U)}) {
        auto cart = fcg_cartridge(h, false);
        MemoryBus bus;
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        bus.connect_cartridge(&cart);
        bus.connect_ppu(&ppu);
        if (h.prg_nvram_size == 128U || h.prg_nvram_size == 256U) {
            bus.write(0x800DU, 0x80U); // Read mode releases SDA even when bit 6 is low.
            bus.write(0U, 0xA5U);
            REQUIRE(bus.read(0x6000U) == 0xB5U); // Only D4 is driven by the EEPROM.
            bus.write(0x800DU, 0U);
            bus.write(0U, 0xA5U);
            REQUIRE(bus.read(0x6000U) == 0xA5U);
        }
        const Address base = h.submapper == 4U ? 0x6000U : 0x8000U;
        bus.write(base + 11U, 0U);
        bus.write(base + 12U, 2U); // 512 cycles, inside a 513-cycle OAM transfer.
        bus.write(base + 10U, 1U);
        REQUIRE_FALSE(cart.irq_pending());
        REQUIRE(bus.run_oam_dma(0U, 0xC000U, false) >= 513U);
        REQUIRE(cart.irq_pending());
        bus.write(base + 10U, 0U);
        REQUIRE_FALSE(cart.irq_pending());
    }
}

TEST_CASE("EEPROM snapshots resume partial data bytes read bits and pending programming",
          "[mappers][fcg][eeprom][state]") {
    for (const bool small : {false, true}) {
        const auto h = fcg_header(small ? 159U : 16U, small ? 0U : 5U, small ? 128U : 256U);
        auto original = fcg_board(h);
        auto restored = fcg_board(h);
        EepromMaster ee{.mapper = *original, .small = small};
        EepromMaster clone{.mapper = *restored, .small = small};
        const auto copy_state = [&] {
            StateWriter state;
            original->save_state(state);
            StateReader reader(state.data());
            restored->load_state(reader);
            REQUIRE(reader.ok());
        };
        ee.write_address(0x25U);
        constexpr Byte kValue = 0x96U;
        for (unsigned bit = 0U; bit < 4U; ++bit) {
            const bool data = (kValue & (0x80U >> bit)) != 0U;
            ee.pins(false, data);
            ee.pins(true, data);
        }
        copy_state();
        for (unsigned bit = 4U; bit < 8U; ++bit) {
            const bool data = (kValue & (0x80U >> bit)) != 0U;
            clone.pins(false, data);
            clone.pins(true, data);
        }
        clone.pins(false, true);
        clone.pins(true, true, true);
        REQUIRE(restored->read_prg(0x6000U) == 0U); // ACK survives the partial-byte snapshot.
        clone.pins(false, true);
        clone.stop();
        for (unsigned i = 0U; i < 4321U; ++i)
            restored->clock_cpu();
        StateWriter programming;
        restored->save_state(programming);
        StateReader programming_reader(programming.data());
        original->load_state(programming_reader);
        REQUIRE(programming_reader.ok());
        original->reset(); // Soft reset retains the independent EEPROM write.
        for (unsigned i = 0U; i < 4678U; ++i)
            original->clock_cpu();
        REQUIRE(original->battery_ram()[0x25U] == 0xFFU);
        original->clock_cpu();
        REQUIRE(original->battery_ram()[0x25U] == kValue);
        ee.read_address(0x25U);
        Byte value = 0U;
        for (unsigned bit = 0U; bit < 3U; ++bit) {
            ee.pins(false, true);
            ee.pins(true, true, true);
            value = static_cast<Byte>((static_cast<unsigned>(value) << 1U) |
                                      (static_cast<unsigned>(original->read_prg(0x6000U)) >> 4U));
        }
        copy_state();
        for (unsigned bit = 3U; bit < 8U; ++bit) {
            clone.pins(false, true);
            clone.pins(true, true, true);
            value = static_cast<Byte>((static_cast<unsigned>(value) << 1U) |
                                      (static_cast<unsigned>(restored->read_prg(0x6000U)) >> 4U));
        }
        REQUIRE(value == kValue);
        clone.pins(false, true);
        clone.pins(true, true);
        clone.pins(false, true);
        clone.stop();
    }
}

} // namespace
