#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper012.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader make_header(Byte submapper = 0U, bool legacy = false, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 12U;
    header.submapper = submapper;
    header.is_nes2 = !legacy;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    if (!legacy) {
        header.prg_ram_size = submapper == 1U ? 32768U : 0U;
        header.chr_ram_size = submapper == 1U ? 32768U : 0U;
    }
    return header;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) + 0x27U);
}
Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 7U) + 0x53U);
}
Byte ram_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 7U + (offset >> 5U) + 0xA1U);
}
std::vector<Byte> image(std::size_t size, bool chr = false) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = chr ? chr_cell(i) : prg_cell(i);
    return data;
}

std::unique_ptr<Mapper012> sl_board(bool legacy = false,
                                    bool vertical = true,
                                    std::size_t prg_size = 524288U,
                                    std::size_t chr_size = 524288U) {
    const auto header = make_header(0U, legacy, vertical);
    REQUIRE(Mapper012::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper012>(header, image(prg_size), image(chr_size, true));
}

std::unique_ptr<Mapper012> ffe_board(std::size_t prg_size = 262144U,
                                     std::size_t chr_size = 262144U,
                                     bool vertical = true,
                                     bool legacy = false) {
    const auto header = make_header(1U, legacy, vertical);
    REQUIRE(Mapper012::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper012>(header, image(prg_size), image(chr_size, true));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void sl_bank(Mapper& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, reg | mode);
    mapper.write_prg(0x8001U, value);
}

Byte ffe_cell(std::size_t offset, std::size_t prg_size, std::size_t chr_size) {
    if (offset < prg_size)
        return prg_cell(offset);
    if (offset >= 0x40000U && offset - 0x40000U < chr_size)
        return chr_cell(offset - 0x40000U);
    return 0U;
}

void expect_ffe_prg(Mapper& mapper,
                    Address addr,
                    std::size_t offset,
                    std::size_t prg_size = 262144U,
                    std::size_t chr_size = 262144U) {
    INFO("cpu=" << addr << " dram=" << offset);
    REQUIRE(mapper.read_prg(addr) == ffe_cell(offset, prg_size, chr_size));
}

void fill_ffe_chr(Mapper& mapper) {
    mapper.write_expansion(0x43FFU, 0U);
    mapper.write_expansion(0x42FFU, 0x30U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, static_cast<Byte>(bank));
        for (std::size_t offset = 0U; offset < 8192U; ++offset)
            mapper.write_chr(static_cast<Address>(offset), ram_cell(bank * 8192U + offset));
    }
}

TEST_CASE("Mapper 012 validates both image geometries and rejects missing trainer routing",
          "[mapper012]") {
    for (bool legacy : {false, true}) {
        for (std::size_t prg : {32768UZ, 49152UZ, 131072UZ, 524288UZ})
            for (std::size_t chr : {8192UZ, 12288UZ, 262144UZ, 524288UZ})
                REQUIRE(Mapper012::validate_geometry(make_header(0U, legacy), prg, chr));
        for (auto [prg, chr] : std::array<std::pair<std::size_t, std::size_t>, 3>{
                 {{131072U, 262144U}, {262144U, 131072U}, {262144U, 262144U}}}) {
            if (!legacy)
                REQUIRE(Mapper012::validate_geometry(make_header(1U), prg, chr));
            else
                REQUIRE(Mapper012::validate_geometry(make_header(0U, true), prg, chr));
        }
    }

    for (Byte sub = 2U; sub < 16U; ++sub)
        REQUIRE_FALSE(Mapper012::validate_geometry(make_header(sub), 524288U, 524288U));
    for (std::size_t size : {0UZ, 8192UZ, 16384UZ, 32767UZ, 32769UZ, 524289UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper012::validate_geometry(make_header(), size, 524288U));
    for (std::size_t size : {0UZ, 1024UZ, 4096UZ, 8191UZ, 8193UZ, 524289UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper012::validate_geometry(make_header(), 524288U, size));
    for (auto [prg, chr] : std::array<std::pair<std::size_t, std::size_t>, 6>{{
             {0U, 0U},
             {131072U, 131072U},
             {131072U, 0U},
             {262144U, 0U},
             {524288U, 262144U},
             {262144U, 524288U},
         }})
        REQUIRE_FALSE(Mapper012::validate_geometry(make_header(1U), prg, chr));

    for (unsigned which = 0U; which < 12U; ++which) {
        auto header = make_header();
        switch (which) {
        case 0U:
            header.mapper_number = 11U;
            break;
        case 1U:
            header.has_battery = true;
            break;
        case 2U:
            header.prg_ram_size = 8192U;
            break;
        case 3U:
            header.prg_nvram_size = 8192U;
            break;
        case 4U:
            header.chr_ram_size = 8192U;
            break;
        case 5U:
            header.chr_nvram_size = 8192U;
            break;
        case 6U:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            header.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 8U:
            header.has_trainer = true;
            break;
        case 9U:
            header = make_header(1U);
            header.prg_ram_size = 0U;
            break;
        case 10U:
            header = make_header(1U);
            header.chr_ram_size = 8192U;
            break;
        default:
            header = make_header(1U);
            header.has_trainer = true;
            break;
        }
        INFO(which);
        REQUIRE_FALSE(Mapper012::validate_geometry(header, 262144U, 262144U));
    }
}

TEST_CASE("SL-5020B implements six-bit PRG banking and every MMC3 register alias", "[mapper012]") {
    auto mapper = sl_board();
    for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x9FFEU, 6U | mode);
            mapper->write_prg(0x9FFFU, static_cast<Byte>(value));
            mapper->write_prg(0x8000U, 7U | mode);
            mapper->write_prg(0x8001U, static_cast<Byte>(value ^ 0xA7U));
            const std::array<unsigned, 4> banks = {
                (mode & 0x40U) != 0U ? 62U : value & 0x3FU,
                (value ^ 0xA7U) & 0x3FU,
                (mode & 0x40U) != 0U ? value & 0x3FU : 62U,
                63U,
            };
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                const auto offset = banks[slot] * 8192U + 0x123U;
                INFO("mode=" << unsigned(mode) << " value=" << value << " slot=" << slot);
                REQUIRE(mapper->read_prg(addr) == prg_cell(offset));
            }
        }
    }
    REQUIRE(mapper->read_expansion(0x4132U) == 0U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE(mapper->read_expansion(0x4132U) == 1U);
    REQUIRE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    auto ffe = ffe_board();
    REQUIRE_FALSE(ffe->configure_board_jumpers(0U));
}

