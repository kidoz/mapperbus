#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper047.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 47U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte cell(std::size_t offset, std::size_t page_size) {
    const auto page = offset / page_size;
    const auto within = offset % page_size;
    return static_cast<Byte>(page * 37U + within + (within >> 8U));
}

std::vector<Byte> rom(std::size_t size, std::size_t page_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = cell(i, page_size);
    return data;
}

std::unique_ptr<Mmc3Multicart47> board(bool legacy = false, bool vertical = true) {
    return std::make_unique<Mmc3Multicart47>(
        header(legacy, vertical), rom(256U * 1024U, 8192U), rom(256U * 1024U, 1024U));
}

void set_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> image(bool legacy = false) {
    std::vector<Byte> data(16U, 0U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 16U;
    data[5] = 32U;
    data[6] = 0xF1U;
    data[7] = legacy ? 0x20U : 0x28U;
    const auto prg = rom(256U * 1024U, 8192U);
    const auto chr = rom(256U * 1024U, 1024U);
    data.insert(data.end(), prg.begin(), prg.end());
    data.insert(data.end(), chr.begin(), chr.end());
    return data;
}

TEST_CASE("Mapper 047 validates its two-block ROM-only geometry", "[mapper047]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            REQUIRE(Mmc3Multicart47::validate_geometry(
                header(legacy, vertical), 256U * 1024U, 256U * 1024U));

    const auto reject =
        [](INesHeader h, std::size_t prg_size = 256U * 1024U, std::size_t chr_size = 256U * 1024U) {
            REQUIRE_FALSE(Mmc3Multicart47::validate_geometry(h, prg_size, chr_size));
        };
    for (std::size_t size : {0UZ, 8192UZ, 131072UZ, 262145UZ, 524288UZ}) {
        reject(header(), size);
        reject(header(), 256U * 1024U, size);
    }
    for (unsigned kind = 0U; kind < 8U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        INFO(kind);
        reject(h);
    }
}

TEST_CASE("Mapper 047 starts without RAM and resets every MMC3 latch", "[mapper047]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto mapper = board(legacy, vertical);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(mapper->maps_prg(0x6000U));
            REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
            REQUIRE(mapper->maps_prg(0x8000U));
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0xE000U) == cell(15U * 8192U, 8192U));
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->maps_expansion(0x5000U));

            set_bank(*mapper, 6U, 9U, 0xC0U);
            mapper->write_prg(0x6000U, 1U);
            mapper->write_prg(0xA000U, vertical ? 0U : 1U);
            mapper->write_prg(0xA001U, 0U);
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
            mapper->reset();
            REQUIRE(mapper->read_prg(0xE000U) == cell(15U * 8192U, 8192U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(state(*mapper) == state(*board(legacy, vertical)));
            mapper->write_prg(0x6000U, 1U);
            mapper->power_on();
            REQUIRE(state(*mapper) == state(*board(legacy, vertical)));
        }
}

TEST_CASE("Mapper 047 applies PRG block pins after MMC3 mode and bank decoding", "[mapper047]") {
    auto mapper = board();
    for (Byte block : {0U, 1U}) {
        mapper->write_prg(0x6000U, block);
        for (unsigned value = 0U; value < 256U; ++value)
            for (Byte mode : {0U, 0x40U}) {
                set_bank(*mapper, 6U, static_cast<Byte>(value), mode);
                set_bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA5U), mode);
                const std::array<unsigned, 4> pages = {
                    block * 16U + (mode == 0U ? (value & 15U) : 14U),
                    block * 16U + ((value ^ 0xA5U) & 15U),
                    block * 16U + (mode == 0U ? 14U : (value & 15U)),
                    block * 16U + 15U,
                };
                for (unsigned slot = 0U; slot < 4U; ++slot)
                    for (Address offset : {0U, 1U, 0x1234U, 0x1FFFU})
                        REQUIRE(mapper->read_prg(
                                    static_cast<Address>(0x8000U + slot * 8192U + offset)) ==
                                cell(pages[slot] * 8192U + offset, 8192U));
            }
    }
}

TEST_CASE("Mapper 047 applies CHR block pins after pairing and inversion", "[mapper047]") {
    auto mapper = board();
    for (Byte block : {0U, 1U}) {
        mapper->write_prg(0x7FFFU, static_cast<Byte>(0xFEU | block));
        for (unsigned value = 0U; value < 256U; ++value)
            for (Byte mode : {0U, 0x80U}) {
                std::array<Byte, 6> banks{};
                for (Byte index = 0U; index < 6U; ++index) {
                    banks[index] = static_cast<Byte>(value + index * 29U);
                    set_bank(*mapper, index, banks[index], mode);
                }
                const std::array<unsigned, 8> pages = {
                    banks[0] & 0x7EU,
                    (banks[0] & 0x7EU) + 1U,
                    banks[1] & 0x7EU,
                    (banks[1] & 0x7EU) + 1U,
                    banks[2] & 0x7FU,
                    banks[3] & 0x7FU,
                    banks[4] & 0x7FU,
                    banks[5] & 0x7FU,
                };
                for (Address addr : {0U,
                                     1U,
                                     0x3FFU,
                                     0x400U,
                                     0x800U,
                                     0xC00U,
                                     0x1000U,
                                     0x1400U,
                                     0x1800U,
                                     0x1C00U,
                                     0x1FFFU}) {
                    const unsigned slot = (addr / 1024U) ^ (mode == 0U ? 0U : 4U);
                    const auto physical = block * 128U + pages[slot];
                    mapper->write_chr(addr, 0xFFU);
                    REQUIRE(mapper->read_chr(addr) ==
                            cell(physical * 1024U + (addr & 1023U), 1024U));
                }
            }
    }
}

