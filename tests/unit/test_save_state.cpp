#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../support/fds_image.hpp"
#include "../support/legacy_state.hpp"
#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco163.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

[[nodiscard]] std::uint64_t hash_frame(const FrameBuffer& fb) {
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a
    for (std::uint32_t pixel : fb.pixels) {
        h = (h ^ pixel) * 1099511628211ULL;
    }
    return h;
}

} // namespace

TEST_CASE("Hybrid snapshots restore timing, active audio and regional emphasis",
          "[state][hybrid]") {
    register_builtin_mappers();
    // Continuous pulse and tinted backdrop, with no external media or BIOS.
    constexpr Byte kProgram[] = {
        0x78, 0xA9, 0x40, 0x8D, 0x17, 0x40, // SEI; inhibit frame IRQ
        0xA9, 0x01, 0x8D, 0x15, 0x40,       // enable pulse 1
        0xA9, 0xBF, 0x8D, 0x00, 0x40, 0xA9, 0xFD, 0x8D, 0x02, 0x40,
        0xA9, 0x08, 0x8D, 0x03, 0x40, 0xA9, 0x3F, 0x8D, 0x06, 0x20,
        0xA9, 0x00, 0x8D, 0x06, 0x20, 0xA9, 0x20, 0x8D, 0x07, 0x20, // white backdrop
        0xA9, 0x20, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20,
        0xA9, 0x20, 0x8D, 0x01, 0x20, // green emphasis on Hybrid
        0x4C, 0x38, 0x80,             // JMP $8038 (idle)
    };
    const auto path = mapperbus::tests::build_ines_rom("hybrid-snapshot", 0, 1, 1, kProgram);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(path.string()));
    emulator.set_region(Region::Dendy);
    emulator.reset();
    emulator.step_frame();
    const auto checkpoint = emulator.save_state();
    const auto replay = [&emulator] {
        for (int frame = 0; frame < 3; ++frame)
            emulator.step_frame();
    };
    replay();
    const auto expected = emulator.save_state();
    const auto pixels = emulator.frame_buffer().pixels;
    CHECK(pixels[0] == 0xFFBFFEBF);
    std::array<float, 8192> audio{};
    const auto sample_count = emulator.drain_audio(audio.data(), audio.size());
    REQUIRE(sample_count > 0);
    bool audible = false;
    for (std::size_t i = 0; i < sample_count; ++i)
        audible = audible || audio[i] > 0.001f || audio[i] < -0.001f;
    CHECK(audible);
    emulator.set_region(Region::NTSC);
    emulator.reset();
    REQUIRE(emulator.load_state(checkpoint));
    CHECK(emulator.region() == Region::Dendy);
    replay();
    CHECK(emulator.save_state() == expected);
    CHECK(emulator.frame_buffer().pixels == pixels);
    std::filesystem::remove(path);
}

TEST_CASE("Version 17 restores partially shifted external pads and validates their state",
          "[state][controller][expansion]") {
    Emulator emulator;
    emulator.set_controller_wiring(ControllerWiring::RfFamicom);
    emulator.set_expansion_controllers(3);
    emulator.expansion_controller().set_button_state(0, Button::Select, true);
    emulator.expansion_controller().set_button_state(1, Button::Start, true);
    emulator.expansion_controller().write(1);
    emulator.expansion_controller().write(0);
    CHECK((emulator.read_cpu(0x4016) & 2U) == 0);
    CHECK((emulator.read_cpu(0x4017) & 2U) == 0);
    const auto before = emulator.save_state();
    const auto replay = [&] {
        CHECK((emulator.read_cpu(0x4016) & 2U) == 0);
        CHECK((emulator.read_cpu(0x4016) & 2U) == 2);
        CHECK((emulator.read_cpu(0x4017) & 2U) == 0);
        CHECK((emulator.read_cpu(0x4017) & 2U) == 0);
        CHECK((emulator.read_cpu(0x4017) & 2U) == 2);
    };
    replay();
    const auto after = emulator.save_state();
    emulator.set_expansion_controllers(0);
    REQUIRE(emulator.load_state(before));
    CHECK(emulator.expansion_controllers() == 3);
    replay();
    CHECK(emulator.save_state() == after);

    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto attachment_offset =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size() - 10;
    for (const auto& [offset, value] : {std::pair{attachment_offset, Byte{4}},
                                        {after.size() - 1, Byte{2}},
                                        {after.size() - 1, Byte{1}}}) {
        auto corrupt = before;
        corrupt[offset] = value;
        REQUIRE_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == after);
    }
    for (std::size_t missing = 1; missing <= 5; ++missing) {
        REQUIRE_FALSE(emulator.load_state(std::span(before).first(before.size() - missing)));
        CHECK(emulator.save_state() == after);
    }
}

