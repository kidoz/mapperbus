#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <vector>

#include "core/mappers/mapper142_ks202.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kStateBytes = 8205U;
constexpr std::size_t kReloadOffset = 7U;
constexpr std::size_t kCounterOffset = 9U;
constexpr std::size_t kControlOffset = 11U;
constexpr std::size_t kPendingOffset = 12U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 142U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::vector<Byte> prg_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = prg_cell(offset / 8192U, offset % 8192U);
    return data;
}

std::unique_ptr<KaiserKs202> board(INesHeader h = header(), std::size_t size = 131072U) {
    REQUIRE(KaiserKs202::validate_geometry(h, size, 0U));
    return std::make_unique<KaiserKs202>(h, prg_rom(size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

uint16_t word_at(const std::vector<Byte>& bytes, std::size_t offset) {
    uint16_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void set_reload(Mapper& mapper, uint16_t value) {
    for (unsigned group = 0U; group < 4U; ++group)
        mapper.write_prg(static_cast<Address>(0x8000U + group * 0x1000U),
                         static_cast<Byte>(value >> (group * 4U)));
}

TEST_CASE("Mapper 142 powers on with PRG in all CPU windows and volatile CHR RAM", "[mapper142]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto mapper = board(header(legacy, vertical));
            for (Address addr : {0x6000U, 0x8000U, 0xA000U, 0xC000U})
                REQUIRE(mapper->read_prg(addr) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xE000U) == prg_cell(15U, 0U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(15U, 0x1FFFU));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(state(*mapper).size() == kStateBytes);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            for (unsigned addr = 0U; addr < 0x10000U; addr += 0x101U) {
                REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
                REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
                REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
            }
            for (unsigned addr = 0U; addr < 8192U; ++addr)
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        }
}

TEST_CASE("Mapper 142 decodes every bank-select and bank-data alias without bus conflicts",
          "[mapper142]") {
    auto mapper = board();
    for (unsigned addr = 0xE000U; addr <= 0xEFFFU; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), 0xF9U);
        mapper->write_prg(0xF000U, 5U);
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(5U, 0x123U));
        mapper->write_prg(0xF000U, 0U);
    }
    for (unsigned addr = 0xF000U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(0xE123U, 4U);
        mapper->write_prg(static_cast<Address>(addr), 0xF7U);
        REQUIRE(mapper->read_prg(0x6123U) == prg_cell(7U, 0x123U));
        mapper->write_prg(static_cast<Address>(addr), 0U);
    }

    constexpr std::array<int, 8> kSlots = {-1, 1, 2, 3, 0, -1, -1, -1};
    constexpr std::array<Address, 4> kWindows = {0x6000U, 0x8000U, 0xA000U, 0xC000U};
    for (unsigned select = 0U; select < 8U; ++select)
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0xE000U + select), static_cast<Byte>(select));
            mapper->write_prg(static_cast<Address>(0xF000U + value), static_cast<Byte>(value));
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto expected = kSlots[select] == static_cast<int>(slot)
                                          ? static_cast<unsigned>(value & 15U)
                                          : 0U;
                REQUIRE(mapper->read_prg(static_cast<Address>(kWindows[slot] + 0x123U)) ==
                        prg_cell(expected, 0x123U));
            }
            REQUIRE(mapper->read_prg(0xE123U) == prg_cell(15U, 0x123U));
        }
}

TEST_CASE("Mapper 142 applies four-bit banks before wrapping fitted PRG ROM", "[mapper142]") {
    constexpr std::array<Address, 4> kWindows = {0x6000U, 0x8000U, 0xA000U, 0xC000U};
    for (std::size_t size = 32768U; size <= 131072U; size += 8192U) {
        auto mapper = board(header(), size);
        const auto count = size / 8192U;
        for (unsigned bank = 0U; bank < 16U; ++bank)
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                mapper->write_prg(0xE000U, static_cast<Byte>(slot == 0U ? 4U : slot));
                mapper->write_prg(0xFFFFU, static_cast<Byte>(bank));
                for (unsigned offset : {0U, 1U, 0x123U, 0x1FFFU})
                    REQUIRE(mapper->read_prg(static_cast<Address>(kWindows[slot] + offset)) ==
                            prg_cell(bank % count, offset));
                REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + bank)) ==
                        prg_cell(count - 1U, bank));
            }
    }
}