TEST_CASE("Mapper 047 gates every block alias with both A001 protection bits", "[mapper047]") {
    auto mapper = board();
    for (unsigned gate = 0U; gate < 256U; ++gate)
        for (Address addr : {0x6000U, 0x6001U, 0x6FFFU, 0x7000U, 0x7FFFU}) {
            mapper->write_prg(0xA001U, 0x80U);
            mapper->write_prg(0x6000U, 0U);
            mapper->write_prg(0xA001U, static_cast<Byte>(gate));
            mapper->write_prg(addr, 0xFFU);
            const unsigned block = (gate & 0xC0U) == 0x80U ? 1U : 0U;
            REQUIRE(mapper->read_prg(0xE000U) == cell((block * 16U + 15U) * 8192U, 8192U));
        }

    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
        const unsigned block = (addr ^ (addr >> 8U)) & 1U;
        REQUIRE(mapper->read_prg(0xE000U) == cell((block * 16U + 15U) * 8192U, 8192U));
    }
}

TEST_CASE("Mapper 047 decodes MMC3 register aliases without bus conflicts", "[mapper047]") {
    auto mapper = board();
    const std::array<std::pair<Address, Address>, 8> aliases = {{{0x8000U, 0x9FFEU},
                                                                 {0x8001U, 0x9FFFU},
                                                                 {0xA000U, 0xBFFEU},
                                                                 {0xA001U, 0xBFFFU},
                                                                 {0xC000U, 0xDFFEU},
                                                                 {0xC001U, 0xDFFFU},
                                                                 {0xE000U, 0xFFFEU},
                                                                 {0xE001U, 0xFFFFU}}};
    for (const auto& [first, last] : aliases) {
        mapper->reset();
        mapper->write_prg(first, 0x86U);
        mapper->write_prg(last, 0x87U);
        const auto current = state(*mapper);
        switch (first) {
        case 0x8000U:
            REQUIRE(current[3] == 0x87U);
            break;
        case 0x8001U:
            REQUIRE(current[4] == 0x87U);
            break;
        case 0xA000U:
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            break;
        case 0xA001U:
            REQUIRE(current[16] == 0x80U);
            break;
        case 0xC000U:
            REQUIRE(current[14] == 0x87U);
            break;
        case 0xC001U:
            REQUIRE((current[15] & 1U) == 1U);
            break;
        case 0xE001U:
            REQUIRE((current[15] & 2U) == 2U);
            break;
        default:
            REQUIRE((current[15] & 6U) == 0U);
            break;
        }
    }
    set_bank(*mapper, 6U, 7U);
    mapper->write_prg(0x9FFFU, 9U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(9U * 8192U, 8192U));
}

TEST_CASE("Mapper 047 retains the Sharp MMC3 IRQ and acknowledgement behavior", "[mapper047]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 4U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 047 low reads stay open while CPU writes select blocks", "[mapper047]") {
    MapperRegistry::instance().register_mapper(
        47U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            return std::make_unique<Mmc3Multicart47>(h, std::move(prg), std::move(chr));
        });
    for (bool legacy : {false, true}) {
        auto cartridge = Cartridge::from_data(image(legacy));
        const auto error = cartridge ? std::string{} : cartridge.error();
        INFO(error);
        REQUIRE(cartridge);
        MemoryBus bus;
        bus.connect_cartridge(&*cartridge);
        for (unsigned value = 0U; value < 256U; ++value) {
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(0x6000U) == value);
            REQUIRE(bus.read(0x7FFFU) == value);
        }
        bus.write(0xA001U, 0x80U);
        bus.write(0x6000U, 1U);
        REQUIRE(bus.read(0xE000U) == cell(31U * 8192U, 8192U));
        for (Byte gate : {0U, 0x40U, 0xC0U}) {
            bus.write(0xA001U, gate);
            bus.write(0x7FFFU, 0U);
            REQUIRE(bus.read(0xE000U) == cell(31U * 8192U, 8192U));
        }
        bus.write(0xA001U, 0x80U);
        bus.write(0x7FFFU, 0U);
        REQUIRE(bus.read(0xE000U) == cell(15U * 8192U, 8192U));
    }
}

TEST_CASE("Mapper 047 state replay is bounded validated and atomic", "[mapper047]") {
    auto mapper = board();
    set_bank(*mapper, 0U, 0xF1U);
    set_bank(*mapper, 1U, 0xA3U);
    set_bank(*mapper, 2U, 0xD5U);
    set_bank(*mapper, 3U, 0x97U);
    set_bank(*mapper, 4U, 0xBDU);
    set_bank(*mapper, 5U, 0xE6U);
    set_bank(*mapper, 6U, 0xC9U, 0xC0U);
    set_bank(*mapper, 7U, 0xAAU);
    mapper->write_prg(0x6000U, 1U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 17U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_prg(0xC000U) == mapper->read_prg(0xC000U));
    REQUIRE(copy->read_chr(0x0000U) == mapper->read_chr(0x0000U));
    REQUIRE(copy->read_chr(0x1000U) == mapper->read_chr(0x1000U));
    copy->write_prg(0x6000U, 0U);
    REQUIRE(state(*copy) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated{std::span(saved).first(length)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 10> invalid = {
        {{0U, 0U},
         {2U, 2U},
         {3U, 8U},
         {10U, 0x40U},
         {11U, 0xFFU},
         {12U, static_cast<Byte>(MirrorMode::FourScreen)},
         {15U, 8U},
         {15U, 4U},
         {16U, 1U},
         {1U, 0U}}};
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader rejected(bad);
        mapper->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto other = board(false, false);
    const auto before = state(*other);
    StateReader wrong_tag(saved);
    other->load_state(wrong_tag);
    REQUIRE_FALSE(wrong_tag.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
