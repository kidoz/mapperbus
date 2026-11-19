#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper284_drip.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kWramStateOffset = 23U;
constexpr std::size_t kAttributeStateOffset = kWramStateOffset + 8192U;
constexpr std::size_t kChannelStateOffset = kAttributeStateOffset + 2048U;
constexpr std::size_t kChannelStateSize = 268U;
constexpr std::size_t kStateSize = kChannelStateOffset + 2U * kChannelStateSize;

INesHeader header(bool legacy = false, bool battery = false) {
    INesHeader h{};
    h.mapper_number = 284U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = battery ? 0U : 8192U;
    h.prg_nvram_size = battery ? 8192U : 0U;
    return h;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) + (offset >> 14U));
}

std::unique_ptr<Drip> board(std::size_t prg_size = 262144U,
                            std::size_t chr_size = 32768U,
                            bool battery = false,
                            bool legacy = false) {
    const auto h = header(legacy, battery);
    REQUIRE(Drip::validate_geometry(h, prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t i = 0U; i < prg.size(); ++i) {
        prg[i] = cell(i);
    }
    for (std::size_t i = 0U; i < chr.size(); ++i) {
        chr[i] = cell(i * 3U + 7U);
    }
    return std::make_unique<Drip>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 284 validates addressable ROM and PRG RAM geometry", "[mapper284]") {
    for (bool legacy : {false, true}) {
        for (bool battery : {false, true}) {
            for (std::size_t prg = 16384U; prg <= 262144U; prg += 16384U) {
                for (std::size_t chr = 2048U; chr <= 32768U; chr += 2048U) {
                    REQUIRE(Drip::validate_geometry(header(legacy, battery), prg, chr));
                }
            }
        }
    }
    const auto h = header();
    for (std::size_t size : {0U, 1U, 8192U, 16383U, 16385U, 262160U, 278528U}) {
        REQUIRE_FALSE(Drip::validate_geometry(h, size, 32768U));
    }
    for (std::size_t size : {0U, 1U, 1024U, 2047U, 2049U, 32770U, 34816U}) {
        REQUIRE_FALSE(Drip::validate_geometry(h, 262144U, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto bad = h;
        bad.submapper = submapper;
        REQUIRE_FALSE(Drip::validate_geometry(bad, 262144U, 32768U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto bad = h;
        bad.mirror_mode = mode;
        REQUIRE_FALSE(Drip::validate_geometry(bad, 262144U, 32768U));
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto bad = h;
        switch (field) {
        case 0U:
            bad.prg_ram_size = 0U;
            break;
        case 1U:
            bad.prg_nvram_size = 8192U;
            break;
        case 2U:
            bad.chr_ram_size = 8192U;
            break;
        default:
            bad.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Drip::validate_geometry(bad, 262144U, 32768U));
    }
    auto battery_header = header(false, true);
    REQUIRE(Drip::validate_geometry(battery_header, 262144U, 32768U));
    battery_header.prg_ram_size = 8192U;
    REQUIRE_FALSE(Drip::validate_geometry(battery_header, 262144U, 32768U));
}

TEST_CASE("Mapper 284 status decoding fixed reset and language DIP are deterministic",
          "[mapper284]") {
    auto mapper = board();
    REQUIRE(mapper->read_expansion(0x4800U) == 0x64U);
    REQUIRE(mapper->read_expansion(0x4FFFU) == 0x64U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE(mapper->read_expansion(0x4800U) == 0xE4U);
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    REQUIRE(mapper->read_expansion(0x4800U) == 0xE4U);
    for (Address addr : {0x4800U, 0x4FFFU, 0x5000U, 0x57FFU, 0x5800U, 0x5FFFU}) {
        REQUIRE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
    }
    for (Address addr : {0x4020U, 0x47FFU, 0x6000U, 0x7FFFU, 0x8000U}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
    }
    for (Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
    }
    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU - 1U));
    REQUIRE(mapper->has_expansion_audio());
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper).size() == kStateSize);
}

TEST_CASE("Mapper 284 every writable register alias matches its canonical port", "[mapper284]") {
    auto actual = board(32768U, 8192U);
    auto expected = board(32768U, 8192U);
    for (unsigned addr = 0x8000U; addr < 0xC000U; ++addr) {
        actual->power_on();
        expected->power_on();
        actual->write_prg(static_cast<Address>(addr), 0xA5U);
        expected->write_prg(static_cast<Address>(0x8000U + (addr & 0x0FU)), 0xA5U);
        REQUIRE(state(*actual) == state(*expected));
    }
    for (unsigned addr = 0xC000U; addr <= 0xFFFFU; ++addr) {
        actual->power_on();
        expected->power_on();
        actual->write_prg(static_cast<Address>(addr), 0xA5U);
        expected->write_prg(static_cast<Address>(0xC000U + (addr & 0x07FFU)), 0xA5U);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 284 switchable and fixed PRG banks retain immutable ROM", "[mapper284]") {
    auto mapper = board();
    for (Byte bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(0xBFFBU, static_cast<Byte>(0xF0U | bank));
        for (std::size_t offset = 0U; offset < 16384U; ++offset) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    cell(static_cast<std::size_t>(bank) * 16384U + offset));
        }
    }
    for (std::size_t offset = 0U; offset < 16384U; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                cell(15U * 16384U + offset));
    }
}