TEST_CASE("SL-5020B CHR pairing inversion and outer A18 are independent and immediate",
          "[mapper012]") {
    auto mapper = sl_board();
    for (Byte inversion : {0U, 0x80U}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            for (unsigned value = 0U; value < 256U; ++value) {
                sl_bank(*mapper, reg, static_cast<Byte>(value), inversion);
                const unsigned start = reg < 2U ? reg * 2U + (inversion == 0U ? 0U : 4U)
                                                : reg - 2U + (inversion == 0U ? 4U : 0U);
                for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                    const auto raw = reg < 2U ? (value & 0xFEU) | half : value;
                    const auto addr = static_cast<Address>((start + half) * 1024U + 0x155U);
                    REQUIRE(mapper->read_chr(addr) == chr_cell(raw * 1024U + 0x155U));
                }
            }
        }
    }

    for (Byte inversion : {0U, 0x80U}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            sl_bank(*mapper, reg, static_cast<Byte>(reg * 31U + 7U), inversion);
        for (Address alias : {0x4100U, 0x4132U, 0x41FFU, 0x4900U, 0x5132U, 0x59FFU}) {
            for (Byte outer : {0U, 1U, 0x10U, 0x11U, 0xFFU}) {
                mapper->write_expansion(alias, outer);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 1024U + 0x77U);
                    const auto logical = static_cast<Address>(slot * 1024U + 0x77U);
                    const unsigned slot_in_map = slot ^ (inversion != 0U ? 4U : 0U);
                    unsigned raw = 0U;
                    if (slot_in_map < 2U)
                        raw = (7U & 0xFEU) | slot_in_map;
                    else if (slot_in_map < 4U)
                        raw = (38U & 0xFEU) | (slot_in_map - 2U);
                    else
                        raw = (slot_in_map - 2U) * 31U + 7U;
                    const unsigned outer_bit = (outer >> ((addr & 0x1000U) != 0U ? 4U : 0U)) & 1U;
                    const auto offset = outer_bit * 262144U + raw * 1024U + 0x77U;
                    REQUIRE(mapper->read_chr(logical) == chr_cell(offset));
                }
            }
        }
    }

    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const bool decoded = (addr & 0xE100U) == 0x4100U;
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)) == decoded);
        if (decoded)
            REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == 1U);
    }
    REQUIRE_FALSE(mapper->maps_expansion(0x6132U));
    mapper->reset();
    mapper->write_chr(0U, 0U);
    REQUIRE(mapper->read_chr(0U) == chr_cell(0U));
}

