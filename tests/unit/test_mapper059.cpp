#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/bmc_t3h53.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader make_header(bool legacy = false) {
    INesHeader header{};
    header.mapper_number = 59U;
    header.is_nes2 = !legacy;
    header.mirror_mode = MirrorMode::Horizontal;
    header.prg_ram_size = legacy ? 8192U : 0U;
    return header;
}

Byte prg_pattern(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset * 13U + offset / 256U);
}

Byte chr_pattern(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 53U + offset * 7U + offset / 128U);
}

std::vector<Byte> make_prg(std::size_t size = 131072U) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        rom[offset] = prg_pattern(static_cast<unsigned>(offset / 16384U),
                                  static_cast<unsigned>(offset % 16384U));
    return rom;
}

std::vector<Byte> make_chr(std::size_t size = 65536U) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        rom[offset] = chr_pattern(static_cast<unsigned>(offset / 8192U),
                                  static_cast<unsigned>(offset % 8192U));
    return rom;
}

std::unique_ptr<BmcT3h53> board(Byte jumpers = 0U,
                                bool legacy = false,
                                std::size_t prg_size = 131072U,
                                std::size_t chr_size = 65536U) {
    const auto header = make_header(legacy);
    REQUIRE(BmcT3h53::validate_geometry(header, prg_size, chr_size));
    auto mapper = std::make_unique<BmcT3h53>(header, make_prg(prg_size), make_chr(chr_size));
    REQUIRE(mapper->configure_board_jumpers(jumpers));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address saved_latch(const Mapper& mapper) {
    const auto bytes = state(mapper);
    StateReader reader(bytes);
    (void)reader.read<uint16_t>();
    const auto latch = reader.read<Address>();
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    return latch;
}

Byte expected_prg(Address addr, Address latch, std::size_t prg_size) {
    unsigned bank = (latch >> 4U) & 7U;
    if ((latch & 0x80U) == 0U)
        bank = (bank & 6U) | ((addr >> 14U) & 1U);
    const auto offset = (bank * 16384U + (addr & 0x3FFFU)) % prg_size;
    return prg_pattern(static_cast<unsigned>(offset / 16384U),
                       static_cast<unsigned>(offset % 16384U));
}

void register_test_factory() {
    MapperRegistry::instance().register_mapper(
        59U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            return std::make_unique<BmcT3h53>(header, std::move(prg), std::move(chr));
        });
}

std::vector<Byte> image(bool legacy = false) {
    std::vector<Byte> bytes(16U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 8U;
    bytes[5] = 8U;
    bytes[6] = 0xB1U;
    bytes[7] = legacy ? 0x30U : 0x38U;
    const auto prg = make_prg();
    const auto chr = make_chr();
    bytes.insert(bytes.end(), prg.begin(), prg.end());
    bytes.insert(bytes.end(), chr.begin(), chr.end());
    return bytes;
}

Cartridge cartridge(bool legacy = false) {
    register_test_factory();
    auto result = Cartridge::from_data(image(legacy));
    INFO("cartridge error: " << (result ? std::string{} : result.error()));
    REQUIRE(result.has_value());
    return std::move(*result);
}

void ppu_address(Ppu& ppu, Address addr) {
    (void)ppu.read_register(2U);
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

void ppu_write(Ppu& ppu, Address addr, Byte value) {
    ppu_address(ppu, addr);
    ppu.write_register(7U, value);
}

Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    ppu_address(ppu, addr);
    return ppu.read_register(7U);
}

TEST_CASE("Mapper 059 decodes every PRG, CHR, mirroring and jumper mode", "[mappers][mapper059]") {
    for (bool legacy : {false, true}) {
        auto mapper = board(2U, legacy);
        for (Address latch = 0U; latch < 0x100U; ++latch) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0xFC00U | latch), 0xA7U);
            REQUIRE(saved_latch(*mapper) == latch);
            REQUIRE(mapper->mirror_mode() ==
                    ((latch & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            for (Address addr : {0x0000U, 0x0123U, 0x1FFFU}) {
                REQUIRE(mapper->read_chr(addr) == chr_pattern(latch & 7U, addr & 0x1FFFU));
            }
            for (Address addr : {0x8000U, 0x9A6BU, 0xBFFFU, 0xC000U, 0xFA6BU, 0xFFFFU}) {
                if ((latch & 0x100U) != 0U) {
                    REQUIRE(mapper->read_prg(addr) == 2U);
                    REQUIRE(mapper->prg_read_mask(addr) == 0x03U);
                } else {
                    REQUIRE(mapper->read_prg(addr) == expected_prg(addr, latch, 131072U));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
            if ((latch & 0x100U) != 0U) {
                mapper->write_prg(static_cast<Address>(0x8000U | (latch & ~0x100U)), 0U);
                for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU})
                    REQUIRE(mapper->read_prg(addr) == expected_prg(addr, latch, 131072U));
            }
        }
    }
}

TEST_CASE("Mapper 059 decodes all register aliases and ignores write data",
          "[mappers][mapper059]") {
    auto mapper = board();
    for (unsigned low = 0U; low < 0x8000U; ++low) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(0x8000U | low), static_cast<Byte>(low * 29U));
        INFO(low);
        REQUIRE(saved_latch(*mapper) == (low & 0x3FFU));
    }
    for (Address addr : {0x8000U, 0x8177U, 0x82A5U, 0x83FFU, 0xFC7EU}) {
        mapper->write_prg(addr, 0U);
        const auto saved = state(*mapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(state(*mapper) == saved);
        }
    }
}

