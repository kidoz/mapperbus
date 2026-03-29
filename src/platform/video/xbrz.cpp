#include "platform/video/xbrz.hpp"

#include <algorithm>
#include <cmath>

namespace mapperbus::platform {

namespace {

// --- Color extraction ---

constexpr uint8_t red(uint32_t c) {
    return static_cast<uint8_t>((c >> 16) & 0xFF);
}
constexpr uint8_t green(uint32_t c) {
    return static_cast<uint8_t>((c >> 8) & 0xFF);
}
constexpr uint8_t blue(uint32_t c) {
    return static_cast<uint8_t>(c & 0xFF);
}

// --- Perceptual color distance (weighted RGB, Compuphase metric) ---

constexpr double color_distance(uint32_t a, uint32_t b) {
    if (a == b)
        return 0.0;
    int dr = static_cast<int>(red(a)) - static_cast<int>(red(b));
    int dg = static_cast<int>(green(a)) - static_cast<int>(green(b));
    int db = static_cast<int>(blue(a)) - static_cast<int>(blue(b));
    int rmean = (static_cast<int>(red(a)) + static_cast<int>(red(b))) / 2;

    // Weighted Euclidean distance in RGB space (redmean approximation)
    double wr = (rmean >= 128) ? 3.0 : 2.0;
    double wg = 4.0;
    double wb = (rmean >= 128) ? 2.0 : 3.0;
    return std::sqrt(wr * dr * dr + wg * dg * dg + wb * db * db);
}

// --- Pixel blending ---

constexpr uint32_t blend(uint32_t a, uint32_t b, int weight_a, int weight_total) {
    int wa = weight_a;
    int wb = weight_total - weight_a;
    auto mix = [wa, wb, weight_total](uint8_t ca, uint8_t cb) -> uint8_t {
        return static_cast<uint8_t>((ca * wa + cb * wb) / weight_total);
    };
    return (0xFF000000) | (mix(red(a), red(b)) << 16) | (mix(green(a), green(b)) << 8) |
           mix(blue(a), blue(b));
}

constexpr uint32_t blend_3_1(uint32_t a, uint32_t b) {
    return blend(a, b, 3, 4);
}
constexpr uint32_t blend_7_1(uint32_t a, uint32_t b) {
    return blend(a, b, 7, 8);
}
constexpr uint32_t blend_5_3(uint32_t a, uint32_t b) {
    return blend(a, b, 5, 8);
}

// --- Blend types ---
enum class BlendType : uint8_t {
    None = 0,
    Normal = 1,
    Dominant = 2,
};

// Pack 4 blend types into one byte (one per corner: TL, TR, BL, BR)
constexpr uint8_t pack_blend(BlendType tl, BlendType tr, BlendType bl, BlendType br) {
    return static_cast<uint8_t>(static_cast<uint8_t>(tl) | (static_cast<uint8_t>(tr) << 2) |
                                (static_cast<uint8_t>(bl) << 4) | (static_cast<uint8_t>(br) << 6));
}

constexpr BlendType get_tl(uint8_t packed) {
    return static_cast<BlendType>(packed & 0x03);
}
constexpr BlendType get_tr(uint8_t packed) {
    return static_cast<BlendType>((packed >> 2) & 0x03);
}
constexpr BlendType get_bl(uint8_t packed) {
    return static_cast<BlendType>((packed >> 4) & 0x03);
}
constexpr BlendType get_br(uint8_t packed) {
    return static_cast<BlendType>((packed >> 6) & 0x03);
}

// --- 3x3 kernel with extended border (for edge detection) ---
//
//   a b
// c D E F d
// e G H I f
// g J K L h
//   i j
//
// We analyze the center 3x3 (D-L) with the extended border providing context.

// 3x3 kernel centered on pixel H, with extended edge pixels for context
struct Kernel {
    uint32_t D, E, F; // Top row
    uint32_t G, H, I; // Middle row (H = center)
    uint32_t J, K, L; // Bottom row
};

uint32_t sample(std::span<const uint32_t> src, int w, int h, int x, int y) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return src[y * w + x];
}

Kernel read_kernel(std::span<const uint32_t> src, int w, int h, int x, int y) {
    return {
        .D = sample(src, w, h, x - 1, y - 1),
        .E = sample(src, w, h, x, y - 1),
        .F = sample(src, w, h, x + 1, y - 1),
        .G = sample(src, w, h, x - 1, y),
        .H = sample(src, w, h, x, y),
        .I = sample(src, w, h, x + 1, y),
        .J = sample(src, w, h, x - 1, y + 1),
        .K = sample(src, w, h, x, y + 1),
        .L = sample(src, w, h, x + 1, y + 1),
    };
}

// --- Preprocessing: detect edges and determine blend types ---

uint8_t preprocess_pixel(const Kernel& k, const XbrzConfig& cfg) {
    // Compare diagonal gradients for each corner
    auto corner_blend = [&](uint32_t center,
                            uint32_t diag,
                            uint32_t ortho1,
                            uint32_t ortho2,
                            uint32_t ext1,
                            uint32_t ext2,
                            uint32_t ext3,
                            uint32_t ext4) -> BlendType {
        if (color_distance(center, diag) < cfg.equal_color_tolerance) {
            return BlendType::None;
        }

        // Gradient along one diagonal vs the other
        double diag_grad = color_distance(center, diag);
        double ortho_grad = color_distance(ortho1, ortho2);

        // Check extended pixels for additional context
        double ext_diag = color_distance(ext1, ext2);
        double ext_ortho = color_distance(ext3, ext4);

        double total_diag = diag_grad + ext_diag;
        double total_ortho = ortho_grad + ext_ortho;

        if (total_diag < total_ortho) {
            return BlendType::None;
        }

        if (total_diag > total_ortho * cfg.dominant_direction_threshold) {
            return BlendType::Dominant;
        }

        return BlendType::Normal;
    };

    // Bottom-right corner: H vs L
    BlendType br = corner_blend(k.H, k.L, k.I, k.K, k.F, k.K, k.G, k.E);

    // Bottom-left corner: H vs J
    BlendType bl = corner_blend(k.H, k.J, k.G, k.K, k.E, k.K, k.I, k.F);

    // Top-right corner: H vs F
    BlendType tr = corner_blend(k.H, k.F, k.E, k.I, k.D, k.I, k.L, k.G);

    // Top-left corner: H vs D
    BlendType tl = corner_blend(k.H, k.D, k.G, k.E, k.D, k.E, k.F, k.I);

    return pack_blend(tl, tr, bl, br);
}

// --- Scale pixel: produce NxN output pixels from one source pixel ---

void scale_pixel_2x(std::span<uint32_t> dst,
                    int dst_w,
                    int dx,
                    int dy,
                    uint32_t center,
                    const Kernel& k,
                    uint8_t blend_info,
                    const XbrzConfig& cfg) {
    int base = dy * 2 * dst_w + dx * 2;

    // Start with center color
    dst[base] = center;
    dst[base + 1] = center;
    dst[base + dst_w] = center;
    dst[base + dst_w + 1] = center;

    // Apply blending per corner
    if (get_tl(blend_info) != BlendType::None) {
        double d_diag = color_distance(center, k.D);
        double d_vert = color_distance(center, k.G);
        double d_horiz = color_distance(center, k.E);
        if (d_diag > cfg.equal_color_tolerance &&
            (d_vert < d_diag * cfg.steep_direction_threshold ||
             d_horiz < d_diag * cfg.steep_direction_threshold)) {
            dst[base] = blend_3_1(center, k.D);
        }
    }
    if (get_tr(blend_info) != BlendType::None) {
        double d_diag = color_distance(center, k.F);
        double d_vert = color_distance(center, k.I);
        double d_horiz = color_distance(center, k.E);
        if (d_diag > cfg.equal_color_tolerance &&
            (d_vert < d_diag * cfg.steep_direction_threshold ||
             d_horiz < d_diag * cfg.steep_direction_threshold)) {
            dst[base + 1] = blend_3_1(center, k.F);
        }
    }
    if (get_bl(blend_info) != BlendType::None) {
        double d_diag = color_distance(center, k.J);
        double d_vert = color_distance(center, k.G);
        double d_horiz = color_distance(center, k.K);
        if (d_diag > cfg.equal_color_tolerance &&
            (d_vert < d_diag * cfg.steep_direction_threshold ||
             d_horiz < d_diag * cfg.steep_direction_threshold)) {
            dst[base + dst_w] = blend_3_1(center, k.J);
        }
    }
    if (get_br(blend_info) != BlendType::None) {
        double d_diag = color_distance(center, k.L);
        double d_vert = color_distance(center, k.I);
        double d_horiz = color_distance(center, k.K);
        if (d_diag > cfg.equal_color_tolerance &&
            (d_vert < d_diag * cfg.steep_direction_threshold ||
             d_horiz < d_diag * cfg.steep_direction_threshold)) {
            dst[base + dst_w + 1] = blend_3_1(center, k.L);
        }
    }
}

void scale_pixel_3x(std::span<uint32_t> dst,
                    int dst_w,
                    int dx,
                    int dy,
                    uint32_t center,
                    const Kernel& k,
                    uint8_t blend_info,
                    const XbrzConfig& cfg) {
    int base = dy * 3 * dst_w + dx * 3;

    // Fill 3x3 with center
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            dst[base + row * dst_w + col] = center;
        }
    }

    // Corner blending for 3x3
    auto blend_corner = [&](int corner_offset,
                            uint32_t diag,
                            uint32_t ortho_v,
                            uint32_t ortho_h,
                            int edge_v_offset,
                            int edge_h_offset,
                            BlendType bt) {
        if (bt == BlendType::None)
            return;
        double d = color_distance(center, diag);
        if (d < cfg.equal_color_tolerance)
            return;

        double dv = color_distance(center, ortho_v);
        double dh = color_distance(center, ortho_h);
        bool steep = dv < d * cfg.steep_direction_threshold;
        bool shallow = dh < d * cfg.steep_direction_threshold;

        dst[base + corner_offset] = blend_3_1(center, diag);
        if (steep && bt == BlendType::Dominant) {
            dst[base + edge_v_offset] = blend_7_1(center, diag);
        }
        if (shallow && bt == BlendType::Dominant) {
            dst[base + edge_h_offset] = blend_7_1(center, diag);
        }
    };

    // TL corner
    blend_corner(0, k.D, k.G, k.E, dst_w, 1, get_tl(blend_info));
    // TR corner
    blend_corner(2, k.F, k.I, k.E, dst_w + 2, 1, get_tr(blend_info));
    // BL corner
    blend_corner(2 * dst_w, k.J, k.G, k.K, dst_w, 2 * dst_w + 1, get_bl(blend_info));
    // BR corner
    blend_corner(2 * dst_w + 2, k.L, k.I, k.K, dst_w + 2, 2 * dst_w + 1, get_br(blend_info));
}

