#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader jf_header(uint16_t id) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}
std::size_t jf_prg_size(uint16_t id) {
    return id == 101U ? 0x8000U : (id == 72U ? 0x20000U : 0x40000U);
}
std::size_t jf_chr_size(uint16_t id) {
    return id == 101U ? 0x200000U : 0x20000U;
}
std::vector<Byte> jf_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        rom[offset] = static_cast<Byte>(offset / bank_size);
    return rom;
}
std::vector<Byte> jf_prg(uint16_t id) {
    auto rom = jf_rom(jf_prg_size(id), 0x4000U);
    for (std::size_t bank = 0U; bank < rom.size(); bank += 0x4000U) {
        rom[bank + 0x100U] = 0xFFU;
        rom[bank + 0x101U] = 0U;
        rom[bank + 0x102U] = 0xC2U;
        rom[bank + 0x103U] = 0x40U;
        rom[bank + 0x120U] = bank == 0U ? 0xC5U : 0U;
    }
    return rom;
}
std::unique_ptr<Mapper> jf_board(INesHeader header) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header, jf_prg(header.mapper_number), jf_rom(jf_chr_size(header.mapper_number), 0x2000U));
    REQUIRE(result);
    return std::move(*result);
}
Address jf_safe_write(uint16_t id) {
    return id == 72U ? 0xC100U : 0x8100U;
}
Address jf_switched_prg(uint16_t id) {
    return id == 72U ? 0x8000U : 0xC000U;
}
Cartridge jf_cartridge(uint16_t id, bool program = false) {
    const auto prg_size = jf_prg_size(id);
    const auto chr_size = jf_chr_size(id);
    std::vector<Byte> image(16U + prg_size + chr_size, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = static_cast<Byte>(prg_size / 0x4000U);
    image[5U] = static_cast<Byte>(chr_size / 0x2000U);
    image[6U] = static_cast<Byte>(((id & 15U) << 4U) | 1U);
    image[7U] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[9U] = static_cast<Byte>(((chr_size / 0x2000U) >> 8U) << 4U);
    const auto prg = jf_prg(id);
    std::copy(prg.begin(), prg.end(), image.begin() + 16U);
    const auto base = id == 72U ? 0xC000U : 0x8000U;
    const auto offset = 16U + (id == 72U ? prg_size - 0x4000U : 0U);
    if (program && id != 101U) {
        const Byte reg_high = id == 72U ? 0xC1U : 0x81U;
        // Original program runs in the fixed PRG window, with conflict-safe writes.
        // clang-format off
        const std::array<Byte, 65U> code = {
            0xA9U,0x85U,0x8DU,0x00U,reg_high, // PRG bank 5, P rises.
            0xA9U,0x86U,0x8DU,0x00U,reg_high, // P held: keep bank 5.
            0xA9U,0x43U,0x8DU,0x00U,reg_high, // CHR bank 3, C rises.
            0xA9U,0x47U,0x8DU,0x00U,reg_high, // C held: keep bank 3.
            0xA9U,0x00U,0x8DU,0x06U,0x20U,0x8DU,0x06U,0x20U,
            0xADU,0x07U,0x20U,0xADU,0x07U,0x20U,0x85U,0x10U, // Read CHR via PPU -> $10.
            0xA9U,0x00U,0x8DU,0x00U,reg_high, // Rearm both latches.
            0xA9U,0x45U,0x8DU,0x00U,reg_high, // CHR bank 5.
            0xA9U,0x00U,0x8DU,0x06U,0x20U,0x8DU,0x06U,0x20U,
            0xADU,0x07U,0x20U,0xADU,0x07U,0x20U,0x85U,0x11U, // Read CHR via PPU -> $11.
            0x4CU,0x3EU,static_cast<Byte>(base >> 8U)};
        // clang-format on
        std::copy(code.begin(), code.end(), image.begin() + offset);
    } else if (program) {
        // clang-format off
        const std::array<Byte, 50U> code = {
            0xA9U,0x01U,0x8DU,0x00U,0x60U, // CHR bank 1 in legacy dump order.
            0xA9U,0x80U,0x8DU,0xFFU,0x7FU, // Oversize CHR bit 7.
            0xA9U,0x00U,0x8DU,0x06U,0x20U,0x8DU,0x06U,0x20U,
            0xADU,0x07U,0x20U,0xADU,0x07U,0x20U,0x85U,0x10U,
            0xA9U,0xFFU,0x8DU,0x00U,0x80U, // ROM writes do not select banks.
            0xA9U,0x00U,0x8DU,0x06U,0x20U,0x8DU,0x06U,0x20U,
            0xADU,0x07U,0x20U,0xADU,0x07U,0x20U,0x85U,0x11U,
            0x4CU,0x2FU,0x80U};
        // clang-format on
        std::copy(code.begin(), code.end(), image.begin() + offset);
    } else {
        image[offset] = 0x4CU;
        image[offset + 1U] = 0U;
        image[offset + 2U] = static_cast<Byte>(base >> 8U);
    }
    for (std::size_t bank = 16U; bank < 16U + prg_size; bank += 0x4000U) {
        for (std::size_t vector = 0x3FFAU; vector < 0x4000U; vector += 2U) {
            image[bank + vector] = 0U;
            image[bank + vector + 1U] = static_cast<Byte>(base >> 8U);
        }
    }
    const auto chr = jf_rom(chr_size, 0x2000U);
    std::copy(chr.begin(), chr.end(), image.begin() + 16U + prg_size);
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}
void jf_ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte jf_ppu_read(Ppu& ppu, Address addr) {
    jf_ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("JF-17 and JF-19 latch PRG and CHR only on their independent rising edges",
          "[mappers][jaleco-discrete]") {
    for (const uint16_t id : {72U, 92U}) {
        auto mapper = jf_board(jf_header(id));
        const auto reg = jf_safe_write(id);
        const auto window = jf_switched_prg(id);
        mapper->write_prg(reg, 0x85U);
        REQUIRE(mapper->read_prg(window) == 5U);
        mapper->write_prg(reg, 0x86U);
        REQUIRE(mapper->read_prg(window) == 5U);
        mapper->write_prg(reg, 0xC3U); // C rises while P stays high.
        REQUIRE(mapper->read_prg(window) == 5U);
        REQUIRE(mapper->read_chr(0U) == 3U);
        mapper->write_prg(reg, 0xC7U);
        REQUIRE(mapper->read_prg(window) == 5U);
        REQUIRE(mapper->read_chr(0U) == 3U);
        mapper->write_prg(reg, 0U);
        mapper->write_prg(reg, 0xC7U);
        REQUIRE(mapper->read_prg(window) == 7U);
        REQUIRE(mapper->read_chr(0U) == 7U);
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            mapper->write_prg(reg, 0U);
            mapper->write_prg(reg, static_cast<Byte>(0xC0U | bank));
            REQUIRE(mapper->read_prg(window) == (bank & (id == 72U ? 7U : 15U)));
            REQUIRE(mapper->read_chr(0x1FFFU) == bank);
            REQUIRE(mapper->read_prg(id == 72U ? 0xC000U : 0x8000U) == (id == 72U ? 7U : 0U));
        }
    }
}

TEST_CASE("JF bus conflicts affect both data and control history using the old PRG mapping",
          "[mappers][jaleco-discrete][bus]") {
    for (const uint16_t id : {72U, 92U}) {
        auto mapper = jf_board(jf_header(id));
        const auto reg = jf_safe_write(id);
        const auto window = jf_switched_prg(id);
        mapper->write_prg(static_cast<Address>(window + 0x120U), 0xFFU); // Old ROM drives $C5.
        REQUIRE(mapper->read_prg(window) == 5U);
        REQUIRE(mapper->read_chr(0U) == 5U);
        mapper->write_prg(static_cast<Address>(reg + 1U), 0xFFU); // ROM zero clears P and C.
        REQUIRE(mapper->read_prg(window) == 5U);
        REQUIRE(mapper->read_chr(0U) == 5U);
        mapper->write_prg(reg, 0x86U);
        REQUIRE(mapper->read_prg(window) == 6U);
        mapper->write_prg(reg, 0U);
        mapper->write_prg(static_cast<Address>(reg + 2U), 0xFFU); // ROM drives $C2.
        REQUIRE(mapper->read_prg(window) == 2U);
        REQUIRE(mapper->read_chr(0U) == 2U);
        mapper->write_prg(static_cast<Address>(reg + 3U), 0xFFU); // Only C remains high.
        mapper->write_prg(reg, 0x84U);
        REQUIRE(mapper->read_prg(window) == 4U);
        REQUIRE(mapper->read_chr(0U) == 2U);
    }
}

TEST_CASE("Mapper 101 preserves its legacy CHR bank order and all eight oversize bank bits",
          "[mappers][jaleco-discrete][bus]") {
    auto mapper = jf_board(jf_header(101U));
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        const auto bank = static_cast<Byte>(1U << bit);
        mapper->write_prg(static_cast<Address>(0x6000U + bit * 0x400U), bank);
        REQUIRE(mapper->read_chr(0U) == bank);
        REQUIRE(mapper->read_chr(0x1FFFU) == bank);
    }
    mapper->write_prg(0x7FFFU, 0xFFU);
    REQUIRE(mapper->read_chr(0U) == 255U);
    for (const Address ignored : {0x0000U, 0x5FFFU, 0x8000U, 0xFFFFU})
        mapper->write_prg(ignored, 0U);
    mapper->write_expansion(0x5000U, 0U);
    REQUIRE(mapper->read_chr(0U) == 255U);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 1U);
    register_builtin_mappers();
    auto mirrored = MapperRegistry::instance().create(
        jf_header(101U), jf_rom(0x4000U, 0x4000U), jf_rom(0x8000U, 0x2000U));
    REQUIRE(mirrored);
    REQUIRE((*mirrored)->read_prg(0x8000U) == (*mirrored)->read_prg(0xC000U));
}