TEST_CASE("Version 16 imports detach external pads and preserve microphone and wiring",
          "[state][controller][expansion]") {
    Emulator emulator;
    emulator.set_controller_wiring(ControllerWiring::RfFamicom);
    emulator.set_microphone_active(true);
    emulator.set_expansion_controllers(3);
    emulator.expansion_controller().set_button_state(0, Button::A, true);
    const auto legacy = mapperbus::tests::legacy_state(emulator, 16);
    REQUIRE(emulator.load_state(legacy));
    CHECK(emulator.expansion_controllers() == 0);
    CHECK(emulator.controller_wiring() == ControllerWiring::RfFamicom);
    CHECK(emulator.microphone_active());
    CHECK((emulator.read_cpu(0x4016) & 6U) == 4);
    emulator.expansion_controller().write(1);
    CHECK(emulator.expansion_controller().peek(0) == 0);
}

TEST_CASE("External snapshot replay preserves a held CPU read enable",
          "[state][controller][expansion][cpu]") {
    register_builtin_mappers();
    constexpr Byte kProgram[] = {0xA9,
                                 0x01,
                                 0x8D,
                                 0x16,
                                 0x40, // strobe high
                                 0xA9,
                                 0x00,
                                 0x8D,
                                 0x16,
                                 0x40, // strobe low
                                 0xAD,
                                 0x16,
                                 0x40, // LDA $4016: hold /OE at instruction boundary
                                 0xAD,
                                 0x16,
                                 0x40, // next opcode fetch releases /OE, then reads B
                                 0x4C,
                                 0x00,
                                 0x80};
    const auto rom = mapperbus::tests::build_ines_rom("expansion-held-read", 0, 1, 1, kProgram);
    for (const auto mode :
         {ControllerMode::Standard, ControllerMode::FourScore, ControllerMode::HoriFourPlayer}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(rom.string()));
        emulator.reset();
        emulator.set_expansion_controllers(1);
        emulator.set_controller_mode(mode);
        emulator.controller().set_button_state(0, Button::B, true);
        emulator.expansion_controller().set_button_state(0, Button::B, true);
        for (int instruction = 0; instruction < 5; ++instruction)
            emulator.step_instruction();
        CHECK((emulator.cpu().a() & 3U) == 0);
        const auto before = emulator.save_state();
        CHECK(emulator.expansion_controller().peek(0) == 0); // A, not yet clocked
        emulator.step_instruction();
        CHECK(emulator.expansion_controller().peek(0) == 1); // B
        CHECK((emulator.cpu().a() & 3U) == (mode == ControllerMode::FourScore ? 1 : 3));
        const auto after = emulator.save_state();
        REQUIRE(emulator.load_state(before));
        CHECK(emulator.expansion_controller().peek(0) == 0);
        emulator.step_instruction();
        CHECK(emulator.save_state() == after);
    }
    std::filesystem::remove(rom);
}

TEST_CASE("Four Score snapshots resume independent partially read 24-bit reports",
          "[state][controller][four-score]") {
    register_builtin_mappers();
    constexpr Byte kProgram[] = {
        0xA9, 1, 0x8D, 0x16, 0x40, 0xA9, 0, 0x8D, 0x16, 0x40, 0x4C, 0x0A, 0x80};
    const auto rom = mapperbus::tests::build_ines_rom("four-score-state", 0, 1, 1, kProgram);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(rom.string()));
    emulator.reset();
    emulator.set_expansion_controllers(3);
    emulator.controller().set_button_state(0, Button::A, true);
    emulator.expansion_controller().set_button_state(0, Button::B, true);
    emulator.expansion_controller().set_button_state(1, Button::Right, true);
    emulator.set_controller_mode(ControllerMode::FourScore);
    for (int instruction = 0; instruction < 5; ++instruction)
        emulator.step_instruction();
    for (int bit = 0; bit < 11; ++bit)
        (void)emulator.read_cpu(0x4016);
    for (int bit = 0; bit < 7; ++bit)
        (void)emulator.read_cpu(0x4017);
    const auto before = emulator.save_state();
    const auto replay = [&] {
        const uint32_t reports[] = {0x080201, 0x048000};
        for (int port = 0; port < 2; ++port)
            for (unsigned bit = port == 0 ? 11 : 7; bit < 28; ++bit)
                CHECK((emulator.read_cpu(static_cast<Address>(0x4016 + port)) & 3U) ==
                      (bit < 24 ? (reports[port] >> bit & 1U) : 1U));
    };
    replay();
    const auto after = emulator.save_state();
    emulator.set_controller_mode(ControllerMode::Standard);
    REQUIRE(emulator.load_state(before));
    CHECK(emulator.controller_mode() == ControllerMode::FourScore);
    CHECK(emulator.expansion_controllers() == 3);
    replay();
    CHECK(emulator.save_state() == after);

    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto end =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size();
    for (const auto& [offset, value] : {std::pair{end - 9, Byte{3}},
                                        {end - 5, Byte{1}},
                                        {end - 1, Byte{0xFF}},
                                        {end - 9, Byte{0}}}) {
        auto invalid = before;
        invalid[offset] = value;
        REQUIRE_FALSE(emulator.load_state(invalid));
        CHECK(emulator.save_state() == after);
    }
    const auto legacy = mapperbus::tests::legacy_state(emulator, 17);
    REQUIRE(emulator.load_state(legacy));
    CHECK(emulator.controller_mode() == ControllerMode::Standard);
    CHECK(emulator.expansion_controllers() == 3);
    std::filesystem::remove(rom);
}

