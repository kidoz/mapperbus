#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/karaoke_studio_188.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool legacy = false, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 188U;
    header.is_nes2 = !legacy;
    header.prg_ram_size = legacy ? 0x2000U : 0U;
    header.chr_ram_size = 0x2000U;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return header;
}

Byte prg_cell(unsigned chip, unsigned bank, unsigned offset) {
    if (offset == 0U)
        return 0xFFU;
    return static_cast<Byte>(chip * 0x80U + bank * 7U + offset * 13U + (offset >> 8U));
}

std::vector<Byte> make_prg(std::size_t size) {
    const std::size_t internal_size = size <= 0x20000U ? size : size / 2U;
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i) {
        const bool external = i >= internal_size;
        const std::size_t local = external ? i - internal_size : i;
        rom[i] = prg_cell(external ? 1U : 0U, local / 0x4000U, local % 0x4000U);
    }
    return rom;
}

std::vector<Byte> make_ines_rom(std::size_t prg_size) {
    std::vector<Byte> image = {
        'N',
        'E',
        'S',
        0x1AU,
        static_cast<Byte>(prg_size / 0x4000U),
        0U,
        0xC0U,
        0xB0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    const auto prg = make_prg(prg_size);
    image.insert(image.end(), prg.begin(), prg.end());
    return image;
}

std::unique_ptr<KaraokeStudio188> make_board(std::size_t prg_size = 0x80000U,
                                             bool legacy = false,
                                             bool vertical = true) {
    const auto header = make_header(legacy, vertical);
    REQUIRE(KaraokeStudio188::validate_geometry(header, prg_size, 0U));
    return std::make_unique<KaraokeStudio188>(header, make_prg(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_control(Mapper& mapper, Byte value) {
    mapper.write_prg(0x8000U, value);
}

TEST_CASE("Mapper 188 maps every internal and external PRG bank with a fixed internal upper bank",
          "[mapper188]") {
    for (const std::size_t size : {0x40000UZ, 0x80000UZ}) {
        auto mapper = make_board(size);
        const unsigned banks_per_chip = size == 0x40000U ? 8U : 16U;
        const unsigned fixed_bank = banks_per_chip - 1U;
        for (unsigned source = 0U; source < 2U; ++source) {
            for (unsigned bank = 0U; bank < 16U; ++bank) {
                write_control(*mapper, static_cast<Byte>(bank | (source << 4U)));
                const unsigned selected = bank % banks_per_chip;
                for (const Address addr : {0x8001U, 0x9234U, 0xA555U, 0xBFFFU}) {
                    INFO("size=" << size << " source=" << source << " bank=" << bank
                                 << " addr=" << addr);
                    REQUIRE(mapper->read_prg(addr) ==
                            prg_cell(1U - source, selected, addr & 0x3FFFU));
                }
                REQUIRE(mapper->read_prg(0xC001U) == prg_cell(0U, fixed_bank, 1U));
                REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(0U, fixed_bank, 0x3FFFU));
            }
        }
    }
}

TEST_CASE("Mapper 188 mirrors disconnected high pins on internal-only images", "[mapper188]") {
    for (const std::size_t size : {0x4000UZ, 0x8000UZ, 0x10000UZ, 0x20000UZ}) {
        auto mapper = make_board(size);
        const unsigned bank_count = size / 0x4000U;
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            write_control(*mapper, static_cast<Byte>(0x10U | bank));
            REQUIRE(mapper->read_prg(0x8001U) == prg_cell(0U, bank % bank_count, 1U));
            REQUIRE(mapper->read_prg(0xC001U) == prg_cell(0U, bank_count - 1U, 1U));
            write_control(*mapper, static_cast<Byte>(bank));
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
            REQUIRE(mapper->read_prg(0x8001U) == 0U);
        }
    }
}

TEST_CASE("Mapper 188 decodes every CPU address and all values with PRG bus conflicts",
          "[mapper188]") {
    auto mapper = make_board(0x40000U);
    for (unsigned value = 0U; value < 256U; ++value) {
        write_control(*mapper, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[10] == (value & 0x7FU));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        write_control(*mapper, 0U);
        const auto address = static_cast<Address>(addr);
        const Byte value = static_cast<Byte>(addr * 29U + (addr >> 8U));
        const Byte expected =
            addr >= 0x8000U ? static_cast<Byte>(value & mapper->read_prg(address) & 0x7FU) : 0U;
        mapper->write_prg(address, value);
        INFO("addr=" << addr << " value=" << +value << " expected=" << +expected);
        REQUIRE(state(*mapper)[10] == expected);
        REQUIRE(mapper->maps_prg(address) == (addr >= 0x6000U));
        REQUIRE_FALSE(mapper->maps_expansion(address));
    }
}

TEST_CASE("Mapper 188 conflicts use the old mapping and absent expansion writes do not AND zero",
          "[mapper188]") {
    auto mapper = make_board();
    write_control(*mapper, 0x12U);
    const Byte internal = mapper->read_prg(0x8123U);
    mapper->write_prg(0x8123U, 0xFFU);
    REQUIRE(state(*mapper)[10] == (internal & 0x7FU));

    write_control(*mapper, 0x03U);
    const Byte external = mapper->read_prg(0x9234U);
    mapper->write_prg(0x9234U, 0xFFU);
    REQUIRE(state(*mapper)[10] == (external & 0x7FU));

    const Byte fixed = mapper->read_prg(0xC345U);
    mapper->write_prg(0xC345U, 0xFFU);
    REQUIRE(state(*mapper)[10] == (fixed & 0x7FU));

    auto internal_only = make_board(0x20000U);
    internal_only->write_prg(0x8000U, 0x7FU);
    REQUIRE(state(*internal_only)[10] == 0x7FU);
    REQUIRE(internal_only->read_prg(0x8001U) == prg_cell(0U, 7U, 1U));
}

TEST_CASE("Mapper 188 exposes D2 ADC and active-low buttons with open-bus high bits",
          "[mapper188]") {
    auto mapper = make_board(0x20000U);
    for (unsigned inputs = 0U; inputs < 8U; ++inputs) {
        const bool level = (inputs & 4U) != 0U;
        const bool button_a = (inputs & 1U) != 0U;
        const bool button_b = (inputs & 2U) != 0U;
        mapper->set_microphone_inputs(level, button_a, button_b);
        const Byte expected =
            static_cast<Byte>((level ? 4U : 0U) | (button_b ? 0U : 2U) | (button_a ? 0U : 1U));
        for (const Address addr : {0x6000U, 0x6123U, 0x7000U, 0x7FFFU}) {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0x07U);
            REQUIRE(mapper->read_prg(addr) == expected);
            mapper->write_prg(addr, 0xFFU);
            REQUIRE(mapper->read_prg(addr) == expected);
        }
    }

    MapperRegistry::instance().register_mapper(
        188U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            auto karaoke =
                std::make_unique<KaraokeStudio188>(header, std::move(prg), std::move(chr));
            karaoke->set_microphone_inputs(true, false, true);
            return karaoke;
        });
    auto cartridge = Cartridge::from_data(make_ines_rom(0x20000U));
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    bus.write(0U, 0xA8U);
    REQUIRE(bus.read(0x6000U) == 0xADU);
    bus.write(0U, 0x5AU);
    REQUIRE(bus.read(0x8000U) == 0x5AU);
    REQUIRE(bus.read(0xC001U) == prg_cell(0U, 7U, 1U));
}

