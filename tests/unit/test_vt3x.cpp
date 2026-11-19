#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/vt3x.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader vt3x_header() {
    INesHeader header{};
    header.mapper_number = 296U;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    header.chr_ram_size = 8192U;
    header.region = Region::NTSC;
    return header;
}

Byte pattern(std::size_t address) {
    return static_cast<Byte>((address * 29U) ^ (address >> 7U) ^ (address >> 13U) ^ 0xA5U);
}

std::vector<Byte> patterned_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t address = 0U; address < size; ++address) {
        data[address] = pattern(address);
    }
    return data;
}

Vt3x make_board(std::size_t prg = 262144U,
                std::size_t chr = 262144U,
                std::size_t ram = 0U,
                std::size_t nvram = 0U,
                bool vertical = true) {
    auto header = vt3x_header();
    header.prg_ram_size = ram;
    header.prg_nvram_size = nvram;
    header.has_battery = nvram != 0U;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    REQUIRE(Vt3x::validate_geometry(header, prg, chr));
    return Vt3x(header, patterned_rom(prg), patterned_rom(chr));
}

std::vector<Byte> mapper_state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void serial_write(Vt3x& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg(bit == 4U ? addr : static_cast<Address>(0x8000U + bit * 0x1111U),
                         (value >> bit) & 1U);
    }
}

void a12_edge(Vt3x& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

Byte expected_chr_descramble(Byte value) {
    Byte result = 0U;
    constexpr std::array<unsigned, 8> source{4U, 2U, 1U, 7U, 0U, 6U, 5U, 3U};
    for (unsigned destination = 0U; destination < 8U; ++destination) {
        result |= static_cast<Byte>(((value >> source[destination]) & 1U) << destination);
    }
    return result;
}

TEST_CASE("VT3x geometry accepts only bounded explicit memory populations", "[mapper296][vt3x]") {
    const auto header = vt3x_header();
    for (std::size_t prg : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 1048576UZ, 8388608UZ}) {
        for (std::size_t chr : {0UZ, 8192UZ, 131072UZ, 8388608UZ}) {
            REQUIRE(Vt3x::validate_geometry(header, prg, chr));
        }
    }
    for (std::size_t prg : {0UZ, 16384UZ, 49152UZ, 16777216UZ}) {
        REQUIRE_FALSE(Vt3x::validate_geometry(header, prg, 0U));
    }
    for (std::size_t chr : {4096UZ, 12288UZ, 16777216UZ}) {
        REQUIRE_FALSE(Vt3x::validate_geometry(header, 32768U, chr));
    }
    for (unsigned variant = 0U; variant < 13U; ++variant) {
        auto invalid = header;
        switch (variant) {
        case 0U:
            invalid.is_nes2 = false;
            break;
        case 1U:
            invalid.mapper_number = 295U;
            break;
        case 2U:
            invalid.submapper = 1U;
            break;
        case 3U:
            invalid.prg_ram_size = 4096U;
            break;
        case 4U:
            invalid.prg_ram_size = 8192U;
            invalid.prg_nvram_size = 8192U;
            invalid.has_battery = true;
            break;
        case 5U:
            invalid.has_battery = true;
            break;
        case 6U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 7U:
            invalid.chr_ram_size = 4096U;
            break;
        case 8U:
            invalid.chr_nvram_size = 8192U;
            break;
        case 9U:
            invalid.mirror_mode = MirrorMode::FourScreen;
            break;
        case 10U:
            invalid.mirror_mode = MirrorMode::SingleLower;
            break;
        case 11U:
            invalid.prg_ram_size = 16384U;
            break;
        default:
            invalid.prg_nvram_size = 16384U;
            invalid.has_battery = true;
            break;
        }
        REQUIRE_FALSE(Vt3x::validate_geometry(invalid, 131072U, 131072U));
    }
    auto without_chr_ram = header;
    without_chr_ram.chr_ram_size = 0U;
    REQUIRE(Vt3x::validate_geometry(without_chr_ram, 32768U, 0U));
    auto persistent = header;
    persistent.prg_nvram_size = 8192U;
    persistent.has_battery = true;
    REQUIRE(Vt3x::validate_geometry(persistent, 32768U, 0U));
}

