#pragma once

namespace mapperbus::frontend::shaders {

// xBRZ upscaling compute shader in HLSL.
// Each thread processes one OUTPUT pixel. The shader reads a 3x3 kernel
// from the source texture, detects edges via weighted color distance,
// and blends the output pixel with its diagonal neighbor when an edge
// is detected.
//
// Uniform buffer layout:
//   int scale_factor   (offset 0)
//   int src_width      (offset 4)
//   int src_height     (offset 8)
//
// Bindings:
//   texture(t0): source texture (read)
//   texture(u0): destination texture (read_write)
//   buffer(b0):  uniform params

// clang-format off
inline constexpr const char* kXbrzComputeMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

// Perceptual color distance (weighted RGB, Compuphase redmean metric)
float color_dist(float4 a, float4 b) {
    float dr = a.r - b.r;
    float dg = a.g - b.g;
    float db = a.b - b.b;
    float rmean = (a.r + b.r) * 0.5;
    float wr = (rmean >= 0.5) ? 3.0 : 2.0;
    float wg = 4.0;
    float wb = (rmean >= 0.5) ? 2.0 : 3.0;
    return sqrt(wr * dr * dr + wg * dg * dg + wb * db * db);
}

float4 blend_3_1(float4 a, float4 b) {
    return mix(b, a, 0.75);
}

float4 blend_7_1(float4 a, float4 b) {
    return mix(b, a, 0.875);
}

float4 blend_5_3(float4 a, float4 b) {
    return mix(b, a, 0.625);
}

// Clamp-read from source texture
float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

// Edge detection: returns blend type for one corner
// 0 = none, 1 = normal, 2 = dominant
int detect_edge(float4 center, float4 diag, float4 ortho1, float4 ortho2,
                float4 ext1, float4 ext2, float4 ext3, float4 ext4) {
    float tolerance = 30.0 / 255.0;
    float dominant_threshold = 3.6;

    if (color_dist(center, diag) < tolerance) return 0;

    float diag_grad = color_dist(center, diag) + color_dist(ext1, ext2);
    float ortho_grad = color_dist(ortho1, ortho2) + color_dist(ext3, ext4);

    if (diag_grad < ortho_grad) return 0;
    if (diag_grad > ortho_grad * dominant_threshold) return 2;
    return 1;
}

kernel void xbrz_upscale(
    texture2d<float, access::read> src [[texture(0)]],
    texture2d<float, access::write> dst [[texture(1)]],
    constant Params& params [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int scale = params.scale_factor;
    int sw = params.src_width;
    int sh = params.src_height;
    int dw = sw * scale;
    int dh = sh * scale;

    if (int(gid.x) >= dw || int(gid.y) >= dh) return;

    // Which source pixel and sub-pixel offset
    int sx = int(gid.x) / scale;
    int sy = int(gid.y) / scale;
    int sub_x = int(gid.x) % scale;
    int sub_y = int(gid.y) % scale;

    // Read 3x3 kernel
    float4 D = sample_src(src, sx-1, sy-1, sw, sh);
    float4 E = sample_src(src, sx,   sy-1, sw, sh);
    float4 F = sample_src(src, sx+1, sy-1, sw, sh);
    float4 G = sample_src(src, sx-1, sy,   sw, sh);
    float4 H = sample_src(src, sx,   sy,   sw, sh); // center
    float4 I = sample_src(src, sx+1, sy,   sw, sh);
    float4 J = sample_src(src, sx-1, sy+1, sw, sh);
    float4 K = sample_src(src, sx,   sy+1, sw, sh);
    float4 L = sample_src(src, sx+1, sy+1, sw, sh);

    // Detect edges for each corner
    int bl_tl = detect_edge(H, D, G, E, D, E, F, I);
    int bl_tr = detect_edge(H, F, E, I, D, I, L, G);
    int bl_bl = detect_edge(H, J, G, K, E, K, I, F);
    int bl_br = detect_edge(H, L, I, K, F, K, G, E);

    float4 result = H;

    // Determine which corner this sub-pixel belongs to
    float mid = float(scale) * 0.5;
    float px = float(sub_x);
    float py = float(sub_y);
    float steep_threshold = 2.2;

    // Top-left quadrant
    if (px < mid && py < mid && bl_tl > 0) {
        float d = color_dist(H, D);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, E);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            // Corner pixel gets strongest blend
            if (sub_x == 0 && sub_y == 0) {
                result = blend_3_1(H, D);
            } else if ((sub_x == 0 || sub_y == 0) && bl_tl == 2) {
                result = blend_7_1(H, D);
            }
        }
    }
    // Top-right quadrant
    if (px >= mid && py < mid && bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == scale-1 && sub_y == 0) {
                result = blend_3_1(H, F);
            } else if ((sub_x == scale-1 || sub_y == 0) && bl_tr == 2) {
                result = blend_7_1(H, F);
            }
        }
    }
    // Bottom-left quadrant
    if (px < mid && py >= mid && bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == 0 && sub_y == scale-1) {
                result = blend_3_1(H, J);
            } else if ((sub_x == 0 || sub_y == scale-1) && bl_bl == 2) {
                result = blend_7_1(H, J);
            }
        }
    }
    // Bottom-right quadrant
    if (px >= mid && py >= mid && bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == scale-1 && sub_y == scale-1) {
                result = blend_3_1(H, L);
            } else if ((sub_x == scale-1 || sub_y == scale-1) && bl_br == 2) {
                result = blend_7_1(H, L);
            }
        }
    }

    dst.write(result, gid);
}
)MSL";