TEST_CASE("Mapper 059 lock ignores all later writes until soft or cold reset",
          "[mappers][mapper059]") {
    auto mapper = board(3U);
    mapper->write_prg(0x83FFU, 0U);
    const auto locked = state(*mapper);
    REQUIRE(saved_latch(*mapper) == 0x03FFU);
    for (unsigned low = 0U; low < 0x8000U; ++low) {
        mapper->write_prg(static_cast<Address>(0x8000U | low), static_cast<Byte>(low));
        REQUIRE(state(*mapper) == locked);
    }
    mapper->reset();
    REQUIRE(saved_latch(*mapper) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0x8000U, 0U, 131072U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(0xC000U, 0U, 131072U));
    mapper->write_prg(0x82FFU, 0U);
    mapper->power_on();
    REQUIRE(saved_latch(*mapper) == 0U);
    mapper->write_prg(0x8005U, 0U);
    REQUIRE(saved_latch(*mapper) == 5U);
}

TEST_CASE("Mapper 059 wraps only after decoding fitted ROM bank pins", "[mappers][mapper059]") {
    for (bool legacy : {false, true})
        for (std::size_t prg_pages = 1U; prg_pages <= 8U; ++prg_pages)
            for (std::size_t chr_pages = 1U; chr_pages <= 8U; ++chr_pages) {
                const auto prg_size = prg_pages * 16384U;
                const auto chr_size = chr_pages * 8192U;
                auto mapper = board(0U, legacy, prg_size, chr_size);
                for (Address latch = 0U; latch < 0x100U; ++latch) {
                    mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
                    if ((latch & 0x100U) != 0U)
                        continue;
                    for (Address addr : {0x8123U, 0xC123U}) {
                        INFO("legacy=" << legacy << " prg=" << prg_pages << " chr=" << chr_pages
                                       << " latch=" << latch << " addr=" << addr);
                        REQUIRE(mapper->read_prg(addr) == expected_prg(addr, latch, prg_size));
                    }
                    const auto offset = ((latch & 7U) * 8192U + 0x123U) % chr_size;
                    REQUIRE(mapper->read_chr(0x123U) ==
                            chr_pattern(static_cast<unsigned>(offset / 8192U),
                                        static_cast<unsigned>(offset % 8192U)));
                }
            }
}

TEST_CASE("Mapper 059 CHR ROM remains immutable in every bank", "[mappers][mapper059]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (Address bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | bank), 0U);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto expected = chr_pattern(bank, addr);
            REQUIRE(mapper->read_chr(addr) == expected);
            mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == expected);
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 059 drives only jumper bits D0 and D1 over the real CPU bus",
          "[mappers][mapper059]") {
    for (bool legacy : {false, true}) {
        auto cart = cartridge(legacy);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        for (Byte jumpers = 0U; jumpers < 4U; ++jumpers) {
            REQUIRE(cart.configure_board_jumpers(jumpers));
            bus.write(0x8100U, 0xFFU);
            for (unsigned open = 0U; open < 256U; ++open) {
                bus.write(0x0010U, static_cast<Byte>(open));
                for (Address addr : {0x8000U, 0x9A55U, 0xC000U, 0xFFFFU})
                    REQUIRE(bus.read(addr) == static_cast<Byte>((open & 0xFCU) | jumpers));
            }
        }
        bus.write(0x8000U, 0U);
        bus.write(0x0010U, 0xA7U);
        REQUIRE(bus.read(0x8000U) == expected_prg(0x8000U, 0U, 131072U));
    }
}