TEST_CASE("Hori snapshots preserve six pads and reject incompatible legacy mode values",
          "[state][controller][hori]") {
    register_builtin_mappers();
    constexpr Byte kProgram[] = {
        0xA9, 1, 0x8D, 0x16, 0x40, 0xA9, 0, 0x8D, 0x16, 0x40, 0x4C, 0x0A, 0x80};
    const auto rom = mapperbus::tests::build_ines_rom("hori-state", 0, 1, 1, kProgram);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(rom.string()));
    emulator.reset();
    emulator.set_expansion_controllers(3);
    emulator.controller().set_button_state(0, Button::A, true);
    emulator.controller().set_button_state(1, Button::B, true);
    emulator.expansion_controller().set_button_state(0, Button::A, true);
    emulator.expansion_controller().set_button_state(1, Button::Start, true);
    emulator.additional_expansion_controller().set_button_state(0, Button::B, true);
    emulator.additional_expansion_controller().set_button_state(1, Button::Right, true);
    emulator.set_controller_mode(ControllerMode::HoriFourPlayer);
    for (int instruction = 0; instruction < 5; ++instruction)
        emulator.step_instruction();
    for (int bit = 0; bit < 11; ++bit)
        (void)emulator.read_cpu(0x4016);
    for (int bit = 0; bit < 7; ++bit)
        (void)emulator.read_cpu(0x4017);
    const auto before = emulator.save_state();
    const auto replay = [&] {
        const uint32_t reports[] = {0x040201, 0x088008};
        for (int port = 0; port < 2; ++port)
            for (unsigned bit = port == 0 ? 11 : 7; bit < 28; ++bit) {
                const auto d0 = bit < 8 ? 0U : 1U;
                const auto d1 = bit < 24 ? reports[port] >> bit & 1U : 1U;
                CHECK((emulator.read_cpu(static_cast<Address>(0x4016 + port)) & 3U) ==
                      (d0 | d1 << 1U));
            }
    };
    replay();
    const auto after = emulator.save_state();
    emulator.set_controller_mode(ControllerMode::FourScore);
    REQUIRE(emulator.load_state(before));
    CHECK(emulator.controller_mode() == ControllerMode::HoriFourPlayer);
    replay();
    CHECK(emulator.save_state() == after);
    for (const auto offset : {before.size() - 1, before.size() - 6}) {
        for (const Byte invalid : {1, 2}) {
            auto corrupt = before;
            corrupt[offset] = invalid;
            REQUIRE_FALSE(emulator.load_state(corrupt));
            CHECK(emulator.save_state() == after);
        }
    }
    auto invalid_legacy = before;
    invalid_legacy.resize(invalid_legacy.size() - 5);
    const uint32_t version18 = 18;
    std::memcpy(invalid_legacy.data() + 4, &version18, sizeof(version18));
    REQUIRE_FALSE(emulator.load_state(invalid_legacy)); // v18 has no Hori mode
    CHECK(emulator.save_state() == after);

    emulator.set_controller_mode(ControllerMode::FourScore);
    const auto legacy = mapperbus::tests::legacy_state(emulator, 18);
    REQUIRE(emulator.load_state(legacy));
    CHECK(emulator.controller_mode() == ControllerMode::FourScore);
    CHECK(emulator.additional_expansion_controller().buttons(0) == 0);
    CHECK(emulator.additional_expansion_controller().buttons(1) == 0);
    CHECK(emulator.expansion_controllers() == 3);
    std::filesystem::remove(rom);
}

