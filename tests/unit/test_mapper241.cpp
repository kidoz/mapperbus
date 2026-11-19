#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "../support/lpc_stream.hpp"
#include "../support/mapper241_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/lpc_speech.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false,
                  bool vertical = true,
                  bool battery = false,
                  bool chr_rom = false) {
    INesHeader h{};
    h.mapper_number = 241U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.has_battery = battery;
    h.prg_ram_size = battery ? 0U : 8192U;
    h.prg_nvram_size = battery ? 8192U : 0U;
    h.chr_ram_size = chr_rom ? 0U : 8192U;
    return h;
}
std::vector<Byte> pages(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t a = 0; a < size; ++a)
        bytes[a] = static_cast<Byte>(a / 32768U);
    return bytes;
}
std::unique_ptr<Mapper> board(std::size_t size = 131072U,
                              bool legacy = false,
                              bool vertical = true,
                              bool battery = false,
                              bool chr_rom = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy, vertical, battery, chr_rom),
                                               pages(size),
                                               chr_rom ? std::vector<Byte>(8192U, 0x23U)
                                                       : std::vector<Byte>{});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 241 registers both WRAM and CHR populations over full-byte PRG geometry",
          "[mapper241]") {
    for (std::size_t size = 32768U; size <= 8388608U; size *= 2U)
        for (bool legacy : {false, true})
            for (bool vertical : {false, true})
                for (bool battery : {false, true})
                    for (bool chr_rom : {false, true}) {
                        auto m = board(size, legacy, vertical, battery, chr_rom);
                        for (unsigned data = 0; data < 256U; ++data) {
                            m->write_prg(0x8000U, static_cast<Byte>(data));
                            for (Address a : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU})
                                REQUIRE(m->read_prg(a) ==
                                        static_cast<Byte>(data & (size / 32768U - 1U)));
                            REQUIRE(m->mirror_mode() ==
                                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                        }
                        REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
                        REQUIRE(state(*m).size() == (chr_rom ? 8195U : 16387U));
                        REQUIRE_FALSE(m->has_expansion_audio());
                        REQUIRE_FALSE(m->irq_pending());
                    }
}
TEST_CASE("Mapper 241 decodes CPU addresses without bus conflicts or NINA overlays",
          "[mapper241]") {
    auto m = board(1048576U);
    for (unsigned a = 0; a <= 0xFFFFU; ++a) {
        m->reset();
        m->write_prg(static_cast<Address>(a), 0x8DU);
        REQUIRE(m->maps_prg(static_cast<Address>(a)) == (a >= 0x6000U));
        REQUIRE(m->read_prg(0x8000U) == (a >= 0x8000U ? 13U : 0U));
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(a)));
    }
    // A write through a ROM byte zero still latches all data bits.
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 0x80U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 31U);
    REQUIRE(m->read_prg(0xFFFFU) == 31U);
    for (unsigned a = 0x6000U; a < 0x8000U; ++a)
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
    for (unsigned a = 0x6000U; a < 0x8000U; ++a)
        REQUIRE(m->read_prg(static_cast<Address>(a)) == static_cast<Byte>(a));
    for (Address a : {0x7FFDU, 0x7FFEU, 0x7FFFU}) {
        m->write_prg(a, 0xA3U);
        REQUIRE(m->read_prg(a) == 0xA3U);
        REQUIRE(m->read_prg(0x8000U) == 31U);
        REQUIRE(m->read_chr(0U) == 0U);
    }
}
TEST_CASE("Mapper 241 keeps fixed CHR and distinguishes soft reset from cold RAM policy",
          "[mapper241]") {
    for (bool battery : {false, true})
        for (bool chr_rom : {false, true}) {
            auto m = board(131072U, false, true, battery, chr_rom);
            for (unsigned a = 0; a < 8192U; ++a)
                m->write_chr(static_cast<Address>(a), static_cast<Byte>(a));
            for (unsigned a = 0; a < 8192U; ++a)
                REQUIRE(m->read_chr(static_cast<Address>(a)) ==
                        (chr_rom ? 0x23U : static_cast<Byte>(a)));
            m->write_prg(0x6000U, 0xA7U);
            m->write_prg(0xFFFFU, 3U);
            m->reset();
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            REQUIRE(m->read_chr(0x123U) == 0x23U);
            const std::array<Byte, 2> save{0x91U, 0x83U};
            m->set_battery_ram(save);
            REQUIRE(m->read_prg(0x6000U) == (battery ? 0x91U : 0xA7U));
            m->power_on();
            REQUIRE(m->read_prg(0x6000U) == (battery ? 0x91U : 0U));
            REQUIRE(m->read_chr(0x123U) == (chr_rom ? 0x23U : 0U));
            if (battery) {
                std::vector<Byte> oversized(8193U, 0x6AU);
                m->set_battery_ram(oversized);
                REQUIRE(m->read_prg(0x7FFFU) == 0x6AU);
            }
        }
}
TEST_CASE("Mapper 241 leaves absent speech and hack registers on the CPU open bus", "[mapper241]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper241_rom());
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    for (Address a : {0x4020U, 0x4800U, 0x4FFFU, 0x5000U, 0x5FFFU}) {
        bus.write(a, 0xBFU);
        REQUIRE(bus.read(a) == 0xBFU);
        bus.write(0U, 0x81U);
        REQUIRE(bus.read(a) == 0x81U);
    }
    Ppu p;
    p.connect_cartridge(&*c);
    p.reset();
    bus.connect_ppu(&p);
    for (unsigned i = 0; i < 256U; ++i)
        bus.write(static_cast<Address>(0x6000U + i), static_cast<Byte>(i ^ 0xA7U));
    bus.write(0x4014U, 0x60U);
    for (unsigned i = 0; i < 256U; ++i) {
        p.write_register(3U, static_cast<Byte>(i));
        // OAM attribute bits 2-4 are physically absent.
        const auto mask = (i & 3U) == 2U ? 0xE3U : 0xFFU;
        REQUIRE(p.read_register(4U) == static_cast<Byte>((i ^ 0xA7U) & mask));
    }
    REQUIRE(c->configure_board_profile("standard"));
    REQUIRE_FALSE(c->configure_board_profile("unknown"));
    REQUIRE_FALSE(c->configure_board_jumpers(0U));
}
TEST_CASE("Mapper 241 rejects ambiguous memory shapes and unsupported native metadata",
          "[mapper241]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, std::size_t p, std::size_t c) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (auto size : {0U, 16384U, 32767U, 32769U, 98304U, 8388609U})
        reject(header(), size, 0U);
    for (auto size : {1U, 4096U, 8193U, 16384U})
        reject(header(), 32768U, size);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h, 32768U, 0U);
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        reject(h, 32768U, 0U);
    }
    for (bool battery : {false, true})
        for (bool chr_rom : {false, true}) {
            const auto valid = header(false, true, battery, chr_rom);
            for (unsigned field = 0; field < 5U; ++field) {
                auto h = valid;
                switch (field) {
                case 0:
                    h.prg_ram_size = 4096U;
                    break;
                case 1:
                    h.prg_nvram_size = 4096U;
                    break;
                case 2:
                    h.chr_ram_size = 4096U;
                    break;
                case 3:
                    h.chr_nvram_size = 8192U;
                    break;
                case 4:
                    h.has_battery = !battery;
                    break;
                }
                reject(h, 32768U, chr_rom ? 8192U : 0U);
            }
        }
}
TEST_CASE("Mapper 241 snapshots bind ROM geometry and memory profile and reject atomically",
          "[mapper241]") {
    for (bool chr_rom : {false, true}) {
        auto m = board(131072U, false, true, false, chr_rom);
        m->write_prg(0x8000U, 0xFFU);
        m->write_prg(0x6000U, 0xA7U);
        m->write_chr(0U, 0x5AU);
        const auto saved = state(*m);
        for (std::size_t n : {0U, 1U, 2U, 3U, 8194U}) {
            StateReader r(std::span<const Byte>(saved).first(n));
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        auto bad = saved;
        bad.pop_back();
        StateReader short_reader(bad);
        m->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
        REQUIRE(state(*m) == saved);
        for (std::size_t bit = 0; bit < 16U; ++bit) {
            bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            StateReader r(bad);
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned data = 0; data < 256U; ++data) {
            bad = saved;
            bad[2] = static_cast<Byte>(data);
            StateReader r(bad);
            m->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(m->read_prg(0xFFFFU) == data % 4U);
        }
        m->power_on();
        StateReader r(saved);
        m->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(state(*m) == saved);
        for (unsigned profile = 0; profile < 4U; ++profile) {
            auto other = board(profile == 0 ? 262144U : 131072U,
                               false,
                               profile != 1U,
                               profile == 2U,
                               profile == 3U ? !chr_rom : chr_rom);
            const auto before = state(*other);
            StateReader mismatch(saved);
            other->load_state(mismatch);
            REQUIRE_FALSE(mismatch.ok());
            REQUIRE(state(*other) == before);
        }
    }
}
TEST_CASE("Mapper 241 original CPU firmware checks banking WRAM CHR CIRAM reset and replay",
          "[mapper241]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (bool battery : {false, true})
                for (bool chr_rom : {false, true}) {
                    const auto bytes =
                        mapperbus::tests::make_mapper241_rom(legacy, vertical, battery, chr_rom);
                    auto c = Cartridge::from_data(bytes);
                    REQUIRE(c);
                    Emulator e;
                    REQUIRE(e.load_cartridge(std::move(*c), ""));
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0U);
                    REQUIRE(e.read_cpu(0x15U) == (chr_rom ? 0x23U : 0U));
                    const auto saved = e.save_state();
                    e.step_frame();
                    const auto replay = e.save_state();
                    REQUIRE(e.load_state(saved));
                    e.step_frame();
                    REQUIRE(e.save_state() == replay);
                    e.reset();
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0xA7U);
                    REQUIRE(e.read_cpu(0x15U) == (chr_rom ? 0x23U : 0x5AU));
                    e.power_on();
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == (battery ? 0xA7U : 0U));
                    const auto before = e.save_state();
                    StateWriter controllers;
                    Controller{}.save_state(controllers);
                    Controller{}.save_state(controllers);
                    auto bad = saved;
                    const auto mapper_size = chr_rom ? 8195U : 16387U;
                    const auto mapper_offset = bad.size() - controllers.data().size() - mapper_size;
                    bad[mapper_offset] ^= 0x80U;
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                    auto truncated = saved;
                    truncated.resize(truncated.size() - 10U);
                    REQUIRE_FALSE(e.load_state(truncated));
                    REQUIRE(e.save_state() == before);
                }
    // Exercise high NES 2.0 PRG size bits with executable banks 128 and 255.
    auto c = Cartridge::from_data(
        mapperbus::tests::make_mapper241_rom(false, true, false, false, 8388608U));
    REQUIRE(c);
    Emulator e;
    REQUIRE(e.load_cartridge(std::move(*c), ""));
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
}
} // namespace

