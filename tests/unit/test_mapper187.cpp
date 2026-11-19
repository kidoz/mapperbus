#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper187.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool legacy = false, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 187U;
    header.is_nes2 = !legacy;
    header.prg_ram_size = legacy ? 0x2000U : 0U;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return header;
}

std::vector<Byte> make_prg(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i) {
        const auto bank = i / 0x2000U;
        const auto offset = i & 0x1FFFU;
        rom[i] = offset == 0U ? static_cast<Byte>(bank)
                              : static_cast<Byte>(bank * 29U + offset * 7U + (offset >> 8U));
    }
    return rom;
}

std::vector<Byte> make_chr(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i) {
        const auto bank = i / 0x400U;
        const auto offset = i & 0x3FFU;
        rom[i] = offset == 0U   ? static_cast<Byte>(bank)
                 : offset == 1U ? static_cast<Byte>(bank >> 8U)
                                : static_cast<Byte>(bank * 17U + offset * 11U + (offset >> 8U));
    }
    return rom;
}

std::unique_ptr<Mapper187> make_board(std::size_t prg_size = 0x40000U,
                                      std::size_t chr_size = 0x40000U,
                                      bool legacy = false,
                                      bool vertical = true) {
    const auto header = make_header(legacy, vertical);
    REQUIRE(Mapper187::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper187>(header, make_prg(prg_size), make_chr(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned chr_marker(Mapper& mapper, Address addr) {
    return mapper.read_chr(addr) | (static_cast<unsigned>(mapper.read_chr(addr + 1U)) << 8U);
}

TEST_CASE("Mapper 187 reset uses normal MMC3 banking and header CIRAM", "[mapper187]") {
    for (const bool legacy : {false, true}) {
        for (const bool vertical : {false, true}) {
            auto mapper = make_board(0x40000U, 0x40000U, legacy, vertical);
            REQUIRE(mapper->read_prg(0x8000U) == 0U);
            REQUIRE(mapper->read_prg(0xA000U) == 0U);
            REQUIRE(mapper->read_prg(0xC000U) == 30U);
            REQUIRE(mapper->read_prg(0xE000U) == 31U);
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->maps_prg(0x6000U));
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(state(*mapper).size() == 26U);
            REQUIRE(state(*mapper)[24] == 0x80U);
            REQUIRE(state(*mapper)[25] == 0U);
        }
    }
}

TEST_CASE("Mapper 187 NROM override covers every bank and both 16/32 KiB modes", "[mapper187]") {
    auto mapper = make_board(0x80000U, 0x80000U);
    for (unsigned bank = 0U; bank < 32U; ++bank) {
        mapper->write_expansion(0x5000U, static_cast<Byte>(0x80U | bank));
        for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
            INFO("NROM-128 bank=" << bank << " addr=" << addr);
            REQUIRE(mapper->read_prg(addr) == bank * 2U + ((addr & 0x2000U) != 0U ? 1U : 0U));
        }

        mapper->write_expansion(0x5000U, static_cast<Byte>(0xA0U | bank));
        const unsigned pair = bank & 0x1EU;
        for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
            const unsigned page = pair + ((addr & 0x4000U) != 0U ? 1U : 0U);
            INFO("NROM-256 bank=" << bank << " addr=" << addr);
            REQUIRE(mapper->read_prg(addr) == page * 2U + ((addr & 0x2000U) != 0U ? 1U : 0U));
        }
    }
}

TEST_CASE("Mapper 187 NROM write mask accepts only even $5000 aliases", "[mapper187]") {
    auto mapper = make_board();
    Byte expected = 0U;
    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
        const Byte value = static_cast<Byte>(addr * 37U + (addr >> 8U));
        if ((addr & 1U) == 0U)
            expected = value & 0xBFU;
        mapper->write_expansion(static_cast<Address>(addr), value);
        INFO("addr=" << addr << " value=" << +value);
        REQUIRE(state(*mapper)[25] == expected);
    }
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x4FFFU, 0x6000U, 0x7FFFU})
        mapper->write_expansion(addr, 0U);
    REQUIRE(state(*mapper) == before);

    mapper->write_expansion(0x5000U, 0xFFU);
    REQUIRE(state(*mapper)[25] == 0xBFU);
    mapper->write_expansion(0x5000U, 0U);
    set_bank(*mapper, 6U, 7U);
    mapper->write_expansion(0x5000U, 0xBFU);
    set_bank(*mapper, 6U, 13U);
    set_bank(*mapper, 7U, 9U);
    mapper->write_expansion(0x5000U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == 13U);
    REQUIRE(mapper->read_prg(0xA000U) == 9U);
}