TEST_CASE("SHA-256 agrees with standard vectors including multi-block padding",
          "[state][identity]") {
    const auto hash = [](std::string_view input) {
        return sha256_hex(
            rom_sha256(std::span(reinterpret_cast<const Byte*>(input.data()), input.size())));
    };
    REQUIRE(hash("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(hash("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    REQUIRE(hash(std::string(1'000'000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("State identity rejects a different ROM with the same mapper", "[state][identity]") {
    register_builtin_mappers();
    const auto first = mapperbus::tests::write_visible_nrom_test_rom("identity-first");
    const auto other = mapperbus::tests::write_visible_nrom_test_rom("identity-other");
    { // A CHR byte changes content without changing mapper or ROM structure.
        std::fstream file(other, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('x');
    }
    Emulator source;
    REQUIRE(source.load_cartridge(first.string()));
    source.reset();
    const auto state = source.save_state();
    Emulator target;
    REQUIRE(target.load_cartridge(other.string()));
    target.reset();
    const auto before = target.save_state();
    REQUIRE_FALSE(target.load_state(state));
    REQUIRE(target.save_state() == before);
    auto wrong_hash = before;
    wrong_hash[11] ^= 1;
    REQUIRE_FALSE(target.load_state(wrong_hash));
    REQUIRE(target.save_state() == before);
    std::filesystem::remove(first);
    std::filesystem::remove(other);
}

TEST_CASE("Disk media identity rejects snapshots from another disk", "[state][identity][fds]") {
    const auto path = std::filesystem::temp_directory_path() / "mapperbus-identity-disk.fds";
    auto image = mapperbus::tests::make_fds_image();
    const auto write = [&] {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(image.data()),
                   static_cast<std::streamsize>(image.size()));
        REQUIRE(file.good());
    };
    write();
    Emulator source;
    REQUIRE(source.load_disk(path.string()));
    const auto state = source.save_state();
    image[100] ^= 1;
    write();
    Emulator target;
    REQUIRE(target.load_disk(path.string()));
    const auto before = target.save_state();
    REQUIRE_FALSE(target.load_state(state));
    REQUIRE(target.save_state() == before);
    image[100] ^= 1;
    write();
    REQUIRE(target.load_disk(path.string()));
    REQUIRE(target.load_state(state));
    std::filesystem::remove(path);
}

TEST_CASE("Version 3 states require explicit legacy import", "[state][identity]") {
    register_builtin_mappers();
    const auto rom = mapperbus::tests::write_visible_nrom_test_rom("identity-legacy");
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(rom.string()));
    emulator.reset();
    emulator.step_frame();
    auto original = emulator.save_state();
    StateWriter cpu, ppu;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    // Pre-v13 snapshots have no PPU I/O latch; import initializes it to zero.
    original[43 + cpu.data().size() + ppu.data().size() - 1] = 0;
    const auto version12 = mapperbus::tests::legacy_state(emulator, 12);
    REQUIRE(emulator.load_state(version12));
    REQUIRE(emulator.save_state() == original);
    const auto version11 = mapperbus::tests::legacy_state(emulator, 11);
    REQUIRE(emulator.load_state(version11));
    REQUIRE(emulator.save_state() == original);
    const auto version10 = mapperbus::tests::legacy_state(emulator, 10);
    REQUIRE(emulator.load_state(version10));
    REQUIRE(emulator.save_state() == original);
    const auto version9 = mapperbus::tests::legacy_state(emulator, 9);
    REQUIRE(emulator.load_state(version9));
    REQUIRE(emulator.save_state() == original);
    const auto version8 = mapperbus::tests::legacy_state(emulator, 8);
    REQUIRE(emulator.load_state(version8));
    REQUIRE(emulator.save_state() == original);
    const auto version7 = mapperbus::tests::legacy_state(emulator, 7);
    REQUIRE(emulator.load_state(version7));
    REQUIRE(emulator.save_state() == original);
    auto version6 = version7;
    version6.erase(version6.end() - 16384 - 5, version6.end() - 16384);
    const std::uint32_t version6_number = 6;
    std::memcpy(version6.data() + 4, &version6_number, sizeof(version6_number));
    REQUIRE(emulator.load_state(version6));
    REQUIRE(emulator.save_state() == original);
    auto version5 = version7;
    version5.erase(version5.end() - 16384 - 9, version5.end() - 16384);
    const std::uint32_t version5_number = 5;
    std::memcpy(version5.data() + 4, &version5_number, sizeof(version5_number));
    REQUIRE(emulator.load_state(version5));
    REQUIRE(emulator.save_state() == original);

    // A rejected v6 audio extension must roll back the entire machine.
    for (const Byte invalid : {32, 255}) {
        auto corrupt = original;
        corrupt[corrupt.size() - 10 - 16384 - 9] = invalid;
        REQUIRE_FALSE(emulator.load_state(corrupt));
        REQUIRE(emulator.save_state() == original);
    }
    auto version4 = version7;
    version4.erase(version4.end() - 16384 - 27, version4.end() - 16384);
    const std::uint32_t version4_number = 4;
    std::memcpy(version4.data() + 4, &version4_number, sizeof(version4_number));
    REQUIRE(emulator.load_state(version4));
    REQUIRE(emulator.save_state() == original);
    auto legacy = version7;
    // V5 adds 18 drive bytes, v6 four audio bytes, v7 five watchdog bytes.
    legacy.erase(legacy.end() - 16384 - 27, legacy.end() - 16384);
    legacy.erase(legacy.begin() + 11, legacy.begin() + 43);
    const std::uint32_t version = 3;
    std::memcpy(legacy.data() + 4, &version, sizeof(version));
    REQUIRE_FALSE(emulator.load_state(legacy));
    REQUIRE(emulator.save_state() == original);
    emulator.step_frame();
    REQUIRE(emulator.load_state(legacy, true));
    REQUIRE(emulator.save_state() == original);
    std::filesystem::remove(rom);
}

TEST_CASE("Snapshots restore controller timing and legacy states select NES wiring",
          "[state][controller]") {
    Emulator emulator;
    emulator.reset();
    REQUIRE(emulator.controller_timing() == ControllerTiming::NesAvFamicom);
    const auto legacy = mapperbus::tests::legacy_state(emulator, 13);
    emulator.set_controller_timing(ControllerTiming::RfFamicom);
    const auto rf = emulator.save_state();
    emulator.step_instruction();
    const auto after = emulator.save_state();
    emulator.set_controller_timing(ControllerTiming::NesAvFamicom);
    REQUIRE(emulator.load_state(rf));
    CHECK(emulator.controller_timing() == ControllerTiming::RfFamicom);
    emulator.step_instruction();
    CHECK(emulator.save_state() == after);

    REQUIRE(emulator.load_state(legacy));
    CHECK(emulator.controller_timing() == ControllerTiming::NesAvFamicom);
    REQUIRE(emulator.load_state(rf));
    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto end =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size() - 10;
    for (const bool invalid_timing : {false, true}) {
        auto corrupt = rf;
        // RF snapshots must have idle /OE at an instruction boundary.
        corrupt[end - (invalid_timing ? 5 : 7)] = invalid_timing ? 255 : 1;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == rf);
    }
}

TEST_CASE("Snapshots preserve RF wiring and version 14 retains its NES wiring",
          "[state][controller]") {
    Emulator emulator;
    emulator.reset();
    emulator.set_controller_timing(ControllerTiming::RfFamicom);
    const auto version14 = mapperbus::tests::legacy_state(emulator, 14);
    emulator.set_controller_wiring(ControllerWiring::RfFamicom);
    const auto rf = emulator.save_state();
    emulator.set_controller_wiring(ControllerWiring::Nes);
    REQUIRE(emulator.load_state(rf));
    CHECK(emulator.controller_wiring() == ControllerWiring::RfFamicom);
    emulator.step_instruction();
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(rf));
    emulator.step_instruction();
    CHECK(emulator.save_state() == after);

    REQUIRE(emulator.load_state(version14));
    CHECK(emulator.controller_wiring() == ControllerWiring::Nes);
    CHECK(emulator.controller_timing() == ControllerTiming::RfFamicom);
    REQUIRE(emulator.load_state(rf));
    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto wiring_offset =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size() - 14;
    for (const Byte invalid : {Byte{2}, Byte{255}}) {
        auto corrupt = rf;
        corrupt[wiring_offset] = invalid;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == rf);
    }
}

TEST_CASE("Microphone snapshots preserve the carrier and validate missing or corrupt history",
          "[state][microphone]") {
    Emulator emulator;
    emulator.reset();
    emulator.set_controller_wiring(ControllerWiring::RfFamicom);
    const auto legacy = mapperbus::tests::legacy_state(emulator, 15);
    emulator.set_microphone_active(true);
    for (int i = 0; i < 80; ++i)
        emulator.step_instruction();
    const auto before = emulator.save_state();
    for (int i = 0; i < 200; ++i)
        emulator.step_instruction();
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    CHECK(emulator.microphone_active());
    for (int i = 0; i < 200; ++i)
        emulator.step_instruction();
    CHECK(emulator.save_state() == after);
    REQUIRE(emulator.load_state(legacy));
    CHECK_FALSE(emulator.microphone_active());
    CHECK(emulator.controller_wiring() == ControllerWiring::RfFamicom);
    REQUIRE(emulator.load_state(before));
    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto end =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size() - 10;
    for (const uint16_t phase : {uint16_t{1024}, uint16_t{65535}}) {
        auto corrupt = before;
        std::memcpy(corrupt.data() + end - 2, &phase, sizeof(phase));
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == before);
    }
    auto invalid_flag = before;
    invalid_flag[end - 3] = 2;
    CHECK_FALSE(emulator.load_state(invalid_flag));
    CHECK(emulator.save_state() == before);
    auto inactive_phase = before;
    inactive_phase[end - 3] = 0;
    const uint16_t phase = 1;
    std::memcpy(inactive_phase.data() + end - 2, &phase, sizeof(phase));
    CHECK_FALSE(emulator.load_state(inactive_phase));
    CHECK(emulator.save_state() == before);
}

TEST_CASE("StateWriter/StateReader round-trips primitives, arrays and byte runs", "[state]") {
    StateWriter writer;
    writer.write<std::uint8_t>(0xAB);
    writer.write<std::uint16_t>(0x1234);
    writer.write<std::uint32_t>(0xDEADBEEF);
    writer.write<std::uint64_t>(0x0102030405060708ULL);
    writer.write<bool>(true);
    const std::array<std::uint8_t, 4> arr = {1, 2, 3, 4};
    writer.write_array(arr);
    const std::vector<Byte> run = {9, 8, 7};
    writer.write_bytes(run);

    StateReader reader(writer.data());
    REQUIRE(reader.read<std::uint8_t>() == 0xAB);
    REQUIRE(reader.read<std::uint16_t>() == 0x1234);
    REQUIRE(reader.read<std::uint32_t>() == 0xDEADBEEF);
    REQUIRE(reader.read<std::uint64_t>() == 0x0102030405060708ULL);
    REQUIRE(reader.read<bool>() == true);
    std::array<std::uint8_t, 4> arr_out{};
    reader.read_array(arr_out);
    REQUIRE(arr_out == arr);
    std::vector<Byte> run_out;
    reader.read_bytes(run_out);
    REQUIRE(run_out == run);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0);
}

