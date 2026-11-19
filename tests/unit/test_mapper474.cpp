#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper474.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kChrSize = 0x2000UZ;
constexpr std::size_t kSaveRamSize = 0x2000UZ;
constexpr std::size_t kGeometrySize = 10UZ;

struct Profile {
    std::size_t prg_size;
    Address prg_start;
};

constexpr std::array<Profile, 4> kProfiles{
    {{0xBFE0UZ, 0x4020U}, {0xB800UZ, 0x4800U}, {0xA000UZ, 0x6000U}, {0xA000UZ, 0x6000U}}};

enum class SaveKind { None, BatterySram, Fram };

const Profile& profile(Byte submapper) {
    return kProfiles[submapper];
}

INesHeader header(Byte submapper = 0U,
                  MirrorMode mirror = MirrorMode::Vertical,
                  SaveKind save = SaveKind::None) {
    INesHeader result{};
    result.mapper_number = 474U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = profile(submapper).prg_size;
    result.chr_rom_size = kChrSize;
    if (save != SaveKind::None) {
        result.prg_nvram_size = kSaveRamSize;
        result.has_battery = save == SaveKind::BatterySram;
    }
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 5U) * 17U + (offset >> 8U) * 13U +
                             (offset >> 11U) * 47U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper474> board_with_save(SaveKind save,
                                           MirrorMode mirror = MirrorMode::Vertical,
                                           Byte salt = 0U) {
    const auto h = header(3U, mirror, save);
    REQUIRE(Mapper474::validate_geometry(h, h.prg_rom_size, h.chr_rom_size));
    return std::make_unique<Mapper474>(
        h, rom(h.prg_rom_size, salt), rom(h.chr_rom_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::unique_ptr<Mapper474> board(Byte submapper,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    if (submapper == 3U) {
        return board_with_save(SaveKind::BatterySram, mirror, salt);
    }
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper474::validate_geometry(h, h.prg_rom_size, h.chr_rom_size));
    return std::make_unique<Mapper474>(
        h, rom(h.prg_rom_size, salt), rom(h.chr_rom_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper474& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 474 validates every documented geometry and storage population",
          "[mapper474][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const auto save : {SaveKind::None, SaveKind::BatterySram, SaveKind::Fram}) {
                if ((submapper == 3U) != (save != SaveKind::None)) {
                    continue;
                }
                const auto h = header(submapper, mirror, save);
                CAPTURE(submapper, mirror, save);
                REQUIRE(Mapper474::validate_geometry(h, h.prg_rom_size, h.chr_rom_size));
                auto mapper = submapper == 3U ? board_with_save(save, mirror, 0x35U)
                                              : board(submapper, mirror, 0x35U);
                auto different = submapper == 3U ? board_with_save(save, mirror, 0x80U)
                                                 : board(submapper, mirror, 0x80U);
                REQUIRE(state(*mapper) == state(*different));
                REQUIRE(state(*mapper).size() ==
                        kGeometrySize + (save == SaveKind::None ? 0UZ : kSaveRamSize));
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().size() ==
                        (save == SaveKind::None ? 0UZ : kSaveRamSize));
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE(mapper->has_flash_storage() == (save == SaveKind::Fram));
                REQUIRE(mapper->read_prg(0xFFFCU) == cell(h.prg_rom_size - 4U, 0x35U));
                REQUIRE(mapper->read_prg(0xFFFDU) == cell(h.prg_rom_size - 3U, 0x35U));
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
            }
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
        REQUIRE_FALSE(Mapper474::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 4U, 473U, 475U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, h.prg_rom_size, h.chr_rom_size);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, legacy.prg_rom_size, legacy.chr_rom_size);
    for (Byte submapper = 4U; submapper < 16U; ++submapper) {
        auto h = header(0U);
        h.submapper = submapper;
        reject(h, h.prg_rom_size, h.chr_rom_size);
    }
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        const auto save = submapper == 3U ? SaveKind::BatterySram : SaveKind::None;
        const auto h = header(submapper, MirrorMode::Vertical, save);
        for (const std::size_t size :
             {0UZ, 1UZ, h.prg_rom_size - 1U, h.prg_rom_size + 1U, 0xC000UZ, 0x10000UZ}) {
            reject(h, size, h.chr_rom_size);
        }
        for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x2001UZ, 0x4000UZ}) {
            reject(h, h.prg_rom_size, size);
        }
        for (const auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            reject(header(submapper, mirror, save), h.prg_rom_size, h.chr_rom_size);
        }
        auto trainer = h;
        trainer.has_trainer = true;
        reject(trainer, h.prg_rom_size, h.chr_rom_size);
        auto chr_ram = h;
        chr_ram.chr_ram_size = kChrSize;
        reject(chr_ram, h.prg_rom_size, h.chr_rom_size);
        auto chr_nvram = h;
        chr_nvram.chr_nvram_size = kChrSize;
        reject(chr_nvram, h.prg_rom_size, h.chr_rom_size);
    }
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        for (const auto save : {SaveKind::BatterySram, SaveKind::Fram}) {
            const auto h = header(submapper, MirrorMode::Vertical, save);
            reject(h, h.prg_rom_size, h.chr_rom_size);
        }
    }
    const auto absent = header(3U);
    reject(absent, absent.prg_rom_size, absent.chr_rom_size);
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x1FE0UZ, 0x2001UZ, 0x4000UZ}) {
        auto h = header(3U, MirrorMode::Vertical, SaveKind::BatterySram);
        h.prg_nvram_size = size;
        reject(h, h.prg_rom_size, h.chr_rom_size);
    }
    auto volatile_ram = header(3U, MirrorMode::Vertical, SaveKind::Fram);
    volatile_ram.prg_ram_size = kSaveRamSize;
    reject(volatile_ram, volatile_ram.prg_rom_size, volatile_ram.chr_rom_size);
    auto both_ram = header(3U, MirrorMode::Vertical, SaveKind::BatterySram);
    both_ram.prg_ram_size = kSaveRamSize;
    reject(both_ram, both_ram.prg_rom_size, both_ram.chr_rom_size);
}

