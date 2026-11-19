#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/txc189.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool legacy = false, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 189U;
    header.is_nes2 = !legacy;
    header.prg_ram_size = legacy ? 8192U : 0U;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 23U + offset + (offset >> 9U));
}

std::unique_ptr<Txc189> make_board(std::size_t prg_size = 0x20000U,
                                   std::size_t chr_size = 0x20000U,
                                   bool legacy = false,
                                   bool vertical = true) {
    const auto header = make_header(legacy, vertical);
    REQUIRE(Txc189::validate_geometry(header, prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i / 0x8000U, i % 0x8000U);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i / 0x400U, i % 0x400U);
    return std::make_unique<Txc189>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_register(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 189 starts at PRG bank zero with ROM-only MMC3 resources", "[mapper189]") {
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = make_board(0x20000U, 0x20000U, legacy, vertical);
            REQUIRE(state(*mapper).size() == 25U);
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(0U, 0x7FFFU));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->maps_expansion(0x4132U));
            REQUIRE_FALSE(mapper->maps_prg(0x4132U));
            REQUIRE(mapper->prg_read_mask(0x4132U) == 0U);
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 189 folds both data nibbles for every generalized bank value", "[mapper189]") {
    for (const std::size_t prg_size : {0x20000U, 0x40000U}) {
        auto mapper = make_board(prg_size);
        const unsigned bank_mask = prg_size / 0x8000U - 1U;
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x4132U, static_cast<Byte>(value));
            const unsigned bank = (value | (value >> 4U)) & 0x0FU;
            for (const Address addr : {0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                INFO("value=" << value << " addr=" << addr << " prg_size=" << prg_size);
                REQUIRE(mapper->read_prg(addr) == prg_cell(bank & bank_mask, addr & 0x7FFFU));
            }
            REQUIRE(state(*mapper)[10] == bank);
        }
    }
}

TEST_CASE("Mapper 189 generalized register covers every low CPU address without RAM",
          "[mapper189]") {
    auto mapper = make_board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0x32U);
        const bool selected = addr >= 0x4020U && addr < 0x8000U;
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(selected ? 3U : 0U));
        REQUIRE(state(*mapper)[10] == (selected ? 3U : 0U));
    }
    for (const Address addr : {0x4020U, 0x4100U, 0x4132U, 0x5FFFU, 0x6000U, 0x6132U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
    }
}

TEST_CASE("Mapper 189 expansion writes reach the latch while expansion reads float",
          "[mapper189]") {
    auto mapper = make_board();
    for (const Address addr : {0x4020U, 0x4100U, 0x4132U, 0x43FFU, 0x5000U, 0x5FFFU}) {
        mapper->write_expansion(addr, 0x32U);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(3U));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->reset();
    }
}

TEST_CASE("Mapper 189 synthetic ROM uses the real CPU expansion and PRG bus routes",
          "[mapper189]") {
    MapperRegistry::instance().register_mapper(
        189U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!Txc189::validate_geometry(header, prg.size(), chr.size()))
                return {};
            return std::make_unique<Txc189>(header, std::move(prg), std::move(chr));
        });
    std::vector<Byte> image(16U + 0x20000U + 0x20000U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 8U;
    image[5] = 16U;
    image[6] = 0xD1U;
    image[7] = 0xB0U;
    for (std::size_t i = 0U; i < 0x20000U; ++i) {
        image[16U + i] = prg_cell(i / 0x8000U, i % 0x8000U);
        image[16U + 0x20000U + i] = chr_cell(i / 0x400U, i % 0x400U);
    }
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    bus.write(0x4132U, 0x32U);
    REQUIRE(bus.read(0x8000U) == prg_cell(3U));
    bus.write(0U, 0x39U);
    REQUIRE(bus.read(0x4132U) == 0x39U);
    bus.write(0x6000U, 0x21U);
    REQUIRE(bus.read(0x8000U) == prg_cell(3U));
    REQUIRE(cartridge->read_chr(0U) == chr_cell(0U));
}

TEST_CASE("Mapper 189 PRG mapping is independent of MMC3 PRG registers and modes", "[mapper189]") {
    auto mapper = make_board(0x40000U);
    mapper->write_prg(0x6132U, 0x52U);
    for (const Byte mode : {0U, 0x40U, 0x80U, 0xC0U}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            write_register(*mapper, 6U, static_cast<Byte>(value), mode);
            write_register(*mapper, 7U, static_cast<Byte>(value ^ 0xFFU), mode);
            for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
                REQUIRE(mapper->read_prg(addr) == prg_cell(7U, addr & 0x7FFFU));
        }
    }
}

TEST_CASE("Mapper 189 maps every CHR bank in normal and inverted MMC3 arrangements",
          "[mapper189]") {
    auto mapper = make_board();
    for (const Byte mode : {0U, 0x80U}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            std::array<unsigned, 6> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = (value + index * 19U) & 0xFFU;
                write_register(*mapper, index, static_cast<Byte>(banks[index]), mode);
            }
            const std::array<unsigned, 8> pages = {banks[0] & 0xFEU,
                                                   (banks[0] & 0xFEU) + 1U,
                                                   banks[1] & 0xFEU,
                                                   (banks[1] & 0xFEU) + 1U,
                                                   banks[2],
                                                   banks[3],
                                                   banks[4],
                                                   banks[5]};
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                const auto slot = (addr / 0x400U) ^ (mode == 0U ? 0U : 4U);
                mapper->write_chr(static_cast<Address>(addr), 0xFFU);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        chr_cell(pages[slot] % 128U, addr & 0x3FFU));
            }
        }
    }
}

