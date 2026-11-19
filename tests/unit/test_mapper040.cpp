#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/ntdec_2722.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader make_header(bool nes2 = true, Byte submapper = 0U, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 40U;
    header.is_nes2 = nes2;
    header.submapper = submapper;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_ram_size = nes2 ? 0U : 8192U;
    return header;
}

std::vector<Byte> patterned_rom(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i)
        rom[i] = static_cast<Byte>((i / 8192U) ^ (i & 0xFFU));
    return rom;
}

std::unique_ptr<Ntdec2722> make_board(bool nes2 = true,
                                      Byte submapper = 0U,
                                      bool vertical = true,
                                      std::size_t prg_size = 0U,
                                      std::size_t chr_size = 0U) {
    if (prg_size == 0U)
        prg_size = 65536U;
    if (chr_size == 0U)
        chr_size = 8192U;
    return std::make_unique<Ntdec2722>(
        make_header(nes2, submapper, vertical), patterned_rom(prg_size), patterned_rom(chr_size));
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void clock(Mapper& mapper, unsigned cycles) {
    for (unsigned i = 0U; i < cycles; ++i)
        mapper.clock_cpu();
}

TEST_CASE("Mapper 040 accepts documented ROM profiles and rejects unsupported geometry",
          "[mapper040]") {
    for (bool nes2 : {false, true}) {
        const auto header = make_header(nes2);
        REQUIRE(Ntdec2722::validate_geometry(header, 65536U, 8192U));
        auto board = make_board(nes2);
        REQUIRE(save(*board).size() == 7U);
        REQUIRE(board->battery_ram().empty());
        REQUIRE_FALSE(board->has_expansion_audio());
        REQUIRE_FALSE(board->irq_pending());
        board->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
        REQUIRE(board->battery_ram().empty());
    }
    for (std::size_t prg : {65536UZ, 131072UZ})
        for (std::size_t chr : {8192UZ, 16384UZ, 32768UZ}) {
            const auto header = make_header(true, 1U);
            REQUIRE(Ntdec2722::validate_geometry(header, prg, chr));
            REQUIRE(save(*make_board(true, 1U, true, prg, chr)).size() == 7U);
        }

    for (std::size_t prg : {0UZ, 8192UZ, 32768UZ, 65535UZ, 65537UZ, 131072UZ, 262144UZ})
        REQUIRE_FALSE(Ntdec2722::validate_geometry(make_header(), prg, 8192U));
    for (std::size_t chr : {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 16384UZ, 32768UZ})
        REQUIRE_FALSE(Ntdec2722::validate_geometry(make_header(), 65536U, chr));
    for (std::size_t prg : {0UZ, 32768UZ, 65535UZ, 65537UZ, 131071UZ, 131073UZ, 262144UZ})
        REQUIRE_FALSE(Ntdec2722::validate_geometry(make_header(true, 1U), prg, 32768U));
    for (std::size_t chr :
         {0UZ, 4096UZ, 8191UZ, 8193UZ, 16383UZ, 16385UZ, 32767UZ, 32769UZ, 65536UZ})
        REQUIRE_FALSE(Ntdec2722::validate_geometry(make_header(true, 1U), 131072U, chr));
    for (Byte submapper = 2U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Ntdec2722::validate_geometry(make_header(true, submapper), 131072U, 32768U));

    for (bool nes2 : {false, true}) {
        auto header = make_header(nes2);
        header.has_battery = true;
        REQUIRE_FALSE(Ntdec2722::validate_geometry(header, 65536U, 8192U));
        for (MirrorMode mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            header = make_header(nes2);
            header.mirror_mode = mode;
            REQUIRE_FALSE(Ntdec2722::validate_geometry(header, 65536U, 8192U));
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.prg_ram_size = 8192U;
            break;
        case 1U:
            header.prg_nvram_size = 8192U;
            break;
        case 2U:
            header.chr_ram_size = 8192U;
            break;
        default:
            header.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Ntdec2722::validate_geometry(header, 65536U, 8192U));
    }
}

TEST_CASE("Mapper 040 SMB2J mode maps every fixed and selectable PRG cell", "[mapper040]") {
    for (Byte submapper : {0U, 1U}) {
        auto board = make_board(true, submapper, true, 131072U);
        const auto rom = patterned_rom(131072U);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            board->write_prg(0xE123U, static_cast<Byte>(0xF8U | bank));
            const std::array<unsigned, 5> banks{6U, 4U, 5U, bank, 7U};
            const std::array<Address, 5> bases{0x6000U, 0x8000U, 0xA000U, 0xC000U, 0xE000U};
            for (unsigned window = 0U; window < bases.size(); ++window)
                for (Address offset = 0U; offset < 8192U; ++offset) {
                    const Address addr = static_cast<Address>(bases[window] + offset);
                    REQUIRE(board->read_prg(addr) == rom[banks[window] * 8192U + offset]);
                }
        }
    }
}

