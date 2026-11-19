#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/vs_system.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
constexpr std::size_t kBankSize = 8192U;

INesHeader make_header(std::size_t prg_size = 40960U,
                       std::size_t chr_size = 16384U,
                       bool battery = false,
                       MirrorMode mirror = MirrorMode::FourScreen,
                       bool legacy = false) {
    INesHeader header{};
    header.mapper_number = 99U;
    header.is_nes2 = !legacy;
    header.has_battery = battery;
    header.mirror_mode = mirror;
    header.prg_rom_size = prg_size;
    header.chr_rom_size = chr_size;
    if (!legacy) {
        header.prg_ram_size = battery ? 0U : 2048U;
        header.prg_nvram_size = battery ? 2048U : 0U;
    } else {
        header.prg_ram_size = battery ? 0U : 8192U;
        header.prg_nvram_size = battery ? 8192U : 0U;
    }
    return header;
}

std::vector<Byte> patterned_rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = static_cast<Byte>(salt + offset / kBankSize * 29U + offset + (offset >> 8U));
    return data;
}

std::unique_ptr<VsSystem> make_board(const INesHeader& header = make_header()) {
    return std::make_unique<VsSystem>(
        header, patterned_rom(header.prg_rom_size, 3U), patterned_rom(header.chr_rom_size, 71U));
}

std::vector<Byte> mapper_state(const auto& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte size_encoding(std::size_t size) {
    if (size == 8192U)
        return 0x34U;
    if (size == 16384U)
        return 1U;
    if (size == 24576U)
        return 0x35U;
    if (size == 32768U)
        return 2U;
    if (size == 65536U)
        return 4U;
    REQUIRE(size == 40960U);
    return 0x36U;
}

std::vector<Byte> make_image(std::size_t prg_size,
                             std::size_t chr_size,
                             bool battery,
                             MirrorMode mirror) {
    register_builtin_mappers();
    const bool exponent_prg = prg_size == 8192U || prg_size == 24576U || prg_size == 40960U;
    std::array<Byte, 16> header{
        'N',
        'E',
        'S',
        0x1AU,
        size_encoding(prg_size),
        static_cast<Byte>(chr_size / 8192U),
        static_cast<Byte>(0x30U | (battery ? 2U : 0U) |
                          (mirror == MirrorMode::FourScreen ? 8U
                           : mirror == MirrorMode::Vertical ? 1U
                                                            : 0U)),
        0x69U,
        0U,
        static_cast<Byte>(exponent_prg ? 0x0FU : 0U),
        static_cast<Byte>(battery ? 0x50U : 0x05U),
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    std::vector<Byte> image(header.begin(), header.end());
    const auto prg = patterned_rom(prg_size, 3U);
    const auto chr = patterned_rom(chr_size, 71U);
    image.insert(image.end(), prg.begin(), prg.end());
    image.insert(image.end(), chr.begin(), chr.end());
    return image;
}

Byte read_ppu(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
    static_cast<void>(ppu.read_register(7U));
    return ppu.read_register(7U);
}

void write_ppu(Ppu& ppu, Address addr, Byte value) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
    ppu.write_register(7U, value);
}

TEST_CASE("Mapper 099 accepts every ROM population and fixed mirroring mode", "[mapper099]") {
    for (bool legacy : {false, true})
        for (bool battery : {false, true})
            for (auto mirror :
                 {MirrorMode::Horizontal, MirrorMode::Vertical, MirrorMode::FourScreen})
                for (std::size_t banks = 1U; banks <= 5U; ++banks)
                    for (std::size_t chr_banks = 1U; chr_banks <= 2U; ++chr_banks) {
                        const auto header = make_header(
                            banks * kBankSize, chr_banks * kBankSize, battery, mirror, legacy);
                        auto mapper = make_board(header);
                        REQUIRE(mapper->mirror_mode() == mirror);
                        REQUIRE(mapper_state(*mapper).size() == 2051U);
                        REQUIRE(mapper->battery_ram().size() == (battery ? 2048U : 0U));
                        REQUIRE_FALSE(mapper->has_expansion_audio());
                        REQUIRE_FALSE(mapper->irq_pending());
                        REQUIRE(mapper->audio_output() == 0.0F);
                    }
}

TEST_CASE("Mapper 099 maps populated PRG sockets and masks every empty socket", "[mapper099]") {
    for (std::size_t banks = 1U; banks <= 5U; ++banks) {
        auto mapper = make_board(make_header(banks * kBankSize));
        const auto prg = patterned_rom(banks * kBankSize, 3U);
        for (Byte selected = 0U; selected <= 1U; ++selected) {
            mapper->observe_cpu_write(0x4016U, static_cast<Byte>(selected << 2U));
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
                std::size_t bank = (addr - 0x8000U) / kBankSize;
                if (banks == 5U && bank == 0U && selected != 0U)
                    bank = 4U;
                const std::size_t offset = bank * kBankSize + (addr & 0x1FFFU);
                const bool populated = offset < banks * kBankSize;
                REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) ==
                        (populated ? 0xFFU : 0U));
                REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                        (populated ? prg[offset] : 0U));
            }
        }
    }
}

