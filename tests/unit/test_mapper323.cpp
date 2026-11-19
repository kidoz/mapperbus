#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper323.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::size_t kChrSize = 1024U * 1024U;
constexpr std::size_t kOuterSize = 128U * 1024U;
constexpr std::size_t kStateSize = 17U;

INesHeader header(bool nes2 = true) {
    INesHeader result{};
    result.mapper_number = 323U;
    result.is_nes2 = nes2;
    result.prg_ram_size = nes2 ? 0U : 8192U;
    result.mirror_mode = MirrorMode::Horizontal;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 7U) * 13U + (offset >> 12U) * 29U +
                             (offset >> 17U) * 47U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = pattern(offset);
    return bytes;
}

std::unique_ptr<FaridSlrom323> board(bool nes2 = true) {
    const auto h = header(nes2);
    REQUIRE(FaridSlrom323::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<FaridSlrom323>(h, rom(kPrgSize), rom(kChrSize));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_serial(Mapper& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
}

std::size_t expected_prg_offset(Byte control, Byte prg, Byte outer, Address addr) {
    const auto selected = static_cast<unsigned>(prg & 7U);
    unsigned bank = 0U;
    switch ((control >> 2U) & 3U) {
    case 0U:
    case 1U:
        bank = (selected & 6U) | (addr >= 0xC000U ? 1U : 0U);
        break;
    case 2U:
        bank = addr < 0xC000U ? 0U : selected;
        break;
    default:
        bank = addr < 0xC000U ? selected : 7U;
        break;
    }
    return static_cast<std::size_t>((outer >> 4U) & 7U) * kOuterSize + bank * 16384U +
           (addr & 0x3FFFU);
}

std::size_t expected_chr_offset(Byte control, Byte chr0, Byte chr1, Byte outer, Address addr) {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0U;
    if ((control & 0x10U) != 0U) {
        bank = upper ? chr1 : chr0;
    } else {
        bank = chr0 & 0x1EU;
        if (upper)
            ++bank;
    }
    return static_cast<std::size_t>((outer >> 4U) & 7U) * kOuterSize + bank * 4096U +
           (addr & 0x0FFFU);
}

TEST_CASE("Mapper 323 accepts only the documented ROM and memory population", "[mapper323]") {
    for (const bool nes2 : {false, true}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto h = header(nes2);
            h.mirror_mode = mirror;
            REQUIRE(FaridSlrom323::validate_geometry(h, kPrgSize, kChrSize));
        }
        auto mapper = board(nes2);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const std::size_t size :
         {0UZ, 8192UZ, 32768UZ, 131072UZ, 524288UZ, 1048575UZ, 1048577UZ, 2097152UZ, 3145728UZ}) {
        REQUIRE_FALSE(FaridSlrom323::validate_geometry(header(), size, kChrSize));
        REQUIRE_FALSE(FaridSlrom323::validate_geometry(header(), kPrgSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(FaridSlrom323::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(FaridSlrom323::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(FaridSlrom323::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header(false);
    legacy.has_battery = true;
    REQUIRE_FALSE(FaridSlrom323::validate_geometry(legacy, kPrgSize, kChrSize));
    legacy = header(false);
    legacy.chr_ram_size = 8192U;
    REQUIRE_FALSE(FaridSlrom323::validate_geometry(legacy, kPrgSize, kChrSize));
}

TEST_CASE("Mapper 323 decodes every outer-register alias and ignores unrelated writes",
          "[mapper323]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x6000U; raw_addr < 0x8000U; ++raw_addr) {
        const auto value = static_cast<Byte>((raw_addr * 7U) & 0x70U);
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        CAPTURE(raw_addr, value);
        REQUIRE(mapper->read_prg(0x8000U) ==
                pattern(expected_prg_offset(0x0CU, 0U, value, 0x8000U)));
        REQUIRE(mapper->read_prg(0xFFFFU) ==
                pattern(expected_prg_offset(0x0CU, 0U, value, 0xFFFFU)));
        REQUIRE(mapper->read_chr(0x0000U) ==
                pattern(expected_chr_offset(0x0CU, 0U, 0U, value, 0U)));
        REQUIRE(mapper->read_chr(0x1FFFU) ==
                pattern(expected_chr_offset(0x0CU, 0U, 0U, value, 0x1FFFU)));
    }
    const auto before = state(*mapper);
    for (unsigned raw_addr = 0U; raw_addr < 0x6000U; raw_addr += 257U)
        mapper->write_prg(static_cast<Address>(raw_addr), static_cast<Byte>(raw_addr));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 323 outer lock and MMC1 WRAM disable qualify writes independently",
          "[mapper323]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x78U);
    for (const Byte value : {0x00U, 0x10U, 0x70U, 0xF8U, 0xFFU}) {
        mapper->write_prg(0x7FFFU, value);
        REQUIRE(mapper->read_prg(0x8000U) ==
                pattern(expected_prg_offset(0x0CU, 0U, 0x78U, 0x8000U)));
        REQUIRE(mapper->read_chr(0x0000U) ==
                pattern(expected_chr_offset(0x0CU, 0U, 0U, 0x78U, 0U)));
    }

    write_serial(*mapper, 0xE000U, 0x10U);
    mapper->reset();
    mapper->write_prg(0x6123U, 0x50U);
    REQUIRE(mapper->read_prg(0x8000U) == pattern(expected_prg_offset(0x0CU, 0x10U, 0U, 0x8000U)));
    write_serial(*mapper, 0xFFFFU, 0x00U);
    mapper->write_prg_cpu(0x7456U, 0x20U, true);
    REQUIRE(mapper->read_prg(0x8000U) == pattern(expected_prg_offset(0x0CU, 0U, 0x20U, 0x8000U)));
    write_serial(*mapper, 0xE000U, 0x10U);
    mapper->write_prg(0x6000U, 0x60U);
    REQUIRE(mapper->read_prg(0x8000U) ==
            pattern(expected_prg_offset(0x0CU, 0x10U, 0x20U, 0x8000U)));
}

TEST_CASE("Mapper 323 applies all MMC1 PRG modes within each 128 KiB outer bank", "[mapper323]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 0x80U; outer += 0x10U) {
        mapper->reset();
        write_serial(*mapper, 0xE000U, 0U);
        mapper->write_prg(0x6000U, outer);
        for (Byte mode = 0U; mode < 4U; ++mode) {
            const auto control = static_cast<Byte>(mode << 2U);
            write_serial(*mapper, 0x8000U, control);
            for (Byte prg = 0U; prg < 32U; ++prg) {
                write_serial(*mapper, 0xE000U, prg);
                for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
                    CAPTURE(outer, control, prg, addr);
                    REQUIRE(mapper->read_prg(addr) ==
                            pattern(expected_prg_offset(control, prg, outer, addr)));
                }
            }
        }
    }
}

TEST_CASE("Mapper 323 applies all MMC1 CHR modes and protects every ROM cell", "[mapper323]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 0x80U; outer += 0x10U) {
        mapper->reset();
        mapper->write_prg(0x6000U, outer);
        for (const Byte control : {0x0CU, 0x1CU}) {
            write_serial(*mapper, 0x8000U, control);
            for (Byte chr0 = 0U; chr0 < 32U; ++chr0) {
                const auto chr1 = static_cast<Byte>(31U - chr0);
                write_serial(*mapper, 0xA000U, chr0);
                write_serial(*mapper, 0xC000U, chr1);
                for (const Address addr : {0x0000U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFFU}) {
                    CAPTURE(outer, control, chr0, chr1, addr);
                    const auto expected =
                        pattern(expected_chr_offset(control, chr0, chr1, outer, addr));
                    REQUIRE(mapper->read_chr(addr) == expected);
                    mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == expected);
                }
            }
        }
    }
}

TEST_CASE("Mapper 323 MMC1 control selects all four CIRAM arrangements", "[mapper323]") {
    auto mapper = board();
    constexpr MirrorMode expected[] = {MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper,
                                       MirrorMode::Vertical,
                                       MirrorMode::Horizontal};
    for (Byte control = 0U; control < 32U; ++control) {
        write_serial(*mapper, 0x9FFFU, control);
        REQUIRE(mapper->mirror_mode() == expected[control & 3U]);
    }
}

TEST_CASE("Mapper 323 serial port honors fifth-write decode, reset and cycle filtering",
          "[mapper323]") {
    auto mapper = board();
    write_serial(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(state(*mapper)[12U] == 0x0CU);
    REQUIRE(state(*mapper)[15U] == 0x15U);

    for (unsigned bit = 0U; bit < 4U; ++bit)
        mapper->write_prg(static_cast<Address>(0x8000U + bit * 0x2000U), 1U);
    mapper->write_prg(0xC000U, 0U);
    REQUIRE(state(*mapper)[14U] == 0x0FU);
    REQUIRE(state(*mapper)[11U] == 0U);

    mapper->write_prg_cpu(0xA000U, 1U, false);
    const auto filtered = state(*mapper);
    mapper->write_prg_cpu(0xE000U, 0U, true);
    REQUIRE(state(*mapper) == filtered);
    mapper->write_prg_cpu(0xE000U, 0x80U, true);
    REQUIRE(state(*mapper)[10U] == 0x10U);
    REQUIRE(state(*mapper)[11U] == 0U);
    REQUIRE(state(*mapper)[12U] == 0x0CU);
    REQUIRE(state(*mapper)[14U] == 0x0FU);
}

TEST_CASE("Mapper 323 warm reset clears only outer state and cold power clears MMC1 state",
          "[mapper323]") {
    auto mapper = board();
    write_serial(*mapper, 0x8000U, 0x01U);
    write_serial(*mapper, 0xA000U, 0x12U);
    write_serial(*mapper, 0xC000U, 0x0DU);
    write_serial(*mapper, 0xE000U, 0x07U);
    mapper->write_prg(0x6000U, 0x78U);
    mapper->write_prg(0x8000U, 1U);

    mapper->reset();
    auto current = state(*mapper);
    REQUIRE(current[10U] == 0x10U);
    REQUIRE(current[11U] == 0U);
    REQUIRE(current[12U] == 0x0DU);
    REQUIRE(current[13U] == 0x12U);
    REQUIRE(current[14U] == 0x0DU);
    REQUIRE(current[15U] == 0x07U);
    REQUIRE(current[16U] == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == pattern(expected_prg_offset(0x0DU, 7U, 0U, 0xC000U)));

    mapper->write_prg(0x6000U, 0x60U);
    mapper->power_on();
    current = state(*mapper);
    REQUIRE(current[12U] == 0x0CU);
    REQUIRE(current[13U] == 0U);
    REQUIRE(current[14U] == 0U);
    REQUIRE(current[15U] == 0U);
    REQUIRE(current[16U] == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == pattern(7U * 16384U));
    REQUIRE(mapper->read_chr(0x1000U) == pattern(4096U));
}

TEST_CASE("Mapper 323 leaves low CPU reads floating and owns no persistence or peripherals",
          "[mapper323]") {
    auto mapper = board();
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    mapper->clock_irq_counter();
    mapper->clock_audio();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 323 snapshots replay all mutable state without ROM bytes", "[mapper323][state]") {
    auto mapper = board();
    write_serial(*mapper, 0x8000U, 0x11U);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x1AU);
    write_serial(*mapper, 0xE000U, 0x05U);
    mapper->write_prg(0x6000U, 0x58U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0x8000U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU})
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    for (const Address addr : {0x0000U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFFU})
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
}

TEST_CASE("Mapper 323 rejects truncated and invalid states without mutation",
          "[mapper323][state]") {
    auto mapper = board();
    write_serial(*mapper, 0x8000U, 0x12U);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x14U);
    write_serial(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0x6000U, 0x58U);
    mapper->write_prg(0x8000U, 1U);
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
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
    for (const Byte value : {0x00U, 0x01U, 0x1FU, 0x20U, 0xFFU}) {
        auto invalid = saved;
        invalid[10U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const Byte value : {2U, 4U, 5U, 0xFFU}) {
        auto invalid = saved;
        invalid[11U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 12U; offset < 16U; ++offset) {
        for (const Byte value : {0x20U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            StateReader reader(invalid);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
    }
    for (const Byte value : {0x01U, 0x04U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[16U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
}

} // namespace