TEST_CASE("Mapper 059 CHR and H/V CIRAM work through PPUDATA", "[mappers][mapper059]") {
    auto cart = cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    ppu.reset();
    cart.write_prg(0x800BU, 0U);
    REQUIRE(ppu_read(ppu, 0x0123U) == chr_pattern(3U, 0x0123U));
    ppu_write(ppu, 0x0123U, 0xA7U);
    REQUIRE(ppu_read(ppu, 0x0123U) == chr_pattern(3U, 0x0123U));
    ppu_write(ppu, 0x2000U, 0x19U);
    ppu_write(ppu, 0x2800U, 0x27U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x19U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x27U);
    cart.write_prg(0x8003U, 0U);
    ppu_write(ppu, 0x2000U, 0x35U);
    ppu_write(ppu, 0x2400U, 0x53U);
    REQUIRE(ppu_read(ppu, 0x2800U) == 0x35U);
    REQUIRE(ppu_read(ppu, 0x2C00U) == 0x53U);
}

TEST_CASE("Mapper 059 snapshots replay every latch and reject damage atomically",
          "[mappers][mapper059][state]") {
    auto mapper = board();
    for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
        const auto jumpers = static_cast<Byte>(latch & 3U);
        REQUIRE(mapper->configure_board_jumpers(jumpers));
        mapper->reset();
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == 4U);
        mapper->reset();
        StateReader reader(saved);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*mapper) == saved);
        REQUIRE(saved_latch(*mapper) == latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->read_chr(0x0123U) == chr_pattern(latch & 7U, 0x0123U));
    }
    REQUIRE(mapper->configure_board_jumpers(2U));
    mapper->reset();
    mapper->write_prg(0x83A5U, 0U);
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 2U; ++offset) {
        auto bad = saved;
        bad[offset] ^= 0xFFU;
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Address reserved : {0x0400U, 0x0800U, 0x1000U, 0x8000U, 0xFC00U}) {
        auto bad = saved;
        StateReader writer_input(saved);
        (void)writer_input.read<uint16_t>();
        const auto latch = writer_input.read<Address>();
        bad[2] = static_cast<Byte>(latch | reserved);
        bad[3] = static_cast<Byte>((latch | reserved) >> 8U);
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto other = board(3U);
    StateReader wrong_jumpers(saved);
    other->load_state(wrong_jumpers);
    REQUIRE_FALSE(wrong_jumpers.ok());
    REQUIRE(state(*other) == state(*board(3U)));
}

TEST_CASE("Mapper 059 validates ROM population and board metadata", "[mappers][mapper059]") {
    for (bool legacy : {false, true})
        for (std::size_t prg_pages = 1U; prg_pages <= 8U; ++prg_pages)
            for (std::size_t chr_pages = 1U; chr_pages <= 8U; ++chr_pages)
                REQUIRE(BmcT3h53::validate_geometry(
                    make_header(legacy), prg_pages * 16384U, chr_pages * 8192U));
    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto header = make_header();
        std::size_t prg_size = 131072U;
        std::size_t chr_size = 65536U;
        switch (kind) {
        case 0U:
            header.submapper = 1U;
            break;
        case 1U:
            header.has_battery = true;
            break;
        case 2U:
            header.prg_ram_size = 8192U;
            break;
        case 3U:
            header.prg_nvram_size = 8192U;
            break;
        case 4U:
            header.chr_ram_size = 8192U;
            break;
        case 5U:
            header.chr_nvram_size = 8192U;
            break;
        case 6U:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            header.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            prg_size = 0U;
            break;
        case 9U:
            prg_size = 147456U;
            break;
        case 10U:
            prg_size = 8192U;
            break;
        case 11U:
            prg_size = 16385U;
            break;
        case 12U:
            chr_size = 0U;
            break;
        case 13U:
            chr_size = 73728U;
            break;
        case 14U:
            chr_size = 4096U;
            break;
        case 15U:
            chr_size = 8193U;
            break;
        case 16U:
            header = make_header(true);
            header.has_battery = true;
            break;
        case 17U:
            header = make_header(true);
            header.chr_ram_size = 8192U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(BmcT3h53::validate_geometry(header, prg_size, chr_size));
    }
}

TEST_CASE("Mapper 059 validates jumpers and exposes no unrelated hardware",
          "[mappers][mapper059]") {
    auto mapper = board(2U);
    mapper->write_prg(0x8005U, 0U);
    const auto saved = state(*mapper);
    for (unsigned value = 4U; value < 256U; ++value) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(static_cast<Byte>(value)));
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned addr = 0x4020U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(addr)));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
        mapper->write_prg(static_cast<Address>(addr), 0xFFU);
        mapper->write_expansion(static_cast<Address>(addr), 0xFFU);
    }
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

} // namespace