inline constexpr const char* kXbrzComputeHlsl = R"HLSL(
cbuffer Params : register(b0) {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

// Perceptual color distance (weighted RGB, Compuphase redmean metric)
float color_dist(float4 a, float4 b) {
    float dr = a.r - b.r;
    float dg = a.g - b.g;
    float db = a.b - b.b;
    float rmean = (a.r + b.r) * 0.5;
    float wr = (rmean >= 0.5) ? 3.0 : 2.0;
    float wg = 4.0;
    float wb = (rmean >= 0.5) ? 2.0 : 3.0;
    return sqrt(wr * dr * dr + wg * dg * dg + wb * db * db);
}

float4 blend_3_1(float4 a, float4 b) {
    return lerp(b, a, 0.75);
}

float4 blend_7_1(float4 a, float4 b) {
    return lerp(b, a, 0.875);
}

float4 blend_5_3(float4 a, float4 b) {
    return lerp(b, a, 0.625);
}

// Clamp-read from source texture
float4 sample_src(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.Load(int3(x, y, 0));
}

// Edge detection: returns blend type for one corner
// 0 = none, 1 = normal, 2 = dominant
int detect_edge(float4 center, float4 diag, float4 ortho1, float4 ortho2,
                float4 ext1, float4 ext2, float4 ext3, float4 ext4) {
    float tolerance = 30.0 / 255.0;
    float dominant_threshold = 3.6;

    if (color_dist(center, diag) < tolerance) return 0;

    float diag_grad = color_dist(center, diag) + color_dist(ext1, ext2);
    float ortho_grad = color_dist(ortho1, ortho2) + color_dist(ext3, ext4);

    if (diag_grad < ortho_grad) return 0;
    if (diag_grad > ortho_grad * dominant_threshold) return 2;
    return 1;
}

[numthreads(16, 16, 1)]
void xbrz_upscale(uint3 gid : SV_DispatchThreadID) {
    int scale = scale_factor;
    int sw = src_width;
    int sh = src_height;
    int dw = sw * scale;
    int dh = sh * scale;

    if ((int)gid.x >= dw || (int)gid.y >= dh) return;

    // Which source pixel and sub-pixel offset
    int sx = (int)gid.x / scale;
    int sy = (int)gid.y / scale;
    int sub_x = (int)gid.x % scale;
    int sub_y = (int)gid.y % scale;

    // Read 3x3 kernel
    float4 D = sample_src(sx-1, sy-1, sw, sh);
    float4 E = sample_src(sx,   sy-1, sw, sh);
    float4 F = sample_src(sx+1, sy-1, sw, sh);
    float4 G = sample_src(sx-1, sy,   sw, sh);
    float4 H = sample_src(sx,   sy,   sw, sh); // center
    float4 I = sample_src(sx+1, sy,   sw, sh);
    float4 J = sample_src(sx-1, sy+1, sw, sh);
    float4 K = sample_src(sx,   sy+1, sw, sh);
    float4 L = sample_src(sx+1, sy+1, sw, sh);

    // Detect edges for each corner
    int bl_tl = detect_edge(H, D, G, E, D, E, F, I);
    int bl_tr = detect_edge(H, F, E, I, D, I, L, G);
    int bl_bl = detect_edge(H, J, G, K, E, K, I, F);
    int bl_br = detect_edge(H, L, I, K, F, K, G, E);

    float4 result = H;

    // Determine which corner this sub-pixel belongs to
    float mid = (float)scale * 0.5;
    float px = (float)sub_x;
    float py = (float)sub_y;
    float steep_threshold = 2.2;

    // Top-left quadrant
    if (px < mid && py < mid && bl_tl > 0) {
        float d = color_dist(H, D);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, E);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            // Corner pixel gets strongest blend
            if (sub_x == 0 && sub_y == 0) {
                result = blend_3_1(H, D);
            } else if ((sub_x == 0 || sub_y == 0) && bl_tl == 2) {
                result = blend_7_1(H, D);
            }
        }
    }
    // Top-right quadrant
    if (px >= mid && py < mid && bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == scale-1 && sub_y == 0) {
                result = blend_3_1(H, F);
            } else if ((sub_x == scale-1 || sub_y == 0) && bl_tr == 2) {
                result = blend_7_1(H, F);
            }
        }
    }
    // Bottom-left quadrant
    if (px < mid && py >= mid && bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == 0 && sub_y == scale-1) {
                result = blend_3_1(H, J);
            } else if ((sub_x == 0 || sub_y == scale-1) && bl_bl == 2) {
                result = blend_7_1(H, J);
            }
        }
    }
    // Bottom-right quadrant
    if (px >= mid && py >= mid && bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        if (d > 30.0/255.0 && (dv < d * steep_threshold || dh_val < d * steep_threshold)) {
            if (sub_x == scale-1 && sub_y == scale-1) {
                result = blend_3_1(H, L);
            } else if ((sub_x == scale-1 || sub_y == scale-1) && bl_br == 2) {
                result = blend_7_1(H, L);
            }
        }
    }

    dst[gid.xy] = result;
}
)HLSL";
// clang-format on

} // namespace mapperbus::frontend::shaders