TEST_CASE("SL-5020B CIRAM ROM-only RAM gates and language read mask use the real CPU bus",
          "[mapper012]") {
    auto mapper = sl_board(false, false);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xBFFEU, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr : {0x6000U, 0x6FFFU, 0x7FFFU})
        REQUIRE_FALSE(mapper->maps_prg(addr));
    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());

    const auto factory = [](const INesHeader& header,
                            std::vector<Byte> prg,
                            std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
        return std::make_unique<Mapper012>(header, std::move(prg), std::move(chr));
    };
    MapperRegistry::instance().register_mapper(12U, factory);
    auto prg = image(32768U);
    auto chr = image(8192U, true);
    std::vector<Byte> bytes(16U, 0U);
    bytes[0U] = 'N';
    bytes[1U] = 'E';
    bytes[2U] = 'S';
    bytes[3U] = 0x1AU;
    bytes[4U] = 2U;
    bytes[5U] = 1U;
    bytes[6U] = 0xC1U;
    bytes.insert(bytes.end(), prg.begin(), prg.end());
    bytes.insert(bytes.end(), chr.begin(), chr.end());
    auto cartridge = Cartridge::from_data(bytes);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    for (unsigned value = 0U; value < 256U; ++value) {
        bus.write(0x10U, static_cast<Byte>(value));
        REQUIRE(bus.read(0x4132U) == (value & 0xFEU));
        bus.write(0x10U, static_cast<Byte>(value));
        REQUIRE(bus.read(0x5132U) == (value & 0xFEU));
        bus.write(0x10U, static_cast<Byte>(value));
        REQUIRE(bus.read(0x4232U) == value);
    }
    REQUIRE(cartridge->configure_board_jumpers(1U));
    bus.write(0x10U, 0xA6U);
    REQUIRE(bus.read(0x4132U) == 0xA7U);
}

TEST_CASE("SL-5020B MMC3A IRQ has single zero and documented forced-reload behavior",
          "[mapper012]") {
    auto mapper = sl_board();
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->reset();
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("SL-5020B state replay is compact atomic and reset-safe", "[mapper012][state]") {
    auto mapper = sl_board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    sl_bank(*mapper, 0U, 0xF1U, 0xC0U);
    sl_bank(*mapper, 6U, 0xFFU, 0xC0U);
    sl_bank(*mapper, 7U, 0x35U, 0xC0U);
    mapper->write_expansion(0x4132U, 0x11U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 33U);

    auto copy = sl_board();
    copy->write_prg(0xA000U, 0U);
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_expansion(0x4132U) == 1U);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length)
        reject(std::span(saved).first(length));
    for (auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 14>{{
             {0U, 1U},
             {2U, 0xFFU},
             {6U, 0xFFU},
             {10U, 0U},
             {11U, 2U},
             {12U, 8U},
             {13U, 2U},
             {14U, 2U},
             {21U, 0x40U},
             {23U, 2U},
             {24U, 4U},
             {27U, 2U},
             {31U, 2U},
             {32U, 2U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        reject(bad);
    }
    auto wrong = sl_board(false, true, 131072U, 262144U);
    const auto before = state(*wrong);
    StateReader wrong_reader(saved);
    wrong->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong) == before);

    mapper->power_on();
    REQUIRE(state(*mapper) != saved);
    REQUIRE(mapper->read_expansion(0x4132U) == 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->reset();
    REQUIRE(state(*mapper).size() == 33U);
}

TEST_CASE("FFE 4M initializes each documented DRAM layout and final PRG banks", "[mapper012]") {
    for (auto [prg_size, chr_size] : std::array<std::pair<std::size_t, std::size_t>, 3>{
             {{131072U, 262144U}, {262144U, 131072U}, {262144U, 262144U}}}) {
        for (bool vertical : {false, true}) {
            auto mapper = ffe_board(prg_size, chr_size, vertical);
            INFO(prg_size << " " << chr_size << " " << vertical);
            REQUIRE(mapper->read_expansion(0x5500U) == 0x42U);
            REQUIRE(mapper->read_expansion(0x4500U) == (vertical ? 0x63U : 0x73U));
            REQUIRE(mapper->read_expansion(0x4501U) == 0U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            const auto first = prg_size / 8192U - 4U;
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8000U + slot * 8192U + 0x91U);
                expect_ffe_prg(*mapper, addr, (first + slot) * 8192U + 0x91U, prg_size, chr_size);
                REQUIRE(mapper->read_expansion(0x5504U + slot) == first + slot);
            }
            mapper->write_expansion(0x4504U, 32U);
            expect_ffe_prg(*mapper, 0x8123U, 0x40123U, prg_size, chr_size);
            mapper->write_expansion(0x4504U, 24U);
            if (prg_size == 131072U)
                REQUIRE(mapper->read_prg(0x8123U) == 0U);
            else
                expect_ffe_prg(*mapper, 0x8123U, 0x30123U, prg_size, chr_size);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_chr(0U) == 0U);
        }
    }
}