TEST_CASE("Mapper 040:1 NROM modes decode all outer PRG pins and mirroring", "[mapper040]") {
    for (std::size_t prg_size : {65536UZ, 131072UZ}) {
        auto board = make_board(true, 1U, false, prg_size);
        const auto rom = patterned_rom(prg_size);
        for (unsigned outer_value = 0U; outer_value < 128U; ++outer_value) {
            if ((outer_value & 0x08U) == 0U)
                continue;
            const Byte outer = static_cast<Byte>(outer_value);
            board->write_prg(0xC000U, outer);
            const bool nrom256 = (outer & 4U) != 0U;
            const unsigned selected = 8U + ((outer >> 5U) & 3U);
            for (Address addr :
                 {0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xDFFFU, 0xE000U, 0xFFFFU}) {
                const unsigned bank =
                    nrom256 ? 8U + ((outer >> 5U) & 2U) + ((addr >> 14U) & 1U) : selected;
                const std::size_t offset = (bank * 16384U + (addr & 0x3FFFU)) % prg_size;
                INFO("outer " << outer << " address " << addr << " PRG size " << prg_size);
                REQUIRE(board->read_prg(addr) == rom[offset]);
            }
            REQUIRE(board->mirror_mode() ==
                    ((outer & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            REQUIRE_FALSE(board->maps_prg(0x6000U));
            REQUIRE_FALSE(board->maps_prg(0x7FFFU));
            REQUIRE(board->read_prg(0x6000U) == 0U);
        }
    }
}

TEST_CASE("Mapper 040 register aliases ignore write data without bus conflicts", "[mapper040]") {
    auto smb = make_board();
    const auto initial = save(*smb);
    for (Address addr = 0xC000U; addr < 0xE000U; ++addr) {
        smb->write_prg(addr, 0xFFU);
        REQUIRE(save(*smb) == initial);
    }
    auto multicart = make_board(true, 1U);
    for (Address addr = 0xC000U; addr < 0xE000U; ++addr) {
        multicart->reset();
        multicart->write_prg(addr, 0xADU);
        REQUIRE(save(*multicart)[3] == 0x2DU);
    }
    for (Address addr = 0xE000U;; ++addr) {
        multicart->write_prg(addr, 0xADU);
        REQUIRE(save(*multicart)[2] == 5U);
        if (addr == 0xFFFFU)
            break;
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        multicart->write_prg(0xFFFFU, static_cast<Byte>(value));
        REQUIRE(save(*multicart)[2] == (value & 7U));
        multicart->write_prg(0xDFFFU, static_cast<Byte>(value));
        REQUIRE(save(*multicart)[3] == (value & 0x7FU));
    }
    for (Address addr = 0x8000U; addr < 0xA000U; ++addr) {
        multicart->write_prg(0xA000U, 0xFFU);
        clock(*multicart, 123U);
        multicart->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE_FALSE(multicart->irq_pending());
        REQUIRE(save(*multicart)[4] == 0U);
        REQUIRE(save(*multicart)[5] == 0U);
        REQUIRE(save(*multicart)[6] == 0U);
    }
    for (Address addr = 0xA000U; addr < 0xC000U; ++addr) {
        multicart->write_prg(0x8000U, 0U);
        multicart->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(save(*multicart)[6] == 1U);
    }
}

TEST_CASE("Mapper 040 CHR outer banks cover every cell and ROM ignores writes", "[mapper040]") {
    auto base = make_board();
    const auto base_rom = patterned_rom(8192U);
    for (Address addr = 0U; addr < 8192U; ++addr) {
        base->write_chr(addr, static_cast<Byte>(~base_rom[addr]));
        REQUIRE(base->read_chr(addr) == base_rom[addr]);
    }
    for (std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ}) {
        auto board = make_board(true, 1U, true, 131072U, chr_size);
        const auto rom = patterned_rom(chr_size);
        for (Byte bank = 0U; bank < 4U; ++bank) {
            board->write_prg(0xC000U, static_cast<Byte>(bank << 1U));
            for (Address addr = 0U; addr < 8192U; ++addr) {
                const std::size_t offset = (bank * 8192U + addr) % chr_size;
                board->write_chr(addr, 0xFFU);
                REQUIRE(board->read_chr(addr) == rom[offset]);
            }
        }
    }
}

TEST_CASE("Mapper 040 exposes PRG at 6000 only in SMB2J mode and leaves other windows open",
          "[mapper040]") {
    auto board = make_board(true, 1U);
    REQUIRE(board->maps_prg(0x6000U));
    REQUIRE(board->maps_prg(0x7FFFU));
    board->write_prg(0xC000U, 0x08U);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(board->maps_prg(addr));
        REQUIRE_FALSE(board->maps_expansion(addr));
        REQUIRE(board->prg_read_mask(addr) == 0xFFU);
        REQUIRE(board->expansion_read_mask(addr) == 0xFFU);
    }
    for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU}) {
        REQUIRE(board->maps_prg(addr));
        REQUIRE(board->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 040 IRQ asserts at 4096 M2 cycles and self-acknowledges at 8192", "[mapper040]") {
    auto board = make_board();
    clock(*board, 10000U);
    REQUIRE_FALSE(board->irq_pending());
    board->write_prg(0xA000U, 0U);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        board->clock_cpu();
        INFO("cycle " << cycle);
        REQUIRE_FALSE(board->irq_pending());
    }
    board->clock_cpu();
    REQUIRE(board->irq_pending());
    board->acknowledge_irq();
    REQUIRE(board->irq_pending());
    board->write_prg(0xA000U, 0xFFU);
    REQUIRE(board->irq_pending());
    for (unsigned cycle = 4097U; cycle < 8192U; ++cycle) {
        board->clock_cpu();
        REQUIRE(board->irq_pending());
    }
    board->clock_cpu();
    REQUIRE_FALSE(board->irq_pending());
    clock(*board, 4095U);
    REQUIRE_FALSE(board->irq_pending());
    board->clock_cpu();
    REQUIRE(board->irq_pending());

    board->write_prg(0x8000U, 0xFFU);
    REQUIRE_FALSE(board->irq_pending());
    clock(*board, 10000U);
    REQUIRE_FALSE(board->irq_pending());
    board->write_prg(0xA000U, 0U);
    clock(*board, 1234U);
    board->write_prg(0xBFFFU, 0U);
    clock(*board, 4096U - 1234U);
    REQUIRE(board->irq_pending());
}