TEST_CASE("VT3x write-only register apertures preserve CPU open bus", "[mapper296][vt3x]") {
    auto mapper = make_board();
    const auto original = mapper_state(mapper);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const bool native = addr >= 0x4100U && addr <= 0x410BU;
        const bool extended =
            addr == 0x411DU || addr == 0x411EU || addr == 0x412CU || addr == 0x412EU;
        INFO("address=" << addr);
        REQUIRE(mapper.maps_expansion(static_cast<Address>(addr)) == (native || extended));
        if (native || extended) {
            REQUIRE(mapper.expansion_read_mask(static_cast<Address>(addr)) == 0U);
            REQUIRE(mapper.read_expansion(static_cast<Address>(addr)) == 0U);
        }
    }
    REQUIRE_FALSE(mapper.maps_expansion(0x4031U));
    REQUIRE_FALSE(mapper.maps_expansion(0x4033U));
    REQUIRE_FALSE(mapper.maps_expansion(0x4036U));
    REQUIRE_FALSE(mapper.has_expansion_audio());
    REQUIRE_FALSE(mapper.maps_prg(0x6000U));
    REQUIRE(mapper.maps_prg(0x8000U));
    REQUIRE(mapper.battery_ram().empty());
    REQUIRE(mapper_state(mapper) == original);

    for (unsigned addr = 0x2000U; addr < 0x4000U; addr += 8U) {
        const bool native = addr >= 0x2010U && addr <= 0x201FU;
        REQUIRE(mapper.read_ppu_register(static_cast<Address>(addr), 0x9CU) ==
                (native ? std::optional<Byte>(0x9CU) : std::nullopt));
    }
    REQUIRE_FALSE(mapper.read_ppu_register(0x2007U, 0x9CU));
    REQUIRE_FALSE(mapper.read_ppu_register(0x2020U, 0x9CU));
}