TEST_CASE("FFE 4M latch modes decode all values and retain CHR protection", "[mapper012]") {
    auto mapper = ffe_board();
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_expansion(0x43FFU, 0U);
            mapper->write_expansion(0x42FFU, static_cast<Byte>((mode << 5U) | 0x10U));
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            const auto latch = value & 0x3FU;
            switch (mode) {
            case 0U:
                expect_ffe_prg(*mapper, 0x8123U, (latch & 7U) * 16384U + 0x123U);
                expect_ffe_prg(*mapper, 0xC123U, 7U * 16384U + 0x123U);
                break;
            case 1U:
                expect_ffe_prg(*mapper, 0x8123U, ((latch >> 2U) & 15U) * 16384U + 0x123U);
                expect_ffe_prg(*mapper, 0xC123U, 7U * 16384U + 0x123U);
                break;
            case 2U:
                expect_ffe_prg(*mapper, 0x8123U, (latch & 15U) * 16384U + 0x123U);
                expect_ffe_prg(*mapper, 0xC123U, 15U * 16384U + 0x123U);
                break;
            case 3U:
                expect_ffe_prg(*mapper, 0x8123U, 15U * 16384U + 0x123U);
                expect_ffe_prg(*mapper, 0xC123U, (latch & 15U) * 16384U + 0x123U);
                break;
            case 4U:
                expect_ffe_prg(*mapper, 0x8123U, ((latch >> 4U) & 3U) * 32768U + 0x123U);
                expect_ffe_prg(*mapper, 0xE123U, ((latch >> 4U) & 3U) * 32768U + 0x6123U);
                break;
            default:
                expect_ffe_prg(*mapper, 0x8123U, 3U * 32768U + 0x123U);
                expect_ffe_prg(*mapper, 0xE123U, 3U * 32768U + 0x6123U);
                break;
            }
        }
    }

    fill_ffe_chr(*mapper);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        mapper->write_expansion(0x43FFU, 0U);
        mapper->write_expansion(0x42FFU, static_cast<Byte>((mode << 5U) | 0x10U));
        mapper->write_prg(0xC000U, 3U);
        const unsigned bank = mode == 0U || mode == 2U || mode == 7U ? 0U : mode == 6U ? 1U : 3U;
        REQUIRE(mapper->read_chr(0x123U) == ram_cell(bank * 8192U + 0x123U));
        mapper->write_chr(0x123U, 0x5AU);
        REQUIRE(mapper->read_chr(0x123U) == (mode < 4U ? 0x5AU : ram_cell(bank * 8192U + 0x123U)));
        if (mode < 4U)
            mapper->write_chr(0x123U, ram_cell(bank * 8192U + 0x123U));
    }
}