TEST_CASE("Mapper 284 four independent CHR windows wrap fitted ROM after four-bit selection",
          "[mapper284]") {
    auto mapper = board();
    for (Byte bank = 0U; bank < 16U; ++bank) {
        for (unsigned window = 0U; window < 4U; ++window) {
            mapper->write_prg(static_cast<Address>(0x800CU + window),
                              static_cast<Byte>(bank + window));
        }
        for (unsigned window = 0U; window < 4U; ++window) {
            const std::size_t selected = (bank + window) & 0x0FU;
            for (std::size_t offset = 0U; offset < 2048U; ++offset) {
                const auto addr = static_cast<Address>(window * 2048U + offset);
                mapper->write_chr(addr, 0xFFU);
                REQUIRE(mapper->read_chr(addr) == cell((selected * 2048U + offset) * 3U + 7U));
            }
        }
    }
    auto small = board(16384U, 2048U);
    small->write_prg(0x800BU, 15U);
    small->write_prg(0x800FU, 15U);
    REQUIRE(small->read_prg(0x8000U) == small->read_prg(0xC000U));
    REQUIRE(small->read_chr(0U) == small->read_chr(0x1800U));
}

TEST_CASE("Mapper 284 CIRAM modes and SRAM write gate cover all control values", "[mapper284]") {
    auto mapper = board();
    for (Byte mode = 0U; mode < 4U; ++mode) {
        mapper->write_prg(0x800AU, mode);
        const auto expected = static_cast<MirrorMode>(mode == 0U   ? 1U
                                                      : mode == 1U ? 0U
                                                      : mode == 2U ? 3U
                                                                   : 4U);
        REQUIRE(mapper->mirror_mode() == expected);
    }
    mapper->write_prg(0x6000U, 0x55U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    mapper->write_prg(0x800AU, 0x04U);
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    mapper->write_prg(0x800AU, 0U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x60U);
    mapper->write_prg(0x800AU, 0x0FU);
    mapper->write_prg(0x7FFFU, 0xA7U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0xA7U);
}

TEST_CASE("Mapper 284 extended attributes select physical CIRAM tables per tile", "[mapper284]") {
    auto mapper = board();
    mapper->write_prg(0xC123U, 0x01U);
    mapper->write_prg(0xC523U, 0x03U);
    mapper->write_prg(0x800AU, 0x08U);
    REQUIRE(mapper->background_attribute_palette(0x2123U) == 1U);
    REQUIRE(mapper->background_attribute_palette(0x2523U) == 3U);
    REQUIRE(mapper->background_attribute_palette(0x0123U) == std::nullopt);
    mapper->write_prg(0x800AU, 0x09U);
    REQUIRE(mapper->background_attribute_palette(0x2923U) == 3U);
    REQUIRE(mapper->background_attribute_palette(0x2D23U) == 3U);
    REQUIRE(mapper->background_attribute_palette(0x2123U) == 1U);
    REQUIRE(mapper->background_attribute_palette(0x2523U) == 1U);
    mapper->write_prg(0x800AU, 0x08U);
    REQUIRE(mapper->background_attribute_palette(0x2923U) == 1U);
    REQUIRE(mapper->background_attribute_palette(0x2D23U) == 3U);
    mapper->write_prg(0x800AU, 0x0AU);
    for (Address addr : {0x2123U, 0x2523U, 0x2923U, 0x2D23U}) {
        REQUIRE(mapper->background_attribute_palette(addr) == 1U);
    }
    mapper->write_prg(0x800AU, 0x0BU);
    for (Address addr : {0x2123U, 0x2523U, 0x2923U, 0x2D23U}) {
        REQUIRE(mapper->background_attribute_palette(addr) == 3U);
    }
    mapper->write_prg(0x800AU, 0x0BU - 0x08U);
    REQUIRE(mapper->background_attribute_palette(0x2523U) == std::nullopt);
}

TEST_CASE("Mapper 284 buffered cycle IRQ reload acknowledge disable and expiry", "[mapper284]") {
    auto mapper = board();
    mapper->write_prg(0x8008U, 0x02U);
    mapper->write_prg(0x8009U, 0x80U);
    mapper->write_prg(0x8008U, 0x7FU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    const auto expired = state(*mapper);
    REQUIRE(expired[20U] == 0U);
    REQUIRE(expired[21U] == 0U);
    REQUIRE(expired[22U] == 2U);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x8008U, 0x01U);
    mapper->write_prg(0x8009U, 0x81U);
    for (unsigned i = 0U; i < 256U; ++i) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x8009U, 0x7FU);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 40000U; ++i) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x8008U, 0U);
    mapper->write_prg(0x8009U, 0x80U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 284 PCM starts immediately applies volume and delays period changes",
          "[mapper284]") {
    auto mapper = board();
    REQUIRE(mapper->read_expansion(0x5000U) == 0x40U);
    mapper->write_prg(0x8002U, 2U);
    mapper->write_prg(0x8003U, 0U);
    mapper->write_prg(0x8001U, 0xC0U);
    mapper->write_prg(0x8001U, 0x80U);
    mapper->write_prg(0x8001U, 0x40U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(mapper->audio_output() == 0.03125F);
    mapper->write_prg(0x8003U, 0x10U);
    REQUIRE(mapper->audio_output() == 0.03125F);
    mapper->write_prg(0x8002U, 1U);
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() == 0.03125F);
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() == -0.03125F);
    mapper->clock_audio();
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->read_expansion(0x5000U) == 0x40U);
    mapper->write_prg(0x8000U, 0U);
    mapper->write_prg(0x8005U, 0xC0U);
    REQUIRE(mapper->read_expansion(0x5800U) == 0U);
    REQUIRE(mapper->audio_output() == 0.03125F);
    mapper->write_prg(0x8004U, 0U);
    REQUIRE(mapper->read_expansion(0x5800U) == 0x40U);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 284 PCM FIFO is bounded and full writes are ignored", "[mapper284]") {
    auto mapper = board();
    mapper->write_prg(0x8002U, 1U);
    mapper->write_prg(0x8003U, 0xF0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8001U, static_cast<Byte>(value));
    }
    REQUIRE(mapper->read_expansion(0x5000U) == 0x80U);
    mapper->write_prg(0x8001U, 0xEEU);
    for (unsigned value = 0U; value < 256U; ++value) {
        REQUIRE(mapper->audio_output() ==
                static_cast<float>(static_cast<int>(value) - 128) / 2048.0F);
        mapper->clock_audio();
    }
    REQUIRE(mapper->read_expansion(0x5000U) == 0x40U);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 284 reset retains RAM while cold power clears only volatile storage",
          "[mapper284]") {
    auto mapper = board(262144U, 32768U, true);
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
    REQUIRE(mapper->battery_ram().size() == 8192U);
    mapper->set_battery_ram(std::vector<Byte>(8191U, 0U));
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->write_prg(0x800AU, 0x0FU);
    mapper->write_prg(0x6000U, 0x55U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0x800BU, 7U);
    mapper->write_prg(0x8001U, 0xC0U);
    mapper->write_prg(0x8009U, 0x80U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0x55U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_expansion(0x5000U) == 0x40U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x800AU, 0x08U);
    REQUIRE(mapper->background_attribute_palette(0x2000U) == 3U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6000U) == 0x55U);
    mapper->write_prg(0x800AU, 0x08U);
    REQUIRE(mapper->background_attribute_palette(0x2000U) == 0U);
    auto volatile_mapper = board();
    volatile_mapper->write_prg(0x800AU, 4U);
    volatile_mapper->write_prg(0x6000U, 0xA7U);
    volatile_mapper->power_on();
    REQUIRE(volatile_mapper->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 284 state replays all hardware and rejects damage atomically", "[mapper284]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x800AU, 0x0DU);
    mapper->write_prg(0x800BU, 9U);
    mapper->write_prg(0x800CU, 3U);
    mapper->write_prg(0x800DU, 4U);
    mapper->write_prg(0x800EU, 5U);
    mapper->write_prg(0x800FU, 6U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0xC123U, 2U);
    mapper->write_prg(0x8008U, 9U);
    mapper->write_prg(0x8009U, 0x82U);
    mapper->write_prg(0x8002U, 20U);
    mapper->write_prg(0x8003U, 0x31U);
    mapper->write_prg(0x8001U, 0xC0U);
    mapper->write_prg(0x8001U, 0x40U);
    mapper->clock_audio();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    mapper->power_on();
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    const auto audio = mapper->audio_output();
    mapper->clock_cpu();
    mapper->clock_audio();
    StateReader replay_again(saved);
    mapper->load_state(replay_again);
    REQUIRE(replay_again.ok());
    REQUIRE(mapper->audio_output() == audio);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const auto reject = [&](std::size_t offset, Byte value) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    reject(0U, saved[0] ^ 1U);
    reject(4U, saved[4] ^ 1U);
    reject(8U, saved[8] ^ 1U);
    reject(12U, 0U);
    reject(13U, 0x10U);
    reject(14U, 0x10U);
    for (std::size_t offset = 15U; offset < 19U; ++offset) {
        reject(offset, 0x10U);
    }
    reject(21U, 0x80U);
    reject(22U, 3U);
    reject(22U, 4U);
    reject(kAttributeStateOffset, 4U);
    reject(kChannelStateOffset + 258U, 1U);
    reject(kChannelStateOffset + 260U, 0x10U);
    reject(kChannelStateOffset + 262U, 0x10U);
    reject(kChannelStateOffset + 264U, 0x10U);
    reject(kChannelStateOffset + 265U, 0x10U);
    reject(kChannelStateOffset + 266U, 0x10U);
    reject(kChannelStateOffset + 267U, 2U);
    reject(kChannelStateOffset + kChannelStateSize + 267U, 2U);

    auto other = board(32768U, 8192U);
    const auto before = state(*other);
    StateReader wrong_geometry(saved);
    other->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other) == before);
}
} // namespace