TEST_CASE("Jaleco discrete boards keep fixed CIRAM wiring, ROM protection and unmapped CPU RAM",
          "[mappers][jaleco-discrete][ppu][bus]") {
    for (const uint16_t id : {72U, 92U, 101U}) {
        for (const MirrorMode mode : {MirrorMode::Vertical, MirrorMode::Horizontal}) {
            auto header = jf_header(id);
            header.mirror_mode = mode;
            auto mapper = jf_board(header);
            mapper->write_prg(id == 101U ? 0x6000U : jf_safe_write(id), 0xC7U);
            REQUIRE(mapper->mirror_mode() == mode);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        }
        auto cart = jf_cartridge(id);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        bus.write(0x6000U, 0xA5U);
        REQUIRE(bus.read(0x6000U) == 0xA5U);
        bus.write(0U, 0x5AU);
        REQUIRE(bus.read(0x7FFFU) == 0x5AU);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        jf_ppu_address(ppu, 0x2000U);
        ppu.write_register(7U, 0x11U);
        jf_ppu_address(ppu, 0x2400U);
        ppu.write_register(7U, 0x22U);
        cart.write_prg(id == 101U ? 0x6000U : jf_safe_write(id), 0xC7U);
        REQUIRE(jf_ppu_read(ppu, 0x2800U) == 0x11U);
        REQUIRE(jf_ppu_read(ppu, 0x2C00U) == 0x22U);
        const auto bank = cart.read_chr(0U);
        cart.write_chr(0U, static_cast<Byte>(bank ^ 0xFFU));
        REQUIRE(cart.read_chr(0U) == bank);
    }
}

