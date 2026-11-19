#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper409.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x4000000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kInitialMirrorOffset = 14UZ;
constexpr std::size_t kBankOffset = 15UZ;
constexpr std::size_t kChrRamOffset = 17UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 409U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + (offset >> 22U) * 97U +
                             (offset >> 25U) * 101U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper409> board(std::size_t prg_size = 0x20000UZ,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper409::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper409>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper409& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t switchable_address(Address addr, unsigned bank, std::size_t prg_size) {
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) &
           (prg_size - 1U);
}

std::size_t fixed_address(Address addr, std::size_t prg_size) {
    return prg_size - kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

template <typename T> void overwrite(std::vector<Byte>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 409 validates DPCMcart geometry and fixed memory population",
          "[mapper409][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8209U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(kPrgBankSize));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(2UZ * kPrgBankSize - 1U));
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size *= 2U) {
        CAPTURE(size);
        REQUIRE(Mapper409::validate_geometry(header(), size, 0U));
    }

    for (const unsigned id : {0U, 2U, 256U, 408U, 410U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper409::validate_geometry(h, kMinPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper409::validate_geometry(legacy, kMinPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper409::validate_geometry(h, kMinPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x18000UZ, kMaxPrgSize + 1U, kMaxPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper409::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kMinPrgSize, kMaxPrgSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper409::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper409::validate_geometry(header(mirror), kMinPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
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
        case 6U:
            h.chr_nvram_size = kChrRamSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper409::validate_geometry(h, kMinPrgSize, 0U));
    }
}

TEST_CASE("Mapper 409 decodes only C000-CFFF address writes without data bus conflicts",
          "[mapper409][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const bool decoded = raw >= 0xC000U && raw < 0xD000U;
        const unsigned bank = decoded ? raw & 0x0FFFU : 0U;
        mapper->write_prg(addr, value);
        CAPTURE(raw, value, decoded, bank);
        REQUIRE(mapper->read_prg(0x8123U) == cell(switchable_address(0x8123U, bank, kMaxPrgSize)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(fixed_address(0xC123U, kMaxPrgSize)));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0xCABCU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->read_prg(0xA123U) ==
                cell(switchable_address(0xA123U, 0x0ABCU, kMaxPrgSize)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(fixed_address(0xC123U, kMaxPrgSize)));
    }

    mapper->reset();
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0x5AU);
    }
    REQUIRE(state(*mapper) == before);
    for (const auto addr : kPrgAddresses) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2012U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2012U, 0xA7U));
}

TEST_CASE("Mapper 409 wraps switchable banks to fitted ROM and fixes the physical last bank",
          "[mapper409][banking][bus]") {
    for (const std::size_t size : {0x8000UZ, 0x10000UZ, 0x20000UZ, 0x100000UZ}) {
        auto mapper = board(size);
        mapper->write_prg(0xCFFFU, 0U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = addr < 0xC000U ? switchable_address(addr, 0x0FFFU, size)
                                                 : fixed_address(addr, size);
            CAPTURE(size, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        mapper->write_prg(0xC000U, 0U);
        for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xB234U}) {
            CAPTURE(size, addr);
            REQUIRE(mapper->read_prg(addr) == cell(switchable_address(addr, 0U, size)));
        }
    }
}

TEST_CASE("Mapper 409 provides unbanked writable 8 KiB CHR RAM", "[mapper409][ram][chr]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto value = cell(addr, 0x35U);
        mapper->write_chr(addr, value);
        CAPTURE(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == value);
    }
    mapper->write_prg(0xCFFFU, 0U);
    for (const Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1123U, 0x1FFFU}) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
    }
}

TEST_CASE("Mapper 409 retains hard-wired CIRAM and has no CPU RAM IRQ or expansion audio",
          "[mapper409][ciram][ram][irq][audio][bus]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(0x20000UZ, mirror);
        mapper->write_prg(0xCABCU, 0xFFU);
        const auto before = state(*mapper);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
        mapper->set_battery_ram(std::array<Byte, 0x2000>{});
        mapper->set_shared_nvram(std::array<Byte, 0x2000>{});
        mapper->clock_irq_counter();
        mapper->acknowledge_irq();
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_address(0x1234U);
        mapper->observe_ppu_read(0x1234U);
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
    }
}

TEST_CASE("Mapper 409 soft reset selects bank zero and retains CHR RAM while cold power clears it",
          "[mapper409][reset][ram]") {
    auto mapper = board(0x20000UZ, MirrorMode::Horizontal);
    mapper->write_prg(0xCABCU, 0U);
    mapper->write_chr(0x1123U, 0x5CU);
    REQUIRE(mapper->read_prg(0x8123U) == cell(switchable_address(0x8123U, 0x0ABCU, 0x20000UZ)));

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x123U));
    REQUIRE(mapper->read_prg(0xC123U) == cell(fixed_address(0xC123U, 0x20000UZ)));
    REQUIRE(mapper->read_chr(0x1123U) == 0x5CU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->write_prg(0xCABCU, 0U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x123U));
    for (Address addr = 0U; addr < 0x2000U; addr = static_cast<Address>(addr + 0x101U)) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 409 bounded snapshots replay latch and CHR RAM without ROM",
          "[mapper409][state][ram]") {
    constexpr std::size_t size = 0x20000UZ;
    auto mapper = board(size);
    mapper->write_prg(0xCABCU, 0U);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_chr(0x1123U, 0x5CU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    std::uint16_t bank = 0U;
    std::memcpy(&bank, saved.data() + kBankOffset, sizeof(bank));
    REQUIRE(bank == 0x0ABCU);
    REQUIRE(saved[kChrRamOffset + 0x0123U] == 0xA7U);
    REQUIRE(saved[kChrRamOffset + 0x1123U] == 0x5CU);

    auto copy = board(size, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        const auto physical =
            addr < 0xC000U ? switchable_address(addr, 0x0ABCU, size) : fixed_address(addr, size);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA7U);
    REQUIRE(copy->read_chr(0x1123U) == 0x5CU);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 409 rejects damaged truncated and mismatched states atomically",
          "[mapper409][state]") {
    auto mapper = board();
    mapper->write_prg(0xCABCU, 0U);
    mapper->write_chr(0x1123U, 0x5AU);
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
    for (std::size_t offset = 0U; offset < kGeometrySize; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    auto wrong_mirror = saved;
    wrong_mirror[kInitialMirrorOffset] ^= 1U;
    reject(wrong_mirror);
    for (const std::uint16_t bank : {0x1000U, 0x8000U, 0xFFFFU}) {
        auto invalid = saved;
        overwrite(invalid, kBankOffset, bank);
        CAPTURE(bank);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_initial_mirror = board(0x20000UZ, MirrorMode::Horizontal);
    const auto wrong_initial_mirror_before = state(*wrong_initial_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_initial_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_initial_mirror) == wrong_initial_mirror_before);

    auto changed_ram = saved;
    changed_ram[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed_ram);
    REQUIRE(state(*mapper) == changed_ram);
}

} // namespace
