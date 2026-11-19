#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/mappers/fs304.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, std::size_t wram = 2048U) {
    INesHeader h{};
    h.mapper_number = 164U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.has_battery = true;
    h.prg_ram_size = legacy ? 8192U : wram;
    h.prg_nvram_size = legacy ? 0U : 512U;
    h.chr_ram_size = 8192U;
    return h;
}

std::vector<Byte> rom(std::size_t size = 524288U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 16384U);
    return data;
}

std::unique_ptr<Fs304> board(INesHeader h = header(), std::size_t prg_size = 524288U) {
    REQUIRE(Fs304::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Fs304>(h, rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Fs304& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Fs304& mapper, std::span<const Byte> data) {
    StateReader reader(data);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

class Serial {
  public:
    explicit Serial(Fs304& mapper) : mapper_(mapper) {}

    void pins(bool select, bool clock, bool data = false) {
        mapper_.write_expansion(
            0x52A5U,
            static_cast<Byte>((select ? 0x10U : 0U) | (clock ? 0x04U : 0U) | (data ? 0x01U : 0U)));
    }

    bool clock(bool data = false) {
        pins(true, false, data);
        pins(true, true, data);
        return (mapper_.read_expansion(0x55A5U) & 0x04U) == 0U;
    }

    void begin() {
        pins(false, false);
        pins(true, false);
    }

    void send(unsigned value, unsigned bits) {
        for (unsigned bit = bits; bit-- > 0U;)
            (void)clock((value & (1U << bit)) != 0U);
    }

    Byte read(Word address) {
        begin();
        send(0x0C00U | address, 12U);
        Byte result = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit)
            result = static_cast<Byte>((result << 1U) | (clock() ? 1U : 0U));
        finish();
        return result;
    }

    void command(unsigned command) {
        begin();
        send(command, 12U);
    }

    void write(Word address, Byte value) {
        begin();
        send(0x0A00U | address, 12U);
        send(value, 8U);
        finish();
    }

    void finish() {
        pins(false, false);
    }

    bool selected_output() {
        pins(true, false);
        return (mapper_.read_expansion(0x5500U) & 0x04U) == 0U;
    }

  private:
    Fs304& mapper_;
};

void tick(Fs304& mapper, unsigned cycles = 9000U) {
    for (unsigned i = 0U; i < cycles; ++i)
        mapper.clock_cpu();
}

TEST_CASE("Mapper 164 decodes every PRG register value and both banking modes", "[mapper164]") {
    auto m = board();
    constexpr std::size_t kPages = 32U;
    for (unsigned high = 0U; high < 4U; ++high) {
        m->write_expansion(0x51FFU, static_cast<Byte>(high));
        for (unsigned low = 0U; low < 256U; ++low) {
            m->write_expansion(0x50A5U, static_cast<Byte>(low));
            const auto outer = high * 32U;
            INFO(high);
            INFO(low);
            if ((low & 0x10U) != 0U) {
                const auto bank = outer | ((low & 15U) << 1U);
                REQUIRE(m->maps_prg(0x8000U) == ((low & 0x40U) != 0U));
                REQUIRE(m->maps_prg(0xFFFFU) == ((low & 0x40U) != 0U));
                if ((low & 0x40U) != 0U) {
                    REQUIRE(m->read_prg(0x8000U) == bank % kPages);
                    REQUIRE(m->read_prg(0xBFFFU) == bank % kPages);
                    REQUIRE(m->read_prg(0xC000U) == (bank + 1U) % kPages);
                    REQUIRE(m->read_prg(0xFFFFU) == (bank + 1U) % kPages);
                }
            } else {
                const auto lower = outer | ((low & 0x20U) >> 1U) | (low & 15U);
                const auto upper =
                    outer | ((low & 0x40U) == 0U ? 0x1FU : (0x1CU | ((low & 1U) << 1U)));
                REQUIRE(m->maps_prg(0x8000U));
                REQUIRE(m->maps_prg(0xFFFFU));
                REQUIRE(m->read_prg(0x8000U) == lower % kPages);
                REQUIRE(m->read_prg(0xBFFFU) == lower % kPages);
                REQUIRE(m->read_prg(0xC000U) == upper % kPages);
                REQUIRE(m->read_prg(0xFFFFU) == upper % kPages);
            }
        }
    }
}

TEST_CASE("Mapper 164 bounds decoded banks before wrapping physical ROM", "[mapper164]") {
    for (std::size_t size = 32768U; size <= 2097152U; size *= 2U) {
        auto m = board(header(), size);
        const auto pages = size / 16384U;
        m->write_expansion(0x5100U, 0xFFU);
        for (unsigned low : {0x00U, 0x01U, 0x2FU, 0x40U, 0x7FU, 0x50U, 0xDFU}) {
            m->write_expansion(0x5000U, static_cast<Byte>(low));
            const auto outer = 3U * 32U;
            const auto lower = (low & 0x10U) != 0U ? outer | ((low & 15U) << 1U)
                                                   : outer | ((low & 0x20U) >> 1U) | (low & 15U);
            REQUIRE(m->read_prg(0x8123U) == lower % pages);
            REQUIRE(m->read_prg(0xF123U) ==
                    (((low & 0x10U) != 0U
                          ? lower + 1U
                          : outer | ((low & 0x40U) == 0U ? 0x1FU : 0x1CU | ((low & 1U) << 1U))) %
                     pages));
        }
    }
}

TEST_CASE("Mapper 164 register pages alias and completed reads latch open-bus data",
          "[mapper164]") {
    auto m = board();
    for (unsigned offset = 0U; offset < 256U; ++offset) {
        m->observe_cpu_read(static_cast<Address>(0x5000U | offset), 0x50U);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        m->observe_cpu_read(static_cast<Address>(0x5100U | offset), 0xFFU);
        m->write_expansion(static_cast<Address>(0x5000U | offset), 0x0FU);
        REQUIRE(m->read_prg(0x8000U) == 15U);
        m->write_expansion(static_cast<Address>(0x5000U | offset), 0x50U);
        m->write_expansion(static_cast<Address>(0x5200U | offset), 0xFFU);
        m->write_expansion(static_cast<Address>(0x5300U | offset), 0x00U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    }
    m->observe_cpu_read(0x5400U, 0x10U);
    m->observe_cpu_read(0x5500U, 0x00U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const auto page = addr & 0xFF00U;
        const bool mapped = (page >= 0x5000U && page <= 0x5300U) || page == 0x5500U;
        REQUIRE(m->maps_expansion(static_cast<Address>(addr)) == mapped);
        REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) ==
                (page == 0x5500U ? 0x04U : 0U));
    }
}

TEST_CASE("Mapper 164 applies mirroring control only in 32 KiB mode", "[mapper164]") {
    auto m = board();
    for (unsigned low = 0U; low < 256U; ++low)
        for (unsigned mirror = 0U; mirror < 256U; ++mirror) {
            m->write_expansion(0x5000U, static_cast<Byte>(low));
            m->write_expansion(0x5300U, static_cast<Byte>(mirror));
            const auto expected = (low & 0x10U) == 0U || (mirror & 0x80U) != 0U
                                      ? MirrorMode::Vertical
                                      : MirrorMode::Horizontal;
            REQUIRE(m->mirror_mode() == expected);
        }
}

TEST_CASE("Mapper 164 work RAM populations mirror correctly and remain volatile", "[mapper164]") {
    for (const auto wram : {0UZ, 2048UZ, 8192UZ}) {
        auto m = board(header(false, wram));
        REQUIRE(m->battery_ram().size() == 512U);
        REQUIRE(std::ranges::all_of(m->battery_ram(), [](Byte value) { return value == 0xFFU; }));
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            REQUIRE(m->maps_prg(static_cast<Address>(addr)) == (wram != 0U));
            m->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        }
        if (wram != 0U) {
            for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        static_cast<Byte>(0x6000U + ((addr - 0x6000U) % wram)));
        }
        m->write_chr(0x0123U, 0xA7U);
        std::array<Byte, 512> saved{};
        for (std::size_t i = 0U; i < saved.size(); ++i)
            saved[i] = static_cast<Byte>(i ^ 0xA5U);
        m->set_battery_ram(saved);
        m->reset();
        REQUIRE(m->read_chr(0x0123U) == 0xA7U);
        REQUIRE(std::ranges::equal(m->battery_ram(), saved));
        m->power_on();
        REQUIRE(m->read_chr(0x0123U) == 0U);
        REQUIRE(std::ranges::equal(m->battery_ram(), saved));
        if (wram != 0U)
            REQUIRE(m->read_prg(0x6000U) == 0U);
        const auto before = state(*m);
        m->set_battery_ram(std::span(saved).first(511U));
        REQUIRE(state(*m) == before);
    }
    auto legacy = board(header(true));
    legacy->write_prg(0x6000U, 0x39U);
    REQUIRE(legacy->read_prg(0x6800U) == 0x39U);
    REQUIRE(legacy->read_prg(0x7800U) == 0x39U);
}