TEST_CASE("Mapper 474 decodes PRG and save RAM across the complete external CPU window",
          "[mapper474][decoding][banking][bus][ram]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto mapper = board(submapper);
        const auto cold = state(*mapper);
        const auto start = profile(submapper).prg_start;
        for (unsigned raw = 0x4020U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const bool prg_mapped = raw >= 0x6000U;
            const bool expansion_mapped =
                raw < 0x6000U && (submapper == 3U ? raw >= 0x4020U : raw >= start);
            const auto expected = submapper == 3U && expansion_mapped ? Byte{0U}
                                  : prg_mapped || expansion_mapped    ? cell(raw - start)
                                                                      : Byte{0U};
            CAPTURE(submapper, raw, start, prg_mapped, expansion_mapped, expected);
            REQUIRE(mapper->maps_prg(addr) == prg_mapped);
            REQUIRE(mapper->prg_read_mask(addr) == (prg_mapped ? 0xFFU : 0U));
            REQUIRE(mapper->read_prg(addr) == (prg_mapped ? expected : 0U));
            REQUIRE(mapper->maps_expansion(addr) == expansion_mapped);
            REQUIRE(mapper->expansion_read_mask(addr) == (expansion_mapped ? 0xFFU : 0U));
            REQUIRE(mapper->read_expansion(addr) == (expansion_mapped ? expected : 0U));

            mapper->write_prg(addr, static_cast<Byte>(raw * 37U));
            if (submapper != 3U) {
                mapper->write_expansion(addr, static_cast<Byte>(raw * 41U));
            }
            REQUIRE(state(*mapper) == cold);
        }
        REQUIRE(mapper->read_prg(0U) == 0U);
        REQUIRE(mapper->read_prg(0x401FU) == 0U);
        REQUIRE(mapper->read_expansion(0U) == 0U);
        REQUIRE(mapper->read_expansion(0x401FU) == 0U);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    }
}

TEST_CASE("Mapper 474 fixed CHR ROM and hard-wired CIRAM ignore writes and reset",
          "[mapper474][banking][chr][ciram][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(1U, mirror);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto expected = cell(addr, 0x55U);
            CAPTURE(mirror, addr, expected);
            REQUIRE(mapper->read_chr(addr) == expected);
            mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == expected);
        }
        REQUIRE(mapper->read_chr(0x2000U) == 0U);
        REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
        const auto before = state(*mapper);
        mapper->reset();
        mapper->power_on();
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 474 submapper 3 SRAM and FRAM persist through reset and cold power",
          "[mapper474][ram][reset][bus]") {
    for (const auto save : {SaveKind::BatterySram, SaveKind::Fram}) {
        auto mapper = board_with_save(save);
        for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto value = cell(raw - 0x4020U, 0xA5U);
            mapper->write_expansion(addr, value);
            CAPTURE(save, raw, value);
            REQUIRE(mapper->read_expansion(addr) == value);
            REQUIRE(mapper->battery_ram()[raw - 0x4020U] == value);
        }
        const auto populated = state(*mapper);
        for (const Address addr :
             {Address{0U}, Address{0x401FU}, Address{0x6000U}, Address{0xFFFFU}}) {
            mapper->write_expansion(addr, 0xFFU);
            mapper->write_prg(addr, 0xFFU);
        }
        REQUIRE(state(*mapper) == populated);
        mapper->reset();
        REQUIRE(state(*mapper) == populated);
        mapper->power_on();
        REQUIRE(state(*mapper) == populated);
        REQUIRE(mapper->battery_ram().size() == kSaveRamSize);
        REQUIRE(mapper->has_flash_storage() == (save == SaveKind::Fram));

        std::array<Byte, kSaveRamSize> imported{};
        for (std::size_t index = 0U; index < imported.size(); ++index) {
            imported[index] = cell(index, 0x3CU);
        }
        mapper->set_battery_ram(imported);
        for (unsigned raw = 0x4020U; raw < 0x6000U; raw += 0x101U) {
            CAPTURE(save, raw);
            REQUIRE(mapper->read_expansion(static_cast<Address>(raw)) == imported[raw - 0x4020U]);
        }
        const auto before_bad_import = state(*mapper);
        mapper->set_battery_ram(std::span(imported).first(kSaveRamSize - 1U));
        std::array<Byte, kSaveRamSize + 1U> oversized{};
        mapper->set_battery_ram(oversized);
        REQUIRE(state(*mapper) == before_bad_import);
    }
}