TEST_CASE("Mapper 099 selects CHR with OUT2 and leaves an absent CHR bank undriven",
          "[mapper099]") {
    for (std::size_t chr_banks = 1U; chr_banks <= 2U; ++chr_banks) {
        auto mapper = make_board(make_header(40960U, chr_banks * kBankSize));
        const auto chr = patterned_rom(chr_banks * kBankSize, 71U);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->observe_cpu_write(0x4016U, static_cast<Byte>(value));
            const std::size_t bank = (value >> 2U) & 1U;
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const std::size_t offset = bank * kBankSize + addr;
                const bool populated = offset < chr.size();
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        (populated ? chr[offset] : static_cast<Byte>(addr)));
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(value ^ 0xFFU));
            }
        }
    }
}

TEST_CASE("Mapper 099 decodes only the $4016 write strobe without bus conflicts", "[mapper099]") {
    auto mapper = make_board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->observe_cpu_write(static_cast<Address>(addr), 0xFFU);
        REQUIRE(mapper_state(*mapper)[2] == (addr == 0x4016U ? 1U : 0U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->observe_cpu_write(0x4016U, static_cast<Byte>(value));
        REQUIRE(mapper_state(*mapper)[2] == ((value >> 2U) & 1U));
        mapper->write_prg(0x8000U, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_expansion(0x4016U, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(mapper_state(*mapper)[2] == ((value >> 2U) & 1U));
    }
}

TEST_CASE("Mapper 099 mirrors 2 KiB shared RAM and applies reset and persistence policies",
          "[mapper099]") {
    for (bool battery : {false, true}) {
        auto mapper = make_board(make_header(40960U, 16384U, battery));
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        for (unsigned offset = 0U; offset < 2048U; ++offset)
            for (unsigned window = 0U; window < 4U; ++window)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + window * 2048U + offset)) ==
                        static_cast<Byte>((0x7800U + offset) ^ ((0x7800U + offset) >> 8U)));
        mapper->observe_cpu_write(0x4016U, 4U);
        const auto before_reset = mapper_state(*mapper);
        mapper->reset();
        REQUIRE(mapper_state(*mapper)[2] == 0U);
        REQUIRE(std::equal(
            before_reset.begin() + 3, before_reset.end(), mapper_state(*mapper).begin() + 3));
        mapper->power_on();
        for (unsigned offset = 0U; offset < 2048U; ++offset)
            REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                    (battery ? before_reset[offset + 3U] : 0U));
        if (battery) {
            const auto imported = patterned_rom(2048U, 0xA7U);
            mapper->set_battery_ram(imported);
            REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
            const auto before = mapper_state(*mapper);
            for (std::size_t size : {0U, 1U, 2047U, 2049U, 8192U}) {
                mapper->set_battery_ram(std::vector<Byte>(size, 0x5AU));
                REQUIRE(mapper_state(*mapper) == before);
            }
        } else {
            mapper->set_battery_ram(std::vector<Byte>(2048U, 0x5AU));
            REQUIRE(mapper->battery_ram().empty());
            for (unsigned offset = 0U; offset < 2048U; ++offset)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == 0U);
        }
    }
}

