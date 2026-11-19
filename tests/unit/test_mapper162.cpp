#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper162_fs304.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 162U;
    h.is_nes2 = !legacy;
    h.has_battery = true;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_nvram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte rom_byte(std::size_t index) {
    return static_cast<Byte>((index / 32768U) ^ (index / 256U) ^ index);
}

std::vector<Byte> prg_image(std::size_t size = 2097152U) {
    std::vector<Byte> image(size);
    for (std::size_t i = 0U; i < size; ++i)
        image[i] = rom_byte(i);
    return image;
}

std::unique_ptr<Mapper162Fs304> board(std::size_t prg_size = 2097152U,
                                      bool legacy = false,
                                      bool vertical = true) {
    const auto h = header(legacy, vertical);
    REQUIRE(Mapper162Fs304::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper162Fs304>(h, prg_image(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::size_t selected_bank(Byte low, Byte alternative, Byte high, Byte mode, std::size_t pages) {
    const unsigned a16 = (mode & 4U) != 0U ? ((low >> 1U) & 1U) : 1U;
    unsigned a15 = 0U;
    switch (mode) {
    case 0U:
    case 4U:
        a15 = (alternative >> 1U) & 1U;
        break;
    case 1U:
        a15 = 1U;
        break;
    case 5U:
        a15 = low & 1U;
        break;
    default:
        break;
    }
    const unsigned bank = (static_cast<unsigned>(high) << 4U) |
                          (static_cast<unsigned>(low) & 0x0CU) | (a16 << 1U) | a15;
    return bank & (pages - 1U);
}

std::vector<Byte> machine_image(bool vertical = true) {
    std::vector<Byte> data(16U, 0U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 8U;
    data[6] = static_cast<Byte>(0x22U | (vertical ? 1U : 0U));
    data[7] = 0xA8U;
    data[10] = 0x70U;
    data[11] = 0x07U;
    const auto prg = prg_image(131072U);
    data.insert(data.end(), prg.begin(), prg.end());
    return data;
}

Cartridge cartridge() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        162U, [](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<Mapper162Fs304>(h, std::move(prg), std::move(chr));
        });
    auto result = Cartridge::from_data(machine_image());
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("Mapper 162 starts at PRG bank two with the specified memory population", "[mapper162]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(131072U, legacy, vertical);
            REQUIRE(m->read_prg(0x8000U) == rom_byte(2U * 32768U));
            REQUIRE(m->read_prg(0xFFFFU) == rom_byte(3U * 32768U - 1U));
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(m->battery_ram().size() == 8192U);
            REQUIRE(state(*m).size() == 16392U);
            REQUIRE(m->observes_ppu_address());
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
            REQUIRE_FALSE(m->configure_board_profile("unknown"));
            REQUIRE_FALSE(m->configure_board_jumpers(0U));
        }
}

TEST_CASE("Mapper 162 decodes every register alias and ignores unconnected data bits",
          "[mapper162]") {
    auto m = board(131072U);
    for (unsigned addr = 0x4020U; addr <= 0x5FFFU; ++addr) {
        m->reset();
        m->write_expansion(static_cast<Address>(addr), 0xFFU);
        const auto page = addr & 0xFF00U;
        const bool mapped = page >= 0x5000U && page <= 0x5300U;
        REQUIRE(m->maps_expansion(static_cast<Address>(addr)) == mapped);
        REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) == 0U);
        const auto bytes = state(*m);
        REQUIRE(bytes[2] == (page == 0x5000U ? 0x8FU : 0U));
        REQUIRE(bytes[3] == (page == 0x5100U ? 0x02U : 0U));
        REQUIRE(bytes[4] == (page == 0x5200U ? 0x03U : 0U));
        REQUIRE(bytes[5] == (page == 0x5300U ? 0x05U : 0U));
    }
    const auto before = state(*m);
    for (Address addr : {0x0000U, 0x4020U, 0x5000U, 0x5FFFU, 0x8000U, 0xFFFFU})
        m->write_prg(addr, 0xFFU);
    REQUIRE(state(*m) == before);
}

TEST_CASE("Mapper 162 combines all PRG address pins in all four modes", "[mapper162]") {
    auto m = board();
    for (Byte low = 0U; low < 16U; ++low)
        for (Byte alternative : {0U, 2U})
            for (Byte high = 0U; high < 4U; ++high)
                for (Byte mode : {0U, 1U, 4U, 5U}) {
                    m->write_expansion(0x5000U, low);
                    m->write_expansion(0x5100U, alternative);
                    m->write_expansion(0x5200U, high);
                    m->write_expansion(0x5300U, mode);
                    const auto bank = selected_bank(low, alternative, high, mode, 64U);
                    for (Address addr : {0x8000U, 0x9234U, 0xC000U, 0xFFFFU}) {
                        INFO("low=" << low << " alternative=" << alternative << " high=" << high
                                    << " mode=" << mode << " addr=" << addr);
                        REQUIRE(m->read_prg(addr) == rom_byte(bank * 32768U + (addr & 0x7FFFU)));
                    }
                }
}

TEST_CASE("Mapper 162 wraps disconnected PRG pins only after bank decoding", "[mapper162]") {
    for (std::size_t pages : {1UZ, 2UZ, 4UZ, 8UZ, 16UZ, 32UZ, 64UZ}) {
        auto m = board(pages * 32768U);
        for (Byte mode : {0U, 1U, 4U, 5U})
            for (Byte low = 0U; low < 16U; ++low) {
                m->write_expansion(0x5000U, low);
                m->write_expansion(0x5100U, 2U);
                m->write_expansion(0x5200U, 3U);
                m->write_expansion(0x5300U, mode);
                const auto bank = selected_bank(low, 2U, 3U, mode, pages);
                REQUIRE(m->read_prg(0x8765U) == rom_byte(bank * 32768U + 0x0765U));
            }
    }
}

TEST_CASE("Mapper 162 battery RAM is unbanked writable and retained across reset and power",
          "[mapper162]") {
    auto m = board(32768U);
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(m->maps_prg(static_cast<Address>(addr)));
        m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 29U));
    }
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(m->read_prg(static_cast<Address>(addr)) == static_cast<Byte>(addr * 29U));
    const auto ram = m->battery_ram();
    m->reset();
    m->power_on();
    REQUIRE(std::equal(ram.begin(), ram.end(), m->battery_ram().begin()));

    std::vector<Byte> imported(8192U);
    for (std::size_t i = 0U; i < imported.size(); ++i)
        imported[i] = static_cast<Byte>(i ^ 0xA5U);
    m->set_battery_ram(imported);
    REQUIRE(std::equal(imported.begin(), imported.end(), m->battery_ram().begin()));
    m->set_battery_ram(std::span(imported).first(17U));
    REQUIRE(std::equal(imported.begin() + 17, imported.end(), m->battery_ram().begin() + 17));
}