TEST_CASE("Jaleco discrete snapshots retain held control lines and reject invalid registers",
          "[mappers][jaleco-discrete][state]") {
    for (const uint16_t id : {72U, 92U, 101U}) {
        auto mapper = jf_board(jf_header(id));
        const auto reg = id == 101U ? Address{0x6000U} : jf_safe_write(id);
        mapper->write_prg(reg, id == 101U ? 0xFFU : 0xC5U);
        StateWriter saved;
        mapper->save_state(saved);
        mapper->reset();
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        StateWriter replayed;
        mapper->save_state(replayed);
        REQUIRE(replayed.data() == saved.data());
        if (id != 101U) {
            mapper->write_prg(reg, 0xC7U);
            REQUIRE(mapper->read_prg(jf_switched_prg(id)) == 5U);
            REQUIRE(mapper->read_chr(0U) == 5U);
        } else {
            REQUIRE(mapper->read_chr(0U) == 255U);
        }
        // Payload: PRG bank, CHR bank, previous P/C command bits.
        for (const std::size_t offset : {0U, 2U}) {
            auto corrupt = saved.data();
            corrupt[offset] = offset == 0U ? 0xFFU : 1U;
            StateReader bad(corrupt);
            mapper->load_state(bad);
            REQUIRE_FALSE(bad.ok());
        }
        if (id != 101U) {
            auto corrupt = saved.data();
            corrupt[1U] = 16U;
            StateReader bad(corrupt);
            mapper->load_state(bad);
            REQUIRE_FALSE(bad.ok());
        }
        StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("Jaleco discrete CPU writes replay snapshots taken while bank strobes are held",
          "[mappers][jaleco-discrete][state][integration]") {
    for (const uint16_t id : {72U, 92U, 101U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(jf_cartridge(id, true), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == (id == 72U ? 0xC000U : 0x8000U));
        for (unsigned i = 0U; i < (id == 101U ? 2U : 6U); ++i)
            emulator.step_instruction();
        const auto saved = emulator.save_state();
        auto finish = [&] {
            emulator.step_instruction();
            emulator.step_instruction();
            for (unsigned i = 0U; i < 6U; ++i)
                emulator.step_instruction();
            REQUIRE(emulator.read_cpu(0x10U) == (id == 101U ? 128U : 3U));
            for (unsigned i = 0U; i < 40U; ++i)
                emulator.step_instruction();
            REQUIRE(emulator.read_cpu(0x11U) == (id == 101U ? 128U : 5U));
            if (id != 101U)
                REQUIRE(emulator.read_cpu(jf_switched_prg(id)) == 5U);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        finish();
        REQUIRE(emulator.save_state() == expected);
        REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("Jaleco discrete factory enforces documented ROM geometry and rejects invented RAM",
          "[mappers][jaleco-discrete]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const uint16_t id : {72U, 92U, 101U}) {
        auto header = jf_header(id);
        REQUIRE(create(header, jf_prg_size(id), jf_chr_size(id)));
        REQUIRE_FALSE(create(header, jf_prg_size(id) * 2U, 0x2000U));
        REQUIRE_FALSE(create(header, jf_prg_size(id), jf_chr_size(id) * 2U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0U));
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2001U));
        header.submapper = 1U;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.prg_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.has_battery = true;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.chr_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.mirror_mode = MirrorMode::FourScreen;
        REQUIRE_FALSE(create(header, jf_prg_size(id), 0x2000U));
        header = jf_header(id);
        header.is_nes2 = false;
        header.prg_ram_size = 0x2000U;
        auto legacy = create(header, jf_prg_size(id), 0x2000U);
        REQUIRE(legacy);
        REQUIRE_FALSE((*legacy)->maps_prg(0x6000U));
    }
}