TEST_CASE("Mapper 099 rejects unsupported geometry and native memory metadata", "[mapper099]") {
    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        header.console_type = 1U;
        REQUIRE_FALSE(VsSystem::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg : {0U, 4096U, 8191U, 12288U, 40961U, 49152U})
        reject(make_header(), prg, 8192U);
    for (std::size_t chr : {0U, 4096U, 8191U, 16385U, 24576U})
        reject(make_header(), 40960U, chr);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        reject(header, 40960U, 16384U);
    }
    for (auto mirror : {MirrorMode::SingleLower, MirrorMode::SingleUpper})
        reject(make_header(40960U, 16384U, false, mirror), 40960U, 16384U);
    for (unsigned field = 0U; field < 6U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.prg_ram_size = 0U;
            break;
        case 1U:
            header.prg_ram_size = 8192U;
            break;
        case 2U:
            header.prg_nvram_size = 2048U;
            break;
        case 3U:
            header.chr_ram_size = 8192U;
            break;
        case 4U:
            header.chr_nvram_size = 8192U;
            break;
        default:
            header = make_header(40960U, 16384U, true);
            header.prg_nvram_size = 0U;
            break;
        }
        reject(header, 40960U, 16384U);
    }
}

TEST_CASE("Mapper 099 state replay is bounded tagged and atomic", "[mapper099]") {
    auto mapper = make_board(make_header(40960U, 16384U, true));
    mapper->observe_cpu_write(0x4016U, 0xFFU);
    const auto ram = patterned_rom(2048U, 19U);
    mapper->set_battery_ram(ram);
    const auto saved = mapper_state(*mapper);
    REQUIRE(saved.size() == 2051U);
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }
    for (unsigned bank = 2U; bank < 256U; ++bank) {
        auto corrupt = saved;
        corrupt[2] = static_cast<Byte>(bank);
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }
    for (bool battery : {false, true})
        for (std::size_t banks = 1U; banks <= 5U; ++banks)
            for (std::size_t chr_banks = 1U; chr_banks <= 2U; ++chr_banks)
                for (auto mirror :
                     {MirrorMode::Horizontal, MirrorMode::Vertical, MirrorMode::FourScreen}) {
                    auto other = make_board(
                        make_header(banks * kBankSize, chr_banks * kBankSize, battery, mirror));
                    const auto before = mapper_state(*other);
                    StateReader reader(saved);
                    other->load_state(reader);
                    const bool same = banks == 5U && chr_banks == 2U && battery &&
                                      mirror == MirrorMode::FourScreen;
                    REQUIRE(reader.ok() == same);
                    REQUIRE(mapper_state(*other) == (same ? saved : before));
                }
    mapper->power_on();
    mapper->set_battery_ram(std::vector<Byte>(2048U, 0U));
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(mapper_state(*mapper) == saved);
    REQUIRE(std::ranges::equal(mapper->battery_ram(), ram));
}