TEST_CASE("Mapper 162 switches CHR A12 from PPU A9 only on an A13 rising edge", "[mapper162]") {
    auto m = board(32768U);
    m->write_chr(0x0123U, 0x34U);
    m->write_chr(0x1123U, 0xA7U);
    REQUIRE(m->read_chr(0x0123U) == 0x34U);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);

    m->write_expansion(0x5000U, 0x80U);
    m->observe_ppu_address(0x2000U);
    REQUIRE(m->read_chr(0x1123U) == 0x34U);
    REQUIRE(m->read_chr(0x0123U) == 0x34U);
    m->observe_ppu_address(0x2200U);
    REQUIRE(m->read_chr(0x0123U) == 0x34U);
    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2200U);
    REQUIRE(m->read_chr(0x0123U) == 0xA7U);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);
    m->observe_ppu_address(0x2000U);
    REQUIRE(m->read_chr(0x0123U) == 0xA7U);

    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2000U);
    m->write_chr(0x1123U, 0x5CU);
    REQUIRE(m->read_chr(0x0123U) == 0x5CU);
    REQUIRE(m->read_chr(0x1123U) == 0x5CU);
    m->write_expansion(0x5000U, 0U);
    REQUIRE(m->read_chr(0x0123U) == 0x5CU);
    REQUIRE(m->read_chr(0x1123U) == 0xA7U);
}

TEST_CASE("Mapper 162 reset clears registers and latch state with documented RAM lifetime",
          "[mapper162]") {
    auto m = board(131072U);
    m->write_expansion(0x5000U, 0x8FU);
    m->write_expansion(0x5100U, 2U);
    m->write_expansion(0x5200U, 3U);
    m->write_expansion(0x5300U, 5U);
    m->observe_ppu_address(0x2A00U);
    m->write_prg(0x6000U, 0x91U);
    m->write_chr(0x1123U, 0x62U);
    m->reset();
    REQUIRE(state(*m)[2] == 0U);
    REQUIRE(state(*m)[3] == 0U);
    REQUIRE(state(*m)[4] == 0U);
    REQUIRE(state(*m)[5] == 0U);
    REQUIRE(state(*m)[6] == 0U);
    REQUIRE(state(*m)[7] == 0U);
    REQUIRE(m->read_prg(0x8000U) == rom_byte(2U * 32768U));
    REQUIRE(m->read_prg(0x6000U) == 0x91U);
    REQUIRE(m->read_chr(0x1123U) == 0x62U);
    m->power_on();
    REQUIRE(m->read_prg(0x6000U) == 0x91U);
    REQUIRE(m->read_chr(0x1123U) == 0U);
}

