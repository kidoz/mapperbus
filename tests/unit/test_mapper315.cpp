#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper315.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 512U * 1024U;
constexpr std::size_t kChrSize = 512U * 1024U;
constexpr std::size_t kStateSize = 25U;
constexpr std::size_t kOuterStateOffset = 20U;
constexpr std::size_t kCounterStateOffset = 23U;
constexpr std::array<Address, 4> kPrgAddresses{0x8123U, 0xA234U, 0xC345U, 0xE456U};

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 315U;
    result.is_nes2 = true;
    result.mirror_mode = MirrorMode::Vertical;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 10U) * 29U +
                             (offset >> 13U) * 37U + (offset >> 17U) * 19U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = pattern(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper315> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    auto h = header();
    h.mirror_mode = mirror;
    REQUIRE(Mapper315::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper315>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_bank(Mapper315& mapper, unsigned index, Byte value, bool inverted = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>((inverted ? 0x80U : 0U) | index));
    mapper.write_prg(0x8001U, value);
}

void write_outer(Mapper315& mapper, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(0x6800U, value);
}

Byte normalized_bank(unsigned index, Byte value) {
    if (index < 2U) {
        return value & 0xFEU;
    }
    if (index >= 6U) {
        return value & 0x3FU;
    }
    return value;
}

Byte expected_prg_bank(Byte outer,
                       Byte select,
                       const std::array<Byte, 8>& registers,
                       Address addr) {
    const unsigned slot = (addr >> 13U) & 3U;
    const Byte r6 = registers[6U] & 0x3FU;
    const Byte r7 = registers[7U] & 0x3FU;
    const bool prg_mode = (select & 0x40U) != 0U;
    std::array<Byte, 4> inner{};

    if ((outer & 0x06U) == 0x06U) {
        const Byte first = prg_mode ? 14U : r6;
        inner = {static_cast<Byte>(first & 0xFDU),
                 static_cast<Byte>(r7 & 0xFDU),
                 static_cast<Byte>(first | 0x02U),
                 static_cast<Byte>(r7 | 0x02U)};
    } else if (prg_mode) {
        inner = {14U, r7, r6, 15U};
    } else {
        inner = {r6, r7, 14U, 15U};
    }
    return static_cast<Byte>((inner[slot] & 0x0FU) | ((outer & 0x06U) << 3U));
}

unsigned expected_chr_bank(Byte outer,
                           bool inverted,
                           const std::array<Byte, 8>& registers,
                           Address addr) {
    std::array<Byte, 8> slots{};
    if (inverted) {
        slots = {registers[2U],
                 registers[3U],
                 registers[4U],
                 registers[5U],
                 static_cast<Byte>(registers[0U] & 0xFEU),
                 static_cast<Byte>(registers[0U] | 1U),
                 static_cast<Byte>(registers[1U] & 0xFEU),
                 static_cast<Byte>(registers[1U] | 1U)};
    } else {
        slots = {static_cast<Byte>(registers[0U] & 0xFEU),
                 static_cast<Byte>(registers[0U] | 1U),
                 static_cast<Byte>(registers[1U] & 0xFEU),
                 static_cast<Byte>(registers[1U] | 1U),
                 registers[2U],
                 registers[3U],
                 registers[4U],
                 registers[5U]};
    }

    unsigned bank = slots[(addr >> 10U) & 7U];
    if ((outer & 0x08U) != 0U) {
        bank |= 0x40U;
    }
    if ((outer & 0x04U) != 0U) {
        bank |= 0x80U;
    }
    if ((outer & 0x01U) != 0U) {
        bank |= 0x100U;
    }
    return bank;
}

TEST_CASE("Mapper 315 validates mapper, submapper, ROM, memory, and CIRAM geometry",
          "[mapper315]") {
    for (std::size_t prg_size = 32U * 1024U; prg_size <= kPrgSize; prg_size *= 2U) {
        for (std::size_t chr_size = 8U * 1024U; chr_size <= kChrSize; chr_size *= 2U) {
            for (auto mirror :
                 {MirrorMode::Horizontal, MirrorMode::Vertical, MirrorMode::FourScreen}) {
                auto h = header();
                h.mirror_mode = mirror;
                REQUIRE(Mapper315::validate_geometry(h, prg_size, chr_size));
            }
        }
    }

    auto wrong_id = header();
    wrong_id.mapper_number = 4U;
    REQUIRE_FALSE(Mapper315::validate_geometry(wrong_id, kPrgSize, kChrSize));
    auto legacy = header();
    legacy.is_nes2 = false;
    legacy.prg_ram_size = 8192U;
    REQUIRE_FALSE(Mapper315::validate_geometry(legacy, kPrgSize, kChrSize));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper315::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16384UZ, 24576UZ, 32767UZ, 32769UZ, 49152UZ, 786432UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper315::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size :
         {0UZ, 1UZ, 1024UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 24576UZ, 786432UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper315::validate_geometry(header(), kPrgSize, size));
    }
    for (auto mirror : {MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper315::validate_geometry(h, kPrgSize, kChrSize));
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
        REQUIRE_FALSE(Mapper315::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 315 replaces PRG outer pins and applies GNROM-like banking",
          "[mapper315][banking]") {
    auto mapper = board();
    std::array<Byte, 8> registers{};
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        write_outer(*mapper, static_cast<Byte>(outer));
        for (Byte select : {0x00U, 0x40U, 0x80U, 0xC0U}) {
            for (unsigned r6 = 0U; r6 < 64U; ++r6) {
                const auto raw_r6 = static_cast<Byte>(r6 | ((r6 & 3U) << 6U));
                mapper->write_prg(0x8000U, static_cast<Byte>(select | 6U));
                mapper->write_prg(0x8001U, raw_r6);
                registers[6U] = normalized_bank(6U, raw_r6);
                for (unsigned r7 = 0U; r7 < 64U; ++r7) {
                    const auto raw_r7 = static_cast<Byte>(r7 | ((r7 & 3U) << 6U));
                    mapper->write_prg(0x8000U, static_cast<Byte>(select | 7U));
                    mapper->write_prg(0x8001U, raw_r7);
                    registers[7U] = normalized_bank(7U, raw_r7);
                    for (const auto addr : kPrgAddresses) {
                        const auto bank =
                            expected_prg_bank(static_cast<Byte>(outer), select, registers, addr);
                        const std::size_t offset =
                            (static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU)) % kPrgSize;
                        CAPTURE(outer, select, r6, r7, addr);
                        REQUIRE(mapper->read_prg(addr) == pattern(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 315 applies CHR pairing, inversion, and outer OR pins", "[mapper315][banking]") {
    auto mapper = board();
    std::array<Byte, 8> registers{};
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        write_outer(*mapper, static_cast<Byte>(outer));
        for (bool inverted : {false, true}) {
            for (unsigned index = 0U; index < 8U; ++index) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    const auto value = static_cast<Byte>(raw);
                    write_bank(*mapper, index, value, inverted);
                    registers[index] = normalized_bank(index, value);
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * 1024U + 0x155U);
                        const auto bank =
                            expected_chr_bank(static_cast<Byte>(outer), inverted, registers, addr);
                        const std::size_t offset =
                            (static_cast<std::size_t>(bank) * 1024U + (addr & 0x3FFU)) % kChrSize;
                        CAPTURE(outer, inverted, index, raw, slot);
                        REQUIRE(mapper->read_chr(addr) == pattern(offset));
                    }
                }
            }
        }
    }

    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * 1024U + 0x155U);
        const auto original = mapper->read_chr(addr);
        mapper->write_chr(addr, original ^ 0xFFU);
        REQUIRE(mapper->read_chr(addr) == original);
    }
}

