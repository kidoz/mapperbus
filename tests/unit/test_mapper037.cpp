#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper037.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 256UZ * 1024UZ;
constexpr std::size_t kChrSize = 256UZ * 1024UZ;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 37U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 29U + offset * 17U + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 43U + offset * 13U + (offset >> 8U));
}

std::vector<Byte> prg_image() {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = prg_cell(i / 8192U, i % 8192U);
    return data;
}

std::vector<Byte> chr_image() {
    std::vector<Byte> data(kChrSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = chr_cell(i / 1024U, i % 1024U);
    return data;
}

std::unique_ptr<Mmc3Multicart37> board(bool legacy = false, bool vertical = true) {
    return std::make_unique<Mmc3Multicart37>(header(legacy, vertical), prg_image(), chr_image());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper& mapper, Address addr, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

unsigned expected_prg_bank(unsigned outer_bank, unsigned inner_bank) {
    const bool a17 = (outer_bank & 4U) != 0U;
    const bool a16 = (outer_bank & 3U) == 3U || (a17 && (inner_bank & 0x08U) != 0U);
    return (inner_bank & 0x07U) | (a16 ? 0x08U : 0U) | (a17 ? 0x10U : 0U);
}

unsigned expected_chr_bank(unsigned outer_bank, unsigned inner_bank) {
    return (inner_bank & 0x7FU) | ((outer_bank & 4U) << 5U);
}

TEST_CASE("Mapper 037 accepts only its ROM-only H/V population", "[mapper037]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            const auto h = header(legacy, vertical);
            REQUIRE(Mmc3Multicart37::validate_geometry(h, kPrgSize, kChrSize));
            auto m = board(legacy, vertical);
            REQUIRE(state(*m).size() == 17U);
            REQUIRE(m->battery_ram().empty());
            REQUIRE(m->read_prg(0x8000U) == prg_cell(0U));
            REQUIRE(m->read_prg(0xA000U) == prg_cell(0U));
            REQUIRE(m->read_prg(0xC000U) == prg_cell(6U));
            REQUIRE(m->read_prg(0xE000U) == prg_cell(7U));
            REQUIRE(m->read_chr(0x1000U) == chr_cell(0U));
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(m->maps_prg(0x6000U));
            REQUIRE(m->maps_prg(0x8000U));
            REQUIRE_FALSE(m->maps_expansion(0x5000U));
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
        }

    const auto reject =
        [](const INesHeader& h, std::size_t p = kPrgSize, std::size_t c = kChrSize) {
            REQUIRE_FALSE(Mmc3Multicart37::validate_geometry(h, p, c));
        };
    for (auto size : {0UZ, 8192UZ, 131072UZ, 262143UZ, 262145UZ, 524288UZ}) {
        reject(header(), size, kChrSize);
        reject(header(), kPrgSize, size);
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        reject(h);
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mode;
            reject(h);
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 8192U;
            break;
        case 2U:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        reject(h);
    }
}

TEST_CASE("Mapper 037 outer register covers every alias data byte and NAND bank mode",
          "[mapper037]") {
    auto m = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U) ^ 0xF8U);
        outer(*m, static_cast<Address>(addr), value);
        const unsigned q = value & 7U;
        REQUIRE(m->read_prg(0xE000U) == prg_cell(expected_prg_bank(q, 7U)));
        REQUIRE(m->read_chr(0x1000U) == chr_cell(expected_chr_bank(q, 0U)));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        outer(*m, 0x6A55U, static_cast<Byte>(value));
        constexpr std::array<unsigned, 8> kFixedBanks = {7U, 7U, 7U, 15U, 23U, 23U, 23U, 31U};
        REQUIRE(m->read_prg(0xE000U) == prg_cell(kFixedBanks[value & 7U]));
        REQUIRE(m->read_chr(0x1000U) == chr_cell((value & 4U) != 0U ? 128U : 0U));
    }
}

TEST_CASE("Mapper 037 outer writes require MMC3 RAM enable without write protection",
          "[mapper037]") {
    auto m = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->reset();
        m->write_prg(0xA001U, static_cast<Byte>(gate));
        m->write_prg(0x6000U, 7U);
        const unsigned expected = (gate & 0xC0U) == 0x80U ? 31U : 7U;
        REQUIRE(m->read_prg(0xE000U) == prg_cell(expected));
        REQUIRE(m->read_prg(0x6000U) == 0U);
        REQUIRE(m->read_prg(0x7FFFU) == 0U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
    }
}

