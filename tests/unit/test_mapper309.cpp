#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper309.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kPrgSize = 16U * kBankSize;
constexpr std::size_t kStateSize = 4U + 2U * kBankSize;

INesHeader header(bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 309U;
    h.is_nes2 = true;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = kBankSize;
    h.chr_ram_size = kBankSize;
    return h;
}

Byte cell(std::size_t offset) {
    const auto bank = offset / kBankSize;
    const auto within = offset & (kBankSize - 1U);
    return static_cast<Byte>(bank * 37U + within + (within >> 8U));
}

std::vector<Byte> rom() {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset)
        data[offset] = cell(offset);
    return data;
}

std::unique_ptr<Mapper309> board(bool vertical = true) {
    const auto h = header(vertical);
    REQUIRE(Mapper309::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper309>(h, rom(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 309 maps all switchable and fixed PRG-ROM cells", "[mapper309]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        const auto bank = value & 0x0FU;
        for (unsigned addr = 0x8000U; addr < 0xA000U; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    cell(bank * kBankSize + (addr & 0x1FFFU)));
    }
    for (unsigned addr = 0xA000U; addr <= 0xFFFFU; ++addr) {
        const auto bank = 13U + (addr - 0xA000U) / kBankSize;
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                cell(bank * kBankSize + (addr & 0x1FFFU)));
    }
    for (unsigned addr = 0U; addr < 0x6000U; ++addr)
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(addr)));
    for (unsigned addr = 0x6000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
}

TEST_CASE("Mapper 309 decodes only the two documented E000 register windows", "[mapper309]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned addr = 0U; addr < 0x6000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), 0xAFU);
        REQUIRE(state(*mapper) == initial);
    }
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0xAFU);
        if (addr < 0xA000U) {
            REQUIRE(mapper->read_prg(0x8000U) == cell(15U * kBankSize));
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        } else if (addr >= 0xE000U) {
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        } else {
            REQUIRE(state(*mapper) == initial);
        }
    }
}

TEST_CASE("Mapper 309 register data retains only bank D0-D3 and mirroring D3", "[mapper309]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Address addr : {0x8000U, 0x9FFFU}) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(mapper->read_prg(0x8123U) == cell((value & 0x0FU) * kBankSize + 0x123U));
        }
        for (Address addr : {0xE000U, 0xFFFFU}) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 309 provides independent unbanked volatile PRG and CHR RAM", "[mapper309]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kBankSize; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        mapper->write_prg(static_cast<Address>(0x6000U + addr), value);
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(value ^ 0xA5U));
    }
    for (unsigned value = 0U; value < 256U; ++value)
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
    for (unsigned addr = 0U; addr < kBankSize; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(value ^ 0xA5U));
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 309 has no IRQ or expansion audio hardware", "[mapper309]") {
    auto mapper = board();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU})
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 309 reset clears latches but only cold power clears RAM", "[mapper309]") {
    auto mapper = board(false);
    mapper->write_prg(0x8000U, 12U);
    mapper->write_prg(0xE000U, 8U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x0321U, 0x59U);

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_chr(0x0321U) == 0x59U);

    mapper->write_prg(0x8000U, 12U);
    mapper->write_prg(0xE000U, 8U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_chr(0x0321U) == 0U);
}

TEST_CASE("Mapper 309 accepts only the documented NES 2.0 board population", "[mapper309]") {
    for (bool vertical : {false, true})
        REQUIRE(Mapper309::validate_geometry(header(vertical), kPrgSize, 0U));

    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 131071UZ, 131073UZ, 262144UZ})
        REQUIRE_FALSE(Mapper309::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1UZ, 8192UZ, 131072UZ})
        REQUIRE_FALSE(Mapper309::validate_geometry(header(), kPrgSize, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper309::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned kind = 0U; kind < 14U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.mapper_number = 199U;
            break;
        case 1U:
            h.is_nes2 = false;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 0U;
            break;
        case 4U:
            h.prg_ram_size = 4096U;
            break;
        case 5U:
            h.prg_ram_size = 16384U;
            break;
        case 6U:
            h.prg_nvram_size = 8192U;
            break;
        case 7U:
            h.chr_ram_size = 0U;
            break;
        case 8U:
            h.chr_ram_size = 4096U;
            break;
        case 9U:
            h.chr_ram_size = 16384U;
            break;
        case 10U:
            h.chr_nvram_size = 8192U;
            break;
        case 11U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 12U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 13U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Mapper309::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 309 state replays both RAM arrays and latches without ROM", "[mapper309]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 13U);
    mapper->write_prg(0xE000U, 8U);
    for (unsigned addr = 0U; addr < kBankSize; ++addr) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr * 13U));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 29U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    mapper->power_on();
    mapper->write_prg(0x8000U, 2U);
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == cell(13U * kBankSize));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned addr = 0U; addr < kBankSize; ++addr) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) ==
                static_cast<Byte>(addr * 13U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 29U));
    }
}

TEST_CASE("Mapper 309 bounded malformed states reject atomically", "[mapper309]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 11U);
    mapper->write_prg(0xE000U, 8U);
    for (unsigned addr = 0U; addr < kBankSize; ++addr) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr ^ 0x5AU));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length)
        reject(std::span(saved).first(length));
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(bad);
    }
    for (unsigned bank = 16U; bank < 256U; ++bank) {
        auto bad = saved;
        bad[2] = static_cast<Byte>(bank);
        reject(bad);
    }
    for (unsigned mirror = 2U; mirror < 256U; ++mirror) {
        auto bad = saved;
        bad[3] = static_cast<Byte>(mirror);
        reject(bad);
    }
}

std::vector<Byte> image() {
    auto prg = rom();
    std::vector<Byte> data(16U, 0U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 8U;
    data[6] = 0x51U;
    data[7] = 0x38U;
    data[8] = 0x01U;
    data[10] = 0x07U;
    data[11] = 0x07U;
    data.insert(data.end(), prg.begin(), prg.end());
    return data;
}

Cartridge cartridge() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        309U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!Mapper309::validate_geometry(h, prg.size(), chr.size()))
                return nullptr;
            return std::make_unique<Mapper309>(h, std::move(prg), std::move(chr));
        });
    auto result = Cartridge::from_data(image());
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("Mapper 309 drives RAM and ROM while unmapped low reads retain CPU open bus",
          "[mapper309]") {
    auto cart = cartridge();
    REQUIRE_FALSE(cart.has_battery());
    REQUIRE(cart.battery_ram().empty());
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x6000U, 0xA7U);
    REQUIRE(bus.read(0x6000U) == 0xA7U);
    bus.write(0x8000U, 5U);
    REQUIRE(bus.read(0x8000U) == cell(5U * kBankSize));
    bus.write(0xE000U, 8U);
    REQUIRE(cart.mirror_mode() == MirrorMode::Horizontal);
}

} // namespace