TEST_CASE("Mapper 315 decodes all MMC3 and outer-register aliases identically",
          "[mapper315][aliases]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto canonical = static_cast<Address>(addr & 0xE001U);
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U));
        actual->write_prg(addr, value);
        expected->write_prg(canonical, value);
        CAPTURE(raw, canonical, value);
        REQUIRE(state(*actual) == state(*expected));
    }

    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        actual->reset();
        expected->reset();
        write_outer(*actual, 3U);
        write_outer(*expected, 3U);
        const auto value = static_cast<Byte>(raw * 17U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(0x6800U, value);
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 315 outer writes require enabled, unprotected A001 gates",
          "[mapper315][openbus]") {
    auto mapper = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        mapper->reset();
        write_outer(*mapper, 3U);
        mapper->write_prg(0xA001U, static_cast<Byte>(gate));
        mapper->write_prg(0x6800U, 0x0DU);
        const bool writable = (gate & 0xC0U) == 0x80U;
        CAPTURE(gate);
        REQUIRE(state(*mapper)[kOuterStateOffset] == (writable ? 0x0DU : 0x03U));
    }

    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x6800U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 315 CIRAM control and deterministic reset preserve documented wiring",
          "[mapper315][reset]") {
    auto mapper = board();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xBFFEU, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U).has_value());

    write_outer(*mapper, 0x0FU);
    for (unsigned index = 0U; index < 8U; ++index) {
        write_bank(*mapper, index, static_cast<Byte>(0xF0U | index), true);
    }
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    auto fresh = board();
    mapper->reset();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x6800U, 0x0FU);
    REQUIRE(state(*mapper) == state(*fresh));

    write_outer(*mapper, 0x0FU);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));

    auto four_screen = board(kPrgSize, kChrSize, MirrorMode::FourScreen);
    four_screen->write_prg(0xA000U, 0U);
    REQUIRE(four_screen->mirror_mode() == MirrorMode::FourScreen);
    four_screen->write_prg(0xA000U, 1U);
    REQUIRE(four_screen->mirror_mode() == MirrorMode::FourScreen);
}