TEST_CASE("StateReader flags truncated reads", "[state]") {
    StateWriter writer;
    writer.write<std::uint16_t>(0x1234);

    StateReader reader(writer.data());
    REQUIRE(reader.read<std::uint16_t>() == 0x1234);
    REQUIRE(reader.ok());
    (void)reader.read<std::uint32_t>(); // past the end
    REQUIRE_FALSE(reader.ok());
}

TEST_CASE("StateReader rejects a byte run with a corrupt length prefix", "[state]") {
    StateWriter writer;
    const std::vector<Byte> run = {1, 2, 3};
    writer.write_bytes(run);
    std::vector<Byte> blob = writer.data();

    // Overwrite the u64 length with UINT64_MAX so that pos_ + size wraps to a
    // small value — an overflow-vulnerable bounds check would accept it and
    // read far out of bounds.
    const std::uint64_t huge = 0xFFFF'FFFF'FFFF'FFFFULL;
    std::memcpy(blob.data(), &huge, sizeof(huge));

    StateReader reader(blob);
    std::vector<Byte> out;
    reader.read_bytes(out);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(out.empty());
}

TEST_CASE("StateReader rejects a byte run above the caller limit", "[state]") {
    StateWriter writer;
    const std::vector<Byte> run(8, 1);
    writer.write_bytes(run);

    StateReader reader(writer.data());
    std::vector<Byte> out;
    reader.read_bytes(out, 4);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(out.empty());
}

