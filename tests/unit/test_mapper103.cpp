#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper103.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 103U;
    h.is_nes2 = !legacy;
    h.mirror_mode = mirror;
    h.prg_ram_size = legacy ? 8192U : 16384U;
    h.chr_ram_size = 8192U;
    return h;
}

std::vector<Byte> rom() {
    std::vector<Byte> data(131072U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>(i / 2048U);
    return data;
}

std::unique_ptr<Mapper103> board(INesHeader h = header()) {
    REQUIRE(Mapper103::validate_geometry(h, 131072U, 0U));
    return std::make_unique<Mapper103>(h, rom(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper103& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void register_mapper103() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        103U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!Mapper103::validate_geometry(h, prg.size(), chr.size()))
                return {};
            return std::make_unique<Mapper103>(h, std::move(prg), std::move(chr));
        });
}

TEST_CASE("Mapper 103 maps all fixed and switchable PRG addresses", "[mappers][mapper103]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        m->write_prg(0xF000U, 0x10U);
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            m->write_prg(0x8000U, static_cast<Byte>(bank));
            for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
                INFO("bank=" << bank << " addr=" << addr);
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        static_cast<Byte>(bank * 4U + (addr - 0x6000U) / 2048U));
            }
        }
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto offset = addr < 0xB800U   ? 0x18000U + addr - 0x8000U
                                : addr < 0xD800U ? 0x1B800U + addr - 0xB800U
                                                 : 0x1D800U + addr - 0xD800U;
            INFO("addr=" << addr);
            REQUIRE(m->read_prg(static_cast<Address>(addr)) == static_cast<Byte>(offset / 2048U));
        }
    }
}

TEST_CASE("Mapper 103 decodes every register alias without bus conflicts", "[mappers][mapper103]") {
    auto m = board();
    m->write_prg(0xF000U, 0x10U);
    Byte bank = 0U;
    for (unsigned addr = 0x8000U; addr < 0x9000U; ++addr) {
        const auto value = static_cast<Byte>((addr * 37U) ^ (addr >> 4U));
        m->write_prg(static_cast<Address>(addr), value);
        bank = value & 15U;
        REQUIRE(m->read_prg(0x6123U) == static_cast<Byte>(bank * 4U));
    }
    for (unsigned addr = 0xE000U; addr < 0xF000U; ++addr) {
        const auto value = static_cast<Byte>((addr * 73U) ^ (addr >> 7U));
        m->write_prg(static_cast<Address>(addr), value);
        REQUIRE(m->mirror_mode() ==
                ((value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (unsigned addr = 0xF000U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>((addr * 19U) ^ (addr >> 3U));
        m->write_prg(static_cast<Address>(addr), value);
        if ((value & 0x10U) != 0U)
            REQUIRE(m->read_prg(0x6123U) == static_cast<Byte>(bank * 4U));
        else {
            m->write_prg(0x6000U, 0xA7U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
        }
    }
    m->write_prg(0x8000U, 0xFFU);
    REQUIRE(m->read_prg(0x6000U) == 60U);
}

TEST_CASE("Mapper 103 RAM writes remain active when ROM is selected for reading",
          "[mappers][mapper103]") {
    auto m = board();
    const auto write_and_check = [&m](bool rom_selected) {
        m->write_prg(0xFFFFU, rom_selected ? 0x10U : 0U);
        m->write_prg(0x8FFFU, 0x0DU);
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            const auto value = static_cast<Byte>(addr ^ (addr >> 8U) ^ 0x5AU);
            m->write_prg(static_cast<Address>(addr), value);
            REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                    (rom_selected ? static_cast<Byte>(52U + (addr - 0x6000U) / 2048U) : value));
        }
        for (unsigned addr = 0xB800U; addr < 0xD800U; ++addr) {
            const auto value = static_cast<Byte>(addr ^ (addr >> 8U) ^ 0xA5U);
            m->write_prg(static_cast<Address>(addr), value);
            INFO("rom_selected=" << rom_selected << " addr=" << addr);
            REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                    (rom_selected ? 55U + (addr - 0xB800U) / 2048U : value));
        }
    };
    write_and_check(false);
    write_and_check(true);
    write_and_check(false);
    m->write_prg(0x9000U, 0xFFU);
    m->write_prg(0xA000U, 0xFFU);
    m->write_prg(0xB7FFU, 0xFFU);
    m->write_prg(0xD800U, 0xFFU);
    REQUIRE(m->read_prg(0x6000U) == 0x3AU);
}

TEST_CASE("Mapper 103 exposes the full CHR RAM and only documented CPU windows",
          "[mappers][mapper103]") {
    auto m = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ (addr >> 8U)));
        REQUIRE(m->read_chr(static_cast<Address>(addr + 8192U)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
    }
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xB800U, 0xD800U, 0xFFFFU})
        REQUIRE(m->maps_prg(addr));
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    REQUIRE(m->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 103 soft reset retains state and cold power restores deterministic defaults",
          "[mappers][mapper103]") {
    for (auto initial_mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto m = board(header(false, initial_mirror));
        const auto initial = state(*m);
        m->write_prg(0x8ABC, 0x0BU);
        m->write_prg(0xE123U,
                     static_cast<Byte>(initial_mirror == MirrorMode::Horizontal ? 0U : 0x10U));
        m->write_prg(0xFABC, 0x10U);
        m->write_prg(0x6123U, 0xA7U);
        m->write_prg(0xC123U, 0x39U);
        m->write_chr(0x1234U, 0xC7U);
        const auto before_reset = state(*m);
        m->reset();
        REQUIRE(state(*m) == before_reset);
        REQUIRE(m->read_prg(0x6123U) == 44U);
        REQUIRE(m->read_prg(0xC123U) == 56U);
        m->power_on();
        REQUIRE(state(*m) == initial);
        REQUIRE(m->mirror_mode() == initial_mirror);
        REQUIRE(m->read_prg(0x6123U) == 0U);
        REQUIRE(m->read_prg(0xC123U) == 0U);
        REQUIRE(m->read_chr(0x1234U) == 0U);
    }
}

