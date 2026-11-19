#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper291.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 256U * 1024U;
constexpr std::size_t kChrSize = 512U * 1024U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 291U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / 8192U * 7U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = pattern(offset);
    return bytes;
}

std::unique_ptr<Mapper291> board(bool vertical = true) {
    REQUIRE(Mapper291::validate_geometry(header(vertical), kPrgSize, kChrSize));
    return std::make_unique<Mapper291>(header(vertical), rom(kPrgSize), rom(kChrSize));
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

TEST_CASE("Mapper 291 validates its exact ROM population and metadata", "[mapper291]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(vertical);
        REQUIRE(state(*mapper).size() == 25U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }

    auto invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper291::validate_geometry(invalid, kPrgSize, kChrSize));
    invalid = header();
    invalid.mapper_number = 47U;
    REQUIRE_FALSE(Mapper291::validate_geometry(invalid, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper291::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    for (std::size_t size : {0UZ, 32768UZ, 131072UZ, 262143UZ, 262145UZ, 524288UZ}) {
        REQUIRE_FALSE(Mapper291::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size : {0UZ, 8192UZ, 262144UZ, 524287UZ, 524289UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper291::validate_geometry(header(), kPrgSize, size));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 8192U;
            break;
        case 2U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3U:
            invalid.chr_ram_size = 8192U;
            break;
        case 4U:
            invalid.chr_nvram_size = 8192U;
            break;
        default:
            FAIL("unexpected field");
        }
        REQUIRE_FALSE(Mapper291::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(Mapper291::validate_geometry(invalid, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 291 outer aliases ignore WRAM gates and retain unknown bit seven",
          "[mapper291]") {
    auto mapper = board();
    const auto image = rom(kPrgSize);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(0xA001U, static_cast<Byte>(addr & 0xC0U));
        mapper->write_prg(addr, 0x26U);
        REQUIRE(mapper->read_prg(0x8000U) == image[3U * 32768U]);
        REQUIRE(mapper->read_prg(0xFFFFU) == image[4U * 32768U - 1U]);
    }

    for (Byte control : {0x00U, 0x40U, 0x80U, 0xC0U}) {
        mapper->write_prg(0xA001U, control);
        mapper->write_prg(0x6000U, 0x66U);
        const auto with_unknown_clear = state(*mapper);
        mapper->write_prg(0x7FFFU, 0xE6U);
        const auto with_unknown_set = state(*mapper);
        REQUIRE(with_unknown_clear[10U] == 0x66U);
        REQUIRE(with_unknown_set[10U] == 0xE6U);
        for (Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
            REQUIRE(mapper->read_prg(addr) == image[0x20000U + 3U * 32768U + (addr & 0x7FFFU)]);
        }
    }
}

TEST_CASE("Mapper 291 maps every MMC3 PRG bank and fixed window in both outer blocks",
          "[mapper291]") {
    auto mapper = board();
    const auto image = rom(kPrgSize);
    for (Byte outer : {0x00U, 0x40U}) {
        mapper->write_prg(0x6000U, outer);
        const std::size_t outer_offset = outer == 0U ? 0U : 0x20000U;
        for (Byte mode : {0x00U, 0x40U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                set_bank(*mapper, 6U, static_cast<Byte>(value), mode);
                set_bank(*mapper, 7U, static_cast<Byte>(255U - value), mode);
                const std::array<unsigned, 4> banks{
                    mode == 0U ? value & 15U : 14U,
                    (255U - value) & 15U,
                    mode == 0U ? 14U : value & 15U,
                    15U,
                };
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const Address addr = static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                    REQUIRE(mapper->read_prg(addr) ==
                            image[outer_offset + banks[slot] * 8192U + 0x123U]);
                }
            }
        }
    }
}

TEST_CASE("Mapper 291 32 KiB override covers every inner page and CPU byte", "[mapper291]") {
    auto mapper = board();
    const auto image = rom(kPrgSize);
    set_bank(*mapper, 6U, 0x0BU, 0x40U);
    set_bank(*mapper, 7U, 0x07U, 0x40U);
    for (Byte outer : {0x00U, 0x40U}) {
        for (unsigned inner = 0U; inner < 4U; ++inner) {
            mapper->write_prg(0x7000U,
                              static_cast<Byte>(0x20U | outer | static_cast<Byte>(inner << 1U)));
            const std::size_t base = (outer == 0U ? 0U : 0x20000U) + inner * 32768U;
            for (unsigned offset = 0U; offset < 32768U; ++offset) {
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                        image[base + offset]);
            }
        }
    }
}

TEST_CASE("Mapper 291 maps complete CHR windows with pairing inversion and outer selection",
          "[mapper291]") {
    auto mapper = board();
    const auto image = rom(kChrSize);
    constexpr std::array<Byte, 6> values{0x03U, 0xFEU, 0x7FU, 0x80U, 0xC1U, 0xFFU};
    for (Byte outer : {0x00U, 0x40U}) {
        mapper->write_prg(0x6000U, outer);
        const std::size_t outer_offset = outer == 0U ? 0U : 0x40000U;
        for (Byte inversion : {0x00U, 0x80U}) {
            for (Byte index = 0U; index < values.size(); ++index)
                set_bank(*mapper, index, values[index], inversion);
            constexpr std::array<unsigned, 8> pages{
                0x02U, 0x03U, 0xFEU, 0xFFU, 0x7FU, 0x80U, 0xC1U, 0xFFU};
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const unsigned slot = (addr / 1024U) ^ (inversion == 0U ? 0U : 4U);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        image[outer_offset + pages[slot] * 1024U + (addr & 1023U)]);
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xFFU));
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        image[outer_offset + pages[slot] * 1024U + (addr & 1023U)]);
            }
        }
    }

    mapper->write_prg(0x6000U, 0U);
    for (Byte index = 0U; index < 6U; ++index) {
        for (unsigned value = 0U; value < 256U; ++value) {
            set_bank(*mapper, index, static_cast<Byte>(value));
            const Address addr = index < 2U ? static_cast<Address>(index * 2048U)
                                            : static_cast<Address>(0x1000U + (index - 2U) * 1024U);
            const unsigned bank = index < 2U ? value & 0xFEU : value;
            REQUIRE(mapper->read_chr(addr) == image[bank * 1024U]);
        }
    }
}