TEST_CASE("FFE 4M 2M and 4M banking registers overlap without losing independent state",
          "[mapper012]") {
    auto mapper = ffe_board();
    for (unsigned slot = 0U; slot < 4U; ++slot)
        mapper->write_expansion(0x4504U + slot, slot * 11U + 3U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        expect_ffe_prg(*mapper, 0x8000U + slot * 8192U + 0x45U, (slot * 11U + 3U) * 8192U + 0x45U);

    for (unsigned value = 0U; value < 256U; ++value) {
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(0x8000U + slot * 8192U, static_cast<Byte>(value));
            expect_ffe_prg(
                *mapper, 0x8000U + slot * 8192U + 0x45U, (slot * 11U + 3U) * 8192U + 0x45U);
        }
    }
    mapper->write_expansion(0x43FEU, 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(0x8000U + slot * 8192U, static_cast<Byte>(value));
            expect_ffe_prg(
                *mapper, 0x8000U + slot * 8192U + 0x45U, (value & 0x3FU) * 8192U + 0x45U);
        }
    }
    mapper->write_expansion(0x43FCU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        expect_ffe_prg(*mapper, 0x8000U + slot * 8192U + 0x45U, (slot * 11U + 3U) * 8192U + 0x45U);

    mapper->write_expansion(0x43FEU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_expansion(0x4504U + slot, 63U - slot);
        expect_ffe_prg(*mapper, 0x8000U + slot * 8192U + 0x45U, (63U - slot) * 8192U + 0x45U);
    }
    mapper->write_expansion(0x43FCU, 2U);
    fill_ffe_chr(*mapper);
    mapper->write_expansion(0x43FCU, 2U);
    REQUIRE(mapper->read_chr(0x123U) == ram_cell(2U * 8192U + 0x123U));

    mapper->write_expansion(0x43FFU, 0U);
    mapper->write_expansion(0x42FCU, 0x20U);
    mapper->write_prg(0x8000U, 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_expansion(0x4501U) == 0x03U);
    mapper->write_expansion(0x42FFU, 0x20U);
    mapper->write_prg(0x8000U, 0x5AU);
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_expansion(0x4501U) == 0x6BU);
}

TEST_CASE("FFE 4M 1 KiB CHR nametables and MMC4 latches wrap the physical 32 KiB RAM",
          "[mapper012]") {
    auto mapper = ffe_board();
    fill_ffe_chr(*mapper);
    mapper->write_expansion(0x4500U, 0x47U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto bank = static_cast<Byte>(slot * 37U + 0xE3U);
        mapper->write_expansion(0x4510U + slot, bank);
        for (unsigned offset : {0U, 1U, 0x123U, 1023U}) {
            const auto addr = static_cast<Address>(slot * 1024U + offset);
            REQUIRE(mapper->read_chr(addr) == ram_cell((bank & 31U) * 1024U + offset));
        }
    }

    mapper->write_expansion(0x4500U, 0x41U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const auto bank = static_cast<Byte>(0xF1U + slot * 13U);
        mapper->write_expansion(0x4518U + slot, bank);
        const auto addr = static_cast<Address>(0x2000U + slot * 1024U + 0x222U);
        REQUIRE(mapper->read_nametable(addr) == ram_cell((bank & 31U) * 1024U + 0x222U));
    }
    mapper->write_expansion(0x42FFU, 0x10U);
    REQUIRE(mapper->write_nametable(0x2000U, 0xC7U));
    REQUIRE(mapper->read_nametable(0x2000U) == 0xC7U);
    mapper->write_expansion(0x42FFU, 0x90U);
    REQUIRE(mapper->write_nametable(0x2000U, 0U));
    REQUIRE(mapper->read_nametable(0x2000U) == 0xC7U);
    mapper->write_expansion(0x4500U, 0x43U);
    REQUIRE_FALSE(mapper->read_nametable(0x2000U));
    REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0U));

    mapper->write_expansion(0x42FFU, 0x30U);
    mapper->write_expansion(0x4500U, 0x43U);
    for (unsigned reg = 0U; reg < 8U; ++reg)
        mapper->write_expansion(0x4510U + reg, reg * 4U + 3U);
    const auto expect_half = [&](unsigned half, unsigned latch) {
        for (unsigned pair = 0U; pair < 2U; ++pair) {
            const auto reg = half * 4U + pair * 2U + latch;
            const auto bank = static_cast<Byte>(reg * 4U + 3U) & 0xFCU;
            const auto addr = static_cast<Address>(half * 4096U + pair * 2048U + 0x123U);
            REQUIRE(mapper->read_chr(addr) == ram_cell(bank * 1024U + pair * 2048U + 0x123U));
        }
    };
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_read(0x1FEFU);
    expect_half(0U, 1U);
    expect_half(1U, 1U);
    mapper->observe_ppu_read(0x0FD8U);
    mapper->observe_ppu_read(0x1FD9U);
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    mapper->observe_ppu_read(0x0FD7U);
    mapper->observe_ppu_read(0x1FF0U);
    expect_half(0U, 0U);
}