TEST_CASE("Save-state is deterministic across a save/load cycle", "[state][integration]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate");
    REQUIRE(std::filesystem::exists(rom));

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();

    // Warm up, then snapshot.
    for (int i = 0; i < 30; ++i) {
        emu.step_frame();
    }
    const std::vector<Byte> snapshot = emu.save_state();
    REQUIRE_FALSE(snapshot.empty());

    // Run further and record where the machine ends up.
    for (int i = 0; i < 20; ++i) {
        emu.step_frame();
    }
    const std::uint64_t reference = hash_frame(emu.frame_buffer());

    // Restore the snapshot and replay the same number of frames.
    REQUIRE(emu.load_state(snapshot));
    for (int i = 0; i < 20; ++i) {
        emu.step_frame();
    }
    REQUIRE(hash_frame(emu.frame_buffer()) == reference);
}

TEST_CASE("Loading a snapshot into a fresh emulator reproduces frames", "[state][integration]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate2");

    Emulator source;
    REQUIRE(source.load_cartridge(rom.string()).has_value());
    source.reset();
    for (int i = 0; i < 45; ++i) {
        source.step_frame();
    }
    const std::vector<Byte> snapshot = source.save_state();

    // Advance the source so any shared-static state cannot mask a bad restore.
    for (int i = 0; i < 10; ++i) {
        source.step_frame();
    }
    const std::uint64_t reference = hash_frame(source.frame_buffer());

    Emulator restored;
    REQUIRE(restored.load_cartridge(rom.string()).has_value());
    restored.reset();
    REQUIRE(restored.load_state(snapshot));
    for (int i = 0; i < 10; ++i) {
        restored.step_frame();
    }
    REQUIRE(hash_frame(restored.frame_buffer()) == reference);
}