TEST_CASE("Mapper 099 CPU bus and PPU preserve open bus and four-screen accesses", "[mapper099]") {
    register_builtin_mappers();
    auto cartridge = Cartridge::from_data(make_image(24576U, 8192U, true, MirrorMode::FourScreen));
    if (!cartridge)
        FAIL(cartridge.error());
    REQUIRE(cartridge);
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&*cartridge);
    bus.connect_ppu(&ppu);
    ppu.connect_cartridge(&*cartridge);

    bus.write(0U, 0xA7U);
    REQUIRE(bus.read(0x8000U) == 0xA7U);
    REQUIRE(bus.read(0xA000U) == patterned_rom(24576U, 3U)[0]);
    bus.write(0x4016U, 0xFFU);
    REQUIRE(bus.read(0xA000U) == patterned_rom(24576U, 3U)[0]);
    REQUIRE(read_ppu(ppu, 0x0123U) == 0x23U);
    bus.write(0x4016U, 0U);
    REQUIRE(read_ppu(ppu, 0x0123U) == patterned_rom(8192U, 71U)[0x0123U]);

    bus.write(0x6000U, 0x39U);
    REQUIRE(bus.read(0x6800U) == 0x39U);
    REQUIRE(bus.read(0x7000U) == 0x39U);
    REQUIRE(bus.read(0x7800U) == 0x39U);
    REQUIRE(cartridge->battery_ram()[0] == 0x39U);

    write_ppu(ppu, 0x2000U, 0x11U);
    write_ppu(ppu, 0x2400U, 0x22U);
    write_ppu(ppu, 0x2800U, 0x33U);
    write_ppu(ppu, 0x2C00U, 0x44U);
    REQUIRE(read_ppu(ppu, 0x2000U) == 0x11U);
    REQUIRE(read_ppu(ppu, 0x2400U) == 0x22U);
    REQUIRE(read_ppu(ppu, 0x2800U) == 0x33U);
    REQUIRE(read_ppu(ppu, 0x2C00U) == 0x44U);
    REQUIRE(read_ppu(ppu, 0x3000U) == 0x11U);
    REQUIRE(read_ppu(ppu, 0x3400U) == 0x22U);
}

TEST_CASE("Vs. Unisystem boots through the reset socket and reads the cabinet harness",
          "[mapper099]") {
    auto image = make_image(24576U, 8192U, false, MirrorMode::FourScreen);
    image[13] = 2U;
    image[15] = 5U; // 1P on the left stick ($4017).
    constexpr std::array<Byte, 25> program{
        0xA9, 0x01, 0x8D, 0x16, 0x40, // LDA #1 / STA $4016
        0xA9, 0x00, 0x8D, 0x16, 0x40, // LDA #0 / STA $4016
        0xAD, 0x16, 0x40, 0x85, 0x00, // LDA $4016 / STA $00
        0xAD, 0x17, 0x40, 0x85, 0x01, // LDA $4017 / STA $01
        0xA9, 0xCA, 0x85, 0x02, 0xEA, // mark completion
    };
    std::ranges::copy(program, image.begin() + 16);
    image[16U + 24576U - 4U] = 0x00;
    image[16U + 24576U - 3U] = 0xA0;
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cartridge), ""));
    emulator.power_on();
    auto* cabinet = emulator.vs_cabinet();
    REQUIRE(cabinet);
    emulator.controller().set_button_state(0, Button::A, true);
    cabinet->set_dips(0x96U);
    cabinet->set_coin(true);
    cabinet->set_service(true);
    for (int step = 0; step < 10; ++step)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0) == 0x34); // DIP2, service, coin and P2 A=0
    REQUIRE(emulator.read_cpu(1) == 0x95); // remaining DIP lines and P1 A=1
    REQUIRE(emulator.read_cpu(2) == 0xCA);
}

