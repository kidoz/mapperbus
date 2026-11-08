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
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
struct X1005BatteryFiles {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-x1005-battery-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    X1005BatteryFiles() {
        std::filesystem::create_directories(root);
    }
    ~X1005BatteryFiles() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

INesHeader x1005_header(uint16_t id, bool battery = false) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    header.has_battery = battery;
    header.prg_nvram_size = battery ? 128U : 0U;
    return header;
}

std::vector<Byte> x1005_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Mapper> x1005_board(uint16_t id, bool battery = false, bool chr_ram = false) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(x1005_header(id, battery),
                                                    x1005_rom(0x80000U, 0x2000U),
                                                    chr_ram ? std::vector<Byte>{}
                                                            : x1005_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}

Cartridge x1005_cartridge(uint16_t id, bool register_program = false, bool battery = false) {
    register_builtin_mappers();
    std::vector<Byte> image(16U + 0x80000U + 0x40000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 32U;
    image[5U] = 32U;
    image[6U] = static_cast<Byte>(((id & 15U) << 4U) | 1U | (battery ? 2U : 0U));
    image[7U] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[10U] = battery ? 0x10U : 1U; // 128-byte internal RAM/NVRAM.
    for (std::size_t i = 0U; i < 0x80000U; ++i)
        image[16U + i] = static_cast<Byte>(i / 0x2000U);
    const std::array<Byte, 3U> loop = {0x4CU, 0x00U, 0xE0U};
    std::copy(loop.begin(), loop.end(), image.begin() + 16U + 0x7E000U);
    if (register_program) {
        // clang-format off
        const std::array<Byte, 48U> program = {
            0xA9U, 0xA3U, 0x8DU, 0x79U, 0x7EU, // Unlock via A7/permission mirror.
            0xA9U, 0x5AU, 0x8DU, 0x85U, 0x7FU, // Write mirrored RAM byte 5.
            0xA9U, 0x07U, 0x8DU, 0x7BU, 0x7EU, // PRG slot 0.
            0xA9U, 0x08U, 0x8DU, 0xFDU, 0x7EU, // PRG slot 1.
            0xA9U, 0x09U, 0x8DU, 0xFFU, 0x7EU, // PRG slot 2.
            0xA9U, 0x80U, 0x8DU, 0x70U, 0x7EU, // CHR / CIRAM selector 0.
            0xA9U, 0x00U, 0x8DU, 0xF1U, 0x7EU, // CHR / CIRAM selector 1.
            0xA9U, 0x01U, 0x8DU, 0xF6U, 0x7EU, // Normal-board mirror register.
            0xA9U, 0x00U, 0x8DU, 0xF9U, 0x7EU, // Lock internal RAM again.
            0x4CU, 0x2DU, 0xE0U};              // JMP $E02D in the fixed bank.
        // clang-format on
        std::copy(program.begin(), program.end(), image.begin() + 16U + 0x7E000U);
    }
    for (std::size_t offset = 0x7FFFAU; offset < 0x80000U; offset += 2U) {
        image[16U + offset] = 0U;
        image[16U + offset + 1U] = 0xE0U;
    }
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

void x1005_ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

Byte x1005_ppu_read(Ppu& ppu, Address addr) {
    x1005_ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("X1-005 selects three PRG banks through exact register pairs and A7 mirrors",
          "[mappers][x1005]") {
    for (const uint16_t id : {80U, 207U}) {
        auto mapper = x1005_board(id);
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
        REQUIRE(mapper->read_prg(0xA000U) == 1U);
        REQUIRE(mapper->read_prg(0xC000U) == 2U);
        REQUIRE(mapper->read_prg(0xFFFFU) == 63U);
        for (unsigned slot = 0U; slot < 3U; ++slot) {
            for (unsigned alias = 0U; alias < 4U; ++alias) {
                const auto reg = static_cast<Address>(0x7E7AU + slot * 2U + (alias & 1U) +
                                                      ((alias & 2U) != 0U ? 0x80U : 0U));
                mapper->write_prg(reg, static_cast<Byte>(0xC0U | (60U + slot)));
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        60U + slot);
            }
        }
        for (const Address ignored : {0x7DFAU, 0x7E6AU, 0x7EEAU, 0x8000U, 0xFFFFU})
            mapper->write_prg(ignored, 5U);
        REQUIRE(mapper->read_prg(0x8000U) == 60U);
        REQUIRE(mapper->read_prg(0xA000U) == 61U);
        REQUIRE(mapper->read_prg(0xC000U) == 62U);
        REQUIRE(mapper->read_prg(0xFFFFU) == 63U);
    }
}

TEST_CASE("X1-005 aligns two-KiB CHR banks and retains all eight one-KiB bank bits",
          "[mappers][x1005]") {
    for (const uint16_t id : {80U, 207U}) {
        auto mapper = x1005_board(id);
        mapper->write_prg(0x7E70U, 0x83U);
        mapper->write_prg(0x7EF1U, 0xFFU);
        REQUIRE(mapper->read_chr(0U) == 130U);
        REQUIRE(mapper->read_chr(0x400U) == 131U);
        REQUIRE(mapper->read_chr(0x800U) == 254U);
        REQUIRE(mapper->read_chr(0xC00U) == 255U);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            mapper->write_prg(static_cast<Address>(0x7E72U + slot), static_cast<Byte>(248U + slot));
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = static_cast<Address>(0x1000U + slot * 0x400U);
            REQUIRE(mapper->read_chr(addr) == 248U + slot);
            mapper->write_chr(addr, 0U);
            REQUIRE(mapper->read_chr(addr) == 248U + slot);
        }
    }
}

TEST_CASE("X1-005 RAM requires the exact permission key and only covers 128 mirrored bytes",
          "[mappers][x1005][bus]") {
    for (const uint16_t id : {80U, 207U}) {
        auto cart = x1005_cartridge(id);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        for (unsigned key = 0U; key < 256U; ++key) {
            bus.write(0x7EF8U, static_cast<Byte>(key));
            REQUIRE(cart.maps_prg(0x7F00U) == (key == 0xA3U));
            REQUIRE(cart.maps_prg(0x7FFFU) == (key == 0xA3U));
            REQUIRE_FALSE(cart.maps_prg(0x7EFFU));
            REQUIRE_FALSE(cart.maps_prg(0x6000U));
        }
        bus.write(0x7E79U, 0xA3U);
        for (unsigned i = 0U; i < 128U; ++i)
            bus.write(static_cast<Address>(0x7F00U + i), static_cast<Byte>(i | 0x80U));
        for (unsigned i = 0U; i < 128U; ++i)
            REQUIRE(bus.read(static_cast<Address>(0x7F80U + i)) == (i | 0x80U));
        bus.write(0x7FFFU, 0x55U);
        REQUIRE(bus.read(0x7F7FU) == 0x55U);
        bus.write(0x7EF9U, 0U);
        bus.write(0U, 0xA5U);
        REQUIRE(bus.read(0x7F00U) == 0xA5U);
        bus.write(0x7F00U, 0U); // Locked write must not modify internal RAM.
        bus.write(0x7E78U, 0xA3U);
        REQUIRE(bus.read(0x7F00U) == 0x80U);
        bus.write(0U, 0x42U);
        REQUIRE(bus.read(0x7EF8U) == 0x42U); // Permission register is write-only.
        REQUIRE(bus.read(0x7E7AU) == 0x42U); // Bank register is write-only too.
    }
}

TEST_CASE("X1-005 mirroring control has no EF7 alias and uses the documented PPU address lines",
          "[mappers][x1005][ppu]") {
    auto cart = x1005_cartridge(80U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0x7EF6U, 1U); // PPU A10 -> CIRAM A10, vertical mirroring.
    x1005_ppu_address(ppu, 0x2000U);
    ppu.write_register(7U, 0x11U);
    x1005_ppu_address(ppu, 0x2400U);
    ppu.write_register(7U, 0x22U);
    cart.write_prg(0x7E76U, 0U); // PPU A11 -> CIRAM A10, horizontal mirroring.
    REQUIRE(cart.mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(x1005_ppu_read(ppu, 0x2400U) == 0x11U);
    REQUIRE(x1005_ppu_read(ppu, 0x2800U) == 0x22U);
    cart.write_prg(0x7EF7U, 1U);
    cart.write_prg(0x7E77U, 1U);
    REQUIRE(cart.mirror_mode() == MirrorMode::Horizontal);
    cart.write_prg(0x7EF6U, 0xFFU);
    REQUIRE(cart.mirror_mode() == MirrorMode::Vertical);
    REQUIRE(x1005_ppu_read(ppu, 0x2400U) == 0x22U);
    REQUIRE(x1005_ppu_read(ppu, 0x2800U) == 0x11U);
}

TEST_CASE("Mapper 207 independently routes both nametable pairs including reversed horizontal",
          "[mappers][x1005][ppu]") {
    auto cart = x1005_cartridge(207U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0x7EF0U, 0U);
    cart.write_prg(0x7EF1U, 0x80U);
    x1005_ppu_address(ppu, 0x2000U);
    ppu.write_register(7U, 0x11U);
    x1005_ppu_address(ppu, 0x2800U);
    ppu.write_register(7U, 0x22U);
    for (unsigned top = 0U; top < 2U; ++top) {
        for (unsigned bottom = 0U; bottom < 2U; ++bottom) {
            cart.write_prg(0x7E70U, static_cast<Byte>(top * 0x80U));
            cart.write_prg(0x7E71U, static_cast<Byte>(bottom * 0x80U));
            for (unsigned table = 0U; table < 4U; ++table) {
                const auto physical = table < 2U ? top : bottom;
                const auto addr = static_cast<Address>(0x2000U + table * 0x400U + 0x23U);
                REQUIRE(cart.nametable_address(addr) == physical * 0x400U + 0x23U);
                REQUIRE(x1005_ppu_read(ppu, static_cast<Address>(addr - 0x23U)) ==
                        (physical == 0U ? 0x11U : 0x22U));
            }
            cart.write_prg(0x7EF6U, 1U);
            REQUIRE(x1005_ppu_read(ppu, 0x2000U) == (top == 0U ? 0x11U : 0x22U));
            REQUIRE(x1005_ppu_read(ppu, 0x2800U) == (bottom == 0U ? 0x11U : 0x22U));
        }
    }
}

TEST_CASE("X1-005 snapshots and reset preserve internal RAM but restore access protection",
          "[mappers][x1005][state]") {
    for (const uint16_t id : {80U, 207U}) {
        auto mapper = x1005_board(id, true, true);
        mapper->write_prg(0x7EF8U, 0xA3U);
        mapper->write_prg(0x7F00U, 0x55U);
        mapper->write_prg(0x7EFAU, 7U);
        mapper->write_prg(0x7EF0U, 0x86U);
        mapper->write_chr(0U, 0x66U);
        REQUIRE(mapper->battery_ram().size() == 128U);
        REQUIRE(mapper->battery_ram()[0U] == 0x55U);
        StateWriter saved;
        mapper->save_state(saved);
        mapper->reset();
        REQUIRE_FALSE(mapper->maps_prg(0x7F00U));
        REQUIRE(mapper->battery_ram()[0U] == 0x55U);
        mapper->write_prg(0x7EF8U, 0xA3U);
        mapper->write_prg(0x7F00U, 0U);
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(mapper->read_prg(0x7F00U) == 0x55U);
        REQUIRE(mapper->read_prg(0x8000U) == 7U);
        REQUIRE(mapper->read_chr(0U) == 0x66U);
        StateWriter restored;
        mapper->save_state(restored);
        REQUIRE(restored.data() == saved.data());
        const std::vector<Byte> battery(128U, 0x77U);
        mapper->set_battery_ram(battery);
        REQUIRE(mapper->read_prg(0x7F80U) == 0x77U);
        auto volatile_board = x1005_board(id);
        REQUIRE(volatile_board->battery_ram().empty());
        StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("X1-005 register and RAM writes execute through the CPU and replay whole-machine state",
          "[mappers][x1005][state][integration]") {
    for (const uint16_t id : {80U, 207U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(x1005_cartridge(id, true), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0xE000U);
        for (unsigned i = 0U; i < 8U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0x7F05U) == 0x5AU);
        const auto saved = emulator.save_state();
        auto finish = [&] {
            for (unsigned i = 0U; i < 40U; ++i)
                emulator.step_instruction();
            REQUIRE(emulator.read_cpu(0x8000U) == 7U);
            REQUIRE(emulator.read_cpu(0xA000U) == 8U);
            REQUIRE(emulator.read_cpu(0xC000U) == 9U);
            REQUIRE(emulator.read_cpu(0x7F05U) != 0x5AU);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        REQUIRE(emulator.read_cpu(0x7F05U) == 0x5AU);
        finish();
        REQUIRE(emulator.save_state() == expected);
        REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("X1-005 factory rejects oversized memories and unsupported board variants",
          "[mappers][x1005]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const uint16_t id : {80U, 207U}) {
        auto header = x1005_header(id);
        header.submapper = 1U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1005_header(id);
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x100000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
        header.prg_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header.prg_ram_size = 64U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1005_header(id, true);
        header.prg_ram_size = 128U; // Cannot fit two separate 128-byte chips internally.
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1005_header(id);
        header.prg_nvram_size = 128U; // Nonvolatile declaration needs the NES battery flag.
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1005_header(id);
        header.chr_ram_size = 0x4000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
        header = x1005_header(id);
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
    }
}

TEST_CASE("X1-005 snapshots reject malformed bank alignment, flags and truncation",
          "[mappers][x1005][state]") {
    for (const uint16_t id : {80U, 207U}) {
        auto mapper = x1005_board(id);
        StateWriter writer;
        mapper->save_state(writer);
        // Three PRG banks, six CHR banks, mirror flag, permission flag, RAM.
        for (const std::size_t offset : {0U, 1U, 2U, 3U, 4U, 9U, 10U}) {
            auto corrupt = writer.data();
            corrupt[offset] = 0xFFU;
            StateReader reader(corrupt);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
        }
    }
}

TEST_CASE("X1-005 battery persistence writes exactly 128 bytes and restores while RAM is locked",
          "[mappers][x1005][battery]") {
    X1005BatteryFiles files;
    for (const uint16_t id : {80U, 207U}) {
        const auto path = files.root / (std::to_string(id) + ".sav");
        {
            Emulator writer;
            REQUIRE(writer.load_cartridge(x1005_cartridge(id, true, true), path.string()));
            writer.reset();
            for (unsigned i = 0U; i < 4U; ++i)
                writer.step_instruction();
            REQUIRE(writer.read_cpu(0x7F05U) == 0x5AU);
            writer.flush_battery_ram();
        }
        auto bytes = read_binary_file_capped(path.string(), 128U, "battery");
        REQUIRE(bytes);
        REQUIRE(bytes->size() == 128U);
        REQUIRE((*bytes)[5U] == 0x5AU);
        Emulator reader;
        REQUIRE(reader.load_cartridge(x1005_cartridge(id, true, true), path.string()));
        reader.reset();
        REQUIRE(reader.read_cpu(0x7F05U) != 0x5AU); // Still locked after reset.
        reader.step_instruction();
        reader.step_instruction(); // Unlock, without executing the RAM write.
        REQUIRE(reader.read_cpu(0x7F05U) == 0x5AU);
        const auto before = reader.save_state();
        const auto oversized = files.root / "oversized.sav";
        REQUIRE(write_binary_file_atomic(oversized.string(), std::vector<Byte>(129U), "battery"));
        REQUIRE_FALSE(reader.load_battery_ram(oversized.string()));
        REQUIRE(reader.save_state() == before);
    }
}
