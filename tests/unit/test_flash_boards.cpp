#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "../support/flash_rom.hpp"
#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/sst39sf040.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader flash_header(uint16_t id, Byte sub = 0U, bool battery = false, Byte layout = 0U) {
    INesHeader h{};
    h.mapper_number = id;
    h.submapper = sub;
    h.is_nes2 = true;
    h.has_battery = battery;
    h.mirror_mode = (layout & 8U) != 0U
                        ? MirrorMode::FourScreen
                        : ((layout & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal);
    h.nametable_bits = layout;
    h.chr_ram_size = 32768U;
    return h;
}
std::vector<Byte> flash_banks(std::size_t size, std::size_t unit = 16384U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    for (std::size_t i = 0U; i < size; i += 4096U) {
        data[i + 0x100U] = 0xFFU;
        data[i + 0x101U] = 0U;
    }
    return data;
}
std::unique_ptr<Mapper> flash_board(INesHeader h, bool mmc = false) {
    register_builtin_mappers();
    auto result =
        MapperRegistry::instance().create(h,
                                          flash_banks(mmc ? 262144U : 524288U),
                                          mmc ? flash_banks(262144U, 4096U) : std::vector<Byte>{});
    REQUIRE(result);
    return std::move(*result);
}
Cartridge flash_cart(uint16_t id, bool legacy = false) {
    register_builtin_mappers();
    auto result = Cartridge::from_data(mapperbus::tests::flash_rom(id, legacy));
    REQUIRE(result);
    return std::move(*result);
}
void unlock(Sst39sf040& flash, Byte command) {
    flash.write(0x5555U, 0xAAU);
    flash.write(0x2AAAU, 0x55U);
    flash.write(0x5555U, command);
}
void clocks(Mapper& mapper, unsigned cycles) {
    for (unsigned i = 0U; i < cycles; ++i)
        mapper.clock_cpu();
}
void clocks(Sst39sf040& flash, unsigned cycles) {
    for (unsigned i = 0U; i < cycles; ++i)
        flash.clock_cpu();
}
void board_unlock(Mapper& mapper, uint16_t id, Byte command) {
    if (id == 30U) {
        mapper.write_prg(0xC000U, 1U);
        mapper.write_prg(0x9555U, 0xAAU);
        mapper.write_prg(0xC000U, 0U);
        mapper.write_prg(0xAAAAU, 0x55U);
        mapper.write_prg(0xC000U, 1U);
        mapper.write_prg(0x9555U, command);
    } else {
        mapper.write_expansion(0x5000U, 0U);
        mapper.write_prg(0xD555U, 0xAAU);
        mapper.write_prg(0xAAAAU, 0x55U);
        mapper.write_prg(0xD555U, command);
    }
}
void board_program(Mapper& mapper, uint16_t id, Byte value = 0x5AU) {
    board_unlock(mapper, id, 0xA0U);
    if (id == 30U)
        mapper.write_prg(0xC000U, 2U);
    else
        mapper.write_expansion(0x5000U, 1U);
    mapper.write_prg(0x8100U, value);
}

TEST_CASE("SST39SF040 validates unlocks IDs busy polling and one-to-zero programming",
          "[mappers][flash][sst]") {
    Sst39sf040 flash(std::vector<Byte>(32768U, 0xFFU));
    flash.write(0x1555U, 0xAAU);
    flash.write(0x2AAAU, 0x55U);
    flash.write(0x5555U, 0xA0U);
    flash.write(0x100U, 0U);
    REQUIRE(flash.read(0x100U) == 0xFFU);
    unlock(flash, 0x90U);
    REQUIRE(flash.read(0U) == 0xBFU);
    REQUIRE(flash.read(1U) == 0xB7U);
    REQUIRE(flash.read(0x8000U) == 0xFFU);
    flash.write(0x12345U, 0xF0U);
    REQUIRE(flash.read(0U) == 0xFFU);
    flash.write(0x3D555U, 0xAAU);
    flash.write(0x42AAAU, 0x55U);
    flash.write(0x15555U, 0xA0U);
    flash.write(0x7FFFEU, 0x5AU);
    REQUIRE(flash.nvram()[0x7FFFEU] == 0xFFU);
    REQUIRE((flash.read(0x7FFFEU) & 0xC0U) == 0xC0U);
    REQUIRE((flash.read(0x7FFFEU) & 0xC0U) == 0x80U);
    flash.write(0U, 0xF0U);
    clocks(flash, 31U);
    REQUIRE(flash.nvram()[0x7FFFEU] == 0xFFU);
    flash.clock_cpu();
    REQUIRE(flash.read(0x7FFFEU) == 0x5AU);
    REQUIRE(flash.read(0x7FFFEU) == 0x5AU);
    unlock(flash, 0xA0U);
    flash.write(0x7FFFEU, 0xF0U);
    clocks(flash, 32U);
    REQUIRE(flash.read(0x7FFFEU) == 0x50U);
}

