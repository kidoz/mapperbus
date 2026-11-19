#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper006.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 1U,
                  uint16_t id = 6U,
                  bool legacy = false,
                  bool vertical = true) {
    INesHeader h{};
    h.mapper_number = id;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 0U : 32768U;
    h.chr_ram_size = legacy ? 0U : 32768U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::vector<Byte> prg_image(std::size_t size = 262144U) {
    std::vector<Byte> data(size);
    for (unsigned i = 0U; i < size; ++i)
        data[i] = prg_cell(i / 8192U, i % 8192U);
    return data;
}

std::unique_ptr<FfeMagicCard> board(Byte submapper = 1U,
                                    uint16_t id = 6U,
                                    bool legacy = false,
                                    bool vertical = true,
                                    std::size_t size = 262144U) {
    const auto h = header(submapper, id, legacy, vertical);
    REQUIRE(FfeMagicCard::validate_geometry(h, size, 0U));
    return std::make_unique<FfeMagicCard>(h, prg_image(size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Cartridge bus_cartridge() {
    const auto factory = [](const INesHeader& h,
                            std::vector<Byte> prg,
                            std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
        return std::make_unique<FfeMagicCard>(h, std::move(prg), std::move(chr));
    };
    MapperRegistry::instance().register_mapper(6U, factory);
    MapperRegistry::instance().register_mapper(8U, factory);
    auto rom = prg_image();
    rom.insert(rom.begin(), 16U, 0U);
    rom[0U] = 'N';
    rom[1U] = 'E';
    rom[2U] = 'S';
    rom[3U] = 0x1AU;
    rom[4U] = 16U;
    rom[6U] = 0x61U;
    auto cartridge = Cartridge::from_data(rom);
    REQUIRE(cartridge);
    return std::move(*cartridge);
}

void fill_chr(Mapper& mapper) {
    mapper.write_expansion(0x43FFU, 0U);
    mapper.write_expansion(0x42FFU, 0x30U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            mapper.write_chr(static_cast<Address>(addr), chr_cell(bank, addr));
    }
}

void expect_prg_8k(Mapper& mapper, Address addr, unsigned bank) {
    REQUIRE(mapper.read_prg(addr) == prg_cell(bank % 32U, addr & 0x1FFFU));
}

void expect_prg_16k(Mapper& mapper, Address addr, unsigned bank) {
    expect_prg_8k(mapper, addr, bank * 2U + ((addr >> 13U) & 1U));
}

TEST_CASE("Mapper 006 and 008 initialize the documented Magic Card play state", "[mapper006]") {
    for (bool legacy : {false, true})
        for (Byte sub = 0U; sub < 8U; ++sub) {
            if (legacy && sub != 1U)
                continue;
            auto mapper = board(sub, 6U, legacy);
            INFO("sub=" << unsigned(sub) << " legacy=" << legacy
                        << " mode-address=" << unsigned(state(*mapper)[10U]));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->read_expansion(0x5500U) == 0x42U);
            REQUIRE(mapper->read_expansion(0x4501U) == 3U);
            REQUIRE(mapper->read_expansion(0x4500U) == static_cast<Byte>(0x43U | (sub << 5U)));
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_chr(0U) == 0U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*mapper).size() == 331848U);
        }

    auto alias = board(0U, 8U);
    auto canonical = board(4U);
    REQUIRE(state(*alias) == state(*canonical));
    auto legacy_alias = board(0U, 8U, true, false, 131072U);
    auto legacy_canonical = board(4U, 6U, false, false, 131072U);
    REQUIRE(state(*legacy_alias) == state(*legacy_canonical));
    REQUIRE(state(*legacy_alias).size() == 200776U);
}

TEST_CASE("Mapper 006 latch modes decode every data value without bus conflicts", "[mapper006]") {
    auto mapper = board();
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_expansion(0x43FFU, 0U);
            mapper->write_expansion(0x42FFU, static_cast<Byte>((mode << 5U) | 0x10U));
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            const auto latch = value & 0x3FU;
            switch (mode) {
            case 0U:
                expect_prg_16k(*mapper, 0x8000U, latch & 7U);
                expect_prg_16k(*mapper, 0xC000U, 7U);
                REQUIRE(mapper->read_chr(0U) == 0U);
                break;
            case 1U:
                expect_prg_16k(*mapper, 0x8000U, (latch >> 2U) & 15U);
                expect_prg_16k(*mapper, 0xC000U, 7U);
                break;
            case 2U:
                expect_prg_16k(*mapper, 0x8000U, latch & 15U);
                expect_prg_16k(*mapper, 0xC000U, 15U);
                break;
            case 3U:
                expect_prg_16k(*mapper, 0x8000U, 15U);
                expect_prg_16k(*mapper, 0xC000U, latch & 15U);
                break;
            case 4U:
                expect_prg_8k(*mapper, 0x8000U, ((latch >> 4U) & 3U) * 4U);
                expect_prg_8k(*mapper, 0xE000U, ((latch >> 4U) & 3U) * 4U + 3U);
                break;
            default:
                expect_prg_8k(*mapper, 0x8000U, 12U);
                expect_prg_8k(*mapper, 0xE000U, 15U);
                break;
            }
            if (mode >= 1U && mode <= 5U) {
                mapper->write_prg(0xC000U, static_cast<Byte>(value));
                REQUIRE(mapper->read_chr(0U) == 0U);
                REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
            }
        }
    }

    fill_chr(*mapper);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        mapper->write_expansion(0x43FFU, 0U);
        mapper->write_expansion(0x42FFU, static_cast<Byte>((mode << 5U) | 0x10U));
        mapper->write_prg(0x8000U, 3U);
        const unsigned bank = mode == 0U || mode == 2U || mode == 7U ? 0U : mode == 6U ? 1U : 3U;
        INFO("mode=" << mode << " bank=" << bank);
        REQUIRE(mapper->read_chr(0x0123U) == chr_cell(bank, 0x0123U));
        mapper->write_chr(0x0123U, 0xA7U);
        REQUIRE(mapper->read_chr(0x0123U) == (mode < 4U ? 0xA7U : chr_cell(bank, 0x0123U)));
        if (mode < 4U)
            mapper->write_chr(0x0123U, chr_cell(bank, 0x0123U));
    }
}

