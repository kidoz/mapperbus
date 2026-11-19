#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "core/mappers/nes_event.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateSize = 16398U;
constexpr std::size_t kControlOffset = 4U;
constexpr std::size_t kChr0Offset = 5U;
constexpr std::size_t kInitializedOffset = 8U;
constexpr std::size_t kIrqOffset = 9U;
constexpr std::size_t kCounterOffset = 10U;
constexpr std::size_t kWramOffset = 14U;
constexpr std::size_t kChrRamOffset = kWramOffset + 8192U;

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 105U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 0U : 8192U;
    h.chr_ram_size = legacy ? 0U : 8192U;
    return h;
}

std::vector<Byte> prg_image() {
    std::vector<Byte> prg(262144U);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = static_cast<Byte>(i / 16384U);
    return prg;
}

std::unique_ptr<NesEvent> board(bool legacy = false) {
    return std::make_unique<NesEvent>(header(legacy), prg_image(), std::vector<Byte>{});
}

void serial_write(Mapper& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

uint32_t counter(const std::vector<Byte>& saved) {
    uint32_t value = 0U;
    std::memcpy(&value, saved.data() + kCounterOffset, sizeof(value));
    return value;
}

void set_counter(std::vector<Byte>& saved, uint32_t value) {
    std::memcpy(saved.data() + kCounterOffset, &value, sizeof(value));
}

void load(Mapper& mapper, const std::vector<Byte>& saved) {
    StateReader reader(saved);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 105 accepts only the populated NES-EVENT geometry", "[mapper105]") {
    REQUIRE(NesEvent::validate_geometry(header(), 262144U, 0U));
    REQUIRE(NesEvent::validate_geometry(header(true), 262144U, 0U));
    for (std::size_t size : {0U, 32768U, 131072U, 262143U, 262145U, 524288U}) {
        REQUIRE_FALSE(NesEvent::validate_geometry(header(), size, 0U));
        if (size != 0U)
            REQUIRE_FALSE(NesEvent::validate_geometry(header(), 262144U, size));
    }
    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 16384U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_nvram_size = 8192U;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::Horizontal;
            REQUIRE(NesEvent::validate_geometry(h, 262144U, 0U));
            continue;
        case 9U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(NesEvent::validate_geometry(h, 262144U, 0U));
    }
}

TEST_CASE("Mapper 105 decodes every MMC1 serial register alias", "[mapper105]") {
    auto m = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto expected = static_cast<Byte>((addr * 13U) & 0x1FU);
        serial_write(*m, static_cast<Address>(addr), expected);
        const auto saved = state(*m);
        REQUIRE(saved[2U] == 0x10U);
        REQUIRE(saved[3U] == 0U);
        const std::size_t offset = 4U + ((addr - 0x8000U) >> 13U);
        REQUIRE(saved[offset] == expected);
    }
}

TEST_CASE("Mapper 105 serial reset and consecutive-write filtering match MMC1", "[mapper105]") {
    auto m = board();
    m->write_prg_cpu(0x8000U, 1U, false);
    m->write_prg_cpu(0x8000U, 1U, true);
    for (unsigned n = 0U; n < 4U; ++n)
        m->write_prg_cpu(0x8000U, 0U, false);
    REQUIRE(m->mirror_mode() == MirrorMode::SingleUpper);
    serial_write(*m, 0xA000U, 0x0AU);
    m->write_prg(0xFFFFU, 1U);
    m->write_prg_cpu(0xFFFFU, 0x80U, true);
    auto saved = state(*m);
    REQUIRE(saved[2U] == 0x10U);
    REQUIRE(saved[3U] == 0U);
    REQUIRE((saved[kControlOffset] & 0x0CU) == 0x0CU);
    REQUIRE(saved[kChr0Offset] == 0x0AU);
    m->write_prg_cpu(0x6000U, 0xA7U, true);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
}