TEST_CASE("Mapper 162 accepts only the decoded ROM and RAM geometry", "[mapper162]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (std::size_t size :
                 {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ, 1048576UZ, 2097152UZ}) {
                auto h = header(legacy, vertical);
                REQUIRE(Mapper162Fs304::validate_geometry(h, size, 0U));
                if (legacy) {
                    h.submapper = 15U;
                    REQUIRE(Mapper162Fs304::validate_geometry(h, size, 0U));
                }
            }
    for (std::size_t size : {0UZ, 1UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 65537UZ, 4194304UZ})
        REQUIRE_FALSE(Mapper162Fs304::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 65536UZ})
        REQUIRE_FALSE(Mapper162Fs304::validate_geometry(header(), 131072U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper162Fs304::validate_geometry(h, 131072U, 0U));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = false;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 0U;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        case 5U:
            h.prg_nvram_size = 4096U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(Mapper162Fs304::validate_geometry(h, 131072U, 0U));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper162Fs304::validate_geometry(h, 131072U, 0U));
    }
}

TEST_CASE("Mapper 162 state replays RAM registers and partial PPU latch state", "[mapper162]") {
    auto m = board(131072U);
    m->write_expansion(0x5000U, 0x8DU);
    m->write_expansion(0x5100U, 2U);
    m->write_expansion(0x5200U, 3U);
    m->write_expansion(0x5300U, 5U);
    m->observe_ppu_address(0x2A00U);
    for (std::size_t i = 0U; i < 8192U; ++i) {
        m->write_prg(static_cast<Address>(0x6000U + i), static_cast<Byte>(i * 7U));
        m->write_chr(static_cast<Address>(i), static_cast<Byte>(i * 13U));
    }
    const auto saved = state(*m);
    auto restored = board(131072U);
    StateReader reader(saved);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0x8000U) == m->read_prg(0x8000U));
    REQUIRE(restored->read_chr(0x0123U) == m->read_chr(0x0123U));
    restored->observe_ppu_address(0x2200U);
    REQUIRE(state(*restored) == saved);
    restored->observe_ppu_address(0x0000U);
    restored->observe_ppu_address(0x2000U);
    REQUIRE(state(*restored)[6] == 0U);
    REQUIRE(state(*board(32768U)).size() == saved.size());
}

TEST_CASE("Mapper 162 rejects malformed and mismatched state without mutation", "[mapper162]") {
    auto m = board(131072U);
    m->write_expansion(0x5000U, 0x8DU);
    m->write_expansion(0x5100U, 2U);
    m->write_expansion(0x5200U, 3U);
    m->write_expansion(0x5300U, 5U);
    m->observe_ppu_address(0x2A00U);
    m->write_prg(0x6000U, 0xC3U);
    m->write_chr(0x0123U, 0x79U);
    const auto saved = state(*m);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    };
    for (std::size_t size : {0UZ, 1UZ, 2UZ, 7UZ, 8UZ, 100UZ, 8199UZ, saved.size() - 1U})
        reject(std::span(saved).first(size));
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        reject(bad);
    }
    for (auto [field, value] : std::array<std::pair<std::size_t, Byte>, 12>{{{2U, 0x10U},
                                                                             {2U, 0x20U},
                                                                             {2U, 0x40U},
                                                                             {3U, 0x01U},
                                                                             {3U, 0x80U},
                                                                             {4U, 0x04U},
                                                                             {5U, 0x02U},
                                                                             {5U, 0x08U},
                                                                             {6U, 0x02U},
                                                                             {6U, 0xFFU},
                                                                             {7U, 0x02U},
                                                                             {7U, 0xFFU}}}) {
        auto bad = saved;
        bad[field] = value;
        reject(bad);
    }
    auto other_geometry = board(262144U);
    const auto geometry_before = state(*other_geometry);
    StateReader wrong_geometry(saved);
    other_geometry->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other_geometry) == geometry_before);
    auto other_mirror = board(131072U, false, false);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);
}

TEST_CASE("Mapper 162 cartridge bus routes registers WRAM and write-only open bus", "[mapper162]") {
    auto c = cartridge();
    REQUIRE(c.read_prg(0x8000U) == rom_byte(2U * 32768U));
    c.write_expansion(0x5300U, 4U);
    c.write_expansion(0x5100U, 2U);
    REQUIRE(c.read_prg(0x8000U) == rom_byte(1U * 32768U));
    MemoryBus bus;
    bus.connect_cartridge(&c);
    bus.write(0x5300U, 4U);
    bus.write(0x5100U, 2U);
    REQUIRE(bus.read(0x8000U) == rom_byte(1U * 32768U));
    bus.write(0x5100U, 0U);
    REQUIRE(bus.read(0x8000U) == rom_byte(0U * 32768U));
    bus.write(0x6ABCU, 0x96U);
    REQUIRE(bus.read(0x6ABCU) == 0x96U);
    for (Address addr : {0x5000U, 0x50FFU, 0x5100U, 0x5200U, 0x5300U, 0x53FFU}) {
        bus.write(0U, 0x5AU);
        REQUIRE(bus.read(addr) == 0x5AU);
    }
}

} // namespace