TEST_CASE("Mapper 006 disabled latch retains its value and writes PRG DRAM", "[mapper006]") {
    auto mapper = board(2U);
    mapper->write_expansion(0x43FFU, 0U);
    mapper->write_expansion(0x42FFU, 0x50U);
    mapper->write_prg(0x8000U, 0x03U);
    expect_prg_16k(*mapper, 0x8000U, 3U);
    REQUIRE(mapper->read_expansion(0x4501U) == static_cast<Byte>((3U << 2U) | 3U));

    mapper->write_expansion(0x42FCU, 0x40U);
    mapper->write_prg(0x8000U, 0xA5U);
    REQUIRE(mapper->read_expansion(0x4501U) == static_cast<Byte>((3U << 2U) | 3U));
    REQUIRE(mapper->read_prg(0x8000U) == 0xA5U);
    mapper->write_expansion(0x42FFU, 0x50U);
    REQUIRE(mapper->read_prg(0x8000U) == 0xA5U);
}

TEST_CASE("Mapper 006 mode-register aliases control protection and all CIRAM arrangements",
          "[mapper006]") {
    auto mapper = board();
    constexpr std::array<MirrorMode, 4> lower_modes = {
        MirrorMode::SingleLower,
        MirrorMode::Vertical,
        MirrorMode::SingleLower,
        MirrorMode::Vertical,
    };
    constexpr std::array<MirrorMode, 4> upper_modes = {
        MirrorMode::SingleUpper,
        MirrorMode::Horizontal,
        MirrorMode::SingleUpper,
        MirrorMode::Horizontal,
    };
    for (unsigned addr = 0x42FCU; addr <= 0x42FFU; ++addr)
        for (Byte data : {0x00U, 0x10U, 0xE0U, 0xF0U}) {
            mapper->write_expansion(static_cast<Address>(addr), data);
            REQUIRE(mapper->mirror_mode() ==
                    ((data & 0x10U) == 0U ? lower_modes[addr & 3U] : upper_modes[addr & 3U]));
            REQUIRE(mapper->read_expansion(0x4500U) ==
                    static_cast<Byte>(0x40U | data | (addr & 2U) | (addr & 1U)));
        }

    for (unsigned addr = 0x43FCU; addr <= 0x43FFU; ++addr)
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x8000U, 0x2CU);
            mapper->write_expansion(static_cast<Address>(addr), static_cast<Byte>(value));
            REQUIRE(mapper->read_expansion(0x4501U) ==
                    static_cast<Byte>(0xB0U | ((value & 3U) << 2U) | (addr & 3U)));
        }
}

