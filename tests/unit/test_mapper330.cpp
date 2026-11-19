#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper330.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kPrgSize = 256U * 1024U;
constexpr std::size_t kChrSize = 256U * 1024U;
constexpr std::size_t kPrgBanksOffset = 10U;
constexpr std::size_t kChrBanksOffset = 13U;
constexpr std::size_t kNametableBanksOffset = 21U;
constexpr std::size_t kIrqCounterOffset = 25U;
constexpr std::size_t kIrqPendingOffset = 27U;
constexpr std::size_t kIrqLoadOffset = 28U;
constexpr std::size_t kStateSize = 29U + kPrgBankSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 330U;
    result.is_nes2 = true;
    result.has_battery = true;
    result.prg_nvram_size = kPrgBankSize;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 10U) * 37U +
                             (offset >> 18U) * 19U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper330> board(MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper330::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper330>(h, rom(kPrgSize), rom(kChrSize));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 330 accepts only the measured native memory population", "[mapper330]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper330::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }

    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 131072UZ, 262143UZ, 262145UZ, 524288UZ}) {
        REQUIRE_FALSE(Mapper330::validate_geometry(header(), size, kChrSize));
        REQUIRE_FALSE(Mapper330::validate_geometry(header(), kPrgSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper330::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper330::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 19U;
            break;
        case 1U:
            h.mapper_number = 331U;
            break;
        case 2U:
            h.is_nes2 = false;
            break;
        case 3U:
            h.has_battery = false;
            break;
        case 4U:
            h.prg_ram_size = kPrgBankSize;
            break;
        case 5U:
            h.prg_nvram_size = 0U;
            break;
        case 6U:
            h.prg_nvram_size = 4096U;
            break;
        case 7U:
            h.prg_nvram_size = 16384U;
            break;
        case 8U:
            h.chr_ram_size = kPrgBankSize;
            break;
        case 9U:
            h.chr_nvram_size = kPrgBankSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper330::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 330 maps every PRG bank value without bus conflicts", "[mapper330]") {
    auto mapper = board();
    constexpr std::array<Address, 3> registers{0xE000U, 0xE800U, 0xF000U};
    for (unsigned slot = 0U; slot < registers.size(); ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(registers[slot], static_cast<Byte>(value));
            const auto bank = value & 0x1FU;
            for (unsigned offset : {0U, 1U, 0x123U, 0x1FFFU}) {
                const auto addr = static_cast<Address>(0x8000U + slot * kPrgBankSize + offset);
                CAPTURE(slot, value, offset);
                REQUIRE(mapper->read_prg(addr) == cell(bank * kPrgBankSize + offset));
            }
        }
    }
    for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + offset)) ==
                cell(31U * kPrgBankSize + offset));
    }
}

TEST_CASE("Mapper 330 maps every CHR bank value and protects ROM", "[mapper330]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto reg = static_cast<Address>(0x8000U + slot * 0x0800U);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(reg, static_cast<Byte>(value));
            for (unsigned offset : {0U, 1U, 0x234U, 0x3FFU}) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + offset);
                CAPTURE(slot, value, offset);
                REQUIRE(mapper->read_chr(addr) == cell(value * kChrBankSize + offset));
            }
        }
    }
    for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
        mapper->write_prg(0x8000U, static_cast<Byte>(offset));
        const auto before = mapper->read_chr(static_cast<Address>(offset));
        mapper->write_chr(static_cast<Address>(offset), static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == before);
    }
}