TEST_CASE("Mapper 188 owns writable unbanked CHR RAM and no unrelated devices", "[mapper188]") {
    auto mapper = make_board();
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        mapper->write_chr(addr, static_cast<Byte>(addr * 37U + (addr >> 8U)));
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 37U + (addr >> 8U)));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                static_cast<Byte>(addr * 37U + (addr >> 8U)));
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->maps_expansion(0x4020U));
    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
    REQUIRE(mapper->prg_read_mask(0x5FFFU) == 0U);
}

TEST_CASE("Mapper 188 soft reset retains the unreset latch and power clears CHR", "[mapper188]") {
    auto mapper = make_board(0x20000U);
    mapper->set_microphone_inputs(true, true, false);
    mapper->write_chr(0x123U, 0xA7U);
    write_control(*mapper, 0x55U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    write_control(*mapper, 0x75U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    const auto before_reset = state(*mapper);

    mapper->reset();
    REQUIRE(state(*mapper) == before_reset);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_chr(0x123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x06U);

    write_control(*mapper, 0x35U);
    mapper->power_on();
    REQUIRE(state(*mapper)[10] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
    REQUIRE(mapper->read_chr(0x123U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x06U);
}

TEST_CASE("Mapper 188 state replays latch and CHR RAM without serializing ROM or physical inputs",
          "[mapper188][state]") {
    auto mapper = make_board();
    write_control(*mapper, 0x7BU);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        mapper->write_chr(addr, static_cast<Byte>(addr * 19U + (addr >> 7U)));
    mapper->set_microphone_inputs(true, true, true);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 0x200BU);

    auto copy = make_board();
    copy->set_microphone_inputs(false, false, false);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8001U) == prg_cell(0U, 11U, 1U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->read_prg(0x6000U) == 0x03U);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(addr * 19U + (addr >> 7U)));
}

TEST_CASE("Mapper 188 malformed state rejection is atomic", "[mapper188][state]") {
    auto mapper = make_board();
    write_control(*mapper, 0x4AU);
    mapper->write_chr(0x345U, 0x91U);
    const auto saved = state(*mapper);
    const auto reject = [&mapper, &saved](std::span<const Byte> candidate) {
        StateReader reader(candidate);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
        REQUIRE(mapper->read_chr(0x345U) == 0x91U);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length)
        reject(std::span(saved).first(length));
    for (const std::size_t offset : {0UZ, 2UZ, 6UZ, 10UZ}) {
        auto bad = saved;
        bad[offset] ^= 0x80U;
        reject(bad);
    }
    for (const std::size_t size : {0x20000UZ, 0x40000UZ}) {
        auto other = make_board(size);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

TEST_CASE("Mapper 188 validates unambiguous layouts and exact cartridge memory metadata",
          "[mapper188]") {
    for (const bool legacy : {false, true}) {
        for (const bool vertical : {false, true}) {
            for (const std::size_t size :
                 {0x4000UZ, 0x8000UZ, 0x10000UZ, 0x20000UZ, 0x40000UZ, 0x80000UZ})
                REQUIRE(
                    KaraokeStudio188::validate_geometry(make_header(legacy, vertical), size, 0U));
        }
    }
    for (const std::size_t size :
         {0UZ, 0x2000UZ, 0x5000UZ, 0x18000UZ, 0x30000UZ, 0x60000UZ, 0xA0000UZ, 0x100000UZ})
        REQUIRE_FALSE(KaraokeStudio188::validate_geometry(make_header(), size, 0U));

    for (unsigned kind = 0U; kind < 13U; ++kind) {
        auto header = make_header();
        std::size_t chr_size = 0U;
        switch (kind) {
        case 0U:
            header.mapper_number = 187U;
            break;
        case 1U:
            header.submapper = 1U;
            break;
        case 2U:
            chr_size = 0x2000U;
            break;
        case 3U:
            header.has_battery = true;
            break;
        case 4U:
            header.prg_ram_size = 0x2000U;
            break;
        case 5U:
            header.prg_nvram_size = 0x2000U;
            break;
        case 6U:
            header.chr_ram_size = 0U;
            break;
        case 7U:
            header.chr_ram_size = 0x4000U;
            break;
        case 8U:
            header.chr_nvram_size = 0x2000U;
            break;
        case 9U:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        case 10U:
            header.mirror_mode = MirrorMode::SingleLower;
            break;
        case 11U:
            header.mirror_mode = MirrorMode::SingleUpper;
            break;
        default:
            header.submapper = 15U;
            break;
        }
        INFO("kind=" << kind);
        REQUIRE_FALSE(KaraokeStudio188::validate_geometry(header, 0x20000U, chr_size));
    }
}

} // namespace
