#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper522.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kDefaultPrgSize = 0x020000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kBankSelectStateOffset = 10UZ;
constexpr std::size_t kPrgBanksStateOffset = 11UZ;
constexpr std::size_t kPrgRamStateOffset = 13UZ;
constexpr std::size_t kChrRamStateOffset = kPrgRamStateOffset + kPrgRamSize;
constexpr std::size_t kStateSize = kChrRamStateOffset + kChrRamSize;
constexpr std::array<Address, 9> kPrgAddresses{
    0x6000U, 0x7FFFU, 0x8000U, 0x9234U, 0xA000U, 0xBFFFU, 0xE000U, 0xFFFEU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 522U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.prg_ram_size = kPrgRamSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 20U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper522> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper522::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper522>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kPrgRamStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kPrgRamStateOffset> result{};
    std::copy_n(bytes.begin(), kPrgRamStateOffset, result.begin());
    return result;
}

void load(Mapper522& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper522& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto wram = mapper.read_prg(0xC123U);
    const auto chr = mapper.read_chr(0x0123U);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_prg(0xC123U) == wram);
    REQUIRE(mapper.read_chr(0x0123U) == chr);
}

std::size_t prg_offset(Address addr, std::size_t prg_size, Byte low_bank, Byte high_bank) {
    if (addr < 0x8000U) {
        return prg_size - 2UZ * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    if (addr < 0xA000U) {
        return (static_cast<std::size_t>(low_bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) %
               prg_size;
    }
    if (addr < 0xC000U) {
        return (static_cast<std::size_t>(high_bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) %
               prg_size;
    }
    return prg_size - kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

void require_prg(Mapper522& mapper,
                 Address addr,
                 std::size_t prg_size,
                 Byte low_bank,
                 Byte high_bank,
                 Byte salt) {
    const auto physical = prg_offset(addr, prg_size, low_bank, high_bank);
    CAPTURE(addr, prg_size, low_bank, high_bank, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 522 validates PRG volatile RAM geometry and bounded state",
          "[mapper522][geometry][ram][ciram][state]") {
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(size, mirror);
            REQUIRE(Mapper522::validate_geometry(header(mirror), size, 0U));
        }
    }
    for (const auto size : {kMinPrgSize, kDefaultPrgSize, kMaxPrgSize}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0xA0U);
            const auto cold = state(*mapper);
            REQUIRE(cold.size() == kStateSize);
            REQUIRE(cold.size() == 16397U);
            REQUIRE(cold == state(*different_rom));
            REQUIRE(register_state(*mapper)[kBankSelectStateOffset] == 0U);
            REQUIRE(register_state(*mapper)[kPrgBanksStateOffset] == 0U);
            REQUIRE(register_state(*mapper)[kPrgBanksStateOffset + 1U] == 0U);
            REQUIRE(std::ranges::all_of(std::span(cold).subspan(kPrgRamStateOffset),
                                        [](Byte value) { return value == 0U; }));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            require_prg(*mapper, 0x6000U, size, 0U, 0U, 0x35U);
            require_prg(*mapper, 0x8000U, size, 0U, 0U, 0x35U);
            require_prg(*mapper, 0xA000U, size, 0U, 0U, 0x35U);
            require_prg(*mapper, 0xE000U, size, 0U, 0U, 0x35U);
            REQUIRE(mapper->read_prg(0xC000U) == 0U);
            REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
        }
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
        REQUIRE_FALSE(Mapper522::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 108U, 520U, 521U, 523U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, 0U);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x1000UZ,
                                   0x2000UZ,
                                   0x4000UZ,
                                   0x6000UZ,
                                   0x7FFFUZ,
                                   0x8001UZ,
                                   0x9000UZ,
                                   kMaxPrgSize + kPrgBankSize,
                                   0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x1000U;
            break;
        case 4U:
            h.prg_ram_size = 0x4000U;
            break;
        case 5U:
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 6U:
            h.chr_ram_size = 0U;
            break;
        case 7U:
            h.chr_ram_size = 0x1000U;
            break;
        case 8U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, 0U);
    }
}

