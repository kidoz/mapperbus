#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper454.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kAddressOffset = 10UZ;
constexpr std::size_t kDataOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 13UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 454U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 14U) * 47U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper454> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper454::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper454>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kChrRamOffset);
    return bytes;
}

Address saved_address(const std::vector<Byte>& bytes) {
    return static_cast<Address>(bytes[kAddressOffset] |
                                (static_cast<Address>(bytes[kAddressOffset + 1U]) << 8U));
}

void load(Mapper454& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select_address(Mapper454& mapper, Address latch) {
    mapper.reset();
    mapper.write_prg(static_cast<Address>(0x8000U | latch), 0xA5U);
}

unsigned expected_bank(Address addr, Address latch, Byte data) {
    const unsigned outer = ((latch >> 8U) & 1U) * 32U + ((latch >> 5U) & 3U) * 8U;
    const unsigned inner = (latch >> 2U) & 7U;
    const bool upper = (addr & 0x4000U) != 0U;
    const bool nrom256 = (latch & 1U) != 0U;
    if (!upper) {
        const unsigned selected = (latch & 0x100U) != 0U ? data : inner;
        return outer | (nrom256 ? selected & ~1U : selected);
    }
    if ((latch & 0x100U) != 0U) {
        return outer | 7U;
    }
    if ((latch & 0x80U) == 0U) {
        return 0U;
    }
    return outer | inner | (nrom256 ? 1U : 0U);
}

std::size_t expected_prg(Address addr, Address latch, Byte data) {
    return expected_bank(addr, latch, data) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

void require_prg(Mapper454& mapper, Address addr, Address latch, Byte data, Byte salt = 0U) {
    const auto physical = expected_prg(addr, latch, data);
    CAPTURE(addr, latch, data, physical, salt);
    REQUIRE(physical < kPrgSize);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 454 validates the 110-in-1 memory geometry", "[mapper454][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8205U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0U, 0x35U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper454::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 227U, 453U, 455U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x8000UZ, 0x080000UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 3U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 454 decodes address writes throughout the CPU space",
          "[mapper454][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        const auto registers = register_state(*mapper);
        const Address expected = raw >= 0x8000U ? static_cast<Address>(raw & 0x1FFU) : 0U;
        CAPTURE(raw, value, expected);
        REQUIRE(saved_address(registers) == expected);
        REQUIRE(registers[kDataOffset] == 0U);
        REQUIRE(mapper->mirror_mode() ==
                ((expected & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->reset();
    mapper->write_prg_cpu(0xF1A6U, 0xF7U, true);
    REQUIRE(saved_address(register_state(*mapper)) == 0x01A6U);
    REQUIRE(register_state(*mapper)[kDataOffset] == 0U);
}

TEST_CASE("Mapper 454 selects exactly one latch from the current mode",
          "[mapper454][decoding][banking]") {
    auto mapper = board();
    for (Address first = 0U; first <= 0x1FFU; ++first) {
        const auto second = static_cast<Address>((first + 0x155U) & 0x1FFU);
        const auto value = static_cast<Byte>(first * 73U + 0xF8U);
        select_address(*mapper, first);
        mapper->write_prg(static_cast<Address>(0x8000U | second), value);
        const auto registers = register_state(*mapper);
        const Address expected_address = (first & 0x100U) != 0U ? first : second;
        const Byte expected_data = (first & 0x100U) != 0U ? static_cast<Byte>(value & 7U) : 0U;
        CAPTURE(first, second, value, expected_address, expected_data);
        REQUIRE(saved_address(registers) == expected_address);
        REQUIRE(registers[kDataOffset] == expected_data);
        REQUIRE(mapper->mirror_mode() ==
                ((expected_address & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    select_address(*mapper, 0x01B6U);
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0x8000U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(saved_address(register_state(*mapper)) == 0x01B6U);
        REQUIRE(register_state(*mapper)[kDataOffset] == (raw & 7U));
    }
}

TEST_CASE("Mapper 454 maps all documented NROM and UNROM bank combinations",
          "[mapper454][banking][prg]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    for (Address latch = 0U; latch <= 0x1FFU; ++latch) {
        for (Byte data = 0U; data < 8U; ++data) {
            select_address(*mapper, latch);
            if ((latch & 0x100U) != 0U) {
                mapper->write_prg(0xFFFFU, data);
            }
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, latch, data, 0x35U);
            }
        }
    }

    constexpr std::array<Address, 6> representatives{
        0x0074U, 0x0075U, 0x00F4U, 0x00F5U, 0x0174U, 0x0175U};
    for (const auto latch : representatives) {
        select_address(*mapper, latch);
        if ((latch & 0x100U) != 0U) {
            mapper->write_prg(0xFFFFU, 3U);
        }
        const Byte data = (latch & 0x100U) != 0U ? 3U : 0U;
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            require_prg(*mapper, addr, latch, data, 0x35U);
        }
    }
}

TEST_CASE("Mapper 454 effective bank table fixes and mirrors the correct pages",
          "[mapper454][banking][prg]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    const auto check = [&](Address latch, Byte data, Address addr, std::size_t physical) {
        select_address(*mapper, latch);
        if ((latch & 0x100U) != 0U) {
            mapper->write_prg(0xC000U, data);
        }
        CAPTURE(latch, data, addr, physical);
        REQUIRE(mapper->read_prg(addr) == cell(physical, 0x35U));
    };

    check(0x0074U, 0U, 0x8000U, 29UZ * kPrgBankSize);
    check(0x0074U, 0U, 0xC000U, 0UZ);
    check(0x0075U, 0U, 0x8000U, 28UZ * kPrgBankSize);
    check(0x0075U, 0U, 0xC000U, 0UZ);
    check(0x00F4U, 0U, 0x8000U, 29UZ * kPrgBankSize);
    check(0x00F4U, 0U, 0xC000U, 29UZ * kPrgBankSize);
    check(0x00F5U, 0U, 0x8000U, 28UZ * kPrgBankSize);
    check(0x00F5U, 0U, 0xC000U, 29UZ * kPrgBankSize);
    check(0x0174U, 3U, 0x8000U, 59UZ * kPrgBankSize);
    check(0x0174U, 3U, 0xC000U, 63UZ * kPrgBankSize);
    check(0x0175U, 3U, 0x8000U, 58UZ * kPrgBankSize);
    check(0x0175U, 3U, 0xC000U, 63UZ * kPrgBankSize);
}

TEST_CASE("Mapper 454 provides writable unbanked 8 KiB CHR RAM", "[mapper454][chr][ram]") {
    auto mapper = board();
    select_address(*mapper, 0x01FFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto before = register_state(*mapper);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x5AU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x5AU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x4000U)) == cell(addr, 0x5AU));
    }
    REQUIRE(register_state(*mapper) == before);
}

TEST_CASE("Mapper 454 uses latch-controlled ordinary H V CIRAM", "[mapper454][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address latch = 0U; latch <= 0x1FFU; ++latch) {
        select_address(*mapper, latch);
        CAPTURE(latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x0101U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 454 leaves low CPU RAM expansion IRQ and audio absent",
          "[mapper454][bus][ram][irq][audio]") {
    auto mapper = board();
    select_address(*mapper, 0x01FFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
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

TEST_CASE("Mapper 454 reset clears both latches and cold power clears CHR RAM",
          "[mapper454][reset][ram]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    select_address(*mapper, 0x01FFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->write_chr(0x0123U, 0x67U);
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto soft = state(*mapper);
    REQUIRE(saved_address(soft) == 0U);
    REQUIRE(soft[kDataOffset] == 0U);
    REQUIRE(std::ranges::equal(std::span(soft).subspan(kChrRamOffset),
                               std::span(programmed).subspan(kChrRamOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    require_prg(*mapper, 0x8000U, 0U, 0U, 0x35U);
    require_prg(*mapper, 0xFFFFU, 0U, 0U, 0x35U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    select_address(*mapper, 0x0175U);
    mapper->write_prg(0xFFFFU, 6U);
    mapper->reset();
    select_address(*mapper, 0x00F5U);
    require_prg(*mapper, 0x8000U, 0x00F5U, 0U, 0x35U);
    require_prg(*mapper, 0xC000U, 0x00F5U, 0U, 0x35U);

    mapper->write_chr(0x0123U, 0xA5U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 454 bounded snapshots replay latches and CHR RAM without source ROM",
          "[mapper454][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x10U);
    select_address(*mapper, 0x01B7U);
    mapper->write_prg(0xFFFFU, 0xFDU);
    mapper->write_chr(0x0456U, 0x67U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrRamSize + 16U);
    REQUIRE(saved_address(saved) == 0x01B7U);
    REQUIRE(saved[kDataOffset] == 5U);
    REQUIRE(saved[kChrRamOffset + 0x0456U] == 0x67U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->read_chr(0x0456U) == 0x67U);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x01B7U, 5U, 0x80U);
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 454 rejects damaged truncated and incompatible states atomically",
          "[mapper454][state][ram]") {
    auto mapper = board();
    select_address(*mapper, 0x01B7U);
    mapper->write_prg(0xFFFFU, 0xFDU);
    mapper->write_chr(0x0456U, 0x67U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < active.size(); ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (unsigned bit = 0U; bit < kAddressOffset * 8U; ++bit) {
        auto invalid = active;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x02U, 0x04U, 0x80U, 0xFEU}) {
        auto invalid = active;
        invalid[kAddressOffset + 1U] |= reserved;
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }
    for (const Byte reserved : {0x08U, 0x10U, 0x80U, 0xF8U}) {
        auto invalid = active;
        invalid[kDataOffset] |= reserved;
        invalid[kChrRamOffset + 0x456U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }

    const auto h = header();
    auto wrong_size = std::make_unique<Mapper454>(h, rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto before = state(*wrong_size);
    StateReader size_reader(active);
    wrong_size->load_state(size_reader);
    REQUIRE_FALSE(size_reader.ok());
    REQUIRE(state(*wrong_size) == before);

    auto changed = active;
    changed[kAddressOffset] = 0xF5U;
    changed[kAddressOffset + 1U] = 0x00U;
    changed[kDataOffset] = 0x07U;
    changed[kChrRamOffset + 0x456U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0x00F5U, 7U);
    }
}

} // namespace