TEST_CASE("Mapper 241 LPC profile installs a real speech device", "[mapper241][lpc]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("lpc"));
    REQUIRE(m->has_expansion_audio());
    REQUIRE(m->maps_expansion(0x5000U));
    REQUIRE(m->expansion_read_mask(0x5000U) == 0x40U);
}

namespace {
std::vector<Byte> speech_state(const LpcSpeech& speech) {
    StateWriter writer;
    speech.save_state(writer);
    return writer.take();
}
void feed(LpcSpeech& s, const mapperbus::tests::LpcStream& stream) {
    for (Byte nibble : stream.nibbles())
        s.write_nibble(nibble);
}
std::vector<int16_t> samples(LpcSpeech& s, unsigned size, uint32_t cpu_clock = 1789773U) {
    uint64_t cycles = 0U;
    std::vector<int16_t> result;
    for (unsigned n = 0; n < size; ++n) {
        const auto chip_clocks = 16000ULL + 80ULL * (n + 1U);
        const auto end = (chip_clocks * cpu_clock + 767999U) / 768000U;
        while (cycles < end) {
            s.clock();
            ++cycles;
        }
        result.push_back(s.output());
    }
    return result;
}
TEST_CASE("LPC readiness counts real FIFO bits and bounded overflow preserves queued input",
          "[mapper241][lpc]") {
    LpcSpeech s;
    REQUIRE_FALSE(s.ready());
    for (unsigned n = 0; n < 37286U; ++n)
        s.clock();
    REQUIRE_FALSE(s.ready());
    s.clock();
    REQUIRE(s.ready());
    for (unsigned n = 0; n < 15U; ++n)
        s.write_nibble(0xFFU);
    REQUIRE(s.ready());
    s.write_nibble(0xFFU);
    REQUIRE_FALSE(s.ready());
    for (unsigned n = 16U; n < 32U; ++n)
        s.write_nibble(0x5U);
    const auto full = speech_state(s);
    REQUIRE(full.size() == 129U);
    REQUIRE(full[19] == 128U);
    for (unsigned n = 0; n < 256U; ++n)
        s.write_nibble(static_cast<Byte>(n));
    REQUIRE(speech_state(s) == full);
    s.reset();
    REQUIRE_FALSE(s.ready());
    REQUIRE(s.output() == 0);
    REQUIRE(speech_state(s) == speech_state(LpcSpeech{}));
}
TEST_CASE("LPC synthetic voiced parameters produce known chirp samples at every region clock",
          "[mapper241][lpc]") {
    mapperbus::tests::LpcStream stream;
    stream.frame();
    stream.frame(15U);
    const std::array<int16_t, 3> prefix{0, 512, 3328};
    std::vector<int16_t> ntsc;
    for (const auto& [region, clock] : {std::pair{Region::NTSC, 1789773U},
                                        std::pair{Region::PAL, 1662607U},
                                        std::pair{Region::Dendy, 1773448U}}) {
        LpcSpeech s;
        s.set_region(region);
        feed(s, stream);
        const auto pcm = samples(s, 202U, clock);
        REQUIRE(std::ranges::equal(std::span(pcm).first(3U), prefix));
        REQUIRE(std::ranges::any_of(pcm, [](int16_t x) { return x > 0; }));
        REQUIRE(pcm[200] == 0);
        REQUIRE(pcm[201] == 0);
        if (region == Region::NTSC)
            ntsc = pcm;
        else
            REQUIRE(pcm == ntsc);
        REQUIRE(s.ready());
    }
}
TEST_CASE(
    "LPC repeated and explicit frames agree and noise silence and stop have distinct behavior",
    "[mapper241][lpc]") {
    for (unsigned pitch : {0U, 20U}) {
        mapperbus::tests::LpcStream repeated, explicit_frames;
        repeated.frame(14U, pitch);
        repeated.frame(14U, pitch, true);
        repeated.frame(15U);
        explicit_frames.frame(14U, pitch);
        explicit_frames.frame(14U, pitch);
        explicit_frames.frame(15U);
        LpcSpeech a, b;
        feed(a, repeated);
        feed(b, explicit_frames);
        const auto first = samples(a, 402U), second = samples(b, 402U);
        REQUIRE(first == second);
        REQUIRE(first[400] == 0);
        REQUIRE(std::ranges::any_of(first, [](int16_t x) { return x != 0; }));
        if (pitch == 0U) {
            REQUIRE(std::ranges::any_of(first, [](int16_t x) { return x < 0; }));
            REQUIRE(std::ranges::any_of(first, [](int16_t x) { return x > 0; }));
        }
    }
    mapperbus::tests::LpcStream silence;
    silence.frame(0U);
    silence.frame(15U);
    LpcSpeech s;
    feed(s, silence);
    REQUIRE(std::ranges::all_of(samples(s, 202U), [](int16_t x) { return x == 0; }));
}
TEST_CASE("LPC partial frames and mid-utterance states resume exactly and reject malformed states "
          "atomically",
          "[mapper241][lpc]") {
    mapperbus::tests::LpcStream stream;
    stream.frame();
    stream.frame(14U, 0U);
    stream.frame(15U);
    const auto nibbles = stream.nibbles();
    LpcSpeech a;
    for (unsigned i = 0; i < 7U; ++i)
        a.write_nibble(nibbles[i]);
    REQUIRE(samples(a, 1U).back() == 0);
    const auto partial = speech_state(a);
    REQUIRE(partial[28] != 0U);
    LpcSpeech b;
    StateReader partial_reader(partial);
    b.load_state(partial_reader);
    REQUIRE(partial_reader.ok());
    for (std::size_t i = 7U; i < nibbles.size(); ++i) {
        a.write_nibble(nibbles[i]);
        b.write_nibble(nibbles[i]);
    }
    for (unsigned i = 0; i < 25000U; ++i) {
        a.clock();
        b.clock();
        REQUIRE(a.output() == b.output());
    }
    const auto mid = speech_state(a);
    REQUIRE(a.output() != 0);
    LpcSpeech restored;
    StateReader r(mid);
    restored.load_state(r);
    REQUIRE(r.ok());
    for (unsigned i = 0; i < 80000U; ++i) {
        a.clock();
        restored.clock();
        REQUIRE(a.output() == restored.output());
        REQUIRE(a.ready() == restored.ready());
    }
    REQUIRE(speech_state(a) == speech_state(restored));
    const auto before = speech_state(a);
    for (std::size_t length = 0; length < before.size(); ++length) {
        StateReader bad{std::span(before).first(length)};
        a.load_state(bad);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(speech_state(a) == before);
    }
    for (const auto& [offset, value] : {std::pair{0U, 0U},
                                        {18U, 128U},
                                        {19U, 129U},
                                        {23U, 255U},
                                        {25U, 255U},
                                        {26U, 80U},
                                        {27U, 200U},
                                        {28U, 13U},
                                        {29U, 2U},
                                        {30U, 2U},
                                        {31U, 2U},
                                        {32U, 159U},
                                        {35U, 1U}}) {
        auto bytes = before;
        bytes[offset] = static_cast<Byte>(value);
        StateReader bad(bytes);
        a.load_state(bad);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(speech_state(a) == before);
    }
    for (auto offset : {33U, 37U, 63U, 89U, 109U}) {
        auto bytes = before;
        bytes[offset] = 0xFFU;
        bytes[offset + 1U] = 0x7FU;
        StateReader bad(bytes);
        a.load_state(bad);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(speech_state(a) == before);
    }
}
TEST_CASE("Mapper 241 speech decode drives D6 only with exact control pins and isolated banking",
          "[mapper241][lpc]") {
    auto m = board();
    REQUIRE(m->configure_board_profile("lpc"));
    for (unsigned a = 0x4020U; a < 0x6000U; ++a) {
        REQUIRE(m->maps_expansion(static_cast<Address>(a)) == (a >= 0x5000U));
        REQUIRE(m->expansion_read_mask(static_cast<Address>(a)) == (a >= 0x5000U ? 0x40U : 0U));
    }
    for (unsigned data = 0U; data < 256U; ++data) {
        m->reset();
        for (unsigned i = 0; i < 40000U; ++i)
            m->clock_audio();
        const auto before = state(*m);
        m->write_expansion(0x5FFFU, static_cast<Byte>(data));
        const auto after = state(*m);
        if ((data & 0x50U) == 0x50U)
            REQUIRE(after[16387U + 19U] == 4U);
        else if ((data & 0x50U) == 0U)
            REQUIRE(m->read_expansion(0x5000U) == 0U);
        else
            REQUIRE(after == before);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_chr(0U) == 0U);
    }
    auto plain = board();
    const auto plain_state = state(*plain), speech = state(*m);
    StateReader r(speech);
    plain->load_state(r);
    REQUIRE_FALSE(r.ok());
    REQUIRE(state(*plain) == plain_state);
    StateReader incompatible(plain_state);
    m->load_state(incompatible);
    REQUIRE_FALSE(incompatible.ok());
    REQUIRE(state(*m) == speech);
    auto bad = speech;
    bad.back() = 0x7FU;
    bad[bad.size() - 2U] = 0xFFU;
    StateReader malformed(bad);
    m->load_state(malformed);
    REQUIRE_FALSE(malformed.ok());
    REQUIRE(state(*m) == speech);
    for (bool cold : {false, true}) {
        m->write_prg(0x6000U, 0xA7U);
        m->write_chr(0U, 0x5AU);
        m->write_prg(0xFFFFU, 3U);
        if (cold)
            m->power_on();
        else
            m->reset();
        REQUIRE(m->read_expansion(0x5000U) == 0U);
        REQUIRE(m->audio_output() == 0.0f);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0x6000U) == (cold ? 0U : 0xA7U));
        REQUIRE(m->read_chr(0U) == (cold ? 0U : 0x5AU));
    }
}
} // namespace