TEST_CASE("Mapper 103 state replay is bounded and rejection is atomic",
          "[mappers][mapper103][state]") {
    auto m = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 11U));
    for (unsigned addr = 0xB800U; addr < 0xD800U; ++addr)
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 17U));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 23U));
    m->write_prg(0x8123U, 0x0EU);
    m->write_prg(0xE456U, 0x10U);
    m->write_prg(0xF789U, 0x10U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 24582U);

    auto copy = board(header(true, MirrorMode::Vertical));
    StateReader restore(saved);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6000U) == 56U);
    REQUIRE(copy->read_prg(0xB800U) == 55U);
    REQUIRE(copy->read_prg(0xC123U) == 56U);

    for (std::size_t size : {0UZ, 1UZ, 2UZ, 5UZ, 6UZ, 100UZ, 24581UZ}) {
        StateReader reader{std::span(saved).first(size)};
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 7>{
             {{0U, 1U}, {1U, 1U}, {2U, 0U}, {3U, 16U}, {4U, 2U}, {5U, 2U}, {5U, 4U}}}) {
        auto bad = saved;
        bad[6U + 123U] ^= 0xFFU;
        bad[offset] = value;
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    auto other = board(header(false, MirrorMode::Horizontal));
    const auto before = state(*other);
    StateReader wrong_header(saved);
    other->load_state(wrong_header);
    REQUIRE_FALSE(wrong_header.ok());
    REQUIRE(state(*other) == before);
}

TEST_CASE("Mapper 103 rejects unsupported geometry and NES 2.0 memory metadata",
          "[mappers][mapper103]") {
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t prg_size = 131072U;
        std::size_t chr_size = 0U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 16384U;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_nvram_size = 8192U;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 10U:
            prg_size = 0U;
            break;
        case 11U:
            prg_size = 65536U;
            break;
        case 12U:
            prg_size = 262144U;
            break;
        case 13U:
            --prg_size;
            break;
        case 14U:
            chr_size = 8192U;
            break;
        case 15U:
            h = header(true);
            h.has_battery = true;
            break;
        case 16U:
            h = header(true);
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Mapper103::validate_geometry(h, prg_size, chr_size));
    }
    REQUIRE(Mapper103::validate_geometry(header(true), 131072U, 0U));
}

TEST_CASE("Mapper 103 cartridge routing reaches RAM, registers and open bus",
          "[mappers][mapper103][bus]") {
    register_mapper103();
    std::vector<Byte> image(16U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 8U;
    image[6] = 0x71U;
    image[7] = 0x68U;
    image[10] = 0x08U;
    image[11] = 0x07U;
    const auto prg = rom();
    image.insert(image.end(), prg.begin(), prg.end());
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    bus.write(0x6000U, 0xA7U);
    bus.write(0xB800U, 0x39U);
    REQUIRE(bus.read(0x6000U) == 0xA7U);
    REQUIRE(bus.read(0xB800U) == 0x39U);
    bus.write(0x8FFFU, 0x0FU);
    bus.write(0xFFFFU, 0x10U);
    REQUIRE(bus.read(0x6000U) == 60U);
    REQUIRE(bus.read(0xB800U) == 55U);
    bus.write(0xE123U, 0U);
    REQUIRE(cartridge->mirror_mode() == MirrorMode::Vertical);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        bus.write(addr, 0xFFU);
        bus.write(0x10U, 0x6CU);
        REQUIRE(bus.read(addr) == 0x6CU);
    }
}

} // namespace