TEST_CASE("Mapper 315 MMC3 IRQ latch, reload, acknowledgement, and zero repetition",
          "[mapper315][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0xA5U);
    mapper->write_prg(0xE001U, 0U);

    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kCounterStateOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kCounterStateOffset] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kCounterStateOffset] == 0U);
    REQUIRE(mapper->irq_pending());

    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kCounterStateOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 315 snapshots replay bounded mutable state without ROM", "[mapper315][state]") {
    auto mapper = board();
    const std::array<Byte, 8> values{3U, 5U, 6U, 7U, 8U, 9U, 10U, 11U};
    for (unsigned index = 0U; index < values.size(); ++index) {
        write_bank(*mapper, index, values[index], true);
    }
    write_outer(*mapper, 0x0DU);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending() == mapper->irq_pending());
    for (const auto addr : kPrgAddresses) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * 1024U + 0x2A5U);
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }

    auto other_geometry = board(256U * 1024U, kChrSize);
    const auto before = state(*other_geometry);
    StateReader mismatch(saved);
    other_geometry->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_geometry) == before);

    auto other_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader mirror_mismatch(saved);
    other_mirror->load_state(mirror_mismatch);
    REQUIRE_FALSE(mirror_mismatch.ok());
    REQUIRE(state(*other_mirror) == mirror_before);
}

TEST_CASE("Mapper 315 rejects truncated and invalid states without mutation",
          "[mapper315][state]") {
    auto mapper = board();
    const std::array<Byte, 8> values{2U, 4U, 6U, 7U, 8U, 9U, 10U, 11U};
    for (unsigned index = 0U; index < values.size(); ++index) {
        write_bank(*mapper, index, values[index], true);
    }
    write_outer(*mapper, 0x0EU);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    const auto reject = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        CAPTURE(offset, value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    reject(0U, saved[0U] ^ 1U);
    reject(1U, saved[1U] ^ 1U);
    for (std::size_t offset = 2U; offset < 10U; ++offset) {
        reject(offset, saved[offset] ^ 1U);
    }
    reject(10U, 0U);
    reject(10U, 0xFFU);
    for (Byte value : {0x08U, 0x20U, 0x38U, 0xFFU}) {
        reject(11U, value);
    }
    reject(12U, 3U);
    reject(13U, 5U);
    reject(18U, 0x40U);
    reject(19U, 0xFFU);
    for (Byte value : {0x10U, 0x80U, 0xFFU}) {
        reject(20U, value);
    }
    for (Byte value : {3U, 4U, 0xFFU}) {
        reject(21U, value);
    }
    for (Byte value : {0x20U, 0x40U, 0x80U, 0xFFU}) {
        reject(24U, value);
    }

    for (unsigned outer = 0U; outer < 16U; ++outer) {
        auto valid = saved;
        valid[kOuterStateOffset] = static_cast<Byte>(outer);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