TEST_CASE("Mapper 142 IRQ reload covers every nibble alias and masks unused data", "[mapper142]") {
    auto mapper = board();
    for (unsigned group = 0U; group < 4U; ++group) {
        for (unsigned addr = group * 0x1000U; addr < (group + 1U) * 0x1000U; ++addr)
            mapper->write_prg(static_cast<Address>(0x8000U + addr), 0xFAU);
    }
    const auto saved = state(*mapper);
    REQUIRE(word_at(saved, kReloadOffset) == 0xAAAAU);

    set_reload(*mapper, 0xFFFFU);
    auto expected = static_cast<uint16_t>(0xFFFFU);
    for (unsigned group = 0U; group < 4U; ++group) {
        mapper->write_prg(static_cast<Address>(0x8000U + group * 0x1000U + 0xFFFU), 0U);
        const auto current = state(*mapper);
        expected = static_cast<uint16_t>(expected & ~(0xFU << (group * 4U)));
        REQUIRE(word_at(current, kReloadOffset) == expected);
    }
    for (Address addr : {0x0000U, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        const auto before = state(*mapper);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 142 implements VRC3 16-bit IRQs with Kaiser acknowledge semantics",
          "[mapper142]") {
    auto mapper = board();
    set_reload(*mapper, 0xFFFDU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC123U, 0x02U);
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xFFFDU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xFFFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xFFFDU);
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xDFFFU, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kControlOffset] == 0x02U);
    mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xFFFFU);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0x00U);
    REQUIRE_FALSE(mapper->irq_pending());
    const auto stopped = word_at(state(*mapper), kCounterOffset);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == stopped);

    mapper->write_prg(0xC000U, 0x01U);
    mapper->write_prg(0xD000U, 0U);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == stopped);
    REQUIRE_FALSE(mapper->irq_pending());

    set_reload(*mapper, 0xFFFFU);
    mapper->write_prg(0xC000U, 0x03U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xD000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kControlOffset] == 0x03U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0xF8U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kControlOffset] == 0U);
}

TEST_CASE("Mapper 142 eight-bit IRQs retain the upper byte and reload only the low byte",
          "[mapper142]") {
    auto mapper = board();
    set_reload(*mapper, 0xABFEU);
    mapper->write_prg(0xCFFFU, 0x06U);
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xABFEU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xABFEU);

    set_reload(*mapper, 0x1202U);
    mapper->write_prg(0xD000U, 0U);
    mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xABFFU);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xAB02U);

    mapper->write_prg(0xC000U, 0x02U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0x1202U);
}

TEST_CASE("Mapper 142 soft reset clears latches and IRQs but retains CHR RAM", "[mapper142]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned select = 1U; select <= 4U; ++select) {
        mapper->write_prg(0xE000U, static_cast<Byte>(select));
        mapper->write_prg(0xF000U, static_cast<Byte>(select + 3U));
    }
    mapper->write_prg(0xE000U, 6U);
    set_reload(*mapper, 0xFFFFU);
    mapper->write_prg(0xC000U, 3U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));

    mapper->reset();
    const auto warm = state(*mapper);
    REQUIRE(std::equal(warm.begin(), warm.begin() + 13U, initial.begin()));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 142 validates decoder geometry and native memory declarations", "[mapper142]") {
    for (bool legacy : {false, true})
        for (std::size_t size = 32768U; size <= 131072U; size += 8192U)
            REQUIRE(KaiserKs202::validate_geometry(header(legacy), size, 0U));

    for (std::size_t size : {0U, 8192U, 16384U, 24576U, 32767U, 32769U, 131073U, 139264U})
        REQUIRE_FALSE(KaiserKs202::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1U, 8192U, 16384U})
        REQUIRE_FALSE(KaiserKs202::validate_geometry(header(), 131072U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(KaiserKs202::validate_geometry(h, 131072U, 0U));
    }
    for (unsigned kind = 0U; kind < 8U; ++kind) {
        auto h = header();
        switch (kind) {
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
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 4096U;
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
        REQUIRE_FALSE(KaiserKs202::validate_geometry(h, 131072U, 0U));
    }
    auto legacy = header(true);
    legacy.has_battery = true;
    REQUIRE_FALSE(KaiserKs202::validate_geometry(legacy, 131072U, 0U));
    legacy = header(true);
    legacy.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(KaiserKs202::validate_geometry(legacy, 131072U, 0U));
}

TEST_CASE("Mapper 142 snapshots replay all mutable state and reject damage atomically",
          "[mapper142][state]") {
    auto mapper = board();
    for (unsigned select = 1U; select <= 4U; ++select) {
        mapper->write_prg(0xE000U, static_cast<Byte>(select));
        mapper->write_prg(0xF000U, static_cast<Byte>(select * 3U));
    }
    mapper->write_prg(0xEFFFU, 5U);
    set_reload(*mapper, 0xFFFFU);
    mapper->write_prg(0xC000U, 2U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 3U + 7U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateBytes);

    mapper->power_on();
    mapper->write_chr(0U, 0xFFU);
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == prg_cell(12U, 0x123U));
    mapper->clock_cpu();
    REQUIRE(word_at(state(*mapper), kCounterOffset) == 0xFFFFU);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bank = 0U; bank < 4U; ++bank)
        for (unsigned value = 16U; value < 256U; ++value) {
            auto bad = saved;
            bad[2U + bank] = static_cast<Byte>(value);
            StateReader reader(bad);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
    for (unsigned offset : {6U, 11U})
        for (unsigned value = 8U; value < 256U; ++value) {
            auto bad = saved;
            bad[offset] = static_cast<Byte>(value);
            StateReader reader(bad);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
    for (unsigned value = 2U; value < 256U; ++value) {
        auto bad = saved;
        bad[kPendingOffset] = static_cast<Byte>(value);
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other = board(header(false, false));
    const auto before = state(*other);
    StateReader wrong_mirror(saved);
    other->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
