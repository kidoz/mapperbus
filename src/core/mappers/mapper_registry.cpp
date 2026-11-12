#include "core/mappers/mapper_registry.hpp"

#include "core/mappers/axrom.hpp"
#include "core/mappers/bandai_fcg.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/datach.hpp"
#include "core/mappers/discrete.hpp"
#include "core/mappers/flash_boards.hpp"
#include "core/mappers/irem.hpp"
#include "core/mappers/irem_lrog017.hpp"
#include "core/mappers/jaleco_discrete.hpp"
#include "core/mappers/jaleco_ss88006.hpp"
#include "core/mappers/jy_company.hpp"
#include "core/mappers/konami_vrc.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc2.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc4.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco108.hpp"
#include "core/mappers/namco163.hpp"
#include "core/mappers/namco175340.hpp"
#include "core/mappers/nrom.hpp"
#include "core/mappers/rambo1.hpp"
#include "core/mappers/sachen.hpp"
#include "core/mappers/sunsoft34.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/taito_tc.hpp"
#include "core/mappers/taito_x1005.hpp"
#include "core/mappers/taito_x1017.hpp"
#include "core/mappers/txc.hpp"
#include "core/mappers/uxrom.hpp"
#include "core/mappers/vrc6.hpp"
#include "core/mappers/vrc7.hpp"

