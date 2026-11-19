#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
std::vector<Byte> image(unsigned mapper, Byte submapper = 0U) {
    const bool jncota = mapper == 551U;
    constexpr Byte kPrgBanks = 32U;
    const Byte chr_banks = jncota ? 4U : 0U;
    std::vector<Byte> rom(16U + kPrgBanks * 16384U + chr_banks * 8192U);
    const std::array<Byte, 12> header{'N',
                                      'E',
                                      'S',
                                      0x1AU,
                                      kPrgBanks,
                                      chr_banks,
                                      static_cast<Byte>((mapper << 4U) | 3U),
                                      static_cast<Byte>((mapper & 0xF0U) | 8U),
                                      static_cast<Byte>((mapper >> 8U) | (submapper << 4U)),
                                      0U,
                                      static_cast<Byte>(jncota ? 0x70U : 0x90U),
                                      static_cast<Byte>(jncota ? 0U : 7U)};
    std::ranges::copy(header, rom.begin());
    for (unsigned bank = 0; bank < kPrgBanks; ++bank)
        std::fill_n(rom.begin() + 16 + bank * 16384U, 16384U, static_cast<Byte>(bank));
    for (unsigned bank = 0; bank < chr_banks; ++bank)
        std::fill_n(rom.begin() + 16 + kPrgBanks * 16384U + bank * 8192U,
                    8192U,
                    static_cast<Byte>(0xA0U + bank));
    return rom;
}
std::vector<Byte> state(const Cartridge& cart) {
    StateWriter writer;
    cart.save_state(writer);
    return writer.take();
}
} // namespace

TEST_CASE("Waixing 178 and Jncota 551 switch four PRG layouts through the cartridge loader",
          "[fs305]") {
    register_builtin_mappers();
    for (const unsigned mapper : {178U, 551U}) {
        auto cart = Cartridge::from_data(image(mapper));
        REQUIRE(cart);
        cart->write_expansion(0x4FFE, 2U);
        for (Byte inner : {2U, 3U}) {
            cart->write_expansion(0x4801, inner);
            const std::array<Byte, 4> low{18U,
                                          static_cast<Byte>(16U + inner),
                                          static_cast<Byte>(16U + inner),
                                          static_cast<Byte>(16U + inner)};
            const std::array<Byte, 4> high{
                19U, 23U, static_cast<Byte>(16U + inner), static_cast<Byte>(22U + (inner & 1U))};
            for (unsigned mode = 0; mode < 4U; ++mode) {
                CAPTURE(mapper, inner, mode);
                cart->write_expansion(0x4870, static_cast<Byte>(mode << 1U | 1U));
                cart->write_expansion(0x47FC, 0U);
                CHECK(cart->read_prg(0x8000) == low[mode]);
                CHECK(cart->read_prg(0xFFFF) == high[mode]);
                CHECK(cart->mirror_mode() ==
                      (mapper == 551U ? MirrorMode::Vertical : MirrorMode::Horizontal));
            }
        }
        cart->write_prg(0x6000, 0x60);
        cart->write_expansion(0x4803, 3);
        cart->write_prg(0x6000, 0x63);
        cart->write_chr(0x0123, 0x71);
        CHECK(cart->read_chr(0x0123) == (mapper == 551U ? 0xA3U : 0x71U));
        CHECK(cart->battery_ram().size() == (mapper == 551U ? 8192U : 32768U));
        const auto before = state(*cart);
        cart->reset();
        CHECK(cart->read_prg(0x6000) == (mapper == 551U ? 0x63U : 0x60U));
        CHECK(cart->read_chr(0x0123) == (mapper == 551U ? 0xA0U : 0x71U));
        const auto reset = state(*cart);
        StateReader truncated(std::span<const Byte>(before).first(before.size() - 1));
        cart->load_state(truncated);
        CHECK_FALSE(truncated.ok());
        CHECK(state(*cart) == reset);
        StateReader saved(before);
        cart->load_state(saved);
        CHECK(saved.ok());
        CHECK(state(*cart) == before);
        cart->power_on();
        CHECK(cart->read_prg(0x6000) == (mapper == 551U ? 0x63U : 0x60U));
        CHECK(cart->read_chr(0x0123) == (mapper == 551U ? 0xA0U : 0U));
    }
}
TEST_CASE("Mapper 178 selects speech and pad I/O only on their boards", "[fs305]") {
    register_builtin_mappers();
    auto speech = Cartridge::from_data(image(178, 2));
    REQUIRE(speech);
    REQUIRE(speech->has_expansion_audio());
    REQUIRE(speech->maps_expansion(0x5000));
    REQUIRE(speech->expansion_read_mask(0x5FFF) == 0x40U);
    CHECK(speech->read_expansion(0x5000) == 0U);
    for (unsigned i = 0; i < 100000U; ++i)
        speech->clock_audio();
    CHECK(speech->read_expansion(0x5000) == 0x40U);
    speech->write_expansion(0x5FFF, 0);
    CHECK(speech->read_expansion(0x5000) == 0U);
    auto pads = Cartridge::from_data(image(178, 3));
    REQUIRE(pads);
    CHECK_FALSE(pads->maps_expansion(0x5000));
    REQUIRE(pads->configure_board_jumpers(3));
    pads->write_prg(0x6801, 1);
    CHECK(pads->read_prg(0xBFFF) == 3U);
    pads->write_prg(0x6801, 0);
    CHECK(pads->read_prg(0xBFFF) == 0U);

    auto no_ram = image(178);
    no_ram[6] &= ~2U;
    no_ram[10] = 0;
    auto cart = Cartridge::from_data(no_ram);
    REQUIRE(cart);
    CHECK_FALSE(cart->maps_prg(0x6000));
    for (auto malformed : {image(551, 1), image(178, 4), image(178, 1)})
        CHECK_FALSE(Cartridge::from_data(malformed));
    auto bad = image(551);
    bad[11] = 7;
    CHECK_FALSE(Cartridge::from_data(bad));
}