TEST_CASE("Mapper 522 exhaustively decodes even selectors and odd data writes",
          "[mapper522][decoding][bus][banking]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        mapper->write_prg(addr, value);
        const auto registers = register_state(*mapper);
        const auto expected_select =
            raw >= 0x8000U && raw < 0xA000U && (raw & 1U) == 0U ? value : 0U;
        CAPTURE(raw, value, expected_select);
        REQUIRE(registers[kBankSelectStateOffset] == expected_select);
        REQUIRE(registers[kPrgBanksStateOffset] == 0U);
        REQUIRE(registers[kPrgBanksStateOffset + 1U] == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x6000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x6000U ? 0xFFU : 0U));
        if (raw < 0x6000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    for (unsigned raw = 0x8001U; raw < 0xA000U; raw += 2U) {
        for (const Byte selector :
             {Byte{0U}, Byte{5U}, Byte{6U}, Byte{7U}, Byte{8U}, Byte{0xFFU}}) {
            mapper->reset();
            mapper->write_prg(0x8000U, selector);
            const auto value = static_cast<Byte>(raw * 13U + selector);
            mapper->write_prg(static_cast<Address>(raw), value);
            const auto registers = register_state(*mapper);
            CAPTURE(raw, selector, value);
            REQUIRE(registers[kBankSelectStateOffset] == selector);
            REQUIRE(registers[kPrgBanksStateOffset] == (selector == 6U ? value : 0U));
            REQUIRE(registers[kPrgBanksStateOffset + 1U] == (selector == 7U ? value : 0U));
        }
    }

    for (unsigned selector = 0U; selector < 256U; ++selector) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(0x9FFEU, static_cast<Byte>(selector));
            mapper->write_prg(0x8001U, static_cast<Byte>(value));
            mapper->write_prg(0x9FFFU, static_cast<Byte>(value ^ 0xFFU));
            const auto registers = register_state(*mapper);
            CAPTURE(selector, value);
            REQUIRE(registers[kBankSelectStateOffset] == selector);
            REQUIRE(registers[kPrgBanksStateOffset] ==
                    (selector == 6U ? static_cast<Byte>(value ^ 0xFFU) : 0U));
            REQUIRE(registers[kPrgBanksStateOffset + 1U] ==
                    (selector == 7U ? static_cast<Byte>(value ^ 0xFFU) : 0U));
        }
    }

    mapper->write_prg_cpu(0x8000U, 6U, true);
    mapper->write_prg_cpu(0x8001U, 0xA5U, false);
    REQUIRE(register_state(*mapper)[kPrgBanksStateOffset] == 0xA5U);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 522 maps two switchable and two fixed PRG-ROM windows",
          "[mapper522][banking][prg][bus]") {
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        auto mapper = board(size, MirrorMode::Horizontal, 0x35U);
        for (unsigned value = 0U; value < 256U; ++value) {
            const auto low = static_cast<Byte>(value);
            const auto high = static_cast<Byte>(255U - value);
            mapper->write_prg(0x8000U, 6U);
            mapper->write_prg(0x8001U, low);
            mapper->write_prg(0x8000U, 7U);
            mapper->write_prg(0x8001U, high);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, size, low, high, 0x35U);
            }
        }
    }

    auto mapper = board(kMaxPrgSize, MirrorMode::Vertical, 0xA0U);
    mapper->write_prg(0x8000U, 6U);
    mapper->write_prg(0x9FFFU, 0xFFU);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x8001U, 0xFEU);
    for (Address addr = 0x6000U; addr < 0xC000U; ++addr) {
        require_prg(*mapper, addr, kMaxPrgSize, 0xFFU, 0xFEU, 0xA0U);
    }
    for (Address addr = 0xE000U;; ++addr) {
        require_prg(*mapper, addr, kMaxPrgSize, 0xFFU, 0xFEU, 0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 522 provides writable CPU RAM unbanked CHR RAM and fixed CIRAM",
          "[mapper522][ram][ciram][bus]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror, 0x35U);
        for (Address addr = 0xC000U; addr < 0xE000U; ++addr) {
            mapper->write_prg(addr, cell(addr, 0x60U));
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(addr, cell(addr, 0xA0U));
        }
        for (Address addr = 0xC000U; addr < 0xE000U; ++addr) {
            CAPTURE(addr);
            REQUIRE(mapper->read_prg(addr) == cell(addr, 0x60U));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA0U));
        }
        for (const Address addr : {0x6000U, 0x7FFFU, 0xE000U, 0xFFFEU, 0xFFFFU}) {
            const auto before = mapper->read_prg(addr);
            mapper->write_prg(addr, static_cast<Byte>(before ^ 0xFFU));
            CAPTURE(addr, before);
            REQUIRE(mapper->read_prg(addr) == before);
        }
        REQUIRE(mapper->mirror_mode() == mirror);
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        const auto before = state(*mapper);
        REQUIRE(mapper->read_chr(0x2000U) == 0U);
        REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
        mapper->write_chr(0x2000U, 0xA5U);
        mapper->write_chr(0xFFFFU, 0x5AU);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    }
}