TEST_CASE("Mapper 330 decodes every upper CPU address with documented masks", "[mapper330]") {
    auto mapper = board();
    mapper->power_on();
    const auto initial = state(*mapper);
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->power_on();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw_addr);
        constexpr Byte value = 0xA5U;
        mapper->write_prg(addr, value);

        switch (addr & 0xFC00U) {
        case 0x8000U:
        case 0x8800U:
        case 0x9000U:
        case 0x9800U:
        case 0xA000U:
        case 0xA800U:
        case 0xB000U:
        case 0xB800U:
            expected[kChrBanksOffset + (addr - 0x8000U) / 0x0800U] = value;
            break;
        case 0xC000U:
        case 0xC800U:
        case 0xD000U:
        case 0xD800U:
            expected[kNametableBanksOffset + (addr - 0xC000U) / 0x0800U] = value & 1U;
            break;
        case 0xE000U:
            expected[kPrgBanksOffset] = value & 0x1FU;
            break;
        case 0xE800U:
            expected[kPrgBanksOffset + 1U] = value & 0x1FU;
            break;
        case 0xF000U:
            expected[kPrgBanksOffset + 2U] = value & 0x1FU;
            break;
        default:
            break;
        }
        switch (addr & 0xE400U) {
        case 0x8400U:
            expected[kIrqCounterOffset] = value;
            expected[kIrqCounterOffset + 1U] = 0U;
            expected[kIrqLoadOffset] = 1U;
            break;
        case 0xA400U:
            expected[kIrqCounterOffset] = 0U;
            expected[kIrqCounterOffset + 1U] = value;
            expected[kIrqLoadOffset] = 1U;
            break;
        default:
            break;
        }
        CAPTURE(raw_addr);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 330 routes each nametable independently through console CIRAM", "[mapper330]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned table = 0U; table < 4U; ++table) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0xC000U + table * 0x0800U),
                              static_cast<Byte>(value));
            for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
                const auto addr = static_cast<Address>(0x2000U + table * kChrBankSize + offset);
                const auto expected = static_cast<Address>((value & 1U) * kChrBankSize + offset);
                CAPTURE(table, value, offset);
                REQUIRE(mapper->nametable_address(addr) == expected);
                if (addr < 0x2F00U) {
                    REQUIRE(mapper->nametable_address(static_cast<Address>(addr + 0x1000U)) ==
                            expected);
                }
            }
        }
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr : {0U, 0x1FFFU, 0x3F00U, 0x3FFFU}) {
        REQUIRE_FALSE(mapper->nametable_address(addr));
    }
}

TEST_CASE("Mapper 330 battery RAM is complete persistent storage with bounded imports",
          "[mapper330]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset),
                          static_cast<Byte>(offset * 13U + (offset >> 8U)));
    }
    for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 13U + (offset >> 8U)));
    }
    REQUIRE(mapper->battery_ram().size() == kPrgBankSize);
    const auto before = state(*mapper);
    for (std::size_t size : {0UZ, 1UZ, 8191UZ, 8193UZ, 16384UZ}) {
        std::vector<Byte> invalid(size, 0xA5U);
        mapper->set_battery_ram(invalid);
        REQUIRE(state(*mapper) == before);
    }
    std::vector<Byte> replacement(kPrgBankSize);
    for (unsigned offset = 0U; offset < replacement.size(); ++offset) {
        replacement[offset] = static_cast<Byte>(offset ^ 0xA5U);
    }
    mapper->set_battery_ram(replacement);
    REQUIRE(std::vector<Byte>(mapper->battery_ram().begin(), mapper->battery_ram().end()) ==
            replacement);
    mapper->power_on();
    REQUIRE(std::vector<Byte>(mapper->battery_ram().begin(), mapper->battery_ram().end()) ==
            replacement);
}

