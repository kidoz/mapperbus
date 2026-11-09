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
INesHeader n210_header(Byte submapper, std::size_t ram = 0U, std::size_t nvram = 0U) {
    INesHeader header{};
    header.mapper_number = 210U;
    header.is_nes2 = true;
    header.submapper = submapper;
    header.mirror_mode = MirrorMode::Vertical;
    header.prg_ram_size = ram;
    header.prg_nvram_size = nvram;
    header.has_battery = nvram != 0U;
    return header;
}
std::vector<Byte> n210_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = static_cast<Byte>(offset / bank);
    return data;
}
std::unique_ptr<Mapper> n210_board(INesHeader header) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header, n210_rom(0x80000U, 0x2000U), n210_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}
Cartridge n210_cartridge(Byte submapper, std::size_t ram = 0U, bool battery = false) {
    std::vector<Byte> image(16U + 0x80000U + 0x40000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 32U;
    image[5U] = 32U;
    image[6U] = static_cast<Byte>(0x21U | (battery ? 2U : 0U));
    image[7U] = 0xD8U;
    image[8U] = static_cast<Byte>(submapper << 4U);
    const Byte shift = ram == 0U ? 0U : (ram == 0x800U ? 5U : 7U);
    image[10U] = battery ? static_cast<Byte>(shift << 4U) : shift;
    auto prg = n210_rom(0x80000U, 0x2000U);
    // Original program runs in the fixed final bank while all three PRG slots change.
    // clang-format off
    const std::array<Byte, 66U> code = {
        0xA9U,0x01U,0x8DU,0x00U,0xC0U, // N175 RAM enable; ignored on N340.
        0xA9U,0x5AU,0x8DU,0x05U,0x60U,
        0xA9U,0x85U,0x8DU,0x00U,0xE0U, // PRG 5; N340 upper single-screen.
        0xA9U,0xFEU,0x8DU,0x00U,0xE8U, // PRG 62, upper bits ignored.
        0xA9U,0x7FU,0x8DU,0x00U,0xF0U, // PRG 63.
        0xA9U,0xE5U,0x8DU,0x00U,0xB8U, // Full-width CHR at $1C00.
        0xA9U,0x1CU,0x8DU,0x06U,0x20U,0xA9U,0x00U,0x8DU,0x06U,0x20U,
        0xADU,0x07U,0x20U,0xADU,0x07U,0x20U,0x85U,0x10U,
        0xADU,0x05U,0x60U,0x85U,0x11U, // RAM or CPU open bus.
        0xA9U,0x00U,0x8DU,0x00U,0xC0U, // Lock N175 RAM again.
        0xADU,0x05U,0x60U,0x85U,0x12U,
        0x4CU,0x3FU,0xE0U};
    // clang-format on
    std::copy(code.begin(), code.end(), prg.begin() + 0x7E000U);
    for (std::size_t vector = 0x7FFFAU; vector < 0x80000U; vector += 2U) {
        prg[vector] = 0U;
        prg[vector + 1U] = 0xE0U;
    }
    std::copy(prg.begin(), prg.end(), image.begin() + 16U);
    const auto chr = n210_rom(0x40000U, 0x400U);
    std::copy(chr.begin(), chr.end(), image.begin() + 16U + 0x80000U);
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}
void n210_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte n210_read(Ppu& ppu, Address addr) {
    n210_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("Namco 175 and 340 decode six-bit PRG banks in exact two-KiB register windows",
          "[mappers][namco210]") {
    for (const Byte sub : {1U, 2U}) {
        auto mapper = n210_board(n210_header(sub));
        for (unsigned slot = 0U; slot < 3U; ++slot) {
            const auto base = static_cast<Address>(0xE000U + slot * 0x800U);
            const auto window = static_cast<Address>(0x8000U + slot * 0x2000U);
            REQUIRE(mapper->read_prg(window) == slot);
            mapper->write_prg(base, static_cast<Byte>(0xC0U | (60U + slot)));
            REQUIRE(mapper->read_prg(window) == 60U + slot);
            mapper->write_prg(static_cast<Address>(base + 0x7FFU), static_cast<Byte>(35U + slot));
            REQUIRE(mapper->read_prg(window) == 35U + slot);
        }
        for (const Address ignored : {0xC800U, 0xD000U, 0xDFFFU, 0xF800U, 0xFFFFU})
            mapper->write_prg(ignored, 0U);
        REQUIRE(mapper->read_prg(0x8000U) == 35U);
        REQUIRE(mapper->read_prg(0xA000U) == 36U);
        REQUIRE(mapper->read_prg(0xC000U) == 37U);
        REQUIRE(mapper->read_prg(0xFFFFU) == 63U);
    }
}

TEST_CASE("Namco 175 and 340 retain all CHR bank bits without Namco 163 CIRAM substitution",
          "[mappers][namco210][ppu]") {
    for (const Byte sub : {1U, 2U}) {
        auto cart = n210_cartridge(sub);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto base = static_cast<Address>(0x8000U + slot * 0x800U);
            cart.write_prg(base, static_cast<Byte>(0xE0U + slot));
            REQUIRE(n210_read(ppu, static_cast<Address>(slot * 0x400U)) == 0xE0U + slot);
            cart.write_prg(static_cast<Address>(base + 0x7FFU), static_cast<Byte>(0xF8U + slot));
            const auto addr = static_cast<Address>(slot * 0x400U + 0x3FFU);
            REQUIRE(n210_read(ppu, addr) == 0xF8U + slot);
            cart.write_chr(addr, 0U);
            REQUIRE(cart.read_chr(addr) == 0xF8U + slot);
        }
    }
}

TEST_CASE("Namco 175 fixed mirroring and Namco 340 selectable CIRAM remain independent of PRG",
          "[mappers][namco210][ppu]") {
    const std::array<MirrorMode, 4U> modes = {MirrorMode::SingleLower,
                                              MirrorMode::Vertical,
                                              MirrorMode::SingleUpper,
                                              MirrorMode::Horizontal};
    const std::array<std::array<Byte, 4U>, 4U> expected = {{{0x11U, 0x11U, 0x11U, 0x11U},
                                                            {0x11U, 0x22U, 0x11U, 0x22U},
                                                            {0x22U, 0x22U, 0x22U, 0x22U},
                                                            {0x11U, 0x11U, 0x22U, 0x22U}}};
    auto cart = n210_cartridge(2U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0xE000U, 0x40U);
    n210_address(ppu, 0x2000U);
    ppu.write_register(7U, 0x11U);
    n210_address(ppu, 0x2400U);
    ppu.write_register(7U, 0x22U);
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        cart.write_prg(0xE7FFU, static_cast<Byte>((mode << 6U) | 5U));
        REQUIRE(cart.mirror_mode() == modes[mode]);
        REQUIRE(cart.read_prg(0x8000U) == 5U);
        for (unsigned table = 0U; table < 4U; ++table)
            REQUIRE(n210_read(ppu, static_cast<Address>(0x2000U + table * 0x400U)) ==
                    expected[mode][table]);
        cart.write_prg(0xEFFFU, 0xC6U);
        cart.write_prg(0xF7FFU, 0xC7U);
        REQUIRE(cart.mirror_mode() == modes[mode]);
    }
    for (const MirrorMode fixed : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto header = n210_header(1U);
        header.mirror_mode = fixed;
        auto mapper = n210_board(header);
        for (unsigned mode = 0U; mode < 4U; ++mode) {
            mapper->write_prg(0xE000U, static_cast<Byte>((mode << 6U) | 5U));
            REQUIRE(mapper->mirror_mode() == fixed);
            REQUIRE(mapper->read_prg(0x8000U) == 5U);
        }
    }
}