TEST_CASE("Mapper 006 2M and 4M banks overlap the latch and retain independent registers",
          "[mapper006]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 4U; ++slot)
        mapper->write_expansion(static_cast<Address>(0x4504U + slot),
                                static_cast<Byte>(slot * 5U + 3U));
    mapper->write_expansion(0x43FEU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        INFO("initial slot=" << slot);
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), slot * 5U + 3U);
    }

    for (unsigned value = 0U; value < 256U; ++value)
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            INFO("value=" << value << " slot=" << slot);
            mapper->write_prg(static_cast<Address>(0x8000U + slot * 8192U),
                              static_cast<Byte>(value));
            expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), value & 0x3FU);
        }

    mapper->write_expansion(0x43FCU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_expansion(static_cast<Address>(0x4504U + slot),
                                static_cast<Byte>(31U - slot));
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), 31U - slot);
    }
    fill_chr(*mapper);
    mapper->write_expansion(0x43FCU, 2U);
    REQUIRE(mapper->read_chr(0x0111U) == chr_cell(2U, 0x0111U));

    auto small = board(1U, 6U, false, true, 131072U);
    small->write_expansion(0x43FEU, 0U);
    small->write_prg(0x8000U, 31U);
    expect_prg_8k(*small, 0x8000U, 15U);
}

TEST_CASE("Mapper 006 1 KiB CHR and CHR nametable registers map all physical banks",
          "[mapper006]") {
    auto mapper = board();
    fill_chr(*mapper);
    mapper->write_expansion(0x4500U, 0x47U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_expansion(static_cast<Address>(0x4510U + slot),
                                static_cast<Byte>(slot * 7U + 3U));
        const auto bank = (slot * 7U + 3U) % 32U;
        for (unsigned offset = 0U; offset < 1024U; ++offset) {
            const auto addr = static_cast<Address>(slot * 1024U + offset);
            INFO("slot=" << slot << " offset=" << offset);
            REQUIRE(mapper->read_chr(addr) == chr_cell(bank / 8U, (bank % 8U) * 1024U + offset));
        }
    }

    mapper->write_expansion(0x4500U, 0x41U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_expansion(static_cast<Address>(0x4518U + slot),
                                static_cast<Byte>(31U - slot * 3U));
        for (unsigned offset = 0U; offset < 1024U; ++offset) {
            const auto addr = static_cast<Address>(0x2000U + slot * 1024U + offset);
            const auto bank = 31U - slot * 3U;
            REQUIRE(mapper->read_nametable(addr) ==
                    chr_cell(bank / 8U, (bank % 8U) * 1024U + offset));
        }
    }
    mapper->write_expansion(0x42FFU, 0x90U);
    REQUIRE(mapper->write_nametable(0x2000U, 0xA7U));
    REQUIRE(mapper->read_nametable(0x2000U) == chr_cell(3U, 0x1C00U));
    mapper->write_expansion(0x42FFU, 0x10U);
    REQUIRE(mapper->write_nametable(0x2000U, 0xA7U));
    REQUIRE(mapper->read_nametable(0x2000U) == 0xA7U);
    mapper->write_expansion(0x4500U, 0x43U);
    REQUIRE_FALSE(mapper->read_nametable(0x2000U));
    REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0U));
}

