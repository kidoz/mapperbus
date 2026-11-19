#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper515.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kInternalPrgSize = 0x100000UZ;
constexpr std::size_t kDefaultPrgSize = 0x180000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kBankStateOffset = 10UZ;
constexpr std::size_t kAudioIndexStateOffset = 11UZ;
constexpr std::size_t kAudioRegistersStateOffset = 12UZ;
constexpr std::size_t kAudioVoicesStateOffset = 76UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 515U;
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

std::unique_ptr<Mapper515> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper515::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper515>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kAudioVoicesStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() > kAudioVoicesStateOffset + kChrRamSize);
    std::array<Byte, kAudioVoicesStateOffset> result{};
    std::copy_n(bytes.begin(), kAudioVoicesStateOffset, result.begin());
    return result;
}

std::size_t chr_state_offset(std::span<const Byte> bytes) {
    REQUIRE(bytes.size() >= kChrRamSize + 3U);
    return bytes.size() - kChrRamSize - 3U;
}

void load(Mapper515& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg_offset(Address addr, Byte bank, std::size_t prg_size) {
    if (addr >= 0xC000U) {
        return prg_size - kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    const bool internal = (bank & 0x80U) != 0U;
    const auto base = internal ? prg_size - kInternalPrgSize : 0UZ;
    const auto size = internal ? kInternalPrgSize : prg_size - kInternalPrgSize;
    if (size == 0U) {
        return prg_size;
    }
    const auto selected = static_cast<std::size_t>(bank & 0x3FU) % (size / kPrgBankSize);
    return base + selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

void require_prg(Mapper515& mapper, Address addr, Byte bank, std::size_t prg_size, Byte salt) {
    const auto physical = expected_prg_offset(addr, bank, prg_size);
    CAPTURE(addr, bank, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == (physical < prg_size ? cell(physical, salt) : 0U));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == (physical < prg_size ? 0xFFU : 0U));
}

void write_audio(Mapper515& mapper, Byte index, Byte value) {
    mapper.write_prg(0x6000U, index);
    mapper.write_prg(0x6001U, value);
}

void start_voice(Mapper515& mapper, unsigned channel) {
    REQUIRE(channel < 9U);
    for (const Byte index : {Byte{0U}, Byte{1U}}) {
        write_audio(mapper, index, 0U);
    }
    for (const Byte index : {Byte{4U}, Byte{5U}, Byte{6U}, Byte{7U}}) {
        write_audio(mapper, index, 0xF0U);
    }
    write_audio(mapper, static_cast<Byte>(0x10U + channel), 0x55U);
    write_audio(mapper, static_cast<Byte>(0x30U + channel), 0U);
    write_audio(mapper, static_cast<Byte>(0x20U + channel), 0x18U);
}

TEST_CASE("Mapper 515 validates packed PRG layout and volatile CHR geometry",
          "[mapper515][geometry][ram][ciram]") {
    for (const auto size : {kInternalPrgSize,
                            kInternalPrgSize + kPrgBankSize,
                            kDefaultPrgSize,
                            kMaxPrgSize - kPrgBankSize,
                            kMaxPrgSize}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(size, mirror);
            REQUIRE(Mapper515::validate_geometry(header(mirror), size, 0U));
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() > kChrRamSize + kAudioVoicesStateOffset);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == 0x80U);
            REQUIRE(register_state(*mapper)[kAudioIndexStateOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            require_prg(*mapper, 0x8000U, 0x80U, size, 0x35U);
            require_prg(*mapper, 0xFFFFU, 0x80U, size, 0x35U);
            REQUIRE(mapper->read_chr(0U) == 0U);
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
        REQUIRE_FALSE(Mapper515::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 188U, 514U, 516U, 4095U}) {
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
                                   kInternalPrgSize - 1U,
                                   kInternalPrgSize + 1U,
                                   kInternalPrgSize + 0x2000UZ,
                                   kMaxPrgSize + kPrgBankSize,
                                   0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x100000UZ}) {
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

TEST_CASE("Mapper 515 exhaustively decodes documented CPU apertures without bus conflicts",
          "[mapper515][decoding][bus][audio]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if ((addr & 0xE003U) == 0x6000U) {
            expected[kAudioIndexStateOffset] = value & 0x3FU;
        } else if ((addr & 0xE003U) == 0x6001U) {
            expected[kAudioRegistersStateOffset] = value;
        } else if (addr == 0xC002U && (value & 0x80U) == 0U) {
            expected[expected.size() - 2U] = 8U;
            expected.back() = 1U;
        } else if (addr == 0xC003U && (value & 0x80U) != 0U) {
            expected.back() = 2U;
        } else if ((addr & 0xE00FU) == 0x8000U) {
            expected[kBankStateOffset] = value;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        const bool audio_read = (addr & 0xE003U) == 0x6000U;
        const bool microphone_read = (addr & 0xE003U) == 0x6003U;
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U || audio_read || microphone_read));
        REQUIRE(mapper->prg_read_mask(addr) == (microphone_read          ? 0x80U
                                                : mapper->maps_prg(addr) ? 0xFFU
                                                                         : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    auto zero_rom = std::make_unique<Mapper515>(
        header(), std::vector<Byte>(kInternalPrgSize, 0U), std::vector<Byte>{});
    zero_rom->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(register_state(*zero_rom)[kBankStateOffset] == 0xFFU);
    zero_rom->write_prg_cpu(0x8001U, 0U, false);
    REQUIRE(register_state(*zero_rom)[kBankStateOffset] == 0xFFU);
}

TEST_CASE("Mapper 515 selects six-bit internal and external banks with fixed internal upper",
          "[mapper515][banking][prg][bus]") {
    for (const auto size :
         {kInternalPrgSize, kInternalPrgSize + kPrgBankSize, kDefaultPrgSize, kMaxPrgSize}) {
        auto mapper = board(size, MirrorMode::Vertical, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto bank = static_cast<Byte>(raw);
            mapper->write_prg(0x8010U, bank);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == bank);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, bank, size, 0x35U);
            }
        }
        mapper->write_prg(0x8000U, 0xBFU);
        for (Address addr = 0x8000U;; ++addr) {
            require_prg(*mapper, addr, 0xBFU, size, 0x35U);
            if (addr == 0xFFFFU) {
                break;
            }
        }
    }
}

