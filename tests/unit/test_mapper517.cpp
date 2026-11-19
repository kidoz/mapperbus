#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper517.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDefaultPrgSize = 0x020000UZ;
constexpr std::size_t kMaxPrgSize = 0x400000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kBankStateOffset = 10UZ;
constexpr std::size_t kChrRamStateOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamStateOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 517U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 14U) * 59U + (offset >> 20U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper517> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper517::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper517>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kChrRamStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kChrRamStateOffset> result{};
    std::copy_n(bytes.begin(), kChrRamStateOffset, result.begin());
    return result;
}

void load(Mapper517& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper517& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0123U);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0123U) == chr);
}

void require_prg(Mapper517& mapper, Address addr, Byte bank, std::size_t prg_size, Byte salt) {
    const auto physical =
        addr >= 0xC000U
            ? prg_size - kPrgBankSize + (addr & (kPrgBankSize - 1U))
            : (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) %
                  prg_size;
    CAPTURE(addr, bank, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 517 validates UNROM geometry and bounded volatile state",
          "[mapper517][geometry][ram][ciram][state]") {
    for (std::size_t size = kPrgBankSize * 2U; size <= kMaxPrgSize; size *= 2U) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(size, mirror);
            REQUIRE(Mapper517::validate_geometry(header(mirror), size, 0U));
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x6001U) == 0U);
            REQUIRE(mapper->read_chr(0U) == 0U);
            REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
            require_prg(*mapper, 0x8000U, 0U, size, 0x35U);
            require_prg(*mapper, 0xFFFFU, 0U, size, 0x35U);
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
        REQUIRE_FALSE(Mapper517::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 515U, 516U, 518U, 4095U}) {
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
                                   0x4000UZ,
                                   0x7FFFUZ,
                                   0x8001UZ,
                                   0xC000UZ,
                                   0x18000UZ,
                                   kMaxPrgSize + kPrgBankSize,
                                   0x800000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, 0U);
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
        reject(h, kDefaultPrgSize, 0U);
    }
}

TEST_CASE("Mapper 517 exhaustively decodes ADC ports and PRG writes without conflicts",
          "[mapper517][decoding][bus][microphone]") {
    auto mapper = board();
    const auto cold = register_state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            expected[kBankStateOffset] = value;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(register_state(*mapper) == expected);
        const bool adc = raw == 0x6000U || raw == 0x6001U;
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U || adc));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U || adc ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    auto zero_rom = std::make_unique<Mapper517>(
        header(), std::vector<Byte>(kDefaultPrgSize, 0U), std::vector<Byte>{});
    zero_rom->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(register_state(*zero_rom)[kBankStateOffset] == 0xFFU);
    zero_rom->write_prg_cpu(0x8001U, 0U, false);
    REQUIRE(register_state(*zero_rom)[kBankStateOffset] == 0U);
}

TEST_CASE("Mapper 517 selects eight-bit lower banks and fixes the upper bank to last",
          "[mapper517][banking][prg][bus]") {
    for (std::size_t size = kPrgBankSize * 2U; size <= kMaxPrgSize; size *= 2U) {
        auto mapper = board(size, MirrorMode::Vertical, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto bank = static_cast<Byte>(raw);
            mapper->write_prg(0x8000U, bank);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == bank);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, bank, size, 0x35U);
            }
        }
        mapper->write_prg(0xFFFFU, 0xFFU);
        for (Address addr = 0x8000U;; ++addr) {
            require_prg(*mapper, addr, 0xFFU, size, 0x35U);
            if (addr == 0xFFFFU) {
                break;
            }
        }
    }
}

TEST_CASE("Mapper 517 provides volatile CHR RAM and ordinary header CIRAM only",
          "[mapper517][ram][ciram][bus][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(addr, cell(addr, 0x60U));
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0x60U));
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

        mapper->reset();
        REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x60U));
        mapper->power_on();
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 517 ADC ports expose the live sample without expansion audio",
          "[mapper517][microphone][audio][decoding][bus][state]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (const Byte sample :
         {Byte{0U}, Byte{1U}, Byte{0x7FU}, Byte{0x80U}, Byte{0xA6U}, Byte{0xFFU}}) {
        mapper->set_microphone_sample(sample);
        CAPTURE(sample);
        REQUIRE(mapper->read_prg(0x6000U) == sample);
        REQUIRE(mapper->read_prg(0x6001U) == sample);
        REQUIRE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->maps_prg(0x6001U));
        REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
        REQUIRE(mapper->prg_read_mask(0x6001U) == 0xFFU);
        REQUIRE(state(*mapper) == cold);
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    mapper->set_microphone_sample(0xA6U);
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto value = static_cast<Byte>(raw);
        mapper->write_prg(0x6000U, value);
        mapper->write_prg(0x6001U, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(mapper->read_prg(0x6000U) == 0xA6U);
        REQUIRE(mapper->read_prg(0x6001U) == 0xA6U);
        REQUIRE(state(*mapper) == cold);
    }
    for (const Address addr : {0x5FFFU, 0x6002U, 0x6003U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == cold);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0xA6U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6001U) == 0xA6U);
}

TEST_CASE("Mapper 517 has no work RAM IRQ or unrelated observer side effects",
          "[mapper517][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_chr(0x0123U, 0x5AU);
    const auto before = state(*mapper);
    for (Address addr = 0x6002U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
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

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 517 reset selects bank zero and cold power clears only CHR RAM",
          "[mapper517][reset][banking][ram][microphone]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror);
        const auto cold = state(*mapper);
        const auto cold_registers = register_state(*mapper);
        mapper->write_chr(0x0123U, 0xA5U);
        mapper->write_prg(0x8000U, 0xDFU);
        mapper->set_microphone_sample(0xF0U);
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(register_state(*mapper) == cold_registers);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
        REQUIRE(mapper->read_prg(0x6000U) == 0xF0U);
        require_prg(*mapper, 0x8000U, 0U, kDefaultPrgSize, 0U);

        mapper->write_prg(0x8000U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_prg(0x6001U) == 0xF0U);
    }
}

TEST_CASE("Mapper 517 bounded snapshots replay bank and CHR without ROM or live ADC",
          "[mapper517][state][banking][ram][microphone]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_chr(0x0123U, 0xA6U);
    mapper->write_prg(0x8000U, 0xDFU);
    mapper->set_microphone_sample(0xF0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kBankStateOffset] == 0xDFU);
    REQUIRE(saved[kChrRamStateOffset + 0x123U] == 0xA6U);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U);
    copy->set_microphone_sample(0x3CU);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0xDFU, kDefaultPrgSize, 0x80U);
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA6U);
    REQUIRE(copy->read_prg(0x6000U) == 0x3CU);
    REQUIRE(copy->read_prg(0x6001U) == 0x3CU);
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

TEST_CASE("Mapper 517 rejects truncated damaged and mismatched states atomically",
          "[mapper517][state][atomic][ram][banking]") {
    auto mapper = board();
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8000U, 0xDFU);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kBankStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Vertical);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kBankStateOffset] = 0xD3U;
    changed[kChrRamStateOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(register_state(*mapper)[kBankStateOffset] == 0xD3U);
}

} // namespace