TEST_CASE("Mapper 241 LPC firmware reaches the APU mixer across regions and replays mid speech",
          "[mapper241][lpc][integration]") {
    register_builtin_mappers();
    std::vector<Byte> speech_snapshot;
    for (Region region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        auto bytes = mapperbus::tests::make_mapper241_lpc_rom();
        bytes[12] = region == Region::PAL ? 1U : region == Region::Dendy ? 3U : 0U;
        auto c = Cartridge::from_data(bytes);
        REQUIRE(c);
        REQUIRE(c->configure_board_profile("lpc"));
        Emulator e;
        REQUIRE(e.load_cartridge(std::move(*c), ""));
        std::array<float, 8192> audio{};
        for (unsigned i = 0; i < 2U; ++i) {
            e.step_frame();
            (void)e.drain_audio(audio.data(), audio.size());
        }
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x6000U) == 0xA7U);
        const auto before = e.save_state();
        if (region == Region::NTSC)
            speech_snapshot = before;
        e.step_frame();
        const auto expected = e.save_state();
        const auto size = e.drain_audio(audio.data(), audio.size());
        REQUIRE(size > 0U);
        const auto [minimum, maximum] = std::ranges::minmax_element(std::span(audio).first(size));
        REQUIRE(*maximum - *minimum > 0.005f);
        REQUIRE(std::ranges::any_of(std::span(audio).first(size),
                                    [](float x) { return std::abs(x) > 0.001f; }));
        REQUIRE(std::ranges::all_of(std::span(audio).first(size),
                                    [](float x) { return std::isfinite(x); }));
        REQUIRE(e.load_state(before));
        e.step_frame();
        REQUIRE(e.save_state() == expected);
        auto bad = expected;
        bad.resize(bad.size() - 30U);
        REQUIRE_FALSE(e.load_state(bad));
        REQUIRE(e.save_state() == expected);
        e.set_region(Region::NTSC);
        e.reset();
        e.step_frame();
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
    }
    // Open bus can look ready on an unpopulated board. It still contributes no
    // speech audio, and rejects a populated-board snapshot of the same ROM.
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper241_lpc_rom());
    REQUIRE(c);
    Emulator plain;
    REQUIRE(plain.load_cartridge(std::move(*c), ""));
    std::array<float, 8192> silent{};
    for (unsigned n = 0; n < 2U; ++n) {
        plain.step_frame();
        (void)plain.drain_audio(silent.data(), silent.size());
    }
    plain.step_frame();
    const auto count = plain.drain_audio(silent.data(), silent.size());
    REQUIRE(count > 0U);
    const auto [minimum, maximum] = std::ranges::minmax_element(std::span(silent).first(count));
    REQUIRE(*maximum - *minimum < 0.001f);
    const auto baseline = plain.save_state();
    REQUIRE_FALSE(plain.load_state(speech_snapshot));
    REQUIRE(plain.save_state() == baseline);
}