TEST_CASE("Mapper 515 provides volatile CHR RAM and ordinary header CIRAM only",
          "[mapper515][ram][ciram][bus][reset]") {
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

TEST_CASE("Mapper 515 K-663A ports read the test register and clock nine melodic voices",
          "[mapper515][audio][decoding][reset]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0x7FFCU, static_cast<Byte>(raw));
        REQUIRE(register_state(*mapper)[kAudioIndexStateOffset] == (raw & 0x3FU));
    }
    write_audio(*mapper, 0x0FU, 0xA6U);
    for (const Address addr : {0x6000U, 0x6004U, 0x7FFCU}) {
        REQUIRE(mapper->read_prg(addr) == 0xA6U);
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    write_audio(*mapper, 0x10U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA6U);

    for (unsigned channel = 0U; channel < 9U; ++channel) {
        mapper->reset();
        REQUIRE(mapper->audio_output() == 0.0F);
        start_voice(*mapper, channel);
        for (unsigned cycle = 0U; cycle < 36U; ++cycle) {
            mapper->clock_audio();
        }
        CAPTURE(channel);
        REQUIRE(mapper->audio_output() != 0.0F);
        REQUIRE(std::isfinite(mapper->audio_output()));
    }
    mapper->reset();
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(register_state(*mapper)[kAudioIndexStateOffset] == 0U);
    const auto registers = register_state(*mapper);
    REQUIRE(std::ranges::all_of(std::span(registers).subspan(kAudioRegistersStateOffset, 64U),
                                [](Byte value) { return value == 0U; }));
}

TEST_CASE("Mapper 515 microphone uses deterministic active-low select and rising-clock ADC",
          "[mapper515][microphone][bus][reset]") {
    auto mapper = board();
    mapper->set_microphone_sample(0xA6U);
    mapper->write_prg(0xC002U, 0x80U);
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    REQUIRE(mapper->prg_read_mask(0x6003U) == 0x80U);
    mapper->write_prg(0xC002U, 0U);
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        CAPTURE(bit);
        REQUIRE(mapper->read_prg(0x6003U) == (((0xA6U << bit) & 0x80U) != 0U ? 0x80U : 0U));
        mapper->write_prg(0xC003U, 0x80U);
        const auto after_rise = state(*mapper);
        mapper->write_prg(0xC003U, 0x80U);
        REQUIRE(state(*mapper) == after_rise);
        mapper->write_prg(0xC003U, 0U);
    }
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    mapper->set_microphone_sample(0x3CU);
    mapper->write_prg(0xC002U, 0x80U);
    mapper->write_prg(0xC002U, 0U);
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    mapper->write_prg(0xC003U, 0x80U);
    mapper->write_prg(0xC003U, 0U);
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    mapper->write_prg(0xC003U, 0x80U);
    mapper->write_prg(0xC003U, 0U);
    REQUIRE(mapper->read_prg(0x6003U) == 0x80U);

    mapper->reset();
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    mapper->write_prg(0xC002U, 0U);
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
}