TEST_CASE("Mapper 187 normal PRG banking preserves both MMC3 modes and six-bit registers",
          "[mapper187]") {
    auto mapper = make_board();
    for (const Byte mode : {0U, 0x40U, 0x80U, 0xC0U}) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            set_bank(*mapper, 6U, static_cast<Byte>(raw), mode);
            set_bank(*mapper, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
            const std::array<unsigned, 4> expected{
                (mode & 0x40U) != 0U ? 30U : (raw & 0x3FU) % 32U,
                ((raw ^ 0x35U) & 0x3FU) % 32U,
                (mode & 0x40U) != 0U ? (raw & 0x3FU) % 32U : 30U,
                31U,
            };
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        expected[slot]);
        }
    }
    auto large = make_board(0x80000U, 0x80000U);
    REQUIRE(large->read_prg(0xC000U) == 62U);
    REQUIRE(large->read_prg(0xE000U) == 63U);
}

TEST_CASE("Mapper 187 MMC3 CHR modes combine with physical PPU A12 on CHR A18", "[mapper187]") {
    auto mapper = make_board(0x40000U, 0x80000U);
    constexpr std::array<Byte, 6> initial{8U, 12U, 47U, 63U, 100U, 191U};
    for (const Byte inversion : {0U, 0x80U}) {
        for (unsigned reg = 0U; reg < 6U; ++reg) {
            for (unsigned value = 0U; value < 256U; ++value) {
                auto regs = initial;
                regs[reg] = static_cast<Byte>(value);
                for (unsigned r = 0U; r < 6U; ++r)
                    set_bank(*mapper, static_cast<Byte>(r), regs[r], inversion);
                const std::array<unsigned, 8> banks{regs[0] & 0xFEU,
                                                    (regs[0] & 0xFEU) + 1U,
                                                    regs[1] & 0xFEU,
                                                    (regs[1] & 0xFEU) + 1U,
                                                    regs[2],
                                                    regs[3],
                                                    regs[4],
                                                    regs[5]};
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto logical = slot ^ (inversion != 0U ? 4U : 0U);
                    INFO("inversion=" << +inversion << " reg=" << reg << " value=" << value
                                      << " slot=" << slot);
                    REQUIRE(chr_marker(*mapper, static_cast<Address>(slot * 0x400U)) ==
                            banks[logical] + (slot >= 4U ? 256U : 0U));
                }
            }
        }
    }
}

TEST_CASE("Mapper 187 wraps CHR A18 only after MMC3 bank decoding", "[mapper187]") {
    for (const std::size_t size : {0x2000UZ, 0x4000UZ, 0x20000UZ, 0x40000UZ}) {
        auto mapper = make_board(0x40000U, size);
        set_bank(*mapper, 2U, 0xFEU);
        set_bank(*mapper, 3U, 0xFFU);
        set_bank(*mapper, 4U, 0xFEU);
        set_bank(*mapper, 5U, 0xFFU);
        const std::size_t banks = size / 0x400U;
        for (const Address addr : {0x1000U, 0x1400U, 0x1800U, 0x1C00U}) {
            const unsigned inner = addr == 0x1000U || addr == 0x1800U ? 0xFEU : 0xFFU;
            const unsigned outer = (addr & 0x1000U) != 0U ? 0x100U : 0U;
            REQUIRE(chr_marker(*mapper, addr) == ((inner | outer) % banks));
        }
    }
}

