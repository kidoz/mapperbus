#include "core/cartridge/cartridge.hpp"

#include <algorithm>

#include "core/binary_file.hpp"
#include "core/cartridge/rom_crc32.hpp"
#include "core/cartridge/rom_database.hpp"
#include "core/fds/fds_mapper.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

Cartridge::Cartridge(INesHeader header, std::unique_ptr<Mapper> mapper)
    : header_(header), mapper_(std::move(mapper)) {}

void Cartridge::save_state(StateWriter& writer) const {
    mapper_->save_state(writer);
}

void Cartridge::load_state(StateReader& reader, std::uint32_t version) {
    mapper_->load_state_version(reader, version);
}

Result<Cartridge> Cartridge::from_file(const std::string& path, std::span<const Byte> dcs_option) {
    auto data = read_binary_file_capped(path, kMaxRomBytes, "ROM file");
    if (!data) {
        return std::unexpected(data.error());
    }

    return from_data_impl(*data, path, dcs_option, &*data);
}

Result<Cartridge> Cartridge::from_data(std::span<const Byte> rom_data,
                                       std::string_view filename,
                                       std::span<const Byte> dcs_option) {
    return from_data_impl(rom_data, filename, dcs_option, nullptr);
}

Result<Cartridge> Cartridge::from_data_impl(std::span<const Byte> rom_data,
                                            std::string_view filename,
                                            std::span<const Byte> dcs_option,
                                            std::vector<Byte>* owned_data) {
    if (rom_data.size() > kMaxRomBytes) {
        return std::unexpected("ROM data exceeds maximum size");
    }

    auto header_result = parse_ines_header(rom_data);
    if (!header_result) {
        return std::unexpected(header_result.error());
    }

    const auto& header = *header_result;
    std::size_t offset = 16;
    if (header.has_trainer) {
        offset += 512;
    }

    const std::size_t prg_size = header.prg_rom_size;
    const std::size_t chr_size = header.chr_rom_size;

    if (offset > rom_data.size() || prg_size > rom_data.size() - offset) {
        return std::unexpected("ROM data is smaller than declared in header");
    }

    const auto chr_offset = offset + prg_size;
    if (chr_size > rom_data.size() - chr_offset) {
        return std::unexpected("ROM data is smaller than declared in header");
    }

    // Reuse only an exact-size owned PRG-only image. Padded/mixed images and
    // borrowed spans keep independent payload buffers instead of retaining
    // unused capacity. Hash the original bytes before erasing the header/trainer.
    const bool reuse = owned_data && chr_size == 0U && chr_offset == rom_data.size() &&
                       owned_data->capacity() == rom_data.size();
    Sha256Digest original_hash{};
    std::uint32_t original_crc = 0U;
    std::vector<Byte> prg_rom;
    std::vector<Byte> chr_rom;
    if (reuse) {
        original_hash = rom_sha256(rom_data);
        if (!header.is_nes2)
            original_crc = rom_crc32(rom_data);
        owned_data->erase(owned_data->begin(),
                          owned_data->begin() + static_cast<std::ptrdiff_t>(offset));
        prg_rom = std::move(*owned_data);
    } else {
        prg_rom.assign(rom_data.begin() + offset, rom_data.begin() + chr_offset);
        chr_rom.assign(rom_data.begin() + chr_offset, rom_data.begin() + chr_offset + chr_size);
    }

    auto mapper_result =
        MapperRegistry::instance().create(header, std::move(prg_rom), std::move(chr_rom));
    if (!mapper_result) {
        return std::unexpected(mapper_result.error());
    }
    if (!dcs_option.empty()) {
        if (auto configured = (*mapper_result)->configure_dcs_option_rom(dcs_option); !configured)
            return std::unexpected(configured.error());
    }

    Cartridge cartridge(header, std::move(*mapper_result));
    cartridge.content_hash_ = reuse ? original_hash : rom_sha256(rom_data);
    cartridge.base_content_hash_ = cartridge.content_hash_;
    if (!dcs_option.empty()) {
        // Domain-separated, fixed-length composition; file paths are not identity.
        std::array<Byte, 72U> identity{'M', 'B', 'D', 'C', 'S', 0U, 0U, 1U};
        std::ranges::copy(cartridge.base_content_hash_, identity.begin() + 8U);
        std::ranges::copy(rom_sha256(dcs_option), identity.begin() + 40U);
        cartridge.content_hash_ = rom_sha256(identity);
    }
    // Header wins, then content database, then the logical ROM filename.
    if (!header.is_nes2) {
        if (auto region = lookup_region_by_crc32(reuse ? original_crc : rom_crc32(rom_data))) {
            cartridge.header_.region = *region;
        } else if (auto filename_region = detect_region_from_filename(std::string(filename))) {
            cartridge.header_.region = *filename_region;
        }
    }
    return cartridge;
}

Result<Cartridge> Cartridge::from_fds_bios(std::span<const Byte> bios, const Fds& drive) {
    if (bios.size() != FdsMapper::kBiosBytes) {
        return std::unexpected("FDS BIOS must contain exactly 8192 bytes");
    }
    INesHeader header{};
    header.mapper_number = 20;
    header.prg_rom_size = bios.size();
    header.region = Region::NTSC;
    header.mirror_mode = MirrorMode::Vertical;
    Cartridge cartridge(header, std::make_unique<FdsMapper>(bios, drive));
    cartridge.content_hash_ = rom_sha256(bios);
    cartridge.base_content_hash_ = cartridge.content_hash_;
    return cartridge;
}

Byte Cartridge::read_prg(Address addr) {
    return mapper_->read_prg(addr);
}

void Cartridge::write_prg(Address addr, Byte value) {
    mapper_->write_prg(addr, value);
}

void Cartridge::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    mapper_->write_prg_cpu(addr, value, consecutive);
}

bool Cartridge::maps_prg(Address addr) const {
    return mapper_->maps_prg(addr);
}

Byte Cartridge::read_chr(Address addr) {
    return mapper_->read_chr(addr);
}

void Cartridge::write_chr(Address addr, Byte value) {
    mapper_->write_chr(addr, value);
}

} // namespace mapperbus::core