TEST_CASE("FFE 4M mode aliases CIRAM WRAM banks GUI windows and scratch caches work",
          "[mapper012]") {
    auto mapper = ffe_board();
    constexpr std::array<MirrorMode, 4> lower = {MirrorMode::SingleLower,
                                                 MirrorMode::Vertical,
                                                 MirrorMode::SingleLower,
                                                 MirrorMode::Vertical};
    constexpr std::array<MirrorMode, 4> upper = {MirrorMode::SingleUpper,
                                                 MirrorMode::Horizontal,
                                                 MirrorMode::SingleUpper,
                                                 MirrorMode::Horizontal};
    for (unsigned addr = 0x42FCU; addr <= 0x42FFU; ++addr)
        for (Byte data : {0U, 0x10U, 0xE0U, 0xF0U}) {
            mapper->write_expansion(static_cast<Address>(addr), data);
            REQUIRE(mapper->mirror_mode() ==
                    ((data & 0x10U) == 0U ? lower[addr & 3U] : upper[addr & 3U]));
            REQUIRE(mapper->read_expansion(0x4500U) == (0x40U | data | (addr & 3U)));
        }
    for (unsigned addr = 0x43FCU; addr <= 0x43FFU; ++addr)
        for (Byte value : {0U, 1U, 2U, 3U, 0xFFU}) {
            mapper->write_expansion(static_cast<Address>(addr), value);
            REQUIRE(mapper->read_expansion(0x4501U) ==
                    static_cast<Byte>(((value & 3U) << 2U) | (addr & 3U)));
        }

    mapper->write_expansion(0x42FFU, 0x30U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4500U, static_cast<Byte>(0x42U | (bank << 4U)));
        mapper->write_prg(0x6123U, static_cast<Byte>(0x40U + bank));
    }
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4500U, static_cast<Byte>(0x42U | (bank << 4U)));
        REQUIRE(mapper->read_prg(0x6123U) == 0x40U + bank);
    }

    mapper->write_expansion(0x4500U, 0x02U);
    mapper->write_expansion(0x4504U, 3U);
    mapper->write_expansion(0x4505U, 4U);
    mapper->write_expansion(0x4507U, 5U);
    expect_ffe_prg(*mapper, 0x8000U, 3U * 8192U);
    expect_ffe_prg(*mapper, 0xA000U, 4U * 8192U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0xC000U));
    mapper->write_expansion(0x42FCU, 0U);
    expect_ffe_prg(*mapper, 0x6000U, 5U * 8192U);
    mapper->write_prg(0x6000U, 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->write_expansion(0x4500U, 0xC2U);
    REQUIRE_FALSE(mapper->maps_prg(0x8000U));
    REQUIRE_FALSE(mapper->maps_expansion(0x4500U));

    mapper->reset();
    mapper->observe_cpu_write(0x4000U, 0x12U);
    mapper->observe_cpu_write(0x4015U, 0x34U);
    mapper->observe_cpu_write(0x2007U, 0x56U);
    mapper->observe_cpu_write(0x3FFFU, 0x78U);
    mapper->observe_cpu_write(0x4504U, 0x9AU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0x12U);
    REQUIRE(mapper->read_expansion(0x5015U) == 0x34U);
    REQUIRE(mapper->read_expansion(0x5807U) == 0x78U);
    REQUIRE(mapper->read_expansion(0x5504U) == 0x9AU);
}