TEST_CASE("Mapper 040 reset and cold power restore deterministic board defaults", "[mapper040]") {
    for (Byte submapper : {0U, 1U})
        for (bool vertical : {false, true}) {
            auto board = make_board(true, submapper, vertical);
            board->write_prg(0xE000U, 7U);
            board->write_prg(0xC000U, 0x6EU);
            board->write_prg(0xA000U, 0U);
            clock(*board, 4096U);
            board->reset();
            REQUIRE(save(*board) == save(*make_board(true, submapper, vertical)));
            REQUIRE(board->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(board->read_prg(0xC000U) == patterned_rom(65536U)[0U]);
            board->write_prg(0xE000U, 7U);
            board->write_prg(0xC000U, 0x6EU);
            board->write_prg(0xA000U, 0U);
            board->clock_cpu();
            board->power_on();
            REQUIRE(save(*board) == save(*make_board(true, submapper, vertical)));
        }
}

TEST_CASE("Mapper 040 state replays banking and partial or pending IRQs without ROM",
          "[mapper040][state]") {
    auto board = make_board(true, 1U, false, 131072U, 32768U);
    board->write_prg(0xE000U, 6U);
    board->write_prg(0xC000U, 0x6FU);
    board->write_prg(0xA000U, 0U);
    clock(*board, 1234U);
    const auto partial = save(*board);
    REQUIRE(partial.size() == 7U);

    auto copy = make_board(true, 1U, false, 131072U, 32768U);
    StateReader restore(partial);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(save(*copy) == partial);
    REQUIRE(copy->read_prg(0x8000U) == board->read_prg(0x8000U));
    REQUIRE(copy->read_prg(0xC000U) == board->read_prg(0xC000U));
    REQUIRE(copy->read_chr(0x123U) == board->read_chr(0x123U));
    clock(*copy, 4096U - 1234U);
    REQUIRE(copy->irq_pending());
    const auto pending = save(*copy);

    copy->reset();
    StateReader pending_reader(pending);
    copy->load_state(pending_reader);
    REQUIRE(pending_reader.ok());
    REQUIRE(copy->irq_pending());
    clock(*copy, 4095U);
    REQUIRE(copy->irq_pending());
    copy->clock_cpu();
    REQUIRE_FALSE(copy->irq_pending());
}

TEST_CASE("Mapper 040 malformed state rejection is bounded and atomic", "[mapper040][state]") {
    auto board = make_board(true, 1U, false);
    board->write_prg(0xE000U, 7U);
    board->write_prg(0xC000U, 0x6FU);
    board->write_prg(0xA000U, 0U);
    clock(*board, 4096U);
    const auto saved = save(*board);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    for (Byte value : {8U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[2] = value;
        StateReader reader(bad);
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    {
        auto bad = saved;
        bad[3] = 0x80U;
        StateReader reader(bad);
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    {
        auto bad = saved;
        bad[4] = bad[5] = 0xFFU;
        StateReader reader(bad);
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    for (Byte value : {2U, 0xFFU}) {
        auto bad = saved;
        bad[6] = value;
        StateReader reader(bad);
        board->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*board) == saved);
    }
    {
        auto base = make_board();
        const auto disabled = save(*base);
        auto bad = disabled;
        bad[4] = 1U;
        StateReader reader(bad);
        base->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*base) == disabled);
        bad = disabled;
        bad[3] = 1U;
        StateReader submapper_zero(bad);
        base->load_state(submapper_zero);
        REQUIRE_FALSE(submapper_zero.ok());
        REQUIRE(save(*base) == disabled);
    }
    for (bool vertical : {false, true}) {
        auto other = make_board(true, 0U, vertical);
        const auto before = save(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*other) == before);
    }
    auto other_mirror = make_board(true, 1U, true);
    const auto before = save(*other_mirror);
    StateReader mismatched(saved);
    other_mirror->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(save(*other_mirror) == before);
}

} // namespace