TEST_CASE("Mapper 474 has no IRQ audio observer or programmable mirroring state",
          "[mapper474][irq][audio][ciram][bus][reset]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto mapper = board(submapper);
        const auto before = state(*mapper);
        mapper->observe_cpu_read(0x8000U, 0xA5U);
        mapper->observe_cpu_write(0x8000U, 0xA5U);
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_address(0x1234U);
        mapper->observe_ppu_read(0x1234U);
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        mapper->clock_irq_counter();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        mapper->acknowledge_irq();
        mapper->set_shared_nvram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
        CAPTURE(submapper);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
        REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    }
}

TEST_CASE("Mapper 474 bounded snapshots replay RAM without serializing ROM",
          "[mapper474][state][ram][banking]") {
    auto mapper = board_with_save(SaveKind::BatterySram);
    mapper->write_expansion(0x4143U, 0xA5U);
    mapper->write_expansion(0x5FFFU, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kGeometrySize + kSaveRamSize);
    REQUIRE(saved[kGeometrySize + 0x123U] == 0xA5U);
    REQUIRE(saved[kGeometrySize + 0x1FDFU] == 0x5AU);

    auto copy = board_with_save(SaveKind::BatterySram, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_expansion(0x4143U) == 0xA5U);
    REQUIRE(copy->read_expansion(0x5FFFU) == 0x5AU);
    REQUIRE(copy->read_prg(0x6000U) == cell(0U, 0x80U));
    REQUIRE(copy->read_prg(0xFFFFU) == cell(profile(3U).prg_size - 1U, 0x80U));
    copy->power_on();
    REQUIRE(state(*copy) == saved);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto rom_only = board(1U, MirrorMode::Horizontal, 0x35U);
    auto rom_copy = board(1U, MirrorMode::Horizontal, 0x80U);
    const auto rom_state = state(*rom_only);
    REQUIRE(rom_state.size() == kGeometrySize);
    load(*rom_copy, rom_state);
    REQUIRE(state(*rom_copy) == rom_state);
    REQUIRE(rom_copy->read_prg(0x6000U) == cell(0x1800U, 0x80U));
}

TEST_CASE("Mapper 474 rejects truncated damaged and mismatched states atomically",
          "[mapper474][state][ram]") {
    auto mapper = board_with_save(SaveKind::Fram);
    mapper->write_expansion(0x4143U, 0xA5U);
    mapper->write_expansion(0x5FFFU, 0x5AU);
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

    const auto mismatch = [&](std::unique_ptr<Mapper474>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_submapper = board(2U);
    mismatch(wrong_submapper);
    auto wrong_mirror = board_with_save(SaveKind::Fram, MirrorMode::Horizontal);
    mismatch(wrong_mirror);
    auto wrong_storage = board_with_save(SaveKind::BatterySram);
    mismatch(wrong_storage);

    const auto h = header(3U, MirrorMode::Vertical, SaveKind::Fram);
    auto wrong_prg =
        std::make_unique<Mapper474>(h, rom(h.prg_rom_size / 2U, 0U), rom(h.chr_rom_size, 0x55U));
    mismatch(wrong_prg);
    auto wrong_chr =
        std::make_unique<Mapper474>(h, rom(h.prg_rom_size, 0U), rom(h.chr_rom_size / 2U, 0x55U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kGeometrySize + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_expansion(0x4143U) == static_cast<Byte>(0xA5U ^ 0xFFU));
}

} // namespace