TEST_CASE("Mapper 189 honors every MMC3 bank register alias and masks PRG data", "[mapper189]") {
    auto mapper = make_board();
    for (unsigned offset = 0U; offset < 0x2000U; offset += 2U) {
        mapper->write_prg(static_cast<Address>(0x8000U + offset), 0xC5U);
        REQUIRE(state(*mapper)[11] == 5U);
        REQUIRE(state(*mapper)[12] == 0xC0U);
    }
    for (Byte index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_prg(0x8000U, index);
        REQUIRE(state(*mapper)[11] == index);
        for (unsigned offset = 1U; offset < 0x2000U; offset += 2U) {
            mapper->write_prg(static_cast<Address>(0x8000U + offset), 0xA5U);
            const auto actual = state(*mapper);
            INFO("index=" << +index << " offset=" << offset
                          << " addr=" << static_cast<Address>(0x8000U + offset)
                          << " select=" << +actual[11] << " value=" << +actual[13U + index]);
            REQUIRE(actual[13U + index] == (index < 6U ? 0xA5U : 0x25U));
        }
    }
}

TEST_CASE("Mapper 189 honors all CIRAM and IRQ register aliases", "[mapper189]") {
    auto mapper = make_board();
    for (unsigned offset = 0U; offset < 0x2000U; offset += 2U) {
        const auto value = static_cast<Byte>((offset >> 1U) & 1U);
        mapper->write_prg(static_cast<Address>(0xA000U + offset), value);
        REQUIRE(mapper->mirror_mode() ==
                (value == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        mapper->write_prg(static_cast<Address>(0xC000U + offset), 0xA7U);
        REQUIRE(state(*mapper)[23] == 0xA7U);
        mapper->write_prg(static_cast<Address>(0xE000U + offset), 0xFFU);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE((state(*mapper)[24] & 6U) == 0U);
    }
    for (unsigned offset = 1U; offset < 0x2000U; offset += 2U) {
        mapper->write_prg(static_cast<Address>(0xC000U + offset), 0U);
        const auto reloaded = state(*mapper);
        INFO("offset=" << offset);
        REQUIRE(reloaded[22] == 0U);
        REQUIRE((reloaded[24] & 1U) == 1U);
        mapper->write_prg(static_cast<Address>(0xE000U + offset), 0U);
        REQUIRE((state(*mapper)[24] & 2U) == 2U);
        const auto enabled = state(*mapper);
        mapper->write_prg(static_cast<Address>(0xA000U + offset), 0U);
        REQUIRE(state(*mapper) == enabled);
    }
}

TEST_CASE("Mapper 189 MMC3 IRQ reload acknowledge enable and zero-latch behavior replay",
          "[mapper189]") {
    auto mapper = make_board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    const auto partial = state(*mapper);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->reset();
    StateReader restore(partial);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned iteration = 0U; iteration < 3U; ++iteration) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 189 reset deterministically clears bank CHR mirroring and IRQ state",
          "[mapper189]") {
    auto mapper = make_board(0x20000U, 0x20000U, false, false);
    mapper->write_prg(0x4132U, 0x32U);
    write_register(*mapper, 1U, 0x55U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE(state(*mapper) == state(*make_board(0x20000U, 0x20000U, false, false)));
    mapper->write_prg(0x4132U, 0x22U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 189 snapshots replay active state and reject corruption atomically",
          "[mapper189]") {
    auto mapper = make_board();
    mapper->write_prg(0x6132U, 0x21U);
    write_register(*mapper, 0U, 0xFEU, 0xC0U);
    write_register(*mapper, 2U, 2U, 0xC0U);
    write_register(*mapper, 6U, 0x3FU, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 25U);

    mapper->reset();
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(3U));
    REQUIRE(mapper->read_chr(0U) == chr_cell(2U));
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    StateReader active(saved);
    mapper->load_state(active);
    REQUIRE(active.ok());

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 10> invalid = {{{0U, 0U},
                                                                   {2U, 1U},
                                                                   {6U, 1U},
                                                                   {10U, 0x10U},
                                                                   {11U, 8U},
                                                                   {12U, 1U},
                                                                   {19U, 0x40U},
                                                                   {20U, 0x40U},
                                                                   {21U, 2U},
                                                                   {24U, 5U}}};
    for (const auto [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const bool vertical : {false, true}) {
        auto other = make_board(0x40000U, 0x20000U, false, vertical);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

TEST_CASE("Mapper 189 validates documented geometry and native memory metadata", "[mapper189]") {
    for (const bool legacy : {false, true}) {
        for (const bool vertical : {false, true}) {
            for (const std::size_t prg_size : {0x20000U, 0x40000U}) {
                for (const std::size_t chr_size : {0x2000U, 0x20000U, 0x40000U})
                    REQUIRE(Txc189::validate_geometry(
                        make_header(legacy, vertical), prg_size, chr_size));
            }
        }
    }
    for (const std::size_t size : {0U, 0x8000U, 0x10000U, 0x18000U, 0x30000U, 0x80000U})
        REQUIRE_FALSE(Txc189::validate_geometry(make_header(), size, 0x20000U));
    for (const std::size_t size : {0U, 0x400U, 0x1000U, 0x2001U, 0x40001U, 0x80000U})
        REQUIRE_FALSE(Txc189::validate_geometry(make_header(), 0x20000U, size));
    for (unsigned variant = 0U; variant < 10U; ++variant) {
        auto header = make_header();
        switch (variant) {
        case 0U:
            header.submapper = 1U;
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
            header.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            header.mirror_mode = MirrorMode::SingleUpper;
            break;
        default:
            header.mapper_number = 196U;
            break;
        }
        REQUIRE_FALSE(Txc189::validate_geometry(header, 0x20000U, 0x20000U));
    }
}

} // namespace