TEST_CASE("SST39SF040 erases only the selected sector or the whole chip and replays busy state",
          "[mappers][flash][sst][state]") {
    Sst39sf040 flash(std::vector<Byte>(524288U, 0U));
    unlock(flash, 0x80U);
    flash.write(0x5555U, 0xAAU);
    flash.write(0x2AAAU, 0x55U);
    flash.write(0x53ABCU, 0x30U);
    REQUIRE((flash.read(0x53000U) & 0x80U) == 0U);
    clocks(flash, 17000U);
    StateWriter saved;
    flash.save_state(saved);
    Sst39sf040 clone({});
    StateReader reader(saved.data());
    clone.load_state(reader);
    REQUIRE(reader.ok());
    clocks(clone, 18999U);
    REQUIRE(clone.nvram()[0x53000U] == 0U);
    clone.clock_cpu();
    REQUIRE(clone.read(0x53000U) == 0xFFU);
    REQUIRE(clone.read(0x53FFFU) == 0xFFU);
    REQUIRE(clone.read(0x52FFFU) == 0U);
    REQUIRE(clone.read(0x54000U) == 0U);
    unlock(clone, 0x80U);
    clone.write(0x5555U, 0xAAU);
    clone.write(0x2AAAU, 0x55U);
    clone.write(0x5555U, 0x10U);
    clocks(clone, 144000U);
    REQUIRE(std::ranges::all_of(clone.nvram(), [](Byte b) { return b == 0xFFU; }));
    for (std::size_t field : {0U, 7U, 524296U, 524297U, 524298U, 524299U, 524304U, 524308U}) {
        auto bad = saved.data();
        bad[field] = 0xFFU;
        StateReader invalid(bad);
        clone.load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
}

TEST_CASE("UNROM 512 submappers decode latch conflicts mirroring CHR banks and LEDs",
          "[mappers][flash][unrom512]") {
    for (Byte sub = 0U; sub < 5U; ++sub) {
        auto mapper = flash_board(flash_header(30U, sub));
        mapper->write_prg(0x8100U, 3U);
        REQUIRE(mapper->read_prg(0x8000U) == (sub == 0U || sub == 2U ? 3U : 0U));
        mapper->write_prg(0xC100U, 7U);
        REQUIRE(mapper->read_prg(0x8000U) == 7U);
        REQUIRE(mapper->read_prg(0xC000U) == 31U);
        mapper->write_prg(0x8101U, 2U);
        REQUIRE(mapper->read_prg(0x8000U) == (sub == 0U || sub == 2U ? 0U : 7U));
        for (Byte bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0xC100U, static_cast<Byte>(bank << 5U));
            mapper->write_chr(0U, static_cast<Byte>(0x30U + bank));
        }
        for (Byte bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0xC100U, static_cast<Byte>(bank << 5U));
            REQUIRE(mapper->read_chr(0U) == 0x30U + bank);
        }
        mapper->write_prg(0xC100U, 0x80U);
        REQUIRE(mapper->mirror_mode() ==
                (sub == 3U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        if (sub == 4U) {
            mapper->write_prg(0x8100U, 0xA5U);
            REQUIRE(mapper->led_outputs() == 0x5AU);
        }
    }
    auto single = flash_board(flash_header(30U, 1U, false, 8U));
    REQUIRE(single->mirror_mode() == MirrorMode::SingleLower);
    single->write_prg(0xC000U, 0x80U);
    REQUIRE(single->mirror_mode() == MirrorMode::SingleUpper);
    for (const Byte layout : {0U, 1U, 8U, 9U}) {
        auto controlled = flash_board(flash_header(30U, 3U, false, layout));
        REQUIRE(controlled->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(controlled->unmirrored_nametables());
        controlled->write_prg(0xC000U, 0x80U);
        REQUIRE(controlled->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE(
    "Flash board writes use physical banks and survive soft reset without persisting busy status",
    "[mappers][flash][banking]") {
    for (uint16_t id : {30U, 111U}) {
        auto mapper = flash_board(flash_header(id, 0U, id == 30U));
        board_program(*mapper, id);
        mapper->reset();
        REQUIRE(mapper->battery_ram().size() == 524288U);
        REQUIRE(mapper->battery_ram()[0x8100U] == 0xFFU);
        clocks(*mapper, 32U);
        REQUIRE(mapper->battery_ram()[0x8100U] == 0x5AU);
        REQUIRE(mapper->read_prg(0x8100U) == 0x5AU);
        board_unlock(*mapper, id, 0x90U);
        if (id == 30U)
            mapper->write_prg(0xC000U, 0U);
        REQUIRE(mapper->read_prg(0x8000U) == 0xBFU);
        REQUIRE(mapper->read_prg(0x8001U) == 0xB7U);
    }
}

TEST_CASE("GTROM aliases latch CPU open bus on reads including DMA and retain two PPU banks",
          "[mappers][flash][gtrom][bus]") {
    auto cart = flash_cart(111U);
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    ppu.connect_cartridge(&cart);
    for (Address addr : {Address{0x5000}, Address{0x5FFF}, Address{0x7000}, Address{0x7FFF}}) {
        bus.write(0U, 0xA3U);
        REQUIRE(bus.read(addr) == 0xA3U);
        StateWriter state;
        cart.save_state(state);
        REQUIRE(state.data()[0] == 0xA3U);
        bus.write(0U, 0x51U);
        REQUIRE(bus.read_cpu(addr) == 0x51U);
        StateWriter timed;
        cart.save_state(timed);
        REQUIRE(timed.data()[0] == 0x51U);
    }
    bus.write(0U, 0xC2U);
    REQUIRE(bus.run_oam_dma(0x50U, 0xC000U, false) >= 513U);
    StateWriter state;
    cart.save_state(state);
    REQUIRE(state.data()[0] == 0x78U); // DMA halt reads the ROM's SEI opcode first.
    REQUIRE(cart.led_outputs() == 2U);
    bus.write(0x5000U, 0U);
    cart.write_chr(0U, 0x15U);
    bus.write(0x7000U, 0x10U);
    cart.write_chr(0U, 0x26U);
    bus.write(0x5000U, 0U);
    REQUIRE(cart.read_chr(0U) == 0x15U);
    bus.write(0x7000U, 0x10U);
    REQUIRE(cart.read_chr(0U) == 0x26U);
}

TEST_CASE("Flash boards expose upper nametable RAM without folding and preserve palette decoding",
          "[mappers][flash][ppu]") {
    register_builtin_mappers();
    for (uint16_t id : {30U, 111U}) {
        auto image = mapperbus::tests::flash_rom(id);
        if (id == 30U)
            image[6] |= 9U;
        auto result = Cartridge::from_data(image);
        REQUIRE(result);
        auto cart = std::move(*result);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        const auto address = [&](Address addr) {
            ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
            ppu.write_register(6U, static_cast<Byte>(addr));
        };
        const auto write = [&](Address addr, Byte value) {
            address(addr);
            ppu.write_register(7U, value);
        };
        const auto read = [&](Address addr) {
            address(addr);
            ppu.read_register(7U);
            return ppu.read_register(7U);
        };
        for (unsigned slot = 0U; slot < 8U; ++slot)
            write(static_cast<Address>(0x2000U + slot * 0x400U), static_cast<Byte>(slot + 0x40U));
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(read(static_cast<Address>(0x2000U + slot * 0x400U)) == slot + 0x40U);
        write(0x3F00U, 0x22U);
        address(0x3F00U);
        REQUIRE(ppu.read_register(7U) == 0x22U);
        if (id == 111U) {
            cart.write_expansion(0x5000U, 0x20U);
            REQUIRE(read(0x2000U) == 0U);
            write(0x2000U, 0x77U);
            cart.write_expansion(0x5000U, 0U);
            REQUIRE(read(0x2000U) == 0x40U);
            cart.write_expansion(0x5000U, 0x20U);
            REQUIRE(read(0x2000U) == 0x77U);
        } else {
            cart.write_prg(0xC000U, 0x60U);
            REQUIRE(cart.read_chr(0U) == 0x40U);
            REQUIRE(cart.read_chr(0x1000U) == 0x44U);
        }
    }
}

TEST_CASE("Mapper 111 CHR ROM selects direct MMC1 including six-bit CHR and consecutive writes",
          "[mappers][flash][mmc1]") {
    auto h = flash_header(111U);
    h.chr_ram_size = 0U;
    h.prg_ram_size = 8192U;
    auto mapper = flash_board(h, true);
    REQUIRE_FALSE(mapper->has_flash_storage());
    mapper->write_prg(0x8000U, 0x1EU);
    mapper->write_prg(0xA000U, 0x3FU);
    mapper->write_prg(0xC000U, 0x20U);
    REQUIRE(mapper->read_chr(0U) == 63U);
    REQUIRE(mapper->read_chr(0x1000U) == 32U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg_cpu(0xE000U, 2U, true);
    REQUIRE(mapper->read_prg(0x8000U) == 2U);
    REQUIRE(mapper->read_prg(0xC000U) == 15U);
    mapper->write_prg(0x6000U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    mapper->write_prg(0xE000U, 0x12U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower); // D7 is data, not serial reset.
    StateWriter saved;
    mapper->save_state(saved);
    mapper->reset();
    StateReader reader(saved.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    StateWriter replay;
    mapper->save_state(replay);
    REQUIRE(replay.data() == saved.data());
}
struct FlashScratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mapperbus-flash-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    FlashScratch() {
        std::filesystem::create_directories(path);
    }
    ~FlashScratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
TEST_CASE(
    "Flash CPU programs execute from RAM and replay pending write snapshots with atomic rollback",
    "[mappers][flash][integration][state]") {
    for (uint16_t id : {30U, 111U})
        for (bool legacy : {false, true}) {
            Emulator emulator;
            REQUIRE(emulator.load_cartridge(flash_cart(id, legacy), {}));
            emulator.reset();
            for (unsigned i = 0U; i < 35U; ++i)
                emulator.step_instruction();
            const auto early = emulator.save_state();
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x10U) == 0x5AU);
            const auto expected = emulator.save_state();
            REQUIRE(emulator.load_state(early));
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x10U) == 0x5AU);
            REQUIRE((emulator.save_state() == expected));
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            StateWriter board;
            flash_cart(id, legacy).save_state(board);
            const auto begin = expected.size() - tail.data().size() - board.data().size();
            const std::size_t flash_start = 2U + 8U + 32768U;
            for (const std::size_t field : {2UZ,
                                            9UZ,
                                            flash_start,
                                            flash_start + 7U,
                                            flash_start + 524296U,
                                            flash_start + 524297U,
                                            flash_start + 524298U,
                                            flash_start + 524299U,
                                            flash_start + 524304U,
                                            flash_start + 524308U}) {
                auto bad = expected;
                bad[begin + field] = 0xFFU;
                REQUIRE_FALSE(emulator.load_state(bad));
                REQUIRE((emulator.save_state() == expected));
            }
            // Save precisely while the self-program operation is in progress.
            REQUIRE(emulator.load_state(early));
            for (unsigned i = 0U; i < 1000U; ++i) {
                emulator.step_instruction();
                const Address after_program = id == 30U ? 0x228U : 0x21EU;
                if (emulator.cpu().pc() == after_program) {
                    const auto pending = emulator.save_state();
                    REQUIRE(pending[begin + flash_start + 524297U] == 1U);
                    emulator.step_frame();
                    const auto finished = emulator.save_state();
                    REQUIRE(emulator.load_state(pending));
                    emulator.step_frame();
                    REQUIRE((emulator.save_state() == finished));
                    break;
                }
                REQUIRE(i < 999U);
            }
        }
}

TEST_CASE(
    "Flash saves contain committed chip bytes preserve source identity and reject truncated images",
    "[mappers][flash][battery]") {
    FlashScratch scratch;
    for (uint16_t id : {30U, 111U}) {
        const auto path = (scratch.path / (std::to_string(id) + ".sav")).string();
        Sha256Digest identity{};
        {
            Emulator writer;
            REQUIRE(writer.load_cartridge(flash_cart(id), path));
            identity = writer.content_hash();
            writer.reset();
            writer.step_frame();
            REQUIRE(writer.read_cpu(0x10U) == 0x5AU);
            REQUIRE(writer.content_hash() == identity);
            REQUIRE(writer.flush_persistent_data());
        }
        const auto data = read_binary_file_capped(path, 524288U, "flash");
        REQUIRE(data);
        REQUIRE(data->size() == 524288U);
        REQUIRE((*data)[0x8100U] == 0x5AU);
        Emulator reader;
        REQUIRE(reader.load_cartridge(flash_cart(id), path));
        REQUIRE(reader.content_hash() == identity);
        const auto restored = (scratch.path / "restored.sav").string();
        REQUIRE(reader.save_battery_ram(restored));
        const auto copy = read_binary_file_capped(restored, 524288U, "flash");
        REQUIRE(copy);
        REQUIRE(*copy == *data);
        const auto before = reader.save_state();
        for (unsigned size : {524287U, 524289U}) {
            const auto invalid = (scratch.path / (std::to_string(size) + ".sav")).string();
            REQUIRE(write_binary_file_atomic(invalid, std::vector<Byte>(size), "invalid"));
            REQUIRE_FALSE(reader.load_battery_ram(invalid));
            REQUIRE((reader.save_state() == before));
            REQUIRE_FALSE(reader.load_cartridge(flash_cart(id), invalid));
            REQUIRE((reader.save_state() == before));
        }
        Emulator staged;
        staged.set_persistence_enabled(false);
        const auto discarded = (scratch.path / "discarded.sav").string();
        REQUIRE(staged.load_cartridge(flash_cart(id), discarded));
        staged.reset();
        staged.step_frame();
        REQUIRE(staged.flush_persistent_data());
        staged.unload_cartridge();
        REQUIRE_FALSE(std::filesystem::exists(discarded));
    }
}

TEST_CASE("Flash geometry distinguishes mapper 111 variants and mapper 30 wiring metadata",
          "[mappers][flash][geometry]") {
    register_builtin_mappers();
    for (uint16_t id : {30U, 111U}) {
        for (unsigned invalid = 0U; invalid < 6U; ++invalid) {
            auto h = flash_header(id);
            std::size_t prg = 32768U, chr = 0U;
            switch (invalid) {
            case 0:
                prg = 16384U;
                break;
            case 1:
                prg = 49152U;
                break;
            case 2:
                h.submapper = 7U;
                break;
            case 3:
                h.prg_nvram_size = 8192U;
                break;
            case 4:
                h.chr_nvram_size = 8192U;
                break;
            default:
                chr = 8192U;
                break;
            }
            // Mapper 111 with CHR ROM must declare CHR RAM absent.
            REQUIRE_FALSE(MapperRegistry::instance().create(
                h, std::vector<Byte>(prg), std::vector<Byte>(chr)));
        }
    }
    for (Byte sub : {2U, 3U})
        REQUIRE_FALSE(MapperRegistry::instance().create(
            flash_header(30U, sub, true), std::vector<Byte>(32768U), {}));
    for (unsigned size : {8192U, 16384U, 32768U}) {
        auto h = flash_header(30U, 1U);
        h.chr_ram_size = size;
        REQUIRE(flash_board(h));
    }
}

TEST_CASE("Mapper 111 direct MMC1 executes CHR banking through CPU and PPU with machine replay",
          "[mappers][flash][mmc1][integration]") {
    register_builtin_mappers();
    auto prg = flash_banks(262144U);
    auto chr = flash_banks(262144U, 4096U);
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    store(0x8000U, 0x1EU);
    store(0xA000U, 0x3FU);
    store(0xC000U, 0x20U);
    store(0xE000U, 2U);
    store(0x6005U, 0xA5U);
    for (Byte table = 0U; table < 2U; ++table) {
        store(0x2006U, table == 0U ? 0U : 0x10U);
        store(0x2006U, 0U);
        code.insert(code.end(),
                    {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, static_cast<Byte>(0x10U + table)});
    }
    code.insert(code.end(), {0xADU, 0U, 0x80U, 0x85U, 0x12U});
    const auto loop = static_cast<Address>(0xC000U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    std::ranges::copy(code, prg.begin() + 0x3C000U);
    for (unsigned v = 0x3FFFAU; v <= 0x3FFFEU; v += 2U) {
        prg[v] = 0U;
        prg[v + 1U] = 0xC0U;
    }
    std::vector<Byte> image(16U + prg.size() + chr.size(), 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 16U;
    image[5] = 32U;
    image[6] = 0xF2U;
    image[7] = 0x68U;
    image[10] = 0x70U;
    std::ranges::copy(prg, image.begin() + 16U);
    std::ranges::copy(chr, image.begin() + 16U + prg.size());
    auto cart = Cartridge::from_data(image);
    REQUIRE(cart);
    Emulator emulator;
    const auto loaded = emulator.load_cartridge(std::move(*cart), {});
    REQUIRE(loaded);
    emulator.reset();
    for (unsigned i = 0U; i < 6U; ++i)
        emulator.step_instruction();
    const auto partial = emulator.save_state();
    const auto finish = [&] {
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x10U) == 63U);
        REQUIRE(emulator.read_cpu(0x11U) == 32U);
        REQUIRE(emulator.read_cpu(0x12U) == 2U);
    };
    finish();
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(partial));
    finish();
    REQUIRE((emulator.save_state() == expected));
    FlashScratch scratch;
    const auto path = (scratch.path / "direct.sav").string();
    REQUIRE(emulator.save_battery_ram(path));
    const auto data = read_binary_file_capped(path, 8192U, "RAM");
    REQUIRE(data);
    REQUIRE(data->size() == 8192U);
    REQUIRE((*data)[5U] == 0xA5U);
}

} // namespace