TEST_CASE("Mapper 330 soft reset is inert and cold power clears only undocumented latches",
          "[mapper330]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0xE000U, 17U);
    mapper->write_prg(0xE800U, 18U);
    mapper->write_prg(0xF000U, 19U);
    mapper->write_prg(0x8000U, 0xD4U);
    mapper->write_prg(0xD000U, 1U);
    mapper->write_prg(0x8400U, 0xFFU);
    mapper->write_prg(0xA400U, 0xFFU);
    mapper->clock_cpu_falling_edge();
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    const auto before = state(*mapper);

    mapper->reset();
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    for (unsigned slot = 0U; slot < 3U; ++slot) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * kPrgBankSize)) == cell(0U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * kChrBankSize)) == cell(0U));
    }
    for (unsigned table = 0U; table < 4U; ++table) {
        REQUIRE(mapper->nametable_address(
                    static_cast<Address>(0x2000U + table * kChrBankSize + 0x123U)) == 0x123U);
    }
    mapper->write_prg(0x8400U, 0U);
    mapper->write_prg(0xA400U, 0x80U);
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 330 IRQ counts M2 cycles to one wrapped pending interrupt", "[mapper330]") {
    auto mapper = board();
    mapper->write_prg(0x8400U, 0U);
    mapper->write_prg(0xA400U, 0x80U);
    mapper->clock_cpu_falling_edge();
    const auto loaded = state(*mapper);
    REQUIRE(loaded[kIrqCounterOffset] == 0U);
    REQUIRE(loaded[kIrqCounterOffset + 1U] == 0x80U);
    REQUIRE(loaded[kIrqLoadOffset] == 0U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper) == loaded);
    for (unsigned cycle = 0U; cycle < 0x7FFFU; ++cycle) {
        mapper->clock_cpu_falling_edge();
        CAPTURE(cycle);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    const auto wrapped = state(*mapper);
    REQUIRE(wrapped[kIrqCounterOffset] == 0U);
    REQUIRE(wrapped[kIrqCounterOffset + 1U] == 0U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu_falling_edge();
        REQUIRE(state(*mapper) == wrapped);
    }
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x8400U, 0x5AU);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xA400U, 0x7FU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    const auto disabled = state(*mapper);
    REQUIRE(disabled[kIrqCounterOffset] == 0x5AU);
    REQUIRE(disabled[kIrqCounterOffset + 1U] == 0x7FU);
    REQUIRE(disabled[kIrqLoadOffset] == 0U);
    for (unsigned cycle = 0U; cycle < 0x10000U; ++cycle) {
        mapper->clock_cpu_falling_edge();
    }
    REQUIRE(state(*mapper) == disabled);

    mapper->write_prg(0x8400U, 0xFFU);
    mapper->write_prg(0xA400U, 0xFFU);
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xA400U, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x8400U, 0xFEU);
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 330 leaves unmapped windows floating and has no audio hardware", "[mapper330]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (Address addr : {0U, 0x401FU, 0x4020U, 0x4800U, 0x5000U, 0x57FFU, 0x5800U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_audio();
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 330 snapshots replay all mutable state without ROM", "[mapper330][state]") {
    auto mapper = board();
    mapper->write_prg(0xE000U, 7U);
    mapper->write_prg(0xE800U, 23U);
    mapper->write_prg(0xF000U, 31U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8000U + slot * 0x0800U),
                          static_cast<Byte>(slot * 29U + 3U));
    }
    for (unsigned table = 0U; table < 4U; ++table) {
        mapper->write_prg(static_cast<Address>(0xC000U + table * 0x0800U), table & 1U);
    }
    mapper->write_prg(0x8400U, 0x34U);
    mapper->write_prg(0xA400U, 0x92U);
    for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset),
                          static_cast<Byte>(offset * 7U + (offset >> 8U)));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0x9234U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(copy->read_chr(static_cast<Address>(slot * kChrBankSize + 0x123U)) ==
                mapper->read_chr(static_cast<Address>(slot * kChrBankSize + 0x123U)));
    }
    REQUIRE(copy->irq_pending() == mapper->irq_pending());
}

TEST_CASE("Mapper 330 rejects bounded malformed states without mutation", "[mapper330][state]") {
    auto mapper = board();
    mapper->write_prg(0xE000U, 11U);
    mapper->write_prg(0xE800U, 22U);
    mapper->write_prg(0xF000U, 31U);
    mapper->write_prg(0x8000U, 0xD3U);
    mapper->write_prg(0xC800U, 1U);
    mapper->write_prg(0x8400U, 0xFFU);
    mapper->write_prg(0xA400U, 0xFFU);
    mapper->clock_cpu_falling_edge();
    mapper->clock_cpu_falling_edge();
    mapper->write_prg(0x6123U, 0xA7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(invalid);
    }
    for (std::size_t offset : {2UZ, 3UZ, 4UZ, 5UZ, 6UZ, 7UZ, 8UZ, 9UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (unsigned slot = 0U; slot < 3U; ++slot) {
        for (Byte value : {0x20U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[kPrgBanksOffset + slot] = value;
            reject(invalid);
        }
    }
    for (unsigned table = 0U; table < 4U; ++table) {
        for (Byte value : {2U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[kNametableBanksOffset + table] = value;
            reject(invalid);
        }
    }
    for (Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqPendingOffset] = value;
        reject(invalid);
        invalid = saved;
        invalid[kIrqLoadOffset] = value;
        reject(invalid);
    }
    auto inconsistent = saved;
    inconsistent[kIrqCounterOffset] = 1U;
    reject(inconsistent);

    auto other_mirror = board(MirrorMode::Horizontal);
    const auto other_before = state(*other_mirror);
    StateReader mismatch(saved);
    other_mirror->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_mirror) == other_before);
}

} // namespace
