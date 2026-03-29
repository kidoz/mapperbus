#include "core/cartridge/cartridge.hpp"

#include <fstream>
#include <iterator>

#include "core/cartridge/rom_crc32.hpp"
#include "core/cartridge/rom_database.hpp"
#include "core/mappers/mapper_registry.hpp"

namespace mapperbus::core {

Cartridge::Cartridge(INesHeader header, std::unique_ptr<Mapper> mapper)
    : header_(header), mapper_(std::move(mapper)) {}

Result<Cartridge> Cartridge::from_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open ROM file: " + path);
    }

    std::vector<Byte> data((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    auto result = from_data(data);
    if (!result) {
        return result;
    }

    // Region detection fallback chain (NES 2.0 byte 12 already applied in parse_ines_header):
    // 1. CRC32 hash database lookup (highest confidence for iNES 1.0)
    // 2. Filename region tags
    // 3. Default NTSC (already set)
    if (!result->header().is_nes2) {
        uint32_t crc = rom_crc32(data);
        auto db_region = lookup_region_by_crc32(crc);
        if (db_region) {
            result->header_.region = *db_region;
        } else {
            auto filename_region = detect_region_from_filename(path);
            if (filename_region) {
                result->header_.region = *filename_region;
            }
        }
    }

    return result;
}

Result<Cartridge> Cartridge::from_data(std::span<const Byte> rom_data) {
    auto header_result = parse_ines_header(rom_data);
    if (!header_result) {
        return std::unexpected(header_result.error());
    }

    const auto& header = *header_result;
    std::size_t offset = 16;
    if (header.has_trainer) {
        offset += 512;
    }

    std::size_t prg_size = header.prg_rom_banks * 16384u;
    std::size_t chr_size = header.chr_rom_banks * 8192u;

    if (offset + prg_size + chr_size > rom_data.size()) {
        return std::unexpected("ROM data is smaller than declared in header");
    }

    std::vector<Byte> prg_rom(rom_data.begin() + offset, rom_data.begin() + offset + prg_size);
    offset += prg_size;

    std::vector<Byte> chr_rom(rom_data.begin() + offset, rom_data.begin() + offset + chr_size);

    auto mapper_result =
        MapperRegistry::instance().create(header, std::move(prg_rom), std::move(chr_rom));
    if (!mapper_result) {
        return std::unexpected(mapper_result.error());
    }

    return Cartridge(header, std::move(*mapper_result));
}

Byte Cartridge::read_prg(Address addr) {
    return mapper_->read_prg(addr);
}

void Cartridge::write_prg(Address addr, Byte value) {
    mapper_->write_prg(addr, value);
}

Byte Cartridge::read_chr(Address addr) {
    return mapper_->read_chr(addr);
}

void Cartridge::write_chr(Address addr, Byte value) {
    mapper_->write_chr(addr, value);
}

} // namespace mapperbus::core