void scale_pixel_4x(std::span<uint32_t> dst,
                    int dst_w,
                    int dx,
                    int dy,
                    uint32_t center,
                    const Kernel& k,
                    uint8_t blend_info,
                    const XbrzConfig& cfg) {
    int base = dy * 4 * dst_w + dx * 4;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            dst[base + row * dst_w + col] = center;
        }
    }

    auto blend_corner_4 = [&](int c0,
                              int c1,
                              int c2,
                              int e1,
                              int e2,
                              uint32_t diag,
                              uint32_t ortho_v,
                              uint32_t ortho_h,
                              BlendType bt) {
        if (bt == BlendType::None)
            return;
        double d = color_distance(center, diag);
        if (d < cfg.equal_color_tolerance)
            return;

        dst[base + c0] = blend_3_1(center, diag);

        double dv = color_distance(center, ortho_v);
        double dh = color_distance(center, ortho_h);
        bool steep = dv < d * cfg.steep_direction_threshold;
        bool shallow = dh < d * cfg.steep_direction_threshold;

        if (steep || bt == BlendType::Dominant) {
            dst[base + c1] = blend_5_3(center, diag);
            dst[base + e1] = blend_7_1(center, diag);
        }
        if (shallow || bt == BlendType::Dominant) {
            dst[base + c2] = blend_5_3(center, diag);
            dst[base + e2] = blend_7_1(center, diag);
        }
    };

    // TL
    blend_corner_4(0, dst_w, 1, 2 * dst_w, 2, k.D, k.G, k.E, get_tl(blend_info));
    // TR
    blend_corner_4(3, dst_w + 3, 2, 2 * dst_w + 3, 1, k.F, k.I, k.E, get_tr(blend_info));
    // BL
    blend_corner_4(3 * dst_w,
                   2 * dst_w,
                   3 * dst_w + 1,
                   dst_w,
                   3 * dst_w + 2,
                   k.J,
                   k.G,
                   k.K,
                   get_bl(blend_info));
    // BR
    blend_corner_4(3 * dst_w + 3,
                   2 * dst_w + 3,
                   3 * dst_w + 2,
                   dst_w + 3,
                   3 * dst_w + 1,
                   k.L,
                   k.I,
                   k.K,
                   get_br(blend_info));
}