TEST_CASE("Mapper 291 nametable control uses all MMC3 aliases", "[mapper291]") {
    for (bool initial_vertical : {false, true}) {
        auto mapper = board(initial_vertical);
        for (Address addr = 0xA000U; addr < 0xC000U; addr += 2U) {
            mapper->write_prg(addr, 0U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            mapper->write_prg(addr, 1U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            mapper->write_prg(static_cast<Address>(addr + 1U), 0xFFU);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        }
        mapper->reset();
        REQUIRE(mapper->mirror_mode() ==
                (initial_vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
}

TEST_CASE("Mapper 291 low CPU window floats and no storage is exposed", "[mapper291]") {
    auto mapper = board();
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x6001U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
    }
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    for (Address addr = 0x8000U; addr != 0U; addr += 0x2000U)
        REQUIRE(mapper->maps_prg(addr));
}

TEST_CASE("Mapper 291 reset restores deterministic defaults without changing ROM", "[mapper291]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(vertical);
        const auto image = rom(kPrgSize);
        mapper->write_prg(0x6000U, 0xE6U);
        set_bank(*mapper, 6U, 0x3FU, 0xC0U);
        mapper->write_prg(0xA000U, static_cast<Byte>(vertical ? 0U : 1U));
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->reset();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(mapper->read_prg(0x8000U) == image[0U]);
        REQUIRE(mapper->read_prg(0xA000U) == image[0U]);
        REQUIRE(mapper->read_prg(0xC000U) == image[14U * 8192U]);
        REQUIRE(mapper->read_prg(0xE000U) == image[15U * 8192U]);
        mapper->write_prg(0x6000U, 0x66U);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x8000U) == image[0U]);
    }
}

TEST_CASE("Mapper 291 clocks acknowledges and replays the MMC3 IRQ counter", "[mapper291]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    StateReader reload(partial);
    mapper->load_state(reload);
    REQUIRE(reload.ok());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 291 fixed states replay without ROM and reject corruption atomically",
          "[mapper291][state]") {
    auto mapper = board();
    auto copy = board();
    mapper->write_prg(0x6000U, 0xC0U);
    for (Byte index = 0U; index < 8U; ++index)
        set_bank(*mapper, index, static_cast<Byte>(index * 29U + 7U), 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 25U);

    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0x9234U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU})
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    for (Address addr = 0U; addr < 8192U; addr += 37U)
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated{std::span(saved).first(length)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 10U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 6> invalid_fields{
        {{11U, 0x08U}, {18U, 0x40U}, {19U, 0x40U}, {20U, 2U}, {23U, 4U}, {24U, 1U}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto different_wiring = board(false);
    const auto before = state(*different_wiring);
    StateReader mismatched(saved);
    different_wiring->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*different_wiring) == before);
}

} // namespace
