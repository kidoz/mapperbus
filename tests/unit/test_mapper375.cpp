#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper375.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kAddressLatchOffset = 10UZ;
constexpr std::size_t kDataLatchOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 13UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(bool nes2 = true, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 375U;
    result.is_nes2 = nes2;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    result.prg_ram_size = nes2 ? 0U : kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + (offset >> 20U) * 13U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper375> board(bool nes2 = true,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(nes2, mirror);
    REQUIRE(Mapper375::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper375>(h, rom(salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address expected_address_latch(Address raw) {
    return raw & 0x0FFFU;
}

unsigned expected_bank(Address latch, Byte data, bool upper) {
    const unsigned address_inner = (latch >> 2U) & 7U;
    unsigned inner = (latch & 0x0800U) != 0U && !upper ? data : address_inner;
    if (upper && (latch & 0x0080U) == 0U) {
        inner = (latch & 0x0200U) != 0U ? 7U : 0U;
    }
    if ((latch & 1U) != 0U) {
        inner = (inner & 6U) | static_cast<unsigned>(upper);
    }
    const unsigned outer = ((latch >> 5U) & 3U) | ((latch >> 6U) & 4U) | ((latch >> 7U) & 8U);
    return (outer << 3U) | inner;
}

void select_locked_mode(Mapper375& mapper) {
    mapper.write_prg(0x8455U, 0xA3U);
    mapper.write_prg(0x8D93U, 0xFCU);
}

TEST_CASE("Mapper 375 validates legacy and NES 2.0 board geometry", "[mapper375]") {
    for (const bool nes2 : {false, true}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(nes2, mirror);
            REQUIRE(Mapper375::validate_geometry(header(nes2, mirror), kPrgSize, 0U));
            auto mapper = board(nes2, mirror);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }

    for (const auto id : {0U, 2U, 227U, 242U, 374U, 376U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper375::validate_geometry(h, kPrgSize, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper375::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x4000UZ,
                                   0x8000UZ,
                                   0x100000UZ,
                                   kPrgSize - 1UZ,
                                   kPrgSize + 1UZ,
                                   kPrgSize * 2UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper375::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper375::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper375::validate_geometry(header(true, mirror), kPrgSize, 0U));
        REQUIRE_FALSE(Mapper375::validate_geometry(header(false, mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper375::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy_battery = header(false);
    legacy_battery.has_battery = true;
    REQUIRE_FALSE(Mapper375::validate_geometry(legacy_battery, kPrgSize, 0U));
}

TEST_CASE("Mapper 375 decodes every address and data latch write", "[mapper375][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        const auto saved = state(*mapper);
        const auto latch = expected_address_latch(static_cast<Address>(raw));
        CAPTURE(raw, value, latch);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(saved[kAddressLatchOffset] == static_cast<Byte>(latch));
        REQUIRE(saved[kAddressLatchOffset + 1U] == static_cast<Byte>(latch >> 8U));
        REQUIRE(saved[kDataLatchOffset] == (value & 7U));
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    mapper->power_on();
    mapper->write_prg(0x8A55U, 0xF8U);
    const auto locked_latch = expected_address_latch(0x8A55U);
    REQUIRE((locked_latch & 0x0800U) != 0U);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        const auto saved = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(saved[kAddressLatchOffset] == static_cast<Byte>(locked_latch));
        REQUIRE(saved[kAddressLatchOffset + 1U] == static_cast<Byte>(locked_latch >> 8U));
        REQUIRE(saved[kDataLatchOffset] == (value & 7U));
    }

    mapper->power_on();
    mapper->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(state(*mapper)[kAddressLatchOffset] == 0U);
    REQUIRE(state(*mapper)[kDataLatchOffset] == 7U);
}

TEST_CASE("Mapper 375 applies all inner outer fixed mirrored and aligned PRG modes",
          "[mapper375][banking]") {
    auto mapper = board();
    for (unsigned command = 0U; command < 0x1000U; ++command) {
        for (Byte data = 0U; data < 8U; ++data) {
            mapper->power_on();
            const auto latch = static_cast<Address>(command);
            mapper->write_prg(static_cast<Address>(0x8000U | command), data);
            CAPTURE(command, data, latch);
            for (const auto addr : kPrgAddresses) {
                const bool upper = addr >= 0xC000U;
                const auto bank = expected_bank(latch, data, upper);
                const auto offset = static_cast<std::size_t>(bank) * 0x4000UZ + (addr & 0x3FFFU);
                CAPTURE(addr, upper, bank, offset);
                REQUIRE(mapper->read_prg(addr) == cell(offset));
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }
}

TEST_CASE("Mapper 375 lock writes capture all fields before freezing the address latch",
          "[mapper375][banking][bus]") {
    auto mapper = board();
    for (unsigned initial = 0U; initial < 0x0800U; initial += 0x101U) {
        for (unsigned lock = 0x0800U; lock < 0x1000U; lock += 0x101U) {
            mapper->power_on();
            mapper->write_prg(static_cast<Address>(0x8000U | initial), 1U);
            mapper->write_prg(static_cast<Address>(0x8000U | lock), 6U);
            const auto saved = state(*mapper);
            CAPTURE(initial, lock);
            REQUIRE(saved[kAddressLatchOffset] == static_cast<Byte>(lock));
            REQUIRE(saved[kAddressLatchOffset + 1U] == static_cast<Byte>(lock >> 8U));
            REQUIRE(saved[kDataLatchOffset] == 6U);
            for (const auto addr : kPrgAddresses) {
                const auto bank = expected_bank(static_cast<Address>(lock), 6U, addr >= 0xC000U);
                const auto offset = static_cast<std::size_t>(bank) * 0x4000UZ + (addr & 0x3FFFU);
                REQUIRE(mapper->read_prg(addr) == cell(offset));
            }
        }
    }
}

TEST_CASE("Mapper 375 uses flat CHR RAM and protects it only in NROM modes",
          "[mapper375][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
    }

    for (unsigned command = 0U; command < 0x1000U; ++command) {
        if ((command & 0x0080U) == 0U) {
            continue;
        }
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(0x8000U | command), 0xA7U);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xFFU));
            CAPTURE(command, addr);
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }

    mapper->power_on();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (unsigned command = 0U; command < 0x1000U; ++command) {
        if ((command & 0x0080U) == 0U) {
            continue;
        }
        mapper->write_prg(static_cast<Address>(0x8000U | (command & 0x07FFU)), 0U);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            const auto previous = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(previous ^ 0xFFU));
            CAPTURE(command, addr);
            REQUIRE(mapper->read_chr(addr) == previous);
        }
    }
}

TEST_CASE("Mapper 375 uses latch-controlled ordinary CIRAM routing", "[mapper375][ciram]") {
    for (const bool nes2 : {false, true}) {
        auto mapper = board(nes2, MirrorMode::Horizontal);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (unsigned command = 0U; command < 0x1000U; ++command) {
            mapper->power_on();
            mapper->write_prg(static_cast<Address>(0x8000U | command), 0U);
            CAPTURE(nes2, command);
            REQUIRE(mapper->mirror_mode() ==
                    ((command & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    }
}

TEST_CASE("Mapper 375 leaves CPU RAM expansion IRQ and audio absent",
          "[mapper375][bus][ram][irq][audio]") {
    auto mapper = board();
    select_locked_mode(*mapper);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, static_cast<Byte>(raw * 41U));
        mapper->write_expansion(addr, static_cast<Byte>(raw * 37U));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 375 reset clears both latches and cold power clears CHR RAM",
          "[mapper375][reset][ram]") {
    auto mapper = board(true, MirrorMode::Horizontal);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    select_locked_mode(*mapper);
    REQUIRE(state(*mapper)[kAddressLatchOffset] != 0U);
    REQUIRE(state(*mapper)[kDataLatchOffset] != 0U);
    const auto programmed_state = state(*mapper);
    const auto programmed_chr = std::span(programmed_state).subspan(kChrRamOffset, kChrRamSize);

    mapper->reset();
    const auto reset_state = state(*mapper);
    REQUIRE(reset_state[kAddressLatchOffset] == 0U);
    REQUIRE(reset_state[kAddressLatchOffset + 1U] == 0U);
    REQUIRE(reset_state[kDataLatchOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(reset_state).subspan(kChrRamOffset, kChrRamSize),
                               programmed_chr));
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x3FFFU));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_chr(0x0123U, 0x67U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 375 bounded snapshots replay latches and CHR RAM without ROM",
          "[mapper375][state]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    select_locked_mode(*mapper);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8205U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kAddressLatchOffset] == 0x93U);
    REQUIRE(saved[kAddressLatchOffset + 1U] == 0x0DU);
    REQUIRE(saved[kDataLatchOffset] == 4U);

    auto copy = board(false, MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
        const auto previous = copy->read_chr(addr);
        copy->write_chr(addr, static_cast<Byte>(previous ^ 0xFFU));
        REQUIRE(copy->read_chr(addr) == previous);
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
    copy->reset();
    mapper->reset();
    REQUIRE(state(*copy) == state(*mapper));
}

TEST_CASE("Mapper 375 rejects damaged truncated and incompatible states atomically",
          "[mapper375][state]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    select_locked_mode(*mapper);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 80U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 8> kInvalidFields{{
        {kAddressLatchOffset + 1U, 0x10U},
        {kAddressLatchOffset + 1U, 0xFFU},
        {kDataLatchOffset, 8U},
        {kDataLatchOffset, 0x80U},
        {kDataLatchOffset, 0xFFU},
        {4U, 0U},
        {6U, 1U},
        {1U, 0U},
    }};
    for (const auto [offset, value] : kInvalidFields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
}

} // namespace