TEST_CASE("Mapper 105 locked banking retains MMC1 PRG A14 and all PRG modes", "[mapper105]") {
    auto m = board();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_prg(0xC000U) == 1U);
    for (Byte mode = 0U; mode < 4U; ++mode) {
        for (Byte bank = 0U; bank < 16U; ++bank) {
            serial_write(*m, 0x8000U, static_cast<Byte>(mode << 2U));
            serial_write(*m, 0xE000U, bank);
            const Byte selected = bank & 7U;
            Byte low = 0U;
            Byte high = 0U;
            if (mode < 2U) {
                low = selected & 6U;
                high = low + 1U;
            } else if (mode == 2U) {
                high = selected;
            } else {
                low = selected;
                high = 7U;
            }
            REQUIRE(m->read_prg(0x8000U) == low);
            REQUIRE(m->read_prg(0xBFFFU) == low);
            REQUIRE(m->read_prg(0xC000U) == high);
            REQUIRE(m->read_prg(0xFFFFU) == high);
            REQUIRE(state(*m)[kInitializedOffset] == 0U);
        }
    }
}

TEST_CASE("Mapper 105 initialization selects 32 KiB lower-chip banks", "[mapper105]") {
    auto m = board();
    serial_write(*m, 0xA000U, 0x10U);
    REQUIRE(state(*m)[kInitializedOffset] == 1U);
    serial_write(*m, 0xA000U, 0U);
    for (Byte mode = 0U; mode < 4U; ++mode) {
        serial_write(*m, 0x8000U, static_cast<Byte>(mode << 2U));
        serial_write(*m, 0xE000U, 0x07U);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            serial_write(*m, 0xA000U, bank);
            const Byte base = bank & 6U;
            REQUIRE(m->read_prg(0x8000U) == base);
            REQUIRE(m->read_prg(0xBFFFU) == base);
            REQUIRE(m->read_prg(0xC000U) == base + 1U);
            REQUIRE(m->read_prg(0xFFFFU) == base + 1U);
        }
    }
    serial_write(*m, 0xA000U, 0x10U);
    serial_write(*m, 0xA000U, 0U);
    REQUIRE(state(*m)[kInitializedOffset] == 1U);
}

TEST_CASE("Mapper 105 upper chip uses normal MMC1 banking", "[mapper105]") {
    auto m = board();
    serial_write(*m, 0xA000U, 0x18U);
    serial_write(*m, 0xA000U, 0x08U);
    for (Byte mode = 0U; mode < 4U; ++mode) {
        for (Byte bank = 0U; bank < 32U; ++bank) {
            serial_write(*m, 0x8000U, static_cast<Byte>(mode << 2U));
            serial_write(*m, 0xE000U, bank);
            const Byte selected = bank & 7U;
            Byte low = 0U;
            Byte high = 0U;
            if (mode < 2U) {
                low = selected & 6U;
                high = low + 1U;
            } else if (mode == 2U) {
                high = selected;
            } else {
                low = selected;
                high = 7U;
            }
            REQUIRE(m->read_prg(0x8000U) == 8U + low);
            REQUIRE(m->read_prg(0xC000U) == 8U + high);
        }
    }
}

TEST_CASE("Mapper 105 CHR RAM is fixed and every CIRAM mode is programmable", "[mapper105]") {
    auto m = board();
    for (Address addr : {0U, 1U, 0x0FFFU, 0x1000U, 0x1FFFU, 0x2000U, 0x3FFFU}) {
        m->write_chr(addr, static_cast<Byte>(addr ^ 0xA5U));
        REQUIRE(m->read_chr(addr) == static_cast<Byte>(addr ^ 0xA5U));
        REQUIRE(m->read_chr(addr & 0x1FFFU) == static_cast<Byte>(addr ^ 0xA5U));
    }
    const auto before = state(*m);
    serial_write(*m, 0xA000U, 0x1FU);
    serial_write(*m, 0xC000U, 0x1FU);
    for (Byte mode = 0U; mode < 32U; ++mode) {
        serial_write(*m, 0x8000U, mode);
        constexpr std::array<MirrorMode, 4> kMirrors{MirrorMode::SingleLower,
                                                     MirrorMode::SingleUpper,
                                                     MirrorMode::Vertical,
                                                     MirrorMode::Horizontal};
        REQUIRE(m->mirror_mode() == kMirrors[mode & 3U]);
        REQUIRE(m->read_chr(0U) == before[kChrRamOffset]);
        REQUIRE(m->read_chr(0x1FFFU) == before[kChrRamOffset + 8191U]);
    }
    REQUIRE(state(*m)[kChr0Offset] == 0x1FU);
    REQUIRE(state(*m)[6U] == 0x1FU);
}