TEST_CASE("Mapper 187 honors MMC3 register address aliases", "[mapper187]") {
    auto mapper = make_board();
    for (unsigned offset = 0U; offset < 0x2000U; offset += 2U) {
        mapper->write_prg(static_cast<Address>(0x8000U + offset), 0xC5U);
        REQUIRE(state(*mapper)[10] == 5U);
        REQUIRE(state(*mapper)[11] == 0xC0U);
        mapper->write_prg(static_cast<Address>(0xA000U + offset),
                          static_cast<Byte>((offset >> 1U) & 1U));
        REQUIRE(state(*mapper)[20] == ((offset & 2U) != 0U
                                           ? static_cast<Byte>(MirrorMode::Horizontal)
                                           : static_cast<Byte>(MirrorMode::Vertical)));
        mapper->write_prg(static_cast<Address>(0xC000U + offset), static_cast<Byte>(offset));
        REQUIRE(state(*mapper)[22] == static_cast<Byte>(offset));
        mapper->write_prg(static_cast<Address>(0xE000U + offset), 0U);
        REQUIRE((state(*mapper)[23] & 6U) == 0U);
    }
    for (Byte index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_prg(0x8000U, index);
        for (unsigned offset = 1U; offset < 0x2000U; offset += 2U) {
            mapper->write_prg(static_cast<Address>(0x8000U + offset), 0xA5U);
            REQUIRE(state(*mapper)[12U + index] == (index < 6U ? 0xA5U : 0x25U));
        }
    }
    for (unsigned offset = 1U; offset < 0x2000U; offset += 2U) {
        mapper->write_prg(static_cast<Address>(0xA000U + offset), static_cast<Byte>(offset));
        REQUIRE(state(*mapper)[24] == (offset & 0xC0U));
        mapper->write_prg(static_cast<Address>(0xC000U + offset), 0U);
        REQUIRE((state(*mapper)[23] & 1U) == 1U);
        mapper->write_prg(static_cast<Address>(0xE000U + offset), 0U);
        REQUIRE((state(*mapper)[23] & 2U) == 2U);
    }
}

TEST_CASE("Mapper 187 protection reads drive only D7 throughout $5000-$5FFF", "[mapper187]") {
    auto mapper = make_board();
    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)));
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == 0x80U);
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) == 0x80U);
    }
    for (const Address addr : {0x4020U, 0x4FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    MapperRegistry::instance().register_mapper(
        187U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!Mapper187::validate_geometry(header, prg.size(), chr.size()))
                return {};
            return std::make_unique<Mapper187>(header, std::move(prg), std::move(chr));
        });
    std::vector<Byte> image(16U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 16U;
    image[5] = 32U;
    image[6] = 0xB1U;
    image[7] = 0xB0U;
    const auto prg = make_prg(0x40000U);
    const auto chr = make_chr(0x40000U);
    image.insert(image.end(), prg.begin(), prg.end());
    image.insert(image.end(), chr.begin(), chr.end());
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    bus.write(0U, 0x35U);
    REQUIRE(bus.read(0x5000U) == 0xB5U);
    bus.write(0U, 0x7FU);
    REQUIRE(bus.read(0x5FFFU) == 0xFFU);
}