TEST_CASE("Mapper 037 routes MMC3 PRG outputs through the outer pins in both modes",
          "[mapper037]") {
    auto m = board();
    for (unsigned q = 0U; q < 8U; ++q) {
        m->reset();
        outer(*m, 0x6000U, static_cast<Byte>(q));
        for (Byte mode : {0U, 0x40U}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const unsigned other = raw ^ 0xA5U;
                bank(*m, 6U, static_cast<Byte>(raw), mode);
                bank(*m, 7U, static_cast<Byte>(other), mode);
                const std::array<unsigned, 4> inner = {
                    mode == 0U ? raw & 15U : 6U, other & 15U, mode == 0U ? 6U : raw & 15U, 7U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8123U + slot * 8192U);
                    REQUIRE(m->read_prg(addr) ==
                            prg_cell(expected_prg_bank(q, inner[slot]), 0x123U));
                }
            }
            bank(*m, 6U, 0x35U, mode);
            bank(*m, 7U, 0x0AU, mode);
            const std::array<unsigned, 4> inner = {
                mode == 0U ? 5U : 6U, 10U, mode == 0U ? 6U : 5U, 7U};
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
                const unsigned slot = (addr - 0x8000U) / 8192U;
                const unsigned offset = addr & 0x1FFFU;
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        prg_cell(expected_prg_bank(q, inner[slot]), offset));
            }
        }
    }
}

TEST_CASE("Mapper 037 maps paired and inverted CHR banks and keeps ROM immutable", "[mapper037]") {
    auto m = board();
    for (unsigned q = 0U; q < 8U; ++q) {
        m->reset();
        outer(*m, 0x6000U, static_cast<Byte>(q));
        for (Byte mode : {0U, 0x80U}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                std::array<unsigned, 6> values{};
                for (unsigned reg = 0U; reg < values.size(); ++reg) {
                    values[reg] = (raw + reg * 37U) & 0xFFU;
                    bank(*m, static_cast<Byte>(reg), static_cast<Byte>(values[reg]), mode);
                }
                const std::array<unsigned, 8> inner = {values[0] & 0xFEU,
                                                       (values[0] & 0xFEU) + 1U,
                                                       values[1] & 0xFEU,
                                                       (values[1] & 0xFEU) + 1U,
                                                       values[2],
                                                       values[3],
                                                       values[4],
                                                       values[5]};
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const unsigned physical_slot = slot ^ (mode == 0U ? 0U : 4U);
                    const auto addr = static_cast<Address>(physical_slot * 1024U + (raw & 1023U));
                    m->write_chr(addr, 0xFFU);
                    REQUIRE(m->read_chr(addr) ==
                            chr_cell(expected_chr_bank(q, inner[slot]), raw & 1023U));
                }
            }
            std::array<unsigned, 6> values = {0xD4U, 0xA5U, 0x83U, 0x42U, 0xF5U, 0x7EU};
            for (unsigned reg = 0U; reg < values.size(); ++reg)
                bank(*m, static_cast<Byte>(reg), values[reg], mode);
            const std::array<unsigned, 8> inner = {
                0xD4U, 0xD5U, 0xA4U, 0xA5U, 0x83U, 0x42U, 0xF5U, 0x7EU};
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const unsigned slot = (addr / 1024U) ^ (mode == 0U ? 0U : 4U);
                m->write_chr(static_cast<Address>(addr), 0U);
                REQUIRE(m->read_chr(static_cast<Address>(addr)) ==
                        chr_cell(expected_chr_bank(q, inner[slot]), addr & 1023U));
            }
        }
    }
}