TEST_CASE("LPC coefficient extremes and interpolation transitions stay resumable",
          "[mapper241][lpc]") {
    for (unsigned stage = 0U; stage < 10U; ++stage) {
        const unsigned width = stage < 2U ? 5U : stage < 7U ? 4U : 3U;
        for (unsigned index = 0U; index < (1U << width); ++index) {
            std::array<unsigned, 10> coefficients{23U, 9U, 8U, 6U, 7U, 6U, 6U, 3U, 3U, 3U};
            coefficients[stage] = index;
            mapperbus::tests::LpcStream stream;
            stream.frame(14U, 63U, false, coefficients);
            stream.frame(1U, 63U, true);
            stream.frame(0U);
            stream.frame(14U, 0U, false, coefficients);
            stream.frame(15U);
            LpcSpeech a;
            const auto nibbles = stream.nibbles();
            std::size_t sent = 0U;
            for (unsigned cycle = 0U; cycle < 160000U; ++cycle) {
                if (a.ready() && sent < nibbles.size())
                    a.write_nibble(nibbles[sent++]);
                a.clock();
            }
            REQUIRE(sent == nibbles.size());
            const auto checkpoint = speech_state(a);
            LpcSpeech b;
            StateReader r(checkpoint);
            b.load_state(r);
            REQUIRE(r.ok());
            for (unsigned i = 0; i < 1000U; ++i) {
                a.clock();
                b.clock();
            }
            REQUIRE(speech_state(a) == speech_state(b));
        }
    }
}