TEST_CASE("Mapper 164 reset restores fixed bank 31 and vertical CIRAM but retains RAM",
          "[mapper164]") {
    auto m = board();
    m->write_expansion(0x5000U, 0xD7U);
    m->write_expansion(0x5100U, 3U);
    m->write_expansion(0x5300U, 0U);
    m->write_prg(0x6000U, 0x59U);
    m->write_chr(0x1000U, 0xA7U);
    m->observe_ppu_address(0x2201U);
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_prg(0xC000U) == 31U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(m->read_prg(0x6000U) == 0x59U);
    REQUIRE(m->read_chr(0x1000U) == 0U);
    m->write_chr(0x1000U, 0x73U);
    REQUIRE(m->read_chr(0x1000U) == 0x73U);
}

TEST_CASE("Mapper 164 1 bpp mode uses the last A13-rise A0 and A9 latch", "[mapper164]") {
    auto m = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        m->write_chr(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
    m->write_expansion(0x5000U, 0x80U);
    m->observe_ppu_address(0x2001U);
    m->observe_ppu_address(0x2200U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto mapped = static_cast<Address>((addr & 0x0EF7U) | 0x0008U);
        REQUIRE(m->read_chr(addr) == static_cast<Byte>(mapped ^ (mapped >> 8U)));
    }
    m->observe_ppu_address(0x0000U);
    m->observe_ppu_address(0x2200U);
    m->write_chr(0x0000U, 0x39U);
    REQUIRE(m->read_chr(0x1000U) == 0x39U);
    REQUIRE(m->read_chr(0x0008U) == 0x39U);
    m->write_expansion(0x5000U, 0U);
    m->write_chr(0x0000U, 0xA7U);
    REQUIRE(m->read_chr(0x0000U) == 0xA7U);
    REQUIRE(m->read_chr(0x1000U) == 0x39U);
    REQUIRE(m->read_chr(0x0008U) != 0x39U);
    REQUIRE(m->read_chr(0x1008U) != 0x39U);
}

TEST_CASE("Mapper 164 EEPROM reads sequentially and wraps with inverted D2", "[mapper164]") {
    auto m = board();
    std::array<Byte, 512> data{};
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>(i * 37U + 11U);
    m->set_battery_ram(data);
    Serial serial(*m);
    REQUIRE(serial.read(0U) == data[0U]);
    REQUIRE(serial.read(0x01FFU) == data[0x01FFU]);
    serial.command(0x0D23U);
    for (unsigned bit = 0U; bit < 8U; ++bit)
        REQUIRE(serial.clock() == ((data[0x123U] & (0x80U >> bit)) != 0U));
    for (unsigned bit = 0U; bit < 8U; ++bit)
        REQUIRE(serial.clock() == ((data[0x124U] & (0x80U >> bit)) != 0U));
    serial.finish();
    serial.command(0x0DFFU);
    for (unsigned bit = 0U; bit < 8U; ++bit)
        (void)serial.clock();
    for (unsigned bit = 0U; bit < 8U; ++bit)
        REQUIRE(serial.clock() == ((data[0U] & (0x80U >> bit)) != 0U));
    serial.finish();
    REQUIRE((m->read_expansion(0x5500U) & 0x04U) == 0U);
}

TEST_CASE("Mapper 164 EEPROM honors write protection and READY BUSY timing", "[mapper164]") {
    auto m = board();
    Serial serial(*m);
    serial.write(0x0042U, 0x39U);
    tick(*m);
    REQUIRE(serial.read(0x0042U) == 0xFFU);
    serial.command(0x0980U);
    serial.finish();
    serial.write(0x0042U, 0x39U);
    REQUIRE(m->battery_ram()[0x42U] == 0xFFU);
    REQUIRE_FALSE(serial.selected_output());
    serial.finish();
    tick(*m, 8999U);
    REQUIRE(m->battery_ram()[0x42U] == 0xFFU);
    tick(*m, 1U);
    REQUIRE(m->battery_ram()[0x42U] == 0x39U);
    REQUIRE(serial.selected_output());
    serial.finish();
    serial.command(0x0800U);
    serial.finish();
    serial.write(0x0042U, 0xA7U);
    tick(*m);
    REQUIRE(serial.read(0x0042U) == 0x39U);
}

TEST_CASE("Mapper 164 EEPROM implements erase, write-all, erase-all and clock rejection",
          "[mapper164]") {
    auto m = board();
    Serial serial(*m);
    serial.command(0x0980U);
    serial.finish();
    serial.write(0x0010U, 0x39U);
    tick(*m);
    serial.command(0x0E10U);
    serial.finish();
    tick(*m);
    REQUIRE(serial.read(0x0010U) == 0xFFU);
    serial.command(0x0880U);
    serial.send(0xA7U, 8U);
    serial.finish();
    tick(*m);
    REQUIRE(std::ranges::all_of(m->battery_ram(), [](Byte value) { return value == 0xA7U; }));
    serial.command(0x0900U);
    serial.finish();
    tick(*m);
    REQUIRE(std::ranges::all_of(m->battery_ram(), [](Byte value) { return value == 0xFFU; }));
    serial.write(0x0020U, 0x59U);
    tick(*m);
    serial.begin();
    serial.send(0x0A20U, 12U);
    serial.send(0xA7U, 8U);
    (void)serial.clock(false);
    serial.finish();
    tick(*m);
    REQUIRE(serial.read(0x0020U) == 0x59U);
    serial.begin();
    serial.send(0x0A20U, 12U);
    serial.send(0xA7U, 7U);
    serial.finish();
    tick(*m);
    REQUIRE(serial.read(0x0020U) == 0x59U);
}

TEST_CASE("Mapper 164 snapshots replay partial EEPROM reads and active writes",
          "[mapper164][state]") {
    auto m = board();
    std::array<Byte, 512> data{};
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>(i ^ (i >> 3U));
    m->set_battery_ram(data);
    m->write_expansion(0x5000U, 0xD5U);
    m->write_expansion(0x5100U, 2U);
    m->write_expansion(0x5300U, 0U);
    m->observe_ppu_address(0x2201U);
    m->write_prg(0x6000U, 0x59U);
    m->write_chr(0x0000U, 0x73U);
    Serial input(*m);
    input.command(0x0DFFU);
    (void)input.clock();
    (void)input.clock();
    const auto saved = state(*m);
    auto copy = board();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    Serial original_serial(*m);
    Serial copy_serial(*copy);
    for (unsigned bit = 0U; bit < 30U; ++bit)
        REQUIRE(original_serial.clock() == copy_serial.clock());
    input.finish();
    original_serial.finish();
    copy_serial.finish();

    auto source = board();
    Serial writer(*source);
    writer.command(0x0980U);
    writer.finish();
    writer.write(0x0123U, 0xA7U);
    const auto busy = state(*source);
    auto replay = board();
    load(*replay, busy);
    tick(*source);
    tick(*replay);
    REQUIRE(source->battery_ram()[0x123U] == 0xA7U);
    REQUIRE(std::ranges::equal(source->battery_ram(), replay->battery_ram()));
    REQUIRE(state(*source) == state(*replay));

    auto partial = board();
    Serial original(*partial);
    original.command(0x0980U);
    original.finish();
    original.begin();
    original.send(0x0A10U, 12U);
    original.send(0x0AU, 4U);
    const auto mid_write = state(*partial);
    auto restored = board();
    load(*restored, mid_write);
    Serial resumed(*restored);
    for (unsigned bit = 0U; bit < 4U; ++bit) {
        const bool data_bit = (0x07U & (0x08U >> bit)) != 0U;
        REQUIRE(original.clock(data_bit) == resumed.clock(data_bit));
    }
    original.finish();
    resumed.finish();
    tick(*partial);
    tick(*restored);
    REQUIRE(partial->battery_ram()[0x10U] == 0xA7U);
    REQUIRE(std::ranges::equal(partial->battery_ram(), restored->battery_ram()));
}

TEST_CASE("Mapper 164 rejects malformed and cross-geometry states atomically",
          "[mapper164][state]") {
    auto m = board();
    m->write_expansion(0x5000U, 0xD5U);
    m->write_expansion(0x5100U, 2U);
    m->write_prg(0x6000U, 0x59U);
    m->write_chr(0U, 0xA7U);
    Serial serial(*m);
    serial.command(0x0980U);
    serial.finish();
    serial.write(0x0010U, 0x39U);
    const auto saved = state(*m);
    constexpr std::size_t kEeprom = 9U + 8192U + 8192U;
    for (std::size_t length : {0UZ, 1UZ, 8UZ, 9UZ, 100UZ, 9000UZ, saved.size() - 1U}) {
        auto copy = board();
        load(*copy, saved);
        const auto before = state(*copy);
        StateReader reader{std::span(saved).first(length)};
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == before);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 9>{{
             {0U, 0U},
             {3U, 4U},
             {4U, 0x80U},
             {5U, 1U},
             {6U, 2U},
             {7U, 2U},
             {8U, 2U},
             {kEeprom + 512U, 0xFFU},
             {kEeprom + 522U, 0U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        auto copy = board();
        load(*copy, saved);
        const auto before = state(*copy);
        StateReader reader(bad);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == before);
    }
    auto other = board(header(false, 8192U));
    const auto before = state(*other);
    StateReader reader(saved);
    other->load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(*other) == before);
}

TEST_CASE("Mapper 164 validates ROM, RAM, persistence and CIRAM metadata", "[mapper164]") {
    REQUIRE(Fs304::validate_geometry(header(true), 524288U, 0U));
    for (unsigned kind = 0U; kind < 19U; ++kind) {
        auto h = header();
        std::size_t prg = 524288U;
        std::size_t chr = 0U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = false;
            break;
        case 2U:
            h.prg_nvram_size = 0U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.prg_ram_size = 4096U;
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
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 10U:
            prg = 16384U;
            break;
        case 11U:
            prg = 4194304U;
            break;
        case 12U:
            prg = 49152U;
            break;
        case 13U:
            chr = 8192U;
            break;
        case 14U:
            h = header(true);
            h.has_battery = false;
            break;
        case 15U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 16U:
            h.prg_ram_size = 1U;
            break;
        case 17U:
            prg = 0U;
            break;
        case 18U:
            chr = 1U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Fs304::validate_geometry(h, prg, chr));
    }
}

TEST_CASE("Mapper 164 exposes no IRQ or expansion audio", "[mapper164]") {
    auto m = board();
    REQUIRE_FALSE(m->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->audio_output() == 0.0F);
}

} // namespace