namespace mapperbus::core {
namespace {

[[nodiscard]] Result<void> validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.mapper_number == 31 && prg_size < 4096) {
        return std::unexpected("Mapper 31 requires at least a 4 KiB PRG ROM");
    }
    if (header.mapper_number == 30U || header.mapper_number == 111U)
        return FlashBoards::validate_geometry(header, prg_size, chr_size);
    if (JyCompany::supports_mapper(header.mapper_number))
        return JyCompany::validate_geometry(header, prg_size, chr_size);
    if (Sachen::supports_mapper(header.mapper_number))
        return Sachen::validate_geometry(header, prg_size, chr_size);
    if (Txc::supports_mapper(header.mapper_number))
        return Txc::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 205U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 205 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-205 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 205 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 205 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 202U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 202 supports submapper 0");
        if (prg_size < 0x4000U || prg_size > 0x20000U || prg_size % 0x4000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x10000U || chr_size % 0x2000U != 0U)
            return std::unexpected("Unsupported mapper-202 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 202 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 202 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 199U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 199 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x100000U || prg_size % 0x2000U != 0U ||
            chr_size != 0U)
            return std::unexpected("Unsupported mapper-199 ROM geometry");
        if (!header.has_battery || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0x8000U ||
                                header.chr_ram_size != 0x2000U)))
            return std::unexpected(
                "Mapper 199 requires a 32 KiB PRG NVRAM chip and 8 KiB volatile CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 199 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 198U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 198 supports submapper 0");
        if (prg_size != 0xA0000U || chr_size != 0U)
            return std::unexpected(
                "Mapper 198 requires 640 KiB PRG ROM and CHR RAM without CHR ROM");
        if (!header.has_battery || header.chr_nvram_size != 0U ||
            (header.is_nes2 &&
             (header.prg_ram_size != 0x1000U || header.prg_nvram_size != 0x2000U ||
              header.chr_ram_size != 0x2000U)))
            return std::unexpected(
                "Mapper 198 requires 4 KiB volatile CPU RAM, 8 KiB PRG NVRAM and 8 KiB CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 198 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 197U) {
        if (header.is_nes2 && header.submapper > 3U)
            return std::unexpected("Mapper 197 supports submappers 0 through 3");
        const bool prg_mux = header.is_nes2 && header.submapper == 3U;
        if (prg_size < 0x8000U || prg_size > (prg_mux ? 0x40000U : 0x80000U) ||
            prg_size % 0x2000U != 0U || chr_size < 0x2000U || chr_size > 0x80000U ||
            chr_size % 0x800U != 0U)
            return std::unexpected("Unsupported mapper-197 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 197 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 197 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 196U) {
        if (header.is_nes2 && header.submapper > 3U)
            return std::unexpected("Mapper 196 supports submappers 0 through 3");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-196 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 196 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 196 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 195U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 195 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-195 ROM geometry");
        if (header.chr_nvram_size != 0U || (header.is_nes2 && header.chr_ram_size != 0x2000U))
            return std::unexpected("Mapper 195 requires 8 KiB of addressable volatile CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x1000U &&
                 header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size == 0x2000U && header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mapper 195 supports optional 4 KiB auxiliary RAM and one 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("Mapper 195 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 195 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 74U || header.mapper_number == 192U ||
        header.mapper_number == 194U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mixed-CHR MMC3 boards support submapper 0");
        const bool ram_only = header.mapper_number == 194U && chr_size == 0U;
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            (!ram_only && (chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)))
            return std::unexpected("Unsupported mixed-CHR MMC3 ROM geometry");
        if (header.chr_nvram_size != 0U ||
            (header.is_nes2 && header.chr_ram_size != 0x800U && header.chr_ram_size != 0x1000U))
            return std::unexpected("Mixed-CHR MMC3 boards require 2 or 4 KiB of volatile CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mixed-CHR MMC3 boards support one optional 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mixed-CHR MMC3 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mixed-CHR MMC3 boards support ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 249U ||
        (header.mapper_number == 4U && header.is_nes2 && header.submapper == 5U)) {
        if (header.mapper_number == 249U && header.is_nes2 && header.submapper != 0U)
            return std::unexpected(
                "Mapper 249 supports submapper 0; canonical dumps use mapper 4:5");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported T9552 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("T9552 supports CHR ROM without CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
                return std::unexpected("T9552 supports one optional 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("T9552 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("T9552 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 123U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 123 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-123 ROM geometry");
        // Support the documented ROM-only profile, without inferring WRAM
        // from the legacy iNES header's default allocation.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 123 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 123 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 114U) {
        if (header.is_nes2 && header.submapper > 1U)
            return std::unexpected("Mapper 114 supports submappers 0 and 1");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-114 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 114 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 114 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 115U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 115 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-115 ROM geometry");
        // The documented control-register profile establishes ROM banking,
        // not an alternate WRAM population. Do not infer RAM from legacy defaults.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 115 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 115 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 250U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected(
                "Mapper 250 supports submapper 0; use an explicit board profile");
        if (prg_size < 0x8000U || prg_size > 0x20000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x20000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-250 ROM geometry");
        // Both documented PCBs contain no WRAM. Ignore only legacy iNES's
        // implicit CPU-RAM size, never an explicit NES 2.0 declaration.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 250 PCBs require ROM-only cartridge memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 250 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 263U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 263 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-263 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Mapper 263 supports CHR ROM without CHR RAM");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U)
            return std::unexpected("Mapper 263 WRAM exceeds 8 KiB");
        const auto ram = header.prg_ram_size + header.prg_nvram_size;
        if (ram != 0U && (ram & (ram - 1U)) != 0U)
            return std::unexpected("Mapper 263 WRAM size must be a power of two");
        if (header.has_battery != (header.prg_nvram_size != 0U))
            return std::unexpected("Mapper 263 battery flag and NVRAM declaration disagree");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 263 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 262U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 262 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-262 ROM geometry");
        // Its fixed four-screen VRAM is excluded from the NES 2.0 CHR-RAM
        // field. Only the 8 KiB pattern RAM is counted. No WRAM population
        // is established by the source for this supported board profile.
        if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
            header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x2000U))
            return std::unexpected("Mapper 262 requires 8 KiB volatile CHR RAM and no WRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical &&
            header.mirror_mode != MirrorMode::FourScreen)
            return std::unexpected("Unsupported mapper-262 header mirroring");
        return {};
    }
    if (header.mapper_number == 513U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 513 requires NES 2.0 submapper 0");
        if (prg_size < 0x80000U || prg_size > 0x200000U || prg_size % 0x80000U != 0U ||
            chr_size != 0U)
            return std::unexpected("Mapper 513 requires 512 KiB outer PRG units and no CHR ROM");
        if (!header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
            header.chr_ram_size != 0U ||
            (header.chr_nvram_size != 0U && header.chr_nvram_size != 0x8000U))
            return std::unexpected("Mapper 513 requires 32 KiB battery CHR RAM and no WRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 513 provides ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 512U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 512 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-512 ROM geometry");
        // Its mandatory PPU memory is 4 KiB pattern RAM plus 4 KiB VRAM.
        // NES 2.0 accounts for both in the CHR RAM field. Zero declarations
        // use the board's physical defaults; contradictory sizes are rejected.
        if (!header.has_battery || header.prg_ram_size != 0U ||
            (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x2000U) ||
            header.chr_nvram_size != 0U)
            return std::unexpected(
                "Mapper 512 requires 8 KiB battery WRAM and 8 KiB volatile PPU RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical &&
            header.mirror_mode != MirrorMode::FourScreen)
            return std::unexpected("Unsupported mapper-512 header mirroring");
        return {};
    }
    if (header.mapper_number == 157U)
        return Datach::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 16U || header.mapper_number == 153U || header.mapper_number == 159U)
        return BandaiFcg::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number != 31 && prg_size < 16 * 1024) {
        return std::unexpected("Mapper requires at least a 16 KiB PRG ROM");
    }
    if (header.mapper_number == 0 && prg_size != 16 * 1024 && prg_size != 32 * 1024) {
        return std::unexpected("NROM requires a 16 KiB or 32 KiB PRG ROM");
    }
    if (header.mapper_number == 4 && prg_size > 512 * 1024) {
        return std::unexpected("MMC3 PRG ROM exceeds 512 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 256 * 1024) {
        return std::unexpected("MMC3 CHR ROM exceeds 256 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 0 && chr_size < 1024) {
        return std::unexpected("MMC3 CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 118U || header.mapper_number == 119U) {
        const bool tqrom = header.mapper_number == 119U;
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Unsupported MMC3 variant submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > (tqrom ? 0x20000U : 0x40000U) || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported MMC3 variant ROM geometry");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("MMC3 variant does not provide four-screen VRAM");
        if (header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && (!tqrom || header.chr_ram_size != 0x2000U)))
            return std::unexpected("Unsupported MMC3 variant CHR RAM geometry");
        if (tqrom) {
            if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
                header.has_battery)
                return std::unexpected("TQROM does not provide PRG RAM or NVRAM");
        } else {
            if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
                header.prg_ram_size + header.prg_nvram_size > 0x2000U)
                return std::unexpected("TxSROM PRG RAM exceeds 8 KiB");
            const auto ram_size = header.prg_ram_size + header.prg_nvram_size;
            if (ram_size != 0U && (ram_size & (ram_size - 1U)) != 0U)
                return std::unexpected("TxSROM RAM size must be a power of two");
            if (header.is_nes2 && header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("TxSROM battery flag and NVRAM declaration disagree");
        }
    }
    if (header.mapper_number == 64U || header.mapper_number == 158U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Unsupported RAMBO-1 submapper");
        if (prg_size < 0x8000U || prg_size > 0x40000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported RAMBO-1 ROM geometry");
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
            header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("RAMBO-1 does not provide RAM or NVRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("RAMBO-1 does not provide four-screen VRAM");
    }
    if (header.mapper_number == 3 && chr_size > 0 && chr_size < 8 * 1024) {
        return std::unexpected("CNROM CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 7 && prg_size < 32 * 1024) {
        return std::unexpected("AxROM requires at least a 32 KiB PRG ROM");
    }
    if (header.mapper_number == 9 && prg_size < 32 * 1024) {
        return std::unexpected("MMC2 requires at least a 32 KiB PRG ROM");
    }
    switch (header.mapper_number) {
    case 10:
    case 21:
    case 22:
    case 23:
    case 25:
    case 75:
    case 76:
    case 88:
    case 95:
    case 154:
    case 193:
    case 206:
        if (prg_size < 32 * 1024)
            return std::unexpected("Mapper requires at least a 32 KiB PRG ROM");
        break;
    case 13:
        if (chr_size != 0)
            return std::unexpected("CPROM requires CHR RAM, not CHR ROM");
        break;
    default:
        break;
    }
    if ((header.mapper_number == 21 || header.mapper_number == 22 || header.mapper_number == 23 ||
         header.mapper_number == 25 || header.mapper_number == 73 || header.mapper_number == 75) &&
        (header.prg_ram_size > 8192 || header.prg_nvram_size > 8192 ||
         header.prg_ram_size + header.prg_nvram_size > 8192)) {
        return std::unexpected("VRC board PRG RAM exceeds 8 KiB");
    }
    if (header.is_nes2) {
        if ((header.mapper_number == 32 && header.submapper > 1) ||
            (header.mapper_number == 65 && header.submapper != 0))
            return std::unexpected("Unsupported Irem submapper");
        if (header.mapper_number == 21 && header.submapper > 2)
            return std::unexpected("Unsupported VRC4 submapper");
        if ((header.mapper_number == 23 || header.mapper_number == 25) && header.submapper > 3)
            return std::unexpected("Unsupported VRC2/VRC4 submapper");
        if (header.mapper_number == 34 && header.submapper > 2)
            return std::unexpected("Unsupported mapper 34 submapper");
        if (header.mapper_number == 71 && header.submapper > 1)
            return std::unexpected("Unsupported mapper 71 submapper");
        if (header.mapper_number == 78 && header.submapper != 0 && header.submapper != 1 &&
            header.submapper != 3)
            return std::unexpected("Unsupported mapper 78 submapper");
        if ((header.mapper_number == 61 || header.mapper_number == 200 ||
             header.mapper_number == 206 || header.mapper_number == 232) &&
            header.submapper > 1)
            return std::unexpected("Unsupported board submapper");
    }
    if (header.mapper_number == 32 || header.mapper_number == 65) {
        const std::size_t max_prg = header.mapper_number == 32 ? 0x40000U : 0x80000U;
        if (prg_size < 0x8000U || prg_size > max_prg || prg_size % 0x2000U != 0U ||
            chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Irem ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported Irem RAM geometry");
    }
    if (header.mapper_number == 67 || header.mapper_number == 68) {
        const bool sunsoft4 = header.mapper_number == 68;
        const bool dcs = sunsoft4 && header.is_nes2 && header.submapper == 1;
        if (header.is_nes2 && header.submapper > (sunsoft4 ? 1 : 0))
            return std::unexpected("Unsupported Sunsoft-3/4 submapper");
        if (prg_size < 0x8000U || prg_size > (dcs ? 0x20000U : 0x40000U) ||
            prg_size % 0x4000U != 0U || chr_size > (sunsoft4 ? 0x40000U : 0x20000U) ||
            chr_size % 0x800U != 0U || (sunsoft4 && chr_size == 0U))
            return std::unexpected("Unsupported Sunsoft-3/4 ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported Sunsoft-3/4 RAM geometry");
    }
    if (header.mapper_number == 33 || header.mapper_number == 48) {
        if (header.is_nes2 && header.submapper > (header.mapper_number == 48 ? 1 : 0))
            return std::unexpected("Unsupported Taito TC submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Taito TC ROM geometry");
        if ((header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)) ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U ||
            (chr_size != 0U && header.chr_ram_size != 0U))
            return std::unexpected("Unsupported Taito TC RAM geometry");
    }
    if (header.mapper_number == 80 || header.mapper_number == 207) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported X1-005 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported X1-005 ROM geometry");
        if (header.is_nes2 && ((header.prg_ram_size != 0U && header.prg_ram_size != 128U) ||
                               (header.prg_nvram_size != 0U && header.prg_nvram_size != 128U) ||
                               header.prg_ram_size + header.prg_nvram_size > 128U))
            return std::unexpected("X1-005 has only 128 bytes of internal PRG memory");
        if (header.is_nes2 && header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("X1-005 NVRAM requires the battery flag");
        if (header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U ||
            (chr_size != 0U && header.chr_ram_size != 0U))
            return std::unexpected("Unsupported X1-005 CHR RAM geometry");
    }
    if (header.mapper_number == 210) {
        if (header.is_nes2 && header.submapper > 2U)
            return std::unexpected("Unsupported Namco 175/340 submapper");
        const bool namco175 = header.is_nes2
                                  ? (header.submapper == 1U ||
                                     (header.submapper == 0U &&
                                      (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
                                  : header.has_battery;
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Namco 175/340 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Namco 175/340 requires CHR ROM");
        if (namco175) {
            if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
                header.prg_ram_size + header.prg_nvram_size > 0x2000U)
                return std::unexpected("Namco 175 PRG RAM exceeds 8 KiB");
            const auto size = header.prg_ram_size + header.prg_nvram_size;
            if (size != 0U && (size & (size - 1U)) != 0U)
                return std::unexpected("Namco 175 RAM size must be a power of two");
            if (header.prg_nvram_size != 0U && !header.has_battery)
                return std::unexpected("Namco 175 NVRAM requires the battery flag");
            if (header.is_nes2 && header.has_battery && header.prg_nvram_size == 0U)
                return std::unexpected("Namco 175 battery flag requires declared NVRAM");
        } else if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
                   header.has_battery) {
            return std::unexpected("Namco 340 does not provide RAM or NVRAM");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Namco 175/340 requires an H/V startup arrangement");
    }
    if (header.mapper_number == 77) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported LROG017 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x8000U != 0U ||
            chr_size == 0U || chr_size > 0x8000U || chr_size % 0x800U != 0U)
            return std::unexpected("Unsupported LROG017 ROM geometry");
        // Its mandatory 8 KiB SRAM supplies 6 KiB of pattern RAM and 2 KiB
        // of nametable RAM. NES 2.0 cannot encode a 6 KiB pattern portion.
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.has_battery ||
            header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x1800U &&
             header.chr_ram_size != 0x2000U))
            return std::unexpected("Unsupported LROG017 RAM geometry");
    }
    if (header.mapper_number == 72 || header.mapper_number == 92 || header.mapper_number == 101) {
        const bool chr_only = header.mapper_number == 101;
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported Jaleco discrete submapper");
        const std::size_t max_prg =
            chr_only ? 0x8000U : (header.mapper_number == 72 ? 0x20000U : 0x40000U);
        const std::size_t min_prg = chr_only ? 0x4000U : 0x8000U;
        const std::size_t max_chr = chr_only ? 0x200000U : 0x20000U;
        if (prg_size < min_prg || prg_size > max_prg || prg_size % 0x4000U != 0U ||
            chr_size == 0U || chr_size > max_chr || chr_size % 0x2000U != 0U)
            return std::unexpected("Unsupported Jaleco discrete ROM geometry");
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.has_battery ||
            header.prg_nvram_size != 0U || header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Jaleco discrete boards do not provide RAM or NVRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Jaleco discrete boards require fixed H/V mirroring");
    }
    if (header.mapper_number == 18) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported SS88006 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported SS88006 ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U || header.chr_ram_size != 0U ||
            header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported SS88006 RAM geometry");
        if (header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("SS88006 NVRAM requires the battery flag");
        if (header.mirror_mode == MirrorMode::FourScreen)
            return std::unexpected("SS88006 does not support four-screen nametables");
    }
    if (header.mapper_number == 82 || header.mapper_number == 552) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported X1-017 submapper");
        const std::size_t max_prg = header.mapper_number == 82 ? 0x20000U : 0x80000U;
        if (prg_size < 0x8000U || prg_size > max_prg || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported X1-017 ROM geometry");
        // Mandatory internal 5 KiB cannot be represented by NES 2.0's RAM
        // exponent. Accept bounded declarations (including rounded-up 8 KiB),
        // but never allocate external RAM or enlarge the physical window.
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U || header.chr_ram_size != 0U ||
            header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported X1-017 RAM geometry");
        if (header.is_nes2 && header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("X1-017 NVRAM requires the battery flag");
    }
    return {};
}

} // namespace

MapperRegistry& MapperRegistry::instance() {
    // Readers retaining ownership may continue during static teardown. The
    // cached reference does not access the static shared_ptr after its lifetime.
    static auto& registry = *retain();
    return registry;
}

std::shared_ptr<MapperRegistry> MapperRegistry::retain() {
    struct SharedRegistry final : MapperRegistry {};
    static const auto registry = std::make_shared<SharedRegistry>();
    return registry;
}

void MapperRegistry::register_mapper(uint16_t mapper_number, MapperFactory factory) {
    factories_[mapper_number] = std::move(factory);
}

Result<std::unique_ptr<Mapper>> MapperRegistry::create(const INesHeader& header,
                                                       std::vector<Byte> prg_rom,
                                                       std::vector<Byte> chr_rom) const {
    auto it = factories_.find(header.mapper_number);
    if (it == factories_.end()) {
        return std::unexpected("Unsupported mapper: " + std::to_string(header.mapper_number));
    }
    if (auto valid = validate_geometry(header, prg_rom.size(), chr_rom.size()); !valid) {
        return std::unexpected(valid.error());
    }
    return it->second(header, std::move(prg_rom), std::move(chr_rom));
}

namespace {

template <typename T> MapperFactory make_factory() {
    return [](const INesHeader& header,
              std::vector<Byte> prg_rom,
              std::vector<Byte> chr_rom) -> std::unique_ptr<Mapper> {
        return std::make_unique<T>(header, std::move(prg_rom), std::move(chr_rom));
    };
}

} // namespace

void register_builtin_mappers() {
    auto& registry = MapperRegistry::instance();
    for (const uint16_t id : {133U, 135U, 137U, 138U, 139U, 141U, 143U, 146U, 150U, 243U})
        registry.register_mapper(id, make_factory<Sachen>());
    for (const uint16_t id : {36U, 132U, 136U, 147U, 172U, 173U})
        registry.register_mapper(id, make_factory<Txc>());
    for (const uint16_t id : {35U, 90U, 209U, 211U, 281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U})
        registry.register_mapper(id, make_factory<JyCompany>());
    registry.register_mapper(0, make_factory<Nrom>());
    registry.register_mapper(1, make_factory<Mmc1>());
    registry.register_mapper(2, make_factory<Uxrom>());
    registry.register_mapper(3, make_factory<Cnrom>());
    registry.register_mapper(4, make_factory<Mmc3>());
    registry.register_mapper(114, make_factory<Mmc3>());
    registry.register_mapper(115, make_factory<Mmc3>());
    registry.register_mapper(123, make_factory<Mmc3>());
    registry.register_mapper(118, make_factory<Mmc3>());
    registry.register_mapper(119, make_factory<Mmc3>());
    registry.register_mapper(74, make_factory<Mmc3>());
    registry.register_mapper(192, make_factory<Mmc3>());
    registry.register_mapper(194, make_factory<Mmc3>());
    registry.register_mapper(195, make_factory<Mmc3>());
    registry.register_mapper(196, make_factory<Mmc3>());
    registry.register_mapper(197, make_factory<Mmc3>());
    registry.register_mapper(198, make_factory<Mmc3>());
    registry.register_mapper(199, make_factory<Mmc3>());
    registry.register_mapper(205, make_factory<Mmc3>());
    registry.register_mapper(249, make_factory<Mmc3>());
    registry.register_mapper(250, make_factory<Mmc3>());
    registry.register_mapper(262, make_factory<Mmc3>());
    registry.register_mapper(263, make_factory<Mmc3>());
    registry.register_mapper(512, make_factory<Mmc3>());
    registry.register_mapper(513, make_factory<Mmc3>());
    registry.register_mapper(64, make_factory<Rambo1>());
    registry.register_mapper(158, make_factory<Rambo1>());
    registry.register_mapper(16, make_factory<BandaiFcg>());
    registry.register_mapper(153, make_factory<BandaiFcg>());
    registry.register_mapper(159, make_factory<BandaiFcg>());
    registry.register_mapper(157, make_factory<Datach>());
    registry.register_mapper(30, make_factory<FlashBoards>());
    registry.register_mapper(111,
                             [](const INesHeader& h,
                                std::vector<Byte> prg,
                                std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
                                 if (!chr.empty())
                                     return std::make_unique<Mmc1>(
                                         h, std::move(prg), std::move(chr));
                                 return std::make_unique<FlashBoards>(h, std::move(prg), chr);
                             });
    registry.register_mapper(7, make_factory<Axrom>());
    registry.register_mapper(5, make_factory<Mmc5>());
    registry.register_mapper(9, make_factory<Mmc2>());
    registry.register_mapper(11, make_factory<ColorDreams>());
    registry.register_mapper(19, make_factory<Namco163>());
    registry.register_mapper(24, make_factory<Vrc6>());
    registry.register_mapper(26, make_factory<Vrc6>());
    registry.register_mapper(69, make_factory<Sunsoft5b>());
    registry.register_mapper(85, make_factory<Vrc7>());
    registry.register_mapper(10, make_factory<Mmc4>());
    registry.register_mapper(28, make_factory<DiscreteMapper>());
    registry.register_mapper(31, make_factory<DiscreteMapper>());
    registry.register_mapper(32, make_factory<Irem>());
    registry.register_mapper(65, make_factory<Irem>());
    registry.register_mapper(67, make_factory<Sunsoft34>());
    registry.register_mapper(68, make_factory<Sunsoft34>());
    registry.register_mapper(33, make_factory<TaitoTc>());
    registry.register_mapper(48, make_factory<TaitoTc>());
    registry.register_mapper(80, make_factory<TaitoX1005>());
    registry.register_mapper(207, make_factory<TaitoX1005>());
    registry.register_mapper(82, make_factory<TaitoX1017>());
    registry.register_mapper(552, make_factory<TaitoX1017>());
    registry.register_mapper(18, make_factory<JalecoSs88006>());
    registry.register_mapper(72, make_factory<JalecoDiscrete>());
    registry.register_mapper(92, make_factory<JalecoDiscrete>());
    registry.register_mapper(101, make_factory<JalecoDiscrete>());
    registry.register_mapper(77, make_factory<IremLrog017>());
    registry.register_mapper(210, make_factory<Namco175340>());
    for (const uint16_t id : {21, 22, 23, 25, 73, 75})
        registry.register_mapper(id, make_factory<KonamiVrc>());
    for (const uint16_t id :
         {13, 34, 66, 70, 71, 78, 79, 87, 89, 93, 94, 97, 113, 140, 152, 180, 184}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
    for (const uint16_t id : {76, 88, 95, 154, 206}) {
        registry.register_mapper(id, make_factory<Namco108>());
    }
    for (const uint16_t id : {38,  46,  58,  61,  62,  81,  107, 145, 148, 149, 193, 200,
                              201, 202, 203, 212, 213, 225, 226, 229, 231, 232, 240}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
}

} // namespace mapperbus::core