TEST_CASE("Mapper 105 WRAM enable open-bus masks and persistence boundaries", "[mapper105]") {
    auto m = board();
    REQUIRE(m->battery_ram().empty());
    m->set_battery_ram(std::array<Byte, 2>{1U, 2U});
    REQUIRE(m->battery_ram().empty());
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE(m->maps_prg(addr) == (addr >= 0x6000U));
        REQUIRE(m->prg_read_mask(addr) == 0xFFU);
        REQUIRE(m->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(m->maps_expansion(addr));
    }
    for (Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
        m->write_prg(addr, 0xA7U);
        REQUIRE(m->read_prg(addr) == 0xA7U);
    }
    serial_write(*m, 0xE000U, 0x10U);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    REQUIRE(m->read_prg(0x6000U) == 0U);
    m->write_prg(0x6000U, 0U);
    serial_write(*m, 0xE000U, 0U);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
    REQUIRE_FALSE(m->has_expansion_audio());
    REQUIRE(m->audio_output() == 0.0F);
    REQUIRE(m->read_prg(0x8000U) == 0U);
}

TEST_CASE("Mapper 105 reset and cold power retain only documented RAM", "[mapper105]") {
    auto m = board();
    REQUIRE(m->configure_board_jumpers(7U));
    m->write_prg(0x6000U, 0xA7U);
    m->write_chr(0x123U, 0x5CU);
    serial_write(*m, 0xA000U, 0x18U);
    serial_write(*m, 0x8000U, 0x03U);
    m->clock_cpu();
    m->reset();
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
    REQUIRE(m->read_chr(0x123U) == 0x5CU);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_prg(0xC000U) == 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(state(*m)[kInitializedOffset] == 0U);
    REQUIRE(counter(state(*m)) == 0U);
    REQUIRE((state(*m)[0U] & 15U) == 7U);
    m->power_on();
    REQUIRE(m->read_prg(0x6000U) == 0U);
    REQUIRE(m->read_chr(0x123U) == 0U);
    REQUIRE((state(*m)[0U] & 15U) == 7U);
    REQUIRE_FALSE(m->configure_board_jumpers(16U));
    REQUIRE_FALSE(m->configure_board_jumpers(255U));
}

TEST_CASE("Mapper 105 timer reaches every DIP threshold and latches IRQ", "[mapper105]") {
    auto m = board();
    m->clock_cpu();
    REQUIRE(counter(state(*m)) == 1U);
    for (Byte dips = 0U; dips < 16U; ++dips) {
        REQUIRE(m->configure_board_jumpers(dips));
        auto saved = state(*m);
        const uint32_t threshold = 0x20000000U | (static_cast<uint32_t>(dips) << 25U);
        set_counter(saved, threshold - 1U);
        saved[kIrqOffset] = 0U;
        saved[kChr0Offset] &= 0xEFU;
        load(*m, saved);
        REQUIRE_FALSE(m->irq_pending());
        m->clock_cpu();
        REQUIRE(counter(state(*m)) == threshold);
        REQUIRE(m->irq_pending());
        m->clock_cpu();
        m->acknowledge_irq();
        REQUIRE(m->irq_pending());
        serial_write(*m, 0xA000U, 0x10U);
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE(counter(state(*m)) == 0U);
        for (unsigned n = 0U; n < 8U; ++n)
            m->clock_cpu();
        REQUIRE(counter(state(*m)) == 0U);
        serial_write(*m, 0xA000U, 0U);
        m->clock_cpu();
        REQUIRE(counter(state(*m)) == 1U);
    }
}