TEST_CASE("Vs. cabinet pulses and book-keeping survive state restore", "[mapper099]") {
    auto header = make_header(32768U);
    header.console_type = 1U;
    auto mapper = make_board(header);
    auto* cabinet = mapper->vs_cabinet();
    REQUIRE(cabinet);
    cabinet->set_coin(true);
    cabinet->set_dips(0xFF);
    mapper->observe_cpu_write(0x4020, 1);
    const auto state = mapper_state(*mapper);
    for (int i = 0; i < 89489; ++i)
        mapper->clock_cpu();
    cabinet->set_coin(true);
    REQUIRE(cabinet->read_port(0, 0) == 0x18);
    cabinet->set_coin(false);
    cabinet->set_coin(true);
    REQUIRE(cabinet->read_port(0, 0) == 0x38);
    mapper->observe_cpu_read(0x5F20, 2);
    REQUIRE(cabinet->coins_counted() == 1);
    auto invalid = state;
    invalid.back() = 0x80;
    const auto before = mapper_state(*mapper);
    StateReader corrupt(invalid);
    mapper->load_state(corrupt);
    REQUIRE_FALSE(corrupt.ok());
    REQUIRE(mapper_state(*mapper) == before);
    StateReader restored(state);
    mapper->load_state(restored);
    REQUIRE(restored.ok());
    REQUIRE(cabinet->coins_counted() == 0);
    REQUIRE(cabinet->read_port(0, 0) == 0x38);
    mapper->observe_cpu_read(0x4040, 0); // A5=0 leaves counter unchanged
    mapper->observe_cpu_write(0x5FE0, 0);
    REQUIRE(cabinet->coins_counted() == 1);
}

TEST_CASE("Vs. RGB PPU palettes, emphasis and 2C05 registers match the header", "[mapper099]") {
    auto image = make_image(32768U, 16384U, false, MirrorMode::FourScreen);
    constexpr std::array<Byte, 8> models{0, 2, 3, 4, 5, 9, 10, 11};
    constexpr std::array<uint32_t, 8> color0{0xFF6D6D6D,
                                             0xFFFFB6B6,
                                             0xFF000000,
                                             0xFFB600FF,
                                             0xFF926D00,
                                             0xFF6D6D6D,
                                             0xFF6D6D6D,
                                             0xFF6D6D6D};
    for (std::size_t i = 0; i < models.size(); ++i) {
        CAPTURE(i);
        image[13] = models[i];
        auto cartridge = Cartridge::from_data(image);
        REQUIRE(cartridge);
        Ppu ppu;
        ppu.connect_cartridge(&*cartridge);
        ppu.step_dots(3);
        REQUIRE(ppu.frame_buffer().pixels[0] == color0[i]);
        ppu.write_register(models[i] >= 9U ? 0U : 1U, 0x40U);
        ppu.step_dots(12);
        REQUIRE(ppu.ppumask() == 0x40U);
        REQUIRE(ppu.frame_buffer().pixels[8] == (color0[i] | 0x0000FF00));
        ppu.write_register(3, 0xD2);
        REQUIRE(ppu.read_register(4) == 0xD2);
        if (models[i] >= 9U) {
            const Byte signature = models[i] == 9U ? 0x3DU : models[i] == 10U ? 0x1CU : 0x1BU;
            REQUIRE((ppu.read_register(2) & 0x3FU) == signature);
        }
        write_ppu(ppu, 0x2F00, 0xA5);
        write_ppu(ppu, 0x3F00, 0x32);
        REQUIRE(read_ppu(ppu, 0x3F00) == 0xA5); // buffered external nametable
    }
}

TEST_CASE("Vs. registry rejects unsupported console, cabinet and device profiles", "[mapper099]") {
    auto image = make_image(32768U, 16384U, false, MirrorMode::FourScreen);
    for (Byte profile : {0x08, 0x0F, 0x10, 0x41, 0x50, 0x60}) {
        image[13] = profile;
        REQUIRE_FALSE(Cartridge::from_data(image));
    }
    image[13] = 0U;
    image[15] = 7U;
    REQUIRE_FALSE(Cartridge::from_data(image)); // Vs. Zapper
    image[15] = 4U;
    image[7] = 0x68U;
    REQUIRE_FALSE(Cartridge::from_data(image)); // regular NES
    image[7] = 0x61U;
    REQUIRE_FALSE(Cartridge::from_data(image)); // missing NES 2.0 PPU information
}