TEST_CASE("Namco 175 gates reads and writes and mirrors the physical RAM size",
          "[mappers][namco210][bus]") {
    for (const std::size_t size : {0x800U, 0x2000U}) {
        auto cart = n210_cartridge(1U, size);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        REQUIRE_FALSE(cart.maps_prg(0x6000U));
        for (unsigned value = 0U; value < 256U; ++value) {
            bus.write(0xC7FFU, static_cast<Byte>(value));
            REQUIRE(cart.maps_prg(0x6000U) == ((value & 1U) != 0U));
            REQUIRE(cart.maps_prg(0x7FFFU) == ((value & 1U) != 0U));
        }
        bus.write(0xC000U, 1U);
        bus.write(0x6005U, 0x5AU);
        bus.write(0x6805U, 0xA5U);
        REQUIRE(bus.read(0x6005U) == (size == 0x800U ? 0xA5U : 0x5AU));
        REQUIRE(bus.read(0x6805U) == 0xA5U);
        bus.write(0xC800U, 0U);
        REQUIRE(cart.maps_prg(0x6000U)); // Not a gate alias.
        bus.write(0xC000U, 0U);
        bus.write(0x6005U, 0U);
        bus.write(0U, 0x77U);
        REQUIRE(bus.read(0x6005U) == 0x77U);
        bus.write(0xC000U, 1U);
        REQUIRE(bus.read(0x6005U) == (size == 0x800U ? 0xA5U : 0x5AU));
    }
    for (const Byte sub : {1U, 2U}) {
        auto mapper = n210_board(n210_header(sub));
        mapper->write_prg(0xC000U, 1U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Mapper 210 submapper zero selects the documented RAM or legacy battery profile",
          "[mappers][namco210]") {
    for (const bool nonvolatile : {false, true}) {
        auto header = n210_header(0U, nonvolatile ? 0U : 0x800U, nonvolatile ? 0x800U : 0U);
        auto mapper = n210_board(header);
        mapper->write_prg(0xE000U, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(0xC000U, 1U);
        REQUIRE(mapper->maps_prg(0x6000U));
    }
    auto automatic = n210_board(n210_header(0U));
    automatic->write_prg(0xE000U, 0U);
    REQUIRE(automatic->mirror_mode() == MirrorMode::SingleLower);
    for (const bool battery : {false, true}) {
        auto header = n210_header(0U, battery ? 0U : 0x2000U, battery ? 0x2000U : 0U);
        header.is_nes2 = false;
        header.submapper = 15U; // Legacy submapper bits are ignored.
        auto mapper = n210_board(header);
        mapper->write_prg(0xE000U, 0U);
        REQUIRE(mapper->mirror_mode() ==
                (battery ? MirrorMode::Vertical : MirrorMode::SingleLower));
        mapper->write_prg(0xC000U, 1U);
        REQUIRE(mapper->maps_prg(0x6000U) == battery);
    }
}

TEST_CASE("Namco 175 and 340 ignore absent Namco 163 IRQ, audio and nametable ports",
          "[mappers][namco210][bus]") {
    for (const Byte sub : {1U, 2U}) {
        auto cart = n210_cartridge(sub);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        for (const Address port : {0x4800U, 0x5000U, 0x5800U}) {
            bus.write(port, 0xFFU);
            bus.write(0U, 0xA5U);
            REQUIRE(bus.read(port) == 0xA5U);
            REQUIRE_FALSE(cart.maps_expansion(port));
        }
        auto mapper = n210_board(n210_header(sub));
        StateWriter saved;
        mapper->save_state(saved);
        for (const Address port : {0xC800U, 0xD000U, 0xD800U, 0xF800U, 0xFFFFU})
            mapper->write_prg(port, 0xFFU);
        mapper->write_expansion(0x5000U, 0xFFU);
        mapper->write_expansion(0x5800U, 0xFFU);
        for (unsigned i = 0U; i < 32768U; ++i) {
            mapper->clock_cpu();
            mapper->clock_audio();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        StateWriter after;
        mapper->save_state(after);
        REQUIRE(after.data() == saved.data());
    }
}

TEST_CASE("Namco 175 NVRAM follows volatile bytes and reset retains data while locking RAM",
          "[mappers][namco210][battery][state]") {
    auto mapper = n210_board(n210_header(1U, 0x1000U, 0x1000U));
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0x6000U, 0x11U);
    mapper->write_prg(0x7000U, 0x22U);
    REQUIRE(mapper->battery_ram().size() == 0x1000U);
    REQUIRE(mapper->battery_ram()[0U] == 0x22U);
    mapper->set_battery_ram(std::vector<Byte>(0x1000U, 0x33U));
    REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
    REQUIRE(mapper->read_prg(0x7000U) == 0x33U);
    mapper->reset();
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0xC000U, 1U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
    REQUIRE(mapper->read_prg(0x7000U) == 0x33U);
}

TEST_CASE("Mapper 210 CPU and PPU register writes replay both ASIC profiles atomically",
          "[mappers][namco210][state][integration]") {
    for (const Byte sub : {1U, 2U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(n210_cartridge(sub, sub == 1U ? 0x800U : 0U), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0xE000U);
        for (unsigned i = 0U; i < 12U; ++i)
            emulator.step_instruction();
        const auto saved = emulator.save_state();
        const auto finish = [&] {
            for (unsigned i = 0U; i < 60U; ++i)
                emulator.step_instruction();
            REQUIRE(emulator.read_cpu(0x10U) == 0xE5U);
            REQUIRE(emulator.read_cpu(0x11U) == (sub == 1U ? 0x5AU : 0x60U));
            REQUIRE(emulator.read_cpu(0x12U) == 0x60U);
            REQUIRE(emulator.read_cpu(0x8000U) == 5U);
            REQUIRE(emulator.read_cpu(0xA000U) == 62U);
            REQUIRE(emulator.read_cpu(0xC100U) == 63U);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        finish();
        REQUIRE(emulator.save_state() == expected);
        REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("Mapper 210 state validates bank bits, fixed mirroring, and absent RAM control",
          "[mappers][namco210][state]") {
    for (const Byte sub : {1U, 2U}) {
        auto mapper = n210_board(n210_header(sub, sub == 1U ? 0x800U : 0U));
        mapper->write_prg(0xE000U, 0x85U);
        mapper->write_prg(0xC000U, 1U);
        mapper->write_prg(0x6005U, 0x5AU);
        mapper->write_prg(0xB800U, 0xFFU);
        StateWriter saved;
        mapper->save_state(saved);
        mapper->reset();
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        StateWriter restored;
        mapper->save_state(restored);
        REQUIRE(restored.data() == saved.data());
        REQUIRE(mapper->read_chr(0x1C00U) == 255U);
        if (sub == 1U)
            REQUIRE(mapper->read_prg(0x6805U) == 0x5AU);
        // PRG[3], CHR[8], mirroring control, RAM enable, fixed-size RAM array.
        for (const std::size_t field : {0U, 1U, 2U, 11U, 12U}) {
            auto invalid = saved.data();
            invalid[field] = 0xFFU;
            StateReader bad(invalid);
            mapper->load_state(bad);
            REQUIRE_FALSE(bad.ok());
        }
        auto invalid = saved.data();
        invalid[sub == 1U ? 11U : 12U] = sub == 1U ? 0U : 1U;
        StateReader incompatible(invalid);
        mapper->load_state(incompatible);
        REQUIRE_FALSE(incompatible.ok());
        StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("Mapper 210 factory rejects unsupported variants and impossible board memories",
          "[mappers][namco210]") {
    register_builtin_mappers();
    const auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const Byte sub : {0U, 1U, 2U}) {
        auto header = n210_header(sub);
        REQUIRE(create(header, 0x8000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x100000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
        header.chr_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = n210_header(sub);
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = n210_header(sub);
        header.mirror_mode = MirrorMode::FourScreen;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    }
    REQUIRE_FALSE(create(n210_header(3U), 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(n210_header(1U, 0x4000U), 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(n210_header(1U, 0x1000U, 0x800U), 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(n210_header(2U, 0x800U), 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(n210_header(2U, 0U, 0x800U), 0x8000U, 0x2000U));
    auto header = n210_header(1U, 0U, 0x800U);
    header.has_battery = false;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = n210_header(0U);
    header.has_battery = true; // NES 2.0 has no implicit NVRAM.
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
}

TEST_CASE("Namco 175 persists the actual two-KiB or eight-KiB NVRAM without mirrored copies",
          "[mappers][namco210][battery]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-namco210-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    for (const std::size_t size : {0x800U, 0x2000U}) {
        const auto path = root / (std::to_string(size) + ".sav");
        {
            Emulator writer;
            REQUIRE(writer.load_cartridge(n210_cartridge(1U, size, true), path.string()));
            writer.reset();
            for (unsigned i = 0U; i < 4U; ++i)
                writer.step_instruction();
            writer.flush_battery_ram();
        }
        auto bytes = read_binary_file_capped(path.string(), size, "battery");
        REQUIRE(bytes);
        REQUIRE(bytes->size() == size);
        REQUIRE((*bytes)[5U] == 0x5AU);
        Emulator reader;
        REQUIRE(reader.load_cartridge(n210_cartridge(1U, size, true), path.string()));
        reader.reset();
        REQUIRE(reader.read_cpu(0x6005U) != 0x5AU);
        reader.step_instruction();
        reader.step_instruction();
        REQUIRE(reader.read_cpu(0x6005U) == 0x5AU);
        REQUIRE(reader.read_cpu(0x6805U) == (size == 0x800U ? 0x5AU : 0U));
        const auto saved = reader.save_state();
        REQUIRE(write_binary_file_atomic(path.string(), std::vector<Byte>(size + 1U), "battery"));
        REQUIRE_FALSE(reader.load_battery_ram(path.string()));
        REQUIRE(reader.save_state() == saved);
    }
}