TEST_CASE("load_state rejects malformed and mismatched blobs", "[state]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate3");

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();

    SECTION("garbage is rejected") {
        const std::vector<Byte> garbage(64, 0x55);
        REQUIRE_FALSE(emu.load_state(garbage));
    }
    SECTION("empty blob is rejected") {
        REQUIRE_FALSE(emu.load_state({}));
    }
    SECTION("truncated valid blob is rejected") {
        const std::vector<Byte> before = emu.save_state();
        std::vector<Byte> blob = before;
        blob.resize(blob.size() / 2);
        REQUIRE_FALSE(emu.load_state(blob));
        // The rejected apply must roll back. A following frame also proves the
        // restored PPU and mapper indexes are still in range.
        REQUIRE(emu.save_state() == before);
        emu.step_frame();
    }
    SECTION("corrupted magic is rejected") {
        std::vector<Byte> blob = emu.save_state();
        blob[0] = 0x00;
        REQUIRE_FALSE(emu.load_state(blob));
    }
}

TEST_CASE("Save-state survives a file round-trip", "[state]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("savestate4");
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path() / "mapperbus-savestate.mbst";

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();
    for (int i = 0; i < 25; ++i) {
        emu.step_frame();
    }
    REQUIRE(emu.save_state_to_file(state_path.string()).has_value());
    REQUIRE_FALSE(std::filesystem::exists(state_path.string() + ".tmp"));

    // The framebuffer is not part of the blob (it re-renders on the next
    // frame), so compare after replaying an equal number of frames on both.
    for (int i = 0; i < 5; ++i) {
        emu.step_frame();
    }
    const std::uint64_t reference = hash_frame(emu.frame_buffer());

    Emulator restored;
    REQUIRE(restored.load_cartridge(rom.string()).has_value());
    restored.reset();
    REQUIRE(restored.load_state_from_file(state_path.string()).has_value());
    for (int i = 0; i < 5; ++i) {
        restored.step_frame();
    }
    REQUIRE(hash_frame(restored.frame_buffer()) == reference);

    std::filesystem::remove(state_path);
}

TEST_CASE("Oversized ROM and save-state files are rejected before allocation", "[state]") {
    register_builtin_mappers();
    const auto directory = std::filesystem::temp_directory_path();
    const auto rom_path = directory / "mapperbus-oversize.nes";
    {
        std::ofstream file(rom_path, std::ios::binary | std::ios::trunc);
        file << "NES\x1A";
    }
    std::filesystem::resize_file(rom_path, kMaxRomBytes + 1);

    Emulator emu;
    const auto loaded = emu.load_cartridge(rom_path.string());
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("exceeds maximum size") != std::string::npos);
    REQUIRE_FALSE(emu.has_cartridge());
    std::filesystem::remove(rom_path);

    const auto playable = mapperbus::tests::write_visible_nrom_test_rom("savestate-cap");
    REQUIRE(emu.load_cartridge(playable.string()).has_value());
    emu.reset();
    const std::vector<Byte> before = emu.save_state();

    const auto state_path = directory / "mapperbus-oversize.state";
    {
        std::ofstream file(state_path, std::ios::binary | std::ios::trunc);
        file << "MBST";
    }
    std::filesystem::resize_file(state_path, Emulator::kMaxStateBytes + 1);
    const auto state = emu.load_state_from_file(state_path.string());
    REQUIRE_FALSE(state);
    REQUIRE(state.error().find("exceeds maximum size") != std::string::npos);
    REQUIRE(emu.save_state() == before);
    std::filesystem::remove(state_path);
    std::filesystem::remove(playable);
}

TEST_CASE("MMC5 save-state rejects a PRG mode outside 0..3", "[state][mmc5]") {
    INesHeader header{};
    header.mapper_number = 5;
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    Mmc5 baseline(header, prg, chr);
    StateWriter baseline_writer;
    baseline.save_state(baseline_writer);

    Mmc5 changed(header, prg, chr);
    changed.write_expansion(0x5100, 0x00);
    StateWriter changed_writer;
    changed.save_state(changed_writer);

    REQUIRE(baseline_writer.data().size() == changed_writer.data().size());
    std::vector<Byte> blob = baseline_writer.data();
    int diffs = 0;
    for (std::size_t index = 0; index < blob.size(); ++index) {
        if (baseline_writer.data()[index] != changed_writer.data()[index]) {
            blob[index] = 0xFF;
            ++diffs;
        }
    }
    REQUIRE(diffs >= 1);

    Mmc5 victim(header, prg, chr);
    StateReader reader(blob);
    victim.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    (void)victim.read_prg(0x8000);
    (void)victim.read_prg(0xE000);
}