TEST_CASE("Mapper 522 exposes no additional RAM IRQ audio or register side effects",
          "[mapper522][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x8001U, 0xA5U);
    mapper->write_prg(0xC123U, 0x5AU);
    mapper->write_chr(0x0123U, 0xA6U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    for (Address addr = 0U; addr < 0x6000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        mapper->write_expansion(addr, static_cast<Byte>(addr ^ 0xFFU));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

TEST_CASE("Mapper 522 soft reset clears registers and cold power clears volatile RAM",
          "[mapper522][reset][banking][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror, 0x35U);
        const auto cold = state(*mapper);
        const auto cold_registers = register_state(*mapper);
        mapper->write_prg(0x8000U, 6U);
        mapper->write_prg(0x8001U, 0xDFU);
        mapper->write_prg(0x8000U, 7U);
        mapper->write_prg(0x8001U, 0xA5U);
        mapper->write_prg(0xC123U, 0x5AU);
        mapper->write_chr(0x0123U, 0xA6U);
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(register_state(*mapper) == cold_registers);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_prg(0xC123U) == 0x5AU);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA6U);
        require_prg(*mapper, 0x6000U, kDefaultPrgSize, 0U, 0U, 0x35U);
        require_prg(*mapper, 0x8000U, kDefaultPrgSize, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xA000U, kDefaultPrgSize, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, kDefaultPrgSize, 0U, 0U, 0x35U);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);

        mapper->write_prg(0x8000U, 6U);
        mapper->write_prg(0x8001U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 522 bounded snapshots replay registers and RAM without ROM",
          "[mapper522][state][banking][ram]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x8000U, 6U);
    mapper->write_prg(0x8001U, 0xDFU);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x9FFFU, 0xA5U);
    mapper->write_prg(0xC123U, 0x5AU);
    mapper->write_chr(0x0123U, 0xA6U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x4100UZ);
    REQUIRE(saved[kBankSelectStateOffset] == 7U);
    REQUIRE(saved[kPrgBanksStateOffset] == 0xDFU);
    REQUIRE(saved[kPrgBanksStateOffset + 1U] == 0xA5U);
    REQUIRE(saved[kPrgRamStateOffset + 0x123U] == 0x5AU);
    REQUIRE(saved[kChrRamStateOffset + 0x123U] == 0xA6U);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, kDefaultPrgSize, 0xDFU, 0xA5U, 0x80U);
    }
    REQUIRE(copy->read_prg(0xC123U) == 0x5AU);
    REQUIRE(copy->read_chr(0x0123U) == 0xA6U);
    REQUIRE(copy->audio_output() == 0.0F);
    REQUIRE_FALSE(copy->irq_pending());

    copy->power_on();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 522 rejects truncated damaged and mismatched states atomically",
          "[mapper522][state][atomic][ram][banking]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 6U);
    mapper->write_prg(0x8001U, 0xDFU);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x8001U, 0xA5U);
    mapper->write_prg(0xC123U, 0x5AU);
    mapper->write_chr(0x0123U, 0xA6U);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < kBankSelectStateOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kBankSelectStateOffset] = 0xFFU;
    changed[kPrgBanksStateOffset] = 0x03U;
    changed[kPrgBanksStateOffset + 1U] = 0xFCU;
    changed[kPrgRamStateOffset + 0x123U] ^= 0xFFU;
    changed[kChrRamStateOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(register_state(*mapper)[kBankSelectStateOffset] == 0xFFU);
    REQUIRE(mapper->read_prg(0xC123U) == static_cast<Byte>(0x5AU ^ 0xFFU));
    REQUIRE(mapper->read_chr(0x0123U) == static_cast<Byte>(0xA6U ^ 0xFFU));
}

} // namespace