TEST_CASE("Mapper 037 decodes every MMC3 register alias", "[mapper037]") {
    auto m = board();
    const auto initial = state(*m);
    for (unsigned addr = 0U; addr < 0x6000U; ++addr) {
        m->write_prg(static_cast<Address>(addr), 0xFFU);
        REQUIRE(state(*m) == initial);
    }
    for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0xC6U);
        REQUIRE(state(*m)[3] == 0xC6U);
    }
    for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
        m->reset();
        m->write_prg(0x8000U, 6U);
        m->write_prg(static_cast<Address>(addr), 0xFFU);
        REQUIRE(state(*m)[10] == 0x3FU);
    }
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 1U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    }
    for (unsigned addr = 0xA001U; addr < 0xC000U; addr += 2U) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0x80U);
        m->write_prg(0x6000U, 7U);
        REQUIRE(m->read_prg(0xE000U) == prg_cell(31U));
    }
    for (unsigned addr = 0xC000U; addr < 0xE000U; addr += 2U) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0xABU);
        REQUIRE(state(*m)[14] == 0xABU);
    }
    for (unsigned addr = 0xC001U; addr < 0xE000U; addr += 2U) {
        m->reset();
        m->write_prg(0xC000U, 3U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        m->write_prg(static_cast<Address>(addr), 0U);
        const auto bytes = state(*m);
        REQUIRE(bytes[13] == 0U);
        REQUIRE((bytes[15] & 1U) != 0U);
    }
    for (unsigned addr = 0xE000U; addr <= 0xFFFEU; addr += 2U) {
        m->reset();
        m->write_prg(0xC000U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE_FALSE(m->irq_pending());
    }
    for (unsigned addr = 0xE001U; addr <= 0xFFFFU; addr += 2U) {
        m->reset();
        m->write_prg(0xC000U, 0U);
        m->write_prg(static_cast<Address>(addr), 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
    }
}

TEST_CASE("Mapper 037 preserves MMC3 IRQ progress across game switches", "[mapper037]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
    outer(*m, 0x6000U, 5U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());

    StateReader restore(partial);
    m->load_state(restore);
    REQUIRE(restore.ok());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());

    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
}

TEST_CASE("Mapper 037 CIC reset clears the outer latch and restores deterministic MMC3 state",
          "[mapper037]") {
    for (bool vertical : {false, true}) {
        auto m = board(false, vertical);
        const auto initial = state(*m);
        outer(*m, 0x7FFFU, 7U);
        bank(*m, 6U, 0x3DU, 0xC0U);
        bank(*m, 2U, 0xF3U, 0xC0U);
        m->write_prg(0xA000U, vertical ? 1U : 0U);
        m->write_prg(0xA001U, 0xC0U);
        m->write_prg(0xC000U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        REQUIRE(state(*m) != initial);
        m->reset();
        REQUIRE(state(*m) == initial);
        REQUIRE(m->read_prg(0xE000U) == prg_cell(7U));
        outer(*m, 0x6000U, 3U);
        m->power_on();
        REQUIRE(state(*m) == initial);
    }
}

TEST_CASE("Mapper 037 snapshots replay exactly and reject corruption atomically", "[mapper037]") {
    auto m = board();
    outer(*m, 0x6000U, 5U);
    bank(*m, 6U, 0x35U, 0xC0U);
    bank(*m, 7U, 0x1AU, 0xC0U);
    bank(*m, 2U, 0xD3U, 0xC0U);
    m->write_prg(0xA000U, 0U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0xC000U, 3U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    REQUIRE(saved.size() == 17U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == m->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == m->read_chr(0U));
    outer(*copy, 0x6000U, 7U);
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(state(*copy) == saved);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 10> invalid = {
        std::pair{0UZ, static_cast<Byte>(saved[0] ^ 1U)},
        std::pair{2UZ, 8U},
        std::pair{3UZ, 8U},
        std::pair{3UZ, 0x38U},
        std::pair{10UZ, 0x40U},
        std::pair{11UZ, 0x40U},
        std::pair{12UZ, 2U},
        std::pair{15UZ, 8U},
        std::pair{15UZ, 4U},
        std::pair{16UZ, 1U},
    };
    for (const auto [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader rejected(bad);
        copy->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*copy) == saved);
    }

    auto horizontal = board(false, false);
    const auto before = state(*horizontal);
    StateReader mismatched(saved);
    horizontal->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*horizontal) == before);
}

TEST_CASE("Mapper 037 write-only outer register leaves all CPU reads on open bus", "[mapper037]") {
    MapperRegistry::instance().register_mapper(
        37U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            return std::make_unique<Mmc3Multicart37>(h, std::move(prg), std::move(chr));
        });
    auto prg = prg_image();
    auto chr = chr_image();
    std::vector<Byte> image(16U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 16U;
    image[5] = 32U;
    image[6] = 0x51U;
    image[7] = 0x20U;
    image.insert(image.end(), prg.begin(), prg.end());
    image.insert(image.end(), chr.begin(), chr.end());
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        bus.write(0xA001U, 0x80U);
        bus.write(addr, 0xA7U);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0xA001U, 0x80U);
    bus.write(0x6000U, 7U);
    REQUIRE(bus.read(0xE000U) == prg_cell(31U));
    bus.write(0xA001U, 0U);
    bus.write(0x6000U, 0U);
    REQUIRE(bus.read(0xE000U) == prg_cell(31U));
}

} // namespace