TEST_CASE("Mapper 515 soft reset restores internal bank and peripherals but retains CHR RAM",
          "[mapper515][reset][banking][ram][audio]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8000U, 0x23U);
    write_audio(*mapper, 0x0FU, 0xA6U);
    start_voice(*mapper, 2U);
    mapper->clock_audio();
    mapper->set_microphone_sample(0xF0U);
    mapper->write_prg(0xC002U, 0U);
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    REQUIRE(register_state(*mapper)[kBankStateOffset] == 0x80U);
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6003U) == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
    require_prg(*mapper, 0x8000U, 0x80U, kDefaultPrgSize, 0U);

    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 515 has no cartridge work RAM IRQ or unrelated observer side effects",
          "[mapper515][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_chr(0x0123U, 0x5AU);
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
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 515 bounded snapshots replay banking audio ADC and CHR without ROM",
          "[mapper515][state][banking][ram][audio][microphone]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8000U, 0x23U);
    write_audio(*mapper, 0x0FU, 0xA6U);
    start_voice(*mapper, 8U);
    for (unsigned cycle = 0U; cycle < 40U; ++cycle) {
        mapper->clock_audio();
    }
    mapper->set_microphone_sample(0xF0U);
    mapper->write_prg(0xC002U, 0U);
    mapper->write_prg(0xC003U, 0x80U);
    const auto saved = state(*mapper);
    REQUIRE(saved[kBankStateOffset] == 0x23U);
    REQUIRE(saved[kAudioRegistersStateOffset + 0x0FU] == 0xA6U);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x23U, kDefaultPrgSize, 0x80U);
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA5U);
    REQUIRE(copy->read_prg(0x6000U) == 0xA6U);
    REQUIRE(copy->read_prg(0x6003U) == 0x80U);
    REQUIRE(copy->audio_output() == mapper->audio_output());
    copy->clock_audio();
    mapper->clock_audio();
    REQUIRE(copy->audio_output() == mapper->audio_output());
    REQUIRE(state(*copy) == state(*mapper));

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

TEST_CASE("Mapper 515 rejects truncated damaged and mismatched states atomically",
          "[mapper515][state][ram][audio][microphone]") {
    auto mapper = board();
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8000U, 0xDFU);
    start_voice(*mapper, 3U);
    for (unsigned cycle = 0U; cycle < 40U; ++cycle) {
        mapper->clock_audio();
    }
    mapper->set_microphone_sample(0x80U);
    mapper->write_prg(0xC002U, 0U);
    const auto saved = state(*mapper);
    const auto chr_offset = chr_state_offset(saved);
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
    for (std::size_t offset = 0U; offset < kBankStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    auto invalid_index = saved;
    invalid_index[kAudioIndexStateOffset] = 0x40U;
    reject(invalid_index);
    auto invalid_stage = saved;
    invalid_stage[kAudioVoicesStateOffset + 8U] = 0xFFU;
    reject(invalid_stage);
    auto invalid_divider = saved;
    invalid_divider[chr_offset - 10U] = 36U;
    reject(invalid_divider);
    auto invalid_lfo = saved;
    std::fill_n(invalid_lfo.begin() + static_cast<std::ptrdiff_t>(chr_offset - 8U), 4U, 0xFFU);
    reject(invalid_lfo);
    auto invalid_microphone_bits = saved;
    invalid_microphone_bits[saved.size() - 2U] = 9U;
    reject(invalid_microphone_bits);
    auto invalid_microphone_flags = saved;
    invalid_microphone_flags.back() = 0x80U;
    reject(invalid_microphone_flags);
    auto deselected_with_bits = saved;
    deselected_with_bits.back() = 0U;
    reject(deselected_with_bits);

    auto wrong_prg = board(kInternalPrgSize);
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Vertical);
    const auto before_mirror = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before_mirror);

    auto changed = saved;
    changed[kBankStateOffset] = 0xA3U;
    changed[kAudioRegistersStateOffset + 0x0FU] = 0x5AU;
    changed[chr_offset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
}

} // namespace