TEST_CASE("Mapper 006 MMC4 CHR latches react only to physical FD and FE reads", "[mapper006]") {
    auto mapper = board();
    fill_chr(*mapper);
    mapper->write_expansion(0x4500U, 0x43U);
    for (unsigned reg = 0U; reg < 8U; ++reg)
        mapper->write_expansion(static_cast<Address>(0x4510U + reg), static_cast<Byte>(reg * 4U));

    const auto expect_half = [&](unsigned half, unsigned latch) {
        for (unsigned pair = 0U; pair < 2U; ++pair) {
            const auto reg = half * 4U + pair * 2U + latch;
            const auto addr = static_cast<Address>(half * 4096U + pair * 2048U + 0x123U);
            const auto physical = reg * 4096U + pair * 2048U + 0x123U;
            REQUIRE(mapper->read_chr(addr) == chr_cell(physical / 8192U, physical % 8192U));
        }
    };
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_read(0x1FE9U);
    expect_half(0U, 1U);
    expect_half(1U, 1U);
    mapper->observe_ppu_read(0x0FD8U);
    mapper->observe_ppu_read(0x1FDFU);
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    mapper->observe_ppu_read(0x0FD7U);
    mapper->observe_ppu_read(0x0FF0U);
    mapper->observe_ppu_read(0x1FE7U);
    expect_half(0U, 0U);
    expect_half(1U, 0U);

    mapper->observe_ppu_read(0x0FE8U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->observe_ppu_read(0x0FD8U);
    mapper->write_expansion(0x4500U, 0x43U);
    expect_half(0U, 1U);
}

TEST_CASE("Mapper 006 M2 and PA12 IRQs wrap acknowledge and disable exactly", "[mapper006]") {
    auto mapper = board();
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4501U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 65536U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4500U, 0x4AU);
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 006 scratch status RAM GUI windows and open bus use real CPU dispatch",
          "[mapper006]") {
    auto cartridge = bus_cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&cartridge);
    for (Address addr : {0x42FCU, 0x43FFU, 0x4502U, 0x4508U, 0x4FFFU}) {
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    for (unsigned addr = 0x5000U; addr <= 0x5FFFU; ++addr)
        bus.write(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    for (unsigned addr = 0x5000U; addr <= 0x5FFFU; ++addr)
        REQUIRE(bus.read(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ (addr >> 8U)));

    bus.write(0x4000U, 0x12U);
    bus.write(0x4015U, 0x34U);
    bus.write(0x2007U, 0x56U);
    bus.write(0x3FFFU, 0x78U);
    REQUIRE(bus.read(0x5000U) == 0x12U);
    REQUIRE(bus.read(0x5015U) == 0x34U);
    REQUIRE(bus.read(0x5807U) == 0x78U);
    bus.write(0x4504U, 7U);
    REQUIRE(bus.read(0x5504U) == 7U);

    bus.write(0x4500U, 0x02U);
    bus.write(0x42FFU, 0x10U);
    bus.write(0x4504U, 3U);
    bus.write(0x4505U, 4U);
    bus.write(0x4507U, 5U);
    REQUIRE(bus.read(0x8000U) == prg_cell(3U, 0U));
    REQUIRE(bus.read(0xA000U) == prg_cell(4U, 0U));
    REQUIRE(bus.read(0x6000U) == 0U);
    bus.write(0U, 0x39U);
    REQUIRE(bus.read(0xC000U) == 0x39U);
    bus.write(0x42FCU, 0U);
    REQUIRE(bus.read(0x6000U) == prg_cell(5U, 0U));
    bus.write(0x6000U, 0xA7U);
    REQUIRE(bus.read(0x6000U) == 0xA7U);

    bus.write(0x4500U, 0xC2U);
    bus.write(0U, 0x39U);
    REQUIRE(bus.read(0x8000U) == 0x39U);
    REQUIRE(bus.read(0x4500U) == 0x39U);
    REQUIRE(bus.read(0x5000U) == 0x39U);
}

TEST_CASE("Mapper 006 reset retains loaded DRAM while cold power clears work RAM", "[mapper006]") {
    auto mapper = board(2U);
    mapper->write_expansion(0x42FCU, 0x40U);
    mapper->write_prg(0x8000U, 0xA7U);
    mapper->write_prg(0x6000U, 0x5AU);
    mapper->write_expansion(0x5000U, 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    mapper->write_chr(0U, 0x39U);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    REQUIRE(mapper->read_chr(0U) == 0x39U);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x42U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x42U);
    REQUIRE(mapper->read_chr(0U) == 0U);
}

TEST_CASE("Mapper 006 rejects unsupported geometry submappers and deferred trainers",
          "[mapper006]") {
    for (uint16_t id : {6U, 8U})
        for (bool legacy : {false, true}) {
            auto h = header(id == 6U ? 1U : 0U, id, legacy);
            REQUIRE(FfeMagicCard::validate_geometry(h, 131072U, 0U));
            REQUIRE(FfeMagicCard::validate_geometry(h, 262144U, 0U));
        }
    for (Byte sub = 0U; sub < 8U; ++sub)
        REQUIRE(FfeMagicCard::validate_geometry(header(sub), 262144U, 0U));

    for (std::size_t size : {0U, 8192U, 65536U, 131071U, 131073U, 524288U})
        REQUIRE_FALSE(FfeMagicCard::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1U, 8192U, 65536U, 131071U, 131073U, 524288U})
        REQUIRE_FALSE(FfeMagicCard::validate_geometry(header(), 262144U, size));
    for (Byte sub = 8U; sub < 16U; ++sub)
        REQUIRE_FALSE(FfeMagicCard::validate_geometry(header(sub), 262144U, 0U));
    auto h = header(4U, 8U);
    REQUIRE_FALSE(FfeMagicCard::validate_geometry(h, 262144U, 0U));
    h = header(7U);
    h.mapper_number = 7U;
    REQUIRE_FALSE(FfeMagicCard::validate_geometry(h, 262144U, 0U));
    for (unsigned which = 0U; which < 8U; ++which) {
        h = header();
        switch (which) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_nvram_size = 32768U;
            break;
        case 2U:
            h.chr_nvram_size = 32768U;
            break;
        case 3U:
            h.prg_ram_size = 0U;
            break;
        case 4U:
            h.prg_ram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.has_trainer = true;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(FfeMagicCard::validate_geometry(h, 262144U, 0U));
    }
}