TEST_CASE("VT3x native PRG banking composes all masks and outer address pins",
          "[mapper296][vt3x]") {
    constexpr std::size_t size = 2UZ * 1024UZ * 1024UZ;
    auto mapper = make_board(size, 0U);
    constexpr std::array<unsigned, 8> masks{0x3FU, 0x1FU, 0x0FU, 0x07U, 0x03U, 0x01U, 0x00U, 0xFFU};
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned outer : {0U, 1U, 7U, 15U}) {
            for (unsigned extension : {0U, 1U, 4U, 5U, 15U}) {
                for (unsigned common = 0U; common < 2U; ++common) {
                    for (Byte middle : {0U, 0x55U, 0xAAU, 0xFFU}) {
                        for (Byte selected : {0U, 1U, 0x59U, 0xFFU}) {
                            for (bool third : {false, true}) {
                                for (bool swap : {false, true}) {
                                    mapper.write_expansion(0x4100U, outer << 4U);
                                    mapper.write_expansion(0x412CU, extension);
                                    mapper.write_expansion(0x412EU, common);
                                    mapper.write_expansion(0x4105U, swap ? 0x40U : 0U);
                                    mapper.write_expansion(0x4107U, selected);
                                    mapper.write_expansion(0x4108U, 0xA3U);
                                    mapper.write_expansion(0x4109U, 0x36U);
                                    mapper.write_expansion(0x410AU, middle);
                                    mapper.write_expansion(
                                        0x410BU, static_cast<Byte>(mode | (third ? 0x40U : 0U)));
                                    const std::array<unsigned, 4> pages{
                                        swap ? (third ? 0x36U : 0xFEU) : selected,
                                        0xA3U,
                                        swap ? selected : (third ? 0x36U : 0xFEU),
                                        0xFFU};
                                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                                        unsigned bank = (pages[slot] & masks[mode]) |
                                                        (middle & ~masks[mode] & 0xFFU) |
                                                        (outer << 8U) | ((extension & 1U) << 12U) |
                                                        (((extension >> 2U) & 1U) << 13U) |
                                                        (common << 14U);
                                        const auto address =
                                            static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                                        REQUIRE(mapper.read_prg(address) ==
                                                pattern((static_cast<std::size_t>(bank) * 8192U +
                                                         0x123U) &
                                                        (size - 1U)));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("VT3x native CHR banking composes pairing inversion and every outer pin",
          "[mapper296][vt3x]") {
    constexpr std::size_t size = 2UZ * 1024UZ * 1024UZ;
    auto mapper = make_board(32768U, size);
    constexpr std::array<unsigned, 8> masks{0xFFU, 0x7FU, 0x3FU, 0x00U, 0x1FU, 0x0FU, 0x07U, 0x00U};
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (Byte value : {0U, 0x55U, 0xFFU}) {
            for (unsigned intermediate = 0U; intermediate < 8U; ++intermediate) {
                for (unsigned outer : {0U, 1U, 15U}) {
                    for (unsigned extension : {0U, 2U, 8U, 10U}) {
                        for (unsigned common = 0U; common < 2U; ++common) {
                            for (bool invert : {false, true}) {
                                mapper.write_expansion(0x4100U, outer);
                                mapper.write_expansion(0x412CU, extension);
                                mapper.write_expansion(0x412EU, common);
                                mapper.write_expansion(0x4105U, invert ? 0x80U : 0U);
                                mapper.write_ppu_register(0x2018U, intermediate << 4U);
                                mapper.write_ppu_register(
                                    0x201AU, static_cast<Byte>((value & 0xF8U) | mode));
                                std::array<Byte, 6> banks{};
                                for (unsigned index = 0U; index < banks.size(); ++index) {
                                    banks[index] = static_cast<Byte>(value + index * 17U);
                                    mapper.write_ppu_register(static_cast<Address>(0x2012U + index),
                                                              banks[index]);
                                }
                                const std::array<unsigned, 8> pages{banks[0] & 0xFEU,
                                                                    banks[0] | 1U,
                                                                    banks[1] & 0xFEU,
                                                                    banks[1] | 1U,
                                                                    banks[2],
                                                                    banks[3],
                                                                    banks[4],
                                                                    banks[5]};
                                for (unsigned slot = 0U; slot < 8U; ++slot) {
                                    const auto page = pages[slot ^ (invert ? 4U : 0U)];
                                    const unsigned bank =
                                        (page & masks[mode]) | (value & 0xF8U & ~masks[mode]) |
                                        (intermediate << 8U) | (outer << 11U) |
                                        (((extension >> 1U) & 1U) << 15U) |
                                        (((extension >> 3U) & 1U) << 16U) | (common << 17U);
                                    const auto address =
                                        static_cast<Address>(slot * 1024U + 0x123U);
                                    REQUIRE(
                                        mapper.read_chr(address) ==
                                        pattern((static_cast<std::size_t>(bank) * 1024U + 0x123U) &
                                                (size - 1U)));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("VT3x shared OneBus storage and CHR data descrambling are read-only",
          "[mapper296][vt3x]") {
    auto shared = make_board(131072U, 0U);
    for (unsigned bank = 0U; bank < 128U; ++bank) {
        shared.write_ppu_register(0x2014U, static_cast<Byte>(bank));
        const auto physical = (static_cast<std::size_t>(bank) * 1024U + 0x321U) & 131071U;
        REQUIRE(shared.read_chr(0x1321U) == pattern(physical));
        shared.write_chr(0x1321U, 0xFFU);
        REQUIRE(shared.read_chr(0x1321U) == pattern(physical));
    }

    auto header = vt3x_header();
    std::vector<Byte> chr(8192U);
    for (unsigned value = 0U; value < 256U; ++value) {
        chr[value] = static_cast<Byte>(value);
    }
    Vt3x mapper(header, patterned_rom(32768U), std::move(chr));
    mapper.write_expansion(0x411EU, 0xC0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        REQUIRE(mapper.read_chr(static_cast<Address>(value)) ==
                expected_chr_descramble(static_cast<Byte>(value)));
        mapper.write_chr(static_cast<Address>(value), 0U);
        REQUIRE(mapper.read_chr(static_cast<Address>(value)) ==
                expected_chr_descramble(static_cast<Byte>(value)));
    }
    mapper.write_expansion(0x411EU, 0x40U);
    REQUIRE(mapper_state(mapper)[19U + 19U] == 1U);
    mapper.write_expansion(0x411EU, 0x80U);
    REQUIRE(mapper_state(mapper)[19U + 19U] == 1U);
    mapper.write_expansion(0x411EU, 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        REQUIRE(mapper.read_chr(static_cast<Address>(value)) == value);
    }
}

TEST_CASE("VT3x UNROM mode switches 16 KiB pages and selects unbanked CHR RAM",
          "[mapper296][vt3x]") {
    auto mapper = make_board(262144U, 0U);
    mapper.write_expansion(0x411DU, 6U);
    for (unsigned value = 0U; value < 256U; ++value) {
        INFO("UNROM value=" << value);
        mapper.write_prg(static_cast<Address>(0x8000U + (value * 251U) % 0x8000U), value);
        const auto lower = static_cast<std::size_t>((value * 2U) & 0x3FU) * 8192U;
        REQUIRE(mapper.read_prg(0x8123U) == pattern((lower + 0x123U) & 262143U));
        REQUIRE(mapper.read_prg(0xA123U) == pattern((lower + 8192U + 0x123U) & 262143U));
        REQUIRE(mapper.read_prg(0xC123U) == pattern((0xFEU * 8192U + 0x123U) & 262143U));
        REQUIRE(mapper.read_prg(0xE123U) == pattern((0xFFU * 8192U + 0x123U) & 262143U));
        REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U));
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 7U));
        REQUIRE_FALSE(mapper.ciram_address(static_cast<Address>(addr)));
    }
}

TEST_CASE("VT3x CNROM mode fixes PRG and switches full 8 KiB CHR pages", "[mapper296][vt3x]") {
    auto mapper = make_board(262144U, 262144U);
    mapper.write_expansion(0x411DU, 3U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper.write_prg(static_cast<Address>(0x8000U + (value * 251U) % 0x8000U), value);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + slot * 8192U + 0x123U)) ==
                    pattern(slot * 8192U + 0x123U));
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto physical =
                (static_cast<std::size_t>((value * 8U + slot) & 0xFFU) * 1024U + 0x123U) & 262143U;
            REQUIRE(mapper.read_chr(static_cast<Address>(slot * 1024U + 0x123U)) ==
                    pattern(physical));
        }
    }
}

TEST_CASE("VT3x MMC1 mode implements serial commits PRG CHR and all nametable modes",
          "[mapper296][vt3x]") {
    auto mapper = make_board(262144U, 262144U);
    mapper.write_expansion(0x411DU, 1U);
    for (unsigned control = 0U; control < 32U; ++control) {
        serial_write(mapper, 0x8000U, control);
        const MirrorMode expected_mirror = control % 4U == 0U   ? MirrorMode::SingleLower
                                           : control % 4U == 1U ? MirrorMode::SingleUpper
                                           : control % 4U == 2U ? MirrorMode::Vertical
                                                                : MirrorMode::Horizontal;
        REQUIRE(mapper.mirror_mode() == expected_mirror);
        for (unsigned selected = 0U; selected < 16U; ++selected) {
            serial_write(mapper, 0xE000U, selected);
            std::array<unsigned, 4> pages{};
            switch ((control >> 2U) & 3U) {
            case 0U:
            case 1U:
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    pages[slot] = (selected & 0x0EU) * 2U + slot;
                }
                break;
            case 2U:
                pages = {0U, 1U, selected * 2U, selected * 2U + 1U};
                break;
            default:
                pages = {selected * 2U, selected * 2U + 1U, 0xFEU, 0xFFU};
                break;
            }
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto physical =
                    (static_cast<std::size_t>(pages[slot] & 0x3FU) * 8192U + 0x123U) & 262143U;
                REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + slot * 8192U + 0x123U)) ==
                        pattern(physical));
            }
        }
    }
    for (bool split : {false, true}) {
        serial_write(mapper, 0x8000U, split ? 0x1CU : 0x0CU);
        for (Byte chr0 : {0U, 1U, 2U, 0x1FU}) {
            for (Byte chr1 : {0U, 3U, 0x1CU}) {
                serial_write(mapper, 0xA000U, chr0);
                serial_write(mapper, 0xC000U, chr1);
                const unsigned lower = (split ? chr0 : chr0 & 0x1EU) * 4U;
                const unsigned upper = split ? chr1 * 4U : lower + 4U;
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const unsigned bank = (slot < 4U ? lower : upper) + (slot & 3U);
                    REQUIRE(mapper.read_chr(static_cast<Address>(slot * 1024U + 0x123U)) ==
                            pattern((static_cast<std::size_t>(bank) * 1024U + 0x123U) & 262143U));
                }
            }
        }
    }

    serial_write(mapper, 0xE000U, 0U);
    mapper.write_prg_cpu(0xE000U, 1U, false);
    mapper.write_prg_cpu(0xE000U, 0U, true);
    mapper.write_prg(0xE000U, 0U);
    mapper.write_prg(0xE000U, 0U);
    mapper.write_prg(0xE000U, 0U);
    REQUIRE(mapper.read_prg(0x8123U) == pattern(0x123U));
    mapper.write_prg(0xE000U, 0U);
    REQUIRE(mapper.read_prg(0x8123U) == pattern(2U * 8192U + 0x123U));
    serial_write(mapper, 0x8000U, 0U);
    mapper.write_prg_cpu(0x8000U, 0x80U, true);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper.read_prg(0xC123U) == pattern((0xFEU * 8192U + 0x123U) & 262143U));
}