TEST_CASE("FFE 4M M2 and PA12 IRQ counters acknowledge wrap and disable", "[mapper012]") {
    auto mapper = ffe_board();
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4501U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 65536U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4500U, 0x4AU);
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("FFE 4M native NES 2.0 loading preserves open bus and CHR-in-DRAM data", "[mapper012]") {
    const auto factory = [](const INesHeader& header,
                            std::vector<Byte> prg,
                            std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
        return std::make_unique<Mapper012>(header, std::move(prg), std::move(chr));
    };
    MapperRegistry::instance().register_mapper(12U, factory);
    std::vector<Byte> bytes(16U, 0U);
    bytes[0U] = 'N';
    bytes[1U] = 'E';
    bytes[2U] = 'S';
    bytes[3U] = 0x1AU;
    bytes[4U] = 16U;
    bytes[5U] = 32U;
    bytes[6U] = 0xC1U;
    bytes[7U] = 0x08U;
    bytes[8U] = 0x10U;
    bytes[10U] = 0x09U;
    bytes[11U] = 0x09U;
    const auto prg = image(262144U);
    const auto chr = image(262144U, true);
    bytes.insert(bytes.end(), prg.begin(), prg.end());
    bytes.insert(bytes.end(), chr.begin(), chr.end());
    auto cartridge = Cartridge::from_data(bytes);
    REQUIRE(cartridge);
    REQUIRE(cartridge->header().is_nes2);
    REQUIRE(cartridge->header().submapper == 1U);
    REQUIRE(cartridge->header().prg_ram_size == 32768U);
    REQUIRE(cartridge->header().chr_ram_size == 32768U);

    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    REQUIRE(bus.read(0x4500U) == 0x63U);
    for (Address addr : {0x42FCU, 0x43FFU, 0x4502U, 0x4508U, 0x4FFFU}) {
        bus.write(0x10U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x4504U, 32U);
    REQUIRE(bus.read(0x5504U) == 32U);
    REQUIRE(bus.read(0x8123U) == chr_cell(0x123U));
    bus.write(0x5321U, 0xA7U);
    REQUIRE(bus.read(0x5321U) == 0xA7U);
    bus.write(0x4000U, 0x5AU);
    bus.write(0x2007U, 0xC7U);
    REQUIRE(bus.read(0x5000U) == 0x5AU);
    REQUIRE(bus.read(0x5807U) == 0xC7U);
}

TEST_CASE("FFE 4M reset retains game DRAM while cold power clears work RAM", "[mapper012]") {
    auto mapper = ffe_board();
    mapper->write_expansion(0x42FCU, 0x40U);
    mapper->write_prg(0x8000U, 0xA7U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->write_expansion(0x5321U, 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    mapper->write_chr(0U, 0x39U);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    REQUIRE(mapper->read_expansion(0x5321U) == 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    REQUIRE(mapper->read_chr(0U) == 0x39U);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x42U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_expansion(0x5321U) == 0U);
    REQUIRE(mapper->read_chr(0U) == 0U);
}

TEST_CASE("FFE 4M snapshots replay all volatile RAM and reject corruption atomically",
          "[mapper012][state]") {
    auto mapper = ffe_board();
    fill_ffe_chr(*mapper);
    mapper->write_expansion(0x42FFU, 0x70U);
    mapper->write_expansion(0x43FEU, 1U);
    mapper->write_prg(0x8000U, 0x2AU);
    mapper->write_prg(0xA000U, 0x15U);
    mapper->write_expansion(0x4500U, 0x4BU);
    mapper->write_expansion(0x4504U, 9U);
    mapper->write_expansion(0x4510U, 0xF1U);
    mapper->write_expansion(0x451BU, 0xE7U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_a12(true);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_expansion(0x5321U, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 593995U);

    mapper->power_on();
    REQUIRE(state(*mapper) != saved);
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_expansion(0x5321U) == 0x5AU);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (std::size_t length :
         {0UZ, 1UZ, 7UZ, 20UZ, 42UZ, 43UZ, 50UZ, 51UZ, 100UZ, saved.size() - 1U})
        reject(std::span(saved).first(length));
    for (auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 15>{{
             {0U, 0U},
             {2U, 0xFFU},
             {6U, 0xFFU},
             {10U, 0U},
             {11U, 1U},
             {12U, 0x40U},
             {13U, 4U},
             {14U, 4U},
             {18U, 2U},
             {19U, 2U},
             {20U, 2U},
             {21U, 0x40U},
             {25U, 0x40U},
             {41U, 2U},
             {43U, 1U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        reject(bad);
    }
    auto wrong = ffe_board(131072U, 262144U, false);
    const auto before = state(*wrong);
    StateReader wrong_reader(saved);
    wrong->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong) == before);
}

} // namespace