// Generic fallback for 5x and 6x (uses 4x logic with additional center fill)
void scale_pixel_generic(std::span<uint32_t> dst,
                         int dst_w,
                         int dx,
                         int dy,
                         int n,
                         uint32_t center,
                         const Kernel& k,
                         uint8_t blend_info,
                         const XbrzConfig& cfg) {
    int base = dy * n * dst_w + dx * n;

    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            dst[base + row * dst_w + col] = center;
        }
    }

    // Corner blending (adaptive: blend more pixels for larger scales)
    auto blend_generic = [&](int corner_r,
                             int corner_c,
                             int dr,
                             int dc,
                             uint32_t diag,
                             uint32_t ortho_v,
                             uint32_t ortho_h,
                             BlendType bt) {
        if (bt == BlendType::None)
            return;
        double d = color_distance(center, diag);
        if (d < cfg.equal_color_tolerance)
            return;

        // Corner pixel
        dst[base + corner_r * dst_w + corner_c] = blend_3_1(center, diag);

        // Adjacent edge pixels
        double dv = color_distance(center, ortho_v);
        double dh = color_distance(center, ortho_h);
        bool steep = dv < d * cfg.steep_direction_threshold;
        bool shallow = dh < d * cfg.steep_direction_threshold;

        if (steep || bt == BlendType::Dominant) {
            dst[base + (corner_r + dr) * dst_w + corner_c] = blend_5_3(center, diag);
            if (corner_r + 2 * dr >= 0 && corner_r + 2 * dr < n) {
                dst[base + (corner_r + 2 * dr) * dst_w + corner_c] = blend_7_1(center, diag);
            }
        }
        if (shallow || bt == BlendType::Dominant) {
            dst[base + corner_r * dst_w + corner_c + dc] = blend_5_3(center, diag);
            if (corner_c + 2 * dc >= 0 && corner_c + 2 * dc < n) {
                dst[base + corner_r * dst_w + corner_c + 2 * dc] = blend_7_1(center, diag);
            }
        }
    };

    blend_generic(0, 0, 1, 1, k.D, k.G, k.E, get_tl(blend_info));
    blend_generic(0, n - 1, 1, -1, k.F, k.I, k.E, get_tr(blend_info));
    blend_generic(n - 1, 0, -1, 1, k.J, k.G, k.K, get_bl(blend_info));
    blend_generic(n - 1, n - 1, -1, -1, k.L, k.I, k.K, get_br(blend_info));
}

} // namespace

