#include "core/cartridge/rom_crc32.hpp"

#include <array>

namespace mapperbus::core {

namespace {

// Standard CRC32 lookup table (polynomial 0xEDB88320, reflected)
constexpr std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc32Table = make_crc32_table();

} // namespace

uint32_t crc32(std::span<const Byte> data) {
    uint32_t crc = 0xFFFFFFFF;
    for (Byte byte : data) {
        crc = kCrc32Table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t rom_crc32(std::span<const Byte> rom_data) {
    constexpr std::size_t kHeaderSize = 16;
    if (rom_data.size() <= kHeaderSize) {
        return 0;
    }
    return crc32(rom_data.subspan(kHeaderSize));
}

} // namespace mapperbus::core
