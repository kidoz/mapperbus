#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper125_lh32.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kPrgSize = 16U * kBankSize;
constexpr std::size_t kStateSize = 3U + 2U * kBankSize;

INesHeader header(bool nes2 = true) {
    INesHeader h{};
    h.mapper_number = 125U;
    h.is_nes2 = nes2;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> rom(kPrgSize);
    for (unsigned bank = 0U; bank < 16U; ++bank)
        for (unsigned offset = 0U; offset < kBankSize; ++offset)
            rom[bank * kBankSize + offset] = prg_cell(bank, offset);
    return rom;
}

std::unique_ptr<Lh32> board(bool nes2 = true) {
    const auto h = header(nes2);
    auto rom = prg_rom();
    REQUIRE(Lh32::validate_geometry(h, rom.size(), 0U));
    return std::make_unique<Lh32>(h, std::move(rom), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void require_fixed_prg(Lh32& mapper) {
    for (unsigned offset = 0U; offset < kBankSize; ++offset) {
        REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + offset)) == prg_cell(12U, offset));
        REQUIRE(mapper.read_prg(static_cast<Address>(0xA000U + offset)) == prg_cell(13U, offset));
        REQUIRE(mapper.read_prg(static_cast<Address>(0xE000U + offset)) == prg_cell(15U, offset));
    }
}

