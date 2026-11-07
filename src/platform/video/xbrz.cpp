#include "platform/video/xbrz.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

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

// --- Perceptual color distance (weighted RGB, redmean approximation) ---
// Gradients add distances and compare their ratios, so squared distances
// would change edge decisions as well as the units of the color tolerance.

double color_distance(uint32_t a, uint32_t b) {
    if (a == b)
        return 0;
    int dr = static_cast<int>(red(a)) - static_cast<int>(red(b));
    int dg = static_cast<int>(green(a)) - static_cast<int>(green(b));
    int db = static_cast<int>(blue(a)) - static_cast<int>(blue(b));
    int rmean = (static_cast<int>(red(a)) + static_cast<int>(red(b))) / 2;

    // Weighted squared distance (redmean): the weights depend on whether
    // the mean red channel is above or below 128.
    int wr = (rmean >= 128) ? 3 : 2;
    int wg = 4;
    int wb = (rmean >= 128) ? 2 : 3;
    return std::sqrt(static_cast<double>(wr * dr * dr + wg * dg * dg + wb * db * db));
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

// Corner contributions can overlap at 3x and above. Accumulate their exact
// eighths before rounding so neither corner order nor rotation changes a pixel.
class BlendBlock {
  public:
    void add(int offset, uint32_t color, int weight) {
        auto& pixel = pixels_[offset];
        pixel.r += weight * red(color);
        pixel.g += weight * green(color);
        pixel.b += weight * blue(color);
        pixel.weight += weight;
    }

    void write(uint32_t* dst, int stride, int base, int scale, uint32_t center) const {
        for (int y = 0; y < scale; ++y) {
            for (int x = 0; x < scale; ++x) {
                const auto& pixel = pixels_[y * scale + x];
                uint32_t result = center;
                if (pixel.weight != 0) {
                    // The current stencils contribute at most 4/8 to a pixel.
                    const int remaining = 8 - pixel.weight;
                    const auto r = static_cast<uint32_t>((remaining * red(center) + pixel.r) / 8);
                    const auto g = static_cast<uint32_t>((remaining * green(center) + pixel.g) / 8);
                    const auto b = static_cast<uint32_t>((remaining * blue(center) + pixel.b) / 8);
                    result = 0xFF000000U | (r << 16U) | (g << 8U) | b;
                }
                dst[base + y * stride + x] = result;
            }
        }
    }

  private:
    struct Pixel {
        int r = 0;
        int g = 0;
        int b = 0;
        int weight = 0;
    };
    std::array<Pixel, 36> pixels_{}; // Maximum 6x6 output block.
};

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

Kernel read_kernel(const uint32_t* src, int w, int h, int x, int y) {
    // Clamp each row/column once rather than repeating both clamps for all
    // nine samples. These pointers stay within the source image, including
    // one-pixel-wide or one-pixel-high inputs.
    const int left = std::max(x - 1, 0);
    const int right = std::min(x + 1, w - 1);
    const auto stride = static_cast<std::ptrdiff_t>(w);
    const auto* top = src + std::max(y - 1, 0) * stride;
    const auto* middle = src + y * stride;
    const auto* bottom = src + std::min(y + 1, h - 1) * stride;
    return {
        .D = top[left],
        .E = top[x],
        .F = top[right],
        .G = middle[left],
        .H = middle[x],
        .I = middle[right],
        .J = bottom[left],
        .K = bottom[x],
        .L = bottom[right],
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

    // Rotate the entire top-left neighborhood clockwise for each corner,
    // including both context pairs used in the gradient comparison.
    BlendType tl = corner_blend(k.H, k.D, k.G, k.E, k.D, k.E, k.F, k.I);
    BlendType tr = corner_blend(k.H, k.F, k.E, k.I, k.F, k.I, k.L, k.K);
    BlendType br = corner_blend(k.H, k.L, k.I, k.K, k.L, k.K, k.J, k.G);
    BlendType bl = corner_blend(k.H, k.J, k.K, k.G, k.J, k.G, k.D, k.E);

    return pack_blend(tl, tr, bl, br);
}

// --- Scale pixel: produce NxN output pixels from one source pixel ---

void scale_pixel_2x(uint32_t* dst,
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

void scale_pixel_3x(uint32_t* dst,
                    int dst_w,
                    int dx,
                    int dy,
                    uint32_t center,
                    const Kernel& k,
                    uint8_t blend_info,
                    const XbrzConfig& cfg) {
    int base = dy * 3 * dst_w + dx * 3;

    BlendBlock blends;

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

        blends.add(corner_offset, diag, 2);
        if (steep && bt == BlendType::Dominant) {
            blends.add(edge_v_offset, diag, 1);
        }
        if (shallow && bt == BlendType::Dominant) {
            blends.add(edge_h_offset, diag, 1);
        }
    };

    // TL corner
    blend_corner(0, k.D, k.G, k.E, 3, 1, get_tl(blend_info));
    // TR corner
    blend_corner(2, k.F, k.I, k.E, 5, 1, get_tr(blend_info));
    // BL corner
    blend_corner(6, k.J, k.G, k.K, 3, 7, get_bl(blend_info));
    // BR corner
    blend_corner(8, k.L, k.I, k.K, 5, 7, get_br(blend_info));
    blends.write(dst, dst_w, base, 3, center);
}

void scale_pixel_4x(uint32_t* dst,
                    int dst_w,
                    int dx,
                    int dy,
                    uint32_t center,
                    const Kernel& k,
                    uint8_t blend_info,
                    const XbrzConfig& cfg) {
    int base = dy * 4 * dst_w + dx * 4;

    BlendBlock blends;

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

        blends.add(c0, diag, 2);

        double dv = color_distance(center, ortho_v);
        double dh = color_distance(center, ortho_h);
        bool steep = dv < d * cfg.steep_direction_threshold;
        bool shallow = dh < d * cfg.steep_direction_threshold;

        if (steep || bt == BlendType::Dominant) {
            blends.add(c1, diag, 3);
            blends.add(e1, diag, 1);
        }
        if (shallow || bt == BlendType::Dominant) {
            blends.add(c2, diag, 3);
            blends.add(e2, diag, 1);
        }
    };

    // TL
    blend_corner_4(0, 4, 1, 8, 2, k.D, k.G, k.E, get_tl(blend_info));
    // TR
    blend_corner_4(3, 7, 2, 11, 1, k.F, k.I, k.E, get_tr(blend_info));
    // BL
    blend_corner_4(12, 8, 13, 4, 14, k.J, k.G, k.K, get_bl(blend_info));
    // BR
    blend_corner_4(15, 11, 14, 7, 13, k.L, k.I, k.K, get_br(blend_info));
    blends.write(dst, dst_w, base, 4, center);
}

// Generic fallback for 5x and 6x (uses 4x logic with additional center fill)
void scale_pixel_generic(uint32_t* dst,
                         int dst_w,
                         int dx,
                         int dy,
                         int n,
                         uint32_t center,
                         const Kernel& k,
                         uint8_t blend_info,
                         const XbrzConfig& cfg) {
    int base = dy * n * dst_w + dx * n;

    BlendBlock blends;

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
        blends.add(corner_r * n + corner_c, diag, 2);

        // Adjacent edge pixels
        double dv = color_distance(center, ortho_v);
        double dh = color_distance(center, ortho_h);
        bool steep = dv < d * cfg.steep_direction_threshold;
        bool shallow = dh < d * cfg.steep_direction_threshold;

        if (steep || bt == BlendType::Dominant) {
            blends.add((corner_r + dr) * n + corner_c, diag, 3);
            if (corner_r + 2 * dr >= 0 && corner_r + 2 * dr < n) {
                blends.add((corner_r + 2 * dr) * n + corner_c, diag, 1);
            }
        }
        if (shallow || bt == BlendType::Dominant) {
            blends.add(corner_r * n + corner_c + dc, diag, 3);
            if (corner_c + 2 * dc >= 0 && corner_c + 2 * dc < n) {
                blends.add(corner_r * n + corner_c + 2 * dc, diag, 1);
            }
        }
    };

    blend_generic(0, 0, 1, 1, k.D, k.G, k.E, get_tl(blend_info));
    blend_generic(0, n - 1, 1, -1, k.F, k.I, k.E, get_tr(blend_info));
    blend_generic(n - 1, 0, -1, 1, k.J, k.G, k.K, get_bl(blend_info));
    blend_generic(n - 1, n - 1, -1, -1, k.L, k.I, k.K, get_br(blend_info));
    blends.write(dst, dst_w, base, n, center);
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
    if (scale_factor < 2 || scale_factor > 6) {
        return;
    }
    int dst_w = src_width * scale_factor;
    const auto* input = source.data();
    auto* output = target.data();

    for (int y = 0; y < src_height; ++y) {
        for (int x = 0; x < src_width; ++x) {
            auto k = read_kernel(input, src_width, src_height, x, y);
            uint8_t blend_info = preprocess_pixel(k, config);
            uint32_t center = k.H;

            switch (scale_factor) {
            case 2:
                scale_pixel_2x(output, dst_w, x, y, center, k, blend_info, config);
                break;
            case 3:
                scale_pixel_3x(output, dst_w, x, y, center, k, blend_info, config);
                break;
            case 4:
                scale_pixel_4x(output, dst_w, x, y, center, k, blend_info, config);
                break;
            default:
                scale_pixel_generic(
                    output, dst_w, x, y, scale_factor, center, k, blend_info, config);
                break;
            }
        }
    }
}

} // namespace mapperbus::platform