TEST_CASE("Vs. VRC1 cartridge and historical ID share banking, cabinet and state behavior",
          "[mapper099][mapper151]") {
    for (const unsigned id : {75U, 151U}) {
        CAPTURE(id);
        auto image = make_image(65536U, 65536U, false, MirrorMode::FourScreen);
        image[6] = static_cast<Byte>((id << 4U) | 8U);
        image[7] = static_cast<Byte>((id & 0xF0U) | 9U);
        image[13] = 2U;
        image[15] = 5U;
        auto cart = Cartridge::from_data(image);
        INFO((cart ? "" : cart.error()));
        REQUIRE(cart);
        REQUIRE(cart->header().mapper_number == id);
        auto* cabinet = cart->vs_cabinet();
        REQUIRE(cabinet != nullptr);
        REQUIRE(cabinet->ppu_type() == 2U);
        REQUIRE(cabinet->player_for_port(0) == 1);
        auto& board = *cart;
        board.write_prg(0x8000U, 6U);
        board.write_prg(0xE000U, 3U);
        board.write_prg(0x6002U, 0xACU);
        board.observe_cpu_write(0x4016U, 4U);
        REQUIRE(board.read_prg(0x8000U) == image[16U + 6U * kBankSize]);
        REQUIRE(board.read_chr(0) == image[16U + 65536U + 3U * 4096U]);
        REQUIRE(board.read_prg(0x7802U) == 0xACU);
        REQUIRE(board.mirror_mode() == MirrorMode::FourScreen);
        const auto state = mapper_state(board);
        board.reset();
        StateReader restore(state);
        board.load_state(restore);
        REQUIRE(restore.ok());
        REQUIRE(mapper_state(board) == state);
        StateReader truncated(std::span<const Byte>(state).first(state.size() - 1));
        board.load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(mapper_state(board) == state);
        image[13] = 0x11U;
        REQUIRE_FALSE(Cartridge::from_data(image));
        if (id == 151U) {
            image[7] &= 0xF0U;
            REQUIRE_FALSE(Cartridge::from_data(image)); // unknown legacy socket contents
        }
    }
}

TEST_CASE("Legacy Vs. VRC1 image pairs identify the RGB PPU and joystick harness", "[mapper151]") {
    struct LegacyPair {
        std::array<Byte, 4> prg, chr;
        Byte ppu, port;
    };
    // Zero-filled sockets with four synthetic suffix bytes matching the database CRCs.
    for (auto pair : {LegacyPair{std::array<Byte, 4>{0x53, 0xDE, 0xE4, 0x11},
                                 std::array<Byte, 4>{0x26, 0x86, 0x6F, 0x40},
                                 2,
                                 1},
                      LegacyPair{std::array<Byte, 4>{0xE0, 0x05, 0xCB, 0xAE},
                                 std::array<Byte, 4>{0x57, 0x11, 0xD0, 0xD6},
                                 4,
                                 0}}) {
        auto image = make_image(65536, 65536, false, MirrorMode::FourScreen);
        image[6] = 0x70;
        image[7] = 0x90;
        std::fill(image.begin() + 8, image.end(), 0);
        std::ranges::copy(pair.prg, image.begin() + 16 + 65536 - 4);
        std::ranges::copy(pair.chr, image.end() - 4);
        auto cart = Cartridge::from_data(image);
        REQUIRE(cart);
        REQUIRE_FALSE(cart->header().is_nes2);
        REQUIRE(cart->header().mapper_number == 151);
        REQUIRE(cart->vs_cabinet()->ppu_type() == pair.ppu);
        REQUIRE(cart->vs_cabinet()->player_for_port(0) == pair.port);
        image.back() ^= 1;
        REQUIRE_FALSE(Cartridge::from_data(image));
    }
}

} // namespace
