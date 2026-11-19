#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper305.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 131072UZ;
constexpr std::size_t kPrgBankSize = 2048UZ;
constexpr std::size_t kStateSize = 2UZ + 4UZ + 4UZ + 8192UZ;

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 305U;
    result.is_nes2 = true;
    result.submapper = 0U;
    result.mirror_mode = MirrorMode::Vertical;
    result.chr_ram_size = 8192U;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) * 19U + (offset >> 16U) * 29U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = cell(offset);
    return bytes;
}

std::unique_ptr<Mapper305> board(std::size_t prg_size = kPrgSize, bool battery = false) {
    auto h = header();
    h.has_battery = battery;
    REQUIRE(Mapper305::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper305>(h, rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper305& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper305& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 305 maps every fixed and initially selected PRG cell", "[mapper305]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto bank = 15U - ((addr - 0x8000U) >> 11U);
        const auto offset = bank * kPrgBankSize + (addr & 0x07FFU);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == cell(offset));
    }
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == cell(addr & 0x07FFU));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(state(*mapper).size() == kStateSize);
}

TEST_CASE("Mapper 305 decodes every register alias and all eight bank data bits", "[mapper305]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto selected = addr >= 0x8000U && ((addr >> 11U) & 3U) == slot;
            const auto bank = selected ? 0xA5U & (kPrgSize / kPrgBankSize - 1U) : 0U;
            const auto low_addr = 0x6000U + slot * kPrgBankSize + 0x123U;
            INFO("write=" << addr << " slot=" << slot);
            REQUIRE(mapper->read_prg(static_cast<Address>(low_addr)) ==
                    cell(bank * kPrgBankSize + 0x123U));
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto reg = static_cast<Address>(0x8000U + slot * 0x0800U);
            mapper->write_prg(reg, static_cast<Byte>(value));
            const auto bank = value & (kPrgSize / kPrgBankSize - 1U);
            for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
                const auto addr = 0x6000U + slot * kPrgBankSize + offset;
                REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                        cell(bank * kPrgBankSize + offset));
            }
        }
    }

    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto bank = 15U - ((addr - 0x8000U) >> 11U);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                cell(bank * kPrgBankSize + (addr & 0x07FFU)));
    }
}

TEST_CASE("Mapper 305 owns volatile CHR RAM and no persistent or low CPU memory", "[mapper305]") {
    auto mapper = board(kPrgSize, true);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + addr / 256U));
        mapper->write_prg(static_cast<Address>(0x6000U + addr), 0xFFU);
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto value = static_cast<Byte>(addr * 13U + addr / 256U);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0xE000U)) == value);
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());

    mapper->write_prg(0x9800U, 7U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x7800U) == cell(0U));
    REQUIRE(mapper->read_chr(0x0123U) == static_cast<Byte>(0x0123U * 13U + 0x0123U / 256U));
    mapper->power_on();
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);

    for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU})
        REQUIRE_FALSE(mapper->maps_prg(addr));
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
        mapper->write_expansion(static_cast<Address>(addr), 0xA7U);
    }
}

TEST_CASE("Mapper 305 has no original-cartridge IRQ or expansion audio", "[mapper305]") {
    auto mapper = board();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 305 validates the bounded documented geometry", "[mapper305]") {
    for (std::size_t size = 32768UZ; size <= 524288UZ; size *= 2UZ)
        REQUIRE(Mapper305::validate_geometry(header(), size, 0U));
    auto battery = header();
    battery.has_battery = true;
    REQUIRE(Mapper305::validate_geometry(battery, kPrgSize, 0U));

    for (std::size_t size : {0UZ, 1UZ, 16384UZ, 32767UZ, 49152UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper305::validate_geometry(header(), size, 0U));
    for (std::size_t chr_size : {1UZ, 8192UZ, 16384UZ})
        REQUIRE_FALSE(Mapper305::validate_geometry(header(), kPrgSize, chr_size));
    for (auto mode : {MirrorMode::Horizontal,
                      MirrorMode::FourScreen,
                      MirrorMode::SingleLower,
                      MirrorMode::SingleUpper}) {
        auto invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(Mapper305::validate_geometry(invalid, kPrgSize, 0U));
    }
    for (unsigned kind = 0U; kind < 7U; ++kind) {
        auto invalid = header();
        switch (kind) {
        case 0U:
            invalid.is_nes2 = false;
            break;
        case 1U:
            invalid.mapper_number = 304U;
            break;
        case 2U:
            invalid.submapper = 1U;
            break;
        case 3U:
            invalid.prg_ram_size = 8192U;
            break;
        case 4U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 5U:
            invalid.chr_ram_size = 0U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper305::validate_geometry(invalid, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 305 snapshots are bounded and restore or reject atomically",
          "[mapper305][state]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 4U; ++slot)
        mapper->write_prg(static_cast<Address>(0x8000U + slot * 0x0800U),
                          static_cast<Byte>(0x3DU + slot * 47U));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U + addr / 64U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const auto bank = (0x3DU + slot * 47U) & (kPrgSize / kPrgBankSize - 1U);
        const auto addr = 0x6000U + slot * kPrgBankSize + 0x155U;
        REQUIRE(copy->read_prg(static_cast<Address>(addr)) == cell(bank * kPrgBankSize + 0x155U));
    }

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span<const Byte>(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 0x80U;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    for (std::size_t size : {32768UZ, 65536UZ, 262144UZ, 524288UZ}) {
        auto other = board(size);
        const auto before = state(*other);
        REQUIRE(before.size() == kStateSize);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }
    load(*mapper, saved);
}

} // namespace
