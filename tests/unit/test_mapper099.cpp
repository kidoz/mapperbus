#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
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
    REQUIRE(VsSystem::validate_geometry(header, header.prg_rom_size, header.chr_rom_size));
    return std::make_unique<VsSystem>(
        header, patterned_rom(header.prg_rom_size, 3U), patterned_rom(header.chr_rom_size, 71U));
}

std::vector<Byte> mapper_state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void register_mapper099() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        99U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!VsSystem::validate_geometry(header, prg.size(), chr.size()))
                return nullptr;
            return std::make_unique<VsSystem>(header, std::move(prg), std::move(chr));
        });
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
    REQUIRE(size == 40960U);
    return 0x36U;
}

std::vector<Byte> make_image(std::size_t prg_size,
                             std::size_t chr_size,
                             bool battery,
                             MirrorMode mirror) {
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
        0x68U,
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
    register_mapper099();
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
    REQUIRE(bus.read(0xE000U) == 0xA7U);
    REQUIRE(bus.read(0x8000U) == patterned_rom(24576U, 3U)[0]);
    bus.write(0x4016U, 0xFFU);
    REQUIRE(bus.read(0x8000U) == patterned_rom(24576U, 3U)[0]);
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

} // namespace