TEST_CASE("Mapper 105 counter wraps at 30 bits without clearing a pending IRQ", "[mapper105]") {
    auto m = board();
    auto saved = state(*m);
    set_counter(saved, 0x3FFFFFFFU);
    saved[kIrqOffset] = 1U;
    load(*m, saved);
    m->clock_cpu();
    REQUIRE(counter(state(*m)) == 0U);
    REQUIRE(m->irq_pending());
    serial_write(*m, 0xA000U, 0x10U);
    REQUIRE_FALSE(m->irq_pending());
}

TEST_CASE("Mapper 105 state replay includes RAM partial serial state and timer", "[mapper105]") {
    auto source = board();
    REQUIRE(source->configure_board_jumpers(9U));
    for (Address addr : {0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU})
        source->write_prg(addr, static_cast<Byte>(addr ^ 0xA7U));
    source->write_chr(0U, 0x12U);
    source->write_chr(0x1FFFU, 0x34U);
    serial_write(*source, 0xA000U, 0x08U);
    serial_write(*source, 0xC000U, 0x15U);
    serial_write(*source, 0xE000U, 0x03U);
    source->write_prg(0x8000U, 1U);
    source->write_prg(0x8000U, 0U);
    for (unsigned n = 0U; n < 12345U; ++n)
        source->clock_cpu();
    const auto saved = state(*source);
    REQUIRE(saved.size() == kStateSize);
    auto restored = board();
    REQUIRE(restored->configure_board_jumpers(9U));
    load(*restored, saved);
    REQUIRE(state(*restored) == saved);
    for (Address addr : {0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU})
        REQUIRE(restored->read_prg(addr) == source->read_prg(addr));
    REQUIRE(restored->read_chr(0U) == 0x12U);
    REQUIRE(restored->read_chr(0x1FFFU) == 0x34U);
    source->clock_cpu();
    restored->clock_cpu();
    serial_write(*source, 0x8000U, 0x02U);
    serial_write(*restored, 0x8000U, 0x02U);
    REQUIRE(state(*restored) == state(*source));
    REQUIRE(restored->mirror_mode() == source->mirror_mode());
    REQUIRE(restored->read_prg(0x8000U) == source->read_prg(0x8000U));
}

TEST_CASE("Mapper 105 malformed states reject without mutating live state", "[mapper105]") {
    auto m = board();
    m->write_prg(0x6000U, 0xA7U);
    m->write_chr(0x1FFFU, 0x5CU);
    serial_write(*m, 0xA000U, 0x18U);
    serial_write(*m, 0xE000U, 0x03U);
    const auto saved = state(*m);
    serial_write(*m, 0xA000U, 0x02U);
    m->write_prg(0x6000U, 0x39U);
    m->write_chr(0x1FFFU, 0x81U);
    const auto before = state(*m);
    for (std::size_t length : {0UZ, 1UZ, 2UZ, 13UZ, 14UZ, 8191UZ, kStateSize - 1U}) {
        StateReader reader{std::span(saved).first(length)};
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == before);
    }
    for (unsigned kind = 0U; kind < 13U; ++kind) {
        auto bad = saved;
        switch (kind) {
        case 0U:
            bad[0U] ^= 0xFFU;
            break;
        case 1U:
            bad[2U] = 0x20U;
            break;
        case 2U:
            bad[3U] = 5U;
            break;
        case 3U:
            bad[4U] = 0x20U;
            break;
        case 4U:
            bad[5U] = 0x20U;
            break;
        case 5U:
            bad[6U] = 0x20U;
            break;
        case 6U:
            bad[7U] = 0x20U;
            break;
        case 7U:
            bad[8U] = 2U;
            break;
        case 8U:
            bad[9U] = 2U;
            break;
        case 9U:
            set_counter(bad, 0x40000000U);
            break;
        case 10U:
            bad[kInitializedOffset] = 0U;
            bad[kChr0Offset] = 0x10U;
            break;
        case 11U:
            bad[kChr0Offset] = 0x10U;
            set_counter(bad, 1U);
            break;
        default:
            bad[kChr0Offset] = 0x10U;
            bad[kIrqOffset] = 1U;
            break;
        }
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == before);
    }
    auto other = board();
    REQUIRE(other->configure_board_jumpers(1U));
    const auto other_before = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == other_before);
}

} // namespace