TEST_CASE("Mapper 187 MMC3 IRQ reload enable acknowledge and repeating zero latch replay",
          "[mapper187][irq]") {
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

TEST_CASE("Mapper 187 reset and power-on restore all documented writable state", "[mapper187]") {
    auto mapper = make_board(0x40000U, 0x40000U, false, false);
    set_bank(*mapper, 0U, 0xFEU, 0xC0U);
    set_bank(*mapper, 6U, 0x3FU, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_expansion(0x5000U, 0xBFU);
    REQUIRE(mapper->irq_pending());

    const auto fresh = state(*make_board(0x40000U, 0x40000U, false, false));
    mapper->reset();
    REQUIRE(state(*mapper) == fresh);
    mapper->write_expansion(0x5000U, 0xA5U);
    mapper->power_on();
    REQUIRE(state(*mapper) == fresh);
}

TEST_CASE("Mapper 187 state replays active banks override and partial IRQ without ROM",
          "[mapper187][state]") {
    auto mapper = make_board();
    set_bank(*mapper, 0U, 0xFEU, 0xC0U);
    set_bank(*mapper, 2U, 130U, 0xC0U);
    set_bank(*mapper, 6U, 17U, 0xC0U);
    set_bank(*mapper, 7U, 11U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0x40U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->write_expansion(0x5000U, 0xA3U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 26U);

    auto copy = make_board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == 4U);
    REQUIRE(copy->read_prg(0xE000U) == 7U);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(chr_marker(*copy, 0U) == 130U);
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    StateReader active(saved);
    copy->load_state(active);
    REQUIRE(active.ok());
    copy->write_expansion(0x5000U, 0U);
    REQUIRE(copy->read_prg(0x8000U) == 30U);
    REQUIRE(copy->read_prg(0xC000U) == 17U);
}

TEST_CASE("Mapper 187 malformed state rejection leaves all mapper state unchanged",
          "[mapper187][state]") {
    auto mapper = make_board();
    set_bank(*mapper, 1U, 0x55U, 0xC0U);
    set_bank(*mapper, 6U, 0x23U, 0xC0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_expansion(0x5000U, 0xB5U);
    const auto saved = state(*mapper);
    const auto reject = [&mapper, &saved](std::span<const Byte> candidate) {
        StateReader reader(candidate);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length)
        reject(std::span(saved).first(length));
    const std::array<std::pair<std::size_t, Byte>, 14> invalid{{
        {0U, 1U},
        {2U, 1U},
        {6U, 1U},
        {10U, 8U},
        {11U, 1U},
        {18U, 0x40U},
        {19U, 0x40U},
        {20U, 2U},
        {23U, 8U},
        {23U, 5U},
        {24U, 1U},
        {25U, 0x40U},
        {0U, 0xBCU},
        {1U, 1U},
    }};
    for (const auto [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        reject(bad);
    }
    for (const bool vertical : {false, true}) {
        auto other = make_board(0x80000U, 0x80000U, false, vertical);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

TEST_CASE("Mapper 187 validates decoder geometry and rejects unsupported cartridge memory",
          "[mapper187]") {
    for (const bool legacy : {false, true}) {
        for (const bool vertical : {false, true}) {
            for (const std::size_t prg_size : {0x8000UZ, 0xA000UZ, 0x40000UZ, 0x80000UZ}) {
                for (const std::size_t chr_size : {0x2000UZ, 0x2C00UZ, 0x40000UZ, 0x80000UZ})
                    REQUIRE(Mapper187::validate_geometry(
                        make_header(legacy, vertical), prg_size, chr_size));
            }
        }
    }
    for (const std::size_t size : {0UZ, 0x2000UZ, 0x4000UZ, 0x6000UZ, 0x8001UZ, 0xA0000UZ})
        REQUIRE_FALSE(Mapper187::validate_geometry(make_header(), size, 0x40000U));
    for (const std::size_t size : {0UZ, 0x400UZ, 0x1000UZ, 0x2001UZ, 0x80400UZ, 0x100000UZ})
        REQUIRE_FALSE(Mapper187::validate_geometry(make_header(), 0x40000U, size));

    for (unsigned kind = 0U; kind < 9U; ++kind) {
        auto header = make_header();
        switch (kind) {
        case 0U:
            header.mapper_number = 121U;
            break;
        case 1U:
            header.submapper = 1U;
            break;
        case 2U:
            header.has_battery = true;
            break;
        case 3U:
            header.prg_ram_size = 0x2000U;
            break;
        case 4U:
            header.prg_nvram_size = 0x2000U;
            break;
        case 5U:
            header.chr_ram_size = 0x2000U;
            break;
        case 6U:
            header.chr_nvram_size = 0x2000U;
            break;
        case 7U:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            header.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        REQUIRE_FALSE(Mapper187::validate_geometry(header, 0x40000U, 0x40000U));
    }
}

} // namespace
