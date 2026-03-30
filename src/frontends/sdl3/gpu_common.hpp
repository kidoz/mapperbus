#pragma once

#include <cstdint>
#include <span>

namespace mapperbus::frontend {

struct GpuParams {
    int32_t scale_factor;
    int32_t src_width;
    int32_t src_height;
    int32_t padding; // align to 16 bytes
};

// ARGB (0xAARRGGBB) -> R8G8B8A8_UNORM (byte 0=R, 1=G, 2=B, 3=A)
inline void argb_to_rgba(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint32_t a = (c >> 24) & 0xFF;
        uint32_t r = (c >> 16) & 0xFF;
        uint32_t g = (c >> 8) & 0xFF;
        uint32_t b = c & 0xFF;
        dst[i] = r | (g << 8) | (b << 16) | (a << 24);
    }
}

// R8G8B8A8_UNORM (byte 0=R, 1=G, 2=B, 3=A) -> ARGB (0xAARRGGBB)
inline void rgba_to_argb(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint32_t r = c & 0xFF;
        uint32_t g = (c >> 8) & 0xFF;
        uint32_t b = (c >> 16) & 0xFF;
        uint32_t a = (c >> 24) & 0xFF;
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

} // namespace mapperbus::frontend