TEST_CASE("Namco 163 save-state rejects a zero channel count", "[state][n163]") {
    INesHeader header{};
    header.mapper_number = 19;
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    Namco163 baseline(header, prg, chr);
    StateWriter baseline_writer;
    baseline.save_state(baseline_writer);

    Namco163 changed(header, prg, chr);
    changed.write_prg(0xF800, 0x7F);
    changed.write_expansion(0x4800, 0x70); // channel 8 enabled: ((0x70 >> 4) & 7) + 1
    StateWriter changed_writer;
    changed.save_state(changed_writer);

    REQUIRE(baseline_writer.data().size() == changed_writer.data().size());
    std::vector<Byte> blob = baseline_writer.data();
    int diffs = 0;
    for (std::size_t index = 0; index < blob.size(); ++index) {
        if (baseline_writer.data()[index] != changed_writer.data()[index]) {
            blob[index] = 0; // includes num_channels_, which hardware never stores as 0
            ++diffs;
        }
    }
    REQUIRE(diffs >= 1);

    Namco163 victim(header, prg, chr);
    StateReader reader(blob);
    victim.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    for (int step = 0; step < 64; ++step) {
        victim.clock_audio();
    }
    (void)victim.audio_output();
}

TEST_CASE("PPU save-state rejects a sprite count above the hardware limit", "[state][ppu]") {
    Ppu ppu;
    StateWriter writer;
    ppu.save_state(writer);
    std::vector<Byte> blob = writer.take();
    REQUIRE(blob.size() >= sizeof(int));

    int accepted = 8;
    std::memcpy(blob.data() + blob.size() - 4 - sizeof(accepted), &accepted, sizeof(accepted));
    {
        Ppu restored;
        StateReader reader(blob);
        restored.load_state(reader);
        REQUIRE(reader.ok());
        restored.step(30);
    }

    int rejected = 64;
    std::memcpy(blob.data() + blob.size() - 4 - sizeof(rejected), &rejected, sizeof(rejected));
    Ppu restored;
    StateReader reader(blob);
    restored.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    restored.step(30);
}

TEST_CASE("Mapper save-states reject unsafe restored indices", "[state][mappers]") {
    INesHeader header{};
    header.mirror_mode = MirrorMode::Vertical;
    const std::vector<Byte> prg(16 * 1024, 0xEA);
    const std::vector<Byte> chr(8 * 1024, 0);

    SECTION("MMC3 bank select") {
        Mmc3 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        blob[0x2000 + 0x2000] = 0xFF;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        mapper.write_prg(0x8001, 1);
    }

    SECTION("Namco 163 sound address") {
        Namco163 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        constexpr std::size_t sound_addr_offset = 0x2000 + 0x2000 + 4 + 8 + 1 + 2 + 1 + 1 + 128;
        blob[sound_addr_offset] = 0xFF;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        (void)mapper.read_expansion(0x4800);
    }

    SECTION("MMC5 pulse sequencer") {
        Mmc5 mapper(header, prg, chr);
        StateWriter writer;
        mapper.save_state(writer);
        auto blob = writer.take();
        constexpr std::size_t pulse_offset = 0x10000 + 0x2000 + 1 + 5 + 1 + 16 + 1;
        blob[pulse_offset + 1] = 0xFF;
        blob[pulse_offset + 6] = 1;
        blob[pulse_offset + 7] = 1;
        StateReader reader(blob);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        (void)mapper.audio_output();
    }
}

TEST_CASE("Invalid APU state is rejected and rolled back", "[state][apu]") {
    register_builtin_mappers();
    const std::filesystem::path rom = mapperbus::tests::write_visible_nrom_test_rom("apu-state");
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    emu.reset();
    emu.step_frame();

    const std::vector<Byte> before = emu.save_state();
    StateWriter cpu_writer;
    emu.cpu().save_state(cpu_writer);
    StateWriter ppu_writer;
    emu.ppu().save_state(ppu_writer);
    constexpr std::size_t header_size = 4 + sizeof(std::uint32_t) + sizeof(std::uint16_t) + 1 + 32;
    const std::size_t apu_offset =
        header_size + cpu_writer.data().size() + ppu_writer.data().size();

    std::vector<Byte> blob = before;
    blob[apu_offset] = 0xFF;
    REQUIRE_FALSE(emu.load_state(blob));
    REQUIRE(emu.save_state() == before);
    emu.step_frame();
    std::filesystem::remove(rom);
}