std::vector<Byte> image(bool nes2) {
    std::vector<Byte> bytes(16U, 0U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 8U;
    bytes[6] = 0xD1U;
    bytes[7] = nes2 ? 0x78U : 0x70U;
    bytes[8] = nes2 ? 0U : 1U;
    bytes[10] = nes2 ? 0x07U : 0U;
    bytes[11] = nes2 ? 0x07U : 0U;
    auto rom = prg_rom();
    bytes.insert(bytes.end(), rom.begin(), rom.end());
    return bytes;
}

Cartridge cartridge(bool nes2 = true) {
    MapperRegistry::instance().register_mapper(
        125U, [](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<Lh32>(h, std::move(prg), std::move(chr));
        });
    auto result = Cartridge::from_data(image(nes2));
    REQUIRE(result);
    return std::move(*result);
}

void ppu_address(Ppu& ppu, Address addr) {
    (void)ppu.read_register(2U);
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

void ppu_write(Ppu& ppu, Address addr, Byte value) {
    ppu_address(ppu, addr);
    ppu.write_register(7U, value);
}

Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}

TEST_CASE("Mapper 125 validates the exact LH32 geometry", "[mapper125]") {
    for (bool nes2 : {false, true}) {
        REQUIRE(Lh32::validate_geometry(header(nes2), kPrgSize, 0U));
        for (std::size_t size : {0UZ, 8192UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ})
            REQUIRE_FALSE(Lh32::validate_geometry(header(nes2), size, 0U));
        for (std::size_t size : {1UZ, 8192UZ, 65536UZ, 131072UZ, 262144UZ})
            REQUIRE_FALSE(Lh32::validate_geometry(header(nes2), kPrgSize, size));
        for (MirrorMode mirror : {MirrorMode::Horizontal,
                                  MirrorMode::FourScreen,
                                  MirrorMode::SingleLower,
                                  MirrorMode::SingleUpper}) {
            auto h = header(nes2);
            h.mirror_mode = mirror;
            REQUIRE_FALSE(Lh32::validate_geometry(h, kPrgSize, 0U));
        }
        auto h = header(nes2);
        h.has_battery = true;
        REQUIRE_FALSE(Lh32::validate_geometry(h, kPrgSize, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Lh32::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0U;
            break;
        case 1U:
            h.prg_ram_size = 16384U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 4096U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 8192U;
            h.has_battery = true;
            break;
        default:
            h.chr_ram_size = 0U;
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Lh32::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 125 maps every PRG bank and fixed window", "[mapper125]") {
    auto mapper = board();
    require_fixed_prg(*mapper);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        const auto bank = value & 15U;
        for (unsigned offset = 0U; offset < kBankSize; ++offset)
            REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                    prg_cell(bank, offset));
        require_fixed_prg(*mapper);
    }
}

TEST_CASE("Mapper 125 decodes only the documented register and RAM windows", "[mapper125]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(0x6000U, 0x0BU);
        mapper->write_prg(static_cast<Address>(addr), 0x04U);
        const auto expected_bank = addr >= 0x6000U && addr < 0x8000U ? 4U : 11U;
        REQUIRE(mapper->read_prg(0x6123U) == prg_cell(expected_bank, 0x123U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE("Mapper 125 PRG and CHR RAM cover every cell without ROM writes", "[mapper125]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kBankSize; ++offset) {
        const auto value = static_cast<Byte>(offset ^ (offset >> 8U));
        mapper->write_prg(static_cast<Address>(0xC000U + offset), value);
        mapper->write_chr(static_cast<Address>(offset), value ^ 0xFFU);
    }
    mapper->write_prg(0x7FFFU, 0x0EU);
    for (unsigned offset = 0U; offset < kBankSize; ++offset) {
        const auto value = static_cast<Byte>(offset ^ (offset >> 8U));
        REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == (value ^ 0xFFU));
    }
    require_fixed_prg(*mapper);
    REQUIRE(mapper->read_prg(0x6000U) == prg_cell(14U, 0U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 125 reset retains RAM and cold power clears volatile state", "[mapper125]") {
    for (bool nes2 : {false, true}) {
        auto mapper = board(nes2);
        mapper->write_prg(0x6A55U, 0xAFU);
        mapper->write_prg(0xD123U, 0x39U);
        mapper->write_chr(0x1ABC, 0xA7U);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x6000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->read_prg(0xD123U) == 0x39U);
        REQUIRE(mapper->read_chr(0x1ABC) == 0xA7U);
        mapper->write_prg(0x6000U, 0x0CU);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->read_prg(0xD123U) == 0U);
        REQUIRE(mapper->read_chr(0x1ABC) == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE("Mapper 125 snapshots replay RAM and reject damage atomically", "[mapper125]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kBankSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0xC000U + offset), static_cast<Byte>(offset * 3U));
        mapper->write_chr(static_cast<Address>(offset), static_cast<Byte>(offset * 5U));
    }
    mapper->write_prg(0x7FFFU, 0x0DU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    mapper->power_on();
    mapper->write_prg(0xC000U, 0xFFU);
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x6000U) == prg_cell(13U, 0U));

    const auto before = state(*mapper);
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated(std::span(saved).first(length));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == before);
    }
    for (Byte bank : {16U, 17U, 31U, 128U, 255U}) {
        auto corrupt = saved;
        corrupt[2] = bank;
        StateReader invalid(corrupt);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == before);
    }
    for (std::size_t offset : {0UZ, 1UZ}) {
        auto corrupt = saved;
        corrupt[offset] ^= 1U;
        StateReader invalid(corrupt);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 125 CPU bus routing preserves open bus and RAM windows", "[mapper125]") {
    auto cart = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        bus.write(addr, 0xA7U);
        bus.write(0x0010U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x6FFFU, 0x03U);
    REQUIRE(bus.read(0x6000U) == prg_cell(3U, 0U));
    bus.write(0xD000U, 0xA7U);
    REQUIRE(bus.read(0xD000U) == 0xA7U);
    bus.write(0x8000U, 0x00U);
    bus.write(0xFFFFU, 0x00U);
    REQUIRE(bus.read(0x6000U) == prg_cell(3U, 0U));
    REQUIRE(bus.read(0x8000U) == prg_cell(12U, 0U));
    REQUIRE(bus.read(0xFFFFU) == prg_cell(15U, 0x1FFFU));
    REQUIRE_FALSE(cart.has_battery());
    REQUIRE(cart.battery_ram().empty());
}

TEST_CASE("Mapper 125 PPUDATA uses CHR RAM and vertical CIRAM for both header formats",
          "[mapper125]") {
    for (bool nes2 : {false, true}) {
        auto cart = cartridge(nes2);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        ppu.reset();
        ppu_write(ppu, 0x0123U, 0xA7U);
        REQUIRE(ppu_read(ppu, 0x0123U) == 0xA7U);
        ppu_write(ppu, 0x2000U, 0x19U);
        ppu_write(ppu, 0x2400U, 0x27U);
        ppu_write(ppu, 0x2800U, 0x39U);
        ppu_write(ppu, 0x2C00U, 0x51U);
        REQUIRE(ppu_read(ppu, 0x2000U) == 0x39U);
        REQUIRE(ppu_read(ppu, 0x2400U) == 0x51U);
        REQUIRE(ppu_read(ppu, 0x2800U) == 0x39U);
        REQUIRE(ppu_read(ppu, 0x2C00U) == 0x51U);
        cart.reset();
        REQUIRE(ppu_read(ppu, 0x0123U) == 0xA7U);
        cart.power_on();
        REQUIRE(ppu_read(ppu, 0x0123U) == 0U);
    }
}

} // namespace