TEST_CASE("VT3x MMC3 forwarding aliases FWEN native mirroring and CIRAM selection",
          "[mapper296][vt3x]") {
    auto mapper = make_board(262144U, 262144U, 0U, 0U, false);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
    mapper.write_prg(0xBFFEU, 0U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.write_prg(0xA000U, 1U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
    mapper.write_expansion(0x4105U, 0x20U);
    REQUIRE(mapper.ciram_address(0U) == 0U);
    REQUIRE(mapper.ciram_address(0x1FFFU) == 0x7FFU);
    REQUIRE_FALSE(mapper.ciram_address(0x2000U));
    mapper.write_prg(0x8000U, 0U);
    REQUIRE(mapper.ciram_address(0x100U) == 0x100U);
    mapper.write_expansion(0x411DU, 4U);
    REQUIRE_FALSE(mapper.ciram_address(0x100U));

    mapper.reset();
    mapper.write_expansion(0x410BU, 8U);
    const auto disabled = mapper_state(mapper);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper.write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    }
    REQUIRE(mapper_state(mapper) == disabled);
    mapper.write_expansion(0x4107U, 7U);
    REQUIRE(mapper_state(mapper) != disabled);
}

TEST_CASE("VT3x IRQ sources preserve latches and qualified A12 phase", "[mapper296][vt3x]") {
    auto mapper = make_board();
    for (unsigned latch : {0U, 1U, 2U, 255U}) {
        mapper.reset();
        mapper.write_expansion(0x4101U, latch);
        mapper.write_expansion(0x4102U, 0U);
        mapper.write_expansion(0x4104U, 0U);
        a12_edge(mapper);
        REQUIRE(mapper.irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            a12_edge(mapper);
            REQUIRE(mapper.irq_pending() == (count + 1U == latch));
        }
        mapper.write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(mapper.irq_pending());
    }
    for (unsigned low = 0U; low <= 4U; ++low) {
        mapper.reset();
        mapper.write_expansion(0x4104U, 0U);
        a12_edge(mapper, low);
        REQUIRE(mapper.irq_pending() == (low >= 3U));
    }
    mapper.reset();
    mapper.write_expansion(0x4101U, 2U);
    mapper.write_expansion(0x4104U, 0U);
    a12_edge(mapper);
    mapper.observe_ppu_a12(false);
    mapper.clock_cpu();
    const auto partial = mapper_state(mapper);
    mapper.reset();
    StateReader reader(partial);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    mapper.clock_cpu();
    mapper.clock_cpu();
    mapper.observe_ppu_a12(true);
    REQUIRE_FALSE(mapper.irq_pending());
    a12_edge(mapper);
    REQUIRE(mapper.irq_pending());

    mapper.write_expansion(0x4103U, 0U);
    mapper.write_expansion(0x410BU, 0x80U);
    mapper.write_expansion(0x4101U, 0U);
    mapper.write_expansion(0x4104U, 0U);
    a12_edge(mapper);
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.on_ppu_hblank();
    REQUIRE(mapper.irq_pending());
    mapper.reset();
    REQUIRE_FALSE(mapper.irq_pending());
}

TEST_CASE("VT3x reset and power distinguish volatile and persistent RAM", "[mapper296][vt3x]") {
    for (bool persistent : {false, true}) {
        auto mapper =
            make_board(131072U, 131072U, persistent ? 0U : 8192U, persistent ? 8192U : 0U);
        REQUIRE(mapper.maps_prg(0x6000U));
        REQUIRE(mapper.battery_ram().size() == (persistent ? 8192U : 0U));
        if (persistent) {
            std::array<Byte, 8192> imported{};
            for (unsigned index = 0U; index < imported.size(); ++index) {
                imported[index] = static_cast<Byte>(index ^ 0x5AU);
            }
            mapper.set_battery_ram(imported);
            REQUIRE(mapper.read_prg(0x6123U) == imported[0x123U]);
            const auto before = mapper_state(mapper);
            mapper.set_battery_ram(std::span<const Byte>(imported).first(8191U));
            REQUIRE(mapper_state(mapper) == before);
        }
        mapper.write_expansion(0x411DU, 6U);
        mapper.write_chr(0x123U, 0xA7U);
        mapper.write_prg(0x6123U, 0x5CU);
        mapper.write_expansion(0x4107U, 9U);
        mapper.reset();
        mapper.write_expansion(0x411DU, 6U);
        REQUIRE(mapper.read_chr(0x123U) == 0xA7U);
        REQUIRE(mapper.read_prg(0x6123U) == 0x5CU);
        mapper.power_on();
        mapper.write_expansion(0x411DU, 6U);
        REQUIRE(mapper.read_chr(0x123U) == 0U);
        REQUIRE(mapper.read_prg(0x6123U) == (persistent ? 0x5CU : 0U));
        REQUIRE(mapper_state(mapper).size() == 19U + 34U + 8U + 8192U + 8192U);
    }
}

TEST_CASE("VT3x state is bounded atomic and excludes immutable ROM", "[mapper296][vt3x]") {
    auto mapper = make_board(131072U, 131072U, 0U, 8192U);
    mapper.write_expansion(0x4100U, 0xA7U);
    mapper.write_expansion(0x4105U, 0xE5U);
    mapper.write_expansion(0x4107U, 0x95U);
    mapper.write_ppu_register(0x2012U, 0xFFU);
    mapper.write_expansion(0x411DU, 1U);
    serial_write(mapper, 0xE000U, 3U);
    mapper.write_expansion(0x4101U, 1U);
    mapper.write_expansion(0x4104U, 0U);
    a12_edge(mapper);
    a12_edge(mapper);
    mapper.write_prg(0x6123U, 0x6DU);
    mapper.write_expansion(0x411DU, 4U);
    mapper.write_chr(0x321U, 0x9CU);
    const auto saved = mapper_state(mapper);
    REQUIRE(saved.size() == 19U + 34U + 8U + 8192U + 8192U);
    REQUIRE(saved.size() < 17000U);

    mapper.reset();
    StateReader valid(saved);
    mapper.load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(mapper_state(mapper) == saved);
    REQUIRE(mapper.irq_pending());
    REQUIRE(mapper.read_prg(0x6123U) == 0x6DU);
    REQUIRE(mapper.read_chr(0x321U) == 0x9CU);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(mapper) == saved);
    }
    constexpr std::array<std::pair<unsigned, Byte>, 18> invalid_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 1U},
        {14U, 1U},
        {18U, 2U},
        {19U + 1U, 8U},
        {19U + 2U, 2U},
        {19U + 7U, 0x10U},
        {19U + 14U, 0x80U},
        {19U + 16U, 8U},
        {19U + 17U, 0x10U},
        {19U + 18U, 2U},
        {19U + 19U, 2U},
        {19U + 22U, 2U},
        {19U + 25U, 4U},
        {19U + 27U, 5U},
        {19U + 28U, 0x20U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(mapper) == saved);
    }
    auto bad_length = saved;
    const auto length_offset = bad_length.size() - 8192U - 8192U - 8U;
    std::fill_n(bad_length.begin() + static_cast<std::ptrdiff_t>(length_offset), 8U, 0xFFU);
    StateReader length_reader(bad_length);
    mapper.load_state(length_reader);
    REQUIRE_FALSE(length_reader.ok());
    REQUIRE(mapper_state(mapper) == saved);

    auto other_geometry = make_board(131072U, 0U, 0U, 8192U);
    const auto other_before = mapper_state(other_geometry);
    StateReader wrong(saved);
    other_geometry.load_state(wrong);
    REQUIRE_FALSE(wrong.ok());
    REQUIRE(mapper_state(other_geometry) == other_before);

    auto different_header = vt3x_header();
    different_header.prg_nvram_size = 8192U;
    different_header.has_battery = true;
    Vt3x different_rom(
        different_header, std::vector<Byte>(131072U, 0x11U), std::vector<Byte>(131072U, 0x22U));
    StateReader same_geometry(saved);
    different_rom.load_state(same_geometry);
    REQUIRE(same_geometry.ok());
    REQUIRE(mapper_state(different_rom) == saved);
}

} // namespace