TEST_CASE("Mapper 006 state replays all mutable RAM and rejects corruption atomically",
          "[mapper006]") {
    auto mapper = board(3U);
    fill_chr(*mapper);
    mapper->write_expansion(0x42FFU, 0x70U);
    mapper->write_expansion(0x43FEU, 1U);
    mapper->write_prg(0x8000U, 0x2AU);
    mapper->write_prg(0xA000U, 0x15U);
    mapper->write_expansion(0x4500U, 0x4BU);
    mapper->write_expansion(0x4504U, 9U);
    mapper->write_expansion(0x4510U, 17U);
    mapper->write_expansion(0x451BU, 23U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_a12(true);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_expansion(0x5321U, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 331848U);

    mapper->power_on();
    REQUIRE(state(*mapper) != saved);
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->irq_pending());

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    const std::array lengths = {0UZ, 1UZ, 7UZ, 39UZ, 40UZ, 47UZ, 48UZ, 100UZ, saved.size() - 1U};
    for (const auto length : lengths)
        reject(std::span(saved).first(length));
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(bad);
    }
    for (auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 11>{{
             {2U, 0U},
             {3U, 0x10U},
             {8U, 1U},
             {9U, 0x40U},
             {10U, 4U},
             {11U, 4U},
             {15U, 2U},
             {16U, 2U},
             {17U, 2U},
             {18U, 0x40U},
             {38U, 2U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        reject(bad);
    }
    auto bad_size = saved;
    bad_size[6U] = 0U;
    reject(bad_size);
    auto bad_length = saved;
    bad_length[42U] = 0U;
    reject(bad_length);

    auto other = board(4U, 6U, false, false, 131072U);
    const auto before = state(*other);
    StateReader wrong(saved);
    other->load_state(wrong);
    REQUIRE_FALSE(wrong.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