// --- Public API ---

XbrzUpscaler::XbrzUpscaler(int scale, XbrzConfig config) : scale_(scale), config_(config) {
    if (scale_ < 2)
        scale_ = 2;
    if (scale_ > 6)
        scale_ = 6;
}

void XbrzUpscaler::scale(std::span<const uint32_t> source,
                         int src_width,
                         int src_height,
                         std::span<uint32_t> target) {
    xbrz_scale(scale_, source, src_width, src_height, target, config_);
}

void xbrz_scale(int scale_factor,
                std::span<const uint32_t> source,
                int src_width,
                int src_height,
                std::span<uint32_t> target,
                const XbrzConfig& config) {
    int dst_w = src_width * scale_factor;

    for (int y = 0; y < src_height; ++y) {
        for (int x = 0; x < src_width; ++x) {
            auto k = read_kernel(source, src_width, src_height, x, y);
            uint8_t blend_info = preprocess_pixel(k, config);
            uint32_t center = k.H;

            switch (scale_factor) {
            case 2:
                scale_pixel_2x(target, dst_w, x, y, center, k, blend_info, config);
                break;
            case 3:
                scale_pixel_3x(target, dst_w, x, y, center, k, blend_info, config);
                break;
            case 4:
                scale_pixel_4x(target, dst_w, x, y, center, k, blend_info, config);
                break;
            default:
                scale_pixel_generic(
                    target, dst_w, x, y, scale_factor, center, k, blend_info, config);
                break;
            }
        }
    }
}

} // namespace mapperbus::platform
