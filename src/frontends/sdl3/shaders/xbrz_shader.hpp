#pragma once

namespace mapperbus::frontend::shaders {

// Two-pass xBRZ upscaling. The preprocessing kernel stores four 2-bit blend
// types in R and four active-corner flags in G. RGBA8 UNORM preserves these
// integer values when the output kernel multiplies by 255 and rounds.

// clang-format off
inline constexpr const char* kXbrzPreprocessMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

float color_dist(float4 a, float4 b) {
    // Recover byte channels before measuring distance. Integer squared terms
    // keep equal gradients equal even when the compiler contracts float math.
    int3 ca = int3(round(a.rgb * 255.0));
    int3 cb = int3(round(b.rgb * 255.0));
    int3 delta = ca - cb;
    int wr = (ca.r + cb.r >= 256) ? 3 : 2;
    int wb = (ca.r + cb.r >= 256) ? 2 : 3;
    int distance_squared = wr * delta.r * delta.r + 4 * delta.g * delta.g + wb * delta.b * delta.b;
    return precise::sqrt(float(distance_squared));
}

float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

int detect_edge(float4 center, float4 diag, float4 ortho1, float4 ortho2,
                float4 ext1, float4 ext2, float4 ext3, float4 ext4) {
    float tolerance = 30.0;
    float dominant_threshold = 3.6;

    if (color_dist(center, diag) < tolerance) return 0;

    float diag_grad = color_dist(center, diag) + color_dist(ext1, ext2);
    float ortho_grad = color_dist(ortho1, ortho2) + color_dist(ext3, ext4);

    if (diag_grad < ortho_grad) return 0;
    if (diag_grad > ortho_grad * dominant_threshold) return 2;
    return 1;
}

kernel void xbrz_preprocess(
    texture2d<float, access::read> src [[texture(0)]],
    texture2d<float, access::write> blend_info [[texture(1)]],
    constant Params& params [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int sw = params.src_width;
    int sh = params.src_height;
    if (int(gid.x) >= sw || int(gid.y) >= sh) return;

    int sx = int(gid.x);
    int sy = int(gid.y);
    float4 D = sample_src(src, sx-1, sy-1, sw, sh);
    float4 E = sample_src(src, sx,   sy-1, sw, sh);
    float4 F = sample_src(src, sx+1, sy-1, sw, sh);
    float4 G = sample_src(src, sx-1, sy,   sw, sh);
    float4 H = sample_src(src, sx,   sy,   sw, sh);
    float4 I = sample_src(src, sx+1, sy,   sw, sh);
    float4 J = sample_src(src, sx-1, sy+1, sw, sh);
    float4 K = sample_src(src, sx,   sy+1, sw, sh);
    float4 L = sample_src(src, sx+1, sy+1, sw, sh);

    int bl_tl = detect_edge(H, D, G, E, D, E, F, I);
    int bl_tr = detect_edge(H, F, E, I, F, I, L, K);
    int bl_bl = detect_edge(H, J, K, G, J, G, D, E);
    int bl_br = detect_edge(H, L, I, K, L, K, J, G);
    float steep_threshold = 2.2;

    bool blend_tl = false;
    if (bl_tl > 0) {
        float d = color_dist(H, D);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, E);
        blend_tl = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_tr = false;
    if (bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        blend_tr = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_bl = false;
    if (bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        blend_bl = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_br = false;
    if (bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        blend_br = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }

    int blends = bl_tl | (bl_tr << 2) | (bl_bl << 4) | (bl_br << 6);
    int active = int(blend_tl) | (int(blend_tr) << 1) |
                 (int(blend_bl) << 2) | (int(blend_br) << 3);
    blend_info.write(float4(float(blends) / 255.0, float(active) / 255.0, 0.0, 1.0), gid);
}
)MSL";

inline constexpr const char* kXbrzComputeMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

// Evaluate the same footprint in coordinates reflected from each corner.
// Odd scales share their middle edge pixel between adjacent corners.
int corner_weight(int x, int y, int scale, int blend_type, bool enabled) {
    if (!enabled) return 0;
    if (x == 0 && y == 0) return 2;
    if ((x == 0 || y == 0) && 2 * x < scale && 2 * y < scale && blend_type == 2) return 1;
    return 0;
}

float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

kernel void xbrz_upscale(
    texture2d<float, access::read> src [[texture(0)]],
    texture2d<float, access::read> blend_info [[texture(1)]],
    texture2d<float, access::write> dst [[texture(2)]],
    constant Params& params [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int scale = params.scale_factor;
    int sw = params.src_width;
    int sh = params.src_height;
    int dw = sw * scale;
    int dh = sh * scale;
    if (int(gid.x) >= dw || int(gid.y) >= dh) return;

    int sx = int(gid.x) / scale;
    int sy = int(gid.y) / scale;
    int sub_x = int(gid.x) % scale;
    int sub_y = int(gid.y) % scale;
    float4 H = sample_src(src, sx, sy, sw, sh);
    float4 D = sample_src(src, sx-1, sy-1, sw, sh);
    float4 F = sample_src(src, sx+1, sy-1, sw, sh);
    float4 J = sample_src(src, sx-1, sy+1, sw, sh);
    float4 L = sample_src(src, sx+1, sy+1, sw, sh);
    float4 metadata = blend_info.read(uint2(sx, sy));
    int blends = int(round(metadata.r * 255.0));
    int active = int(round(metadata.g * 255.0));
    // Sum exact eighths before converting to UNORM. This also keeps rounding
    // independent of the order in which overlapping corners are visited.
    int4 center = int4(round(H * 255.0));
    int4 result = center * 8;
    result += corner_weight(sub_x, sub_y, scale, (blends & 3), (active & 1) != 0) *
              (int4(round(D * 255.0)) - center);
    result += corner_weight(scale - 1 - sub_x, sub_y,
                            scale, ((blends >> 2) & 3), (active & 2) != 0) *
              (int4(round(F * 255.0)) - center);
    result += corner_weight(sub_x, scale - 1 - sub_y,
                            scale, ((blends >> 4) & 3), (active & 4) != 0) *
              (int4(round(J * 255.0)) - center);
    result += corner_weight(scale - 1 - sub_x, scale - 1 - sub_y,
                            scale, ((blends >> 6) & 3), (active & 8) != 0) *
              (int4(round(L * 255.0)) - center);
    dst.write(float4(result) / (8.0 * 255.0), gid);
}
)MSL";

inline constexpr const char* kXbrzPreprocessHlsl = R"HLSL(
cbuffer Params : register(b0, space2) {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

Texture2D<float4> src : register(t0, space0);
RWTexture2D<float4> blend_info : register(u0, space1);

float color_dist(float4 a, float4 b) {
    // Recover byte channels before measuring distance. Integer squared terms
    // keep equal gradients equal even when the compiler contracts float math.
    int3 ca = (int3)round(a.rgb * 255.0);
    int3 cb = (int3)round(b.rgb * 255.0);
    int3 delta = ca - cb;
    int wr = (ca.r + cb.r >= 256) ? 3 : 2;
    int wb = (ca.r + cb.r >= 256) ? 2 : 3;
    int distance_squared = wr * delta.r * delta.r + 4 * delta.g * delta.g + wb * delta.b * delta.b;
    precise float distance = sqrt(float(distance_squared));
    return distance;
}

float4 sample_src(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.Load(int3(x, y, 0));
}

int detect_edge(float4 center, float4 diag, float4 ortho1, float4 ortho2,
                float4 ext1, float4 ext2, float4 ext3, float4 ext4) {
    float tolerance = 30.0;
    float dominant_threshold = 3.6;

    if (color_dist(center, diag) < tolerance) return 0;

    precise float diag_grad = color_dist(center, diag) + color_dist(ext1, ext2);
    precise float ortho_grad = color_dist(ortho1, ortho2) + color_dist(ext3, ext4);

    if (diag_grad < ortho_grad) return 0;
    if (diag_grad > ortho_grad * dominant_threshold) return 2;
    return 1;
}

[numthreads(16, 16, 1)]
void xbrz_preprocess(uint3 gid : SV_DispatchThreadID) {
    int sw = src_width;
    int sh = src_height;
    if ((int)gid.x >= sw || (int)gid.y >= sh) return;

    int sx = (int)gid.x;
    int sy = (int)gid.y;
    float4 D = sample_src(sx-1, sy-1, sw, sh);
    float4 E = sample_src(sx,   sy-1, sw, sh);
    float4 F = sample_src(sx+1, sy-1, sw, sh);
    float4 G = sample_src(sx-1, sy,   sw, sh);
    float4 H = sample_src(sx,   sy,   sw, sh);
    float4 I = sample_src(sx+1, sy,   sw, sh);
    float4 J = sample_src(sx-1, sy+1, sw, sh);
    float4 K = sample_src(sx,   sy+1, sw, sh);
    float4 L = sample_src(sx+1, sy+1, sw, sh);

    int bl_tl = detect_edge(H, D, G, E, D, E, F, I);
    int bl_tr = detect_edge(H, F, E, I, F, I, L, K);
    int bl_bl = detect_edge(H, J, K, G, J, G, D, E);
    int bl_br = detect_edge(H, L, I, K, L, K, J, G);
    float steep_threshold = 2.2;

    bool blend_tl = false;
    if (bl_tl > 0) {
        float d = color_dist(H, D);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, E);
        blend_tl = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_tr = false;
    if (bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        blend_tr = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_bl = false;
    if (bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        blend_bl = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_br = false;
    if (bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        blend_br = d > 30.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }

    int blends = bl_tl | (bl_tr << 2) | (bl_bl << 4) | (bl_br << 6);
    int active = (int)blend_tl | ((int)blend_tr << 1) |
                 ((int)blend_bl << 2) | ((int)blend_br << 3);
    blend_info[gid.xy] = float4((float)blends / 255.0, (float)active / 255.0, 0.0, 1.0);
}
)HLSL";

inline constexpr const char* kXbrzComputeHlsl = R"HLSL(
cbuffer Params : register(b0, space2) {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

Texture2D<float4> src : register(t0, space0);
Texture2D<float4> blend_info : register(t1, space0);
RWTexture2D<float4> dst : register(u0, space1);

// Evaluate the same footprint in coordinates reflected from each corner.
// Odd scales share their middle edge pixel between adjacent corners.
int corner_weight(int x, int y, int scale, int blend_type, bool enabled) {
    if (!enabled) return 0;
    if (x == 0 && y == 0) return 2;
    if ((x == 0 || y == 0) && 2 * x < scale && 2 * y < scale && blend_type == 2) return 1;
    return 0;
}

float4 sample_src(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.Load(int3(x, y, 0));
}

[numthreads(16, 16, 1)]
void xbrz_upscale(uint3 gid : SV_DispatchThreadID) {
    int scale = scale_factor;
    int sw = src_width;
    int sh = src_height;
    int dw = sw * scale;
    int dh = sh * scale;
    if ((int)gid.x >= dw || (int)gid.y >= dh) return;

    int sx = (int)gid.x / scale;
    int sy = (int)gid.y / scale;
    int sub_x = (int)gid.x % scale;
    int sub_y = (int)gid.y % scale;
    float4 H = sample_src(sx, sy, sw, sh);
    float4 D = sample_src(sx-1, sy-1, sw, sh);
    float4 F = sample_src(sx+1, sy-1, sw, sh);
    float4 J = sample_src(sx-1, sy+1, sw, sh);
    float4 L = sample_src(sx+1, sy+1, sw, sh);
    float4 metadata = blend_info.Load(int3(sx, sy, 0));
    int blends = (int)round(metadata.r * 255.0);
    int active = (int)round(metadata.g * 255.0);
    // Sum exact eighths before converting to UNORM. This also keeps rounding
    // independent of the order in which overlapping corners are visited.
    int4 center = (int4)round(H * 255.0);
    int4 result = center * 8;
    result += corner_weight(sub_x, sub_y, scale, (blends & 3), (active & 1) != 0) *
              ((int4)round(D * 255.0) - center);
    result += corner_weight(scale - 1 - sub_x, sub_y,
                            scale, ((blends >> 2) & 3), (active & 2) != 0) *
              ((int4)round(F * 255.0) - center);
    result += corner_weight(sub_x, scale - 1 - sub_y,
                            scale, ((blends >> 4) & 3), (active & 4) != 0) *
              ((int4)round(J * 255.0) - center);
    result += corner_weight(scale - 1 - sub_x, scale - 1 - sub_y,
                            scale, ((blends >> 6) & 3), (active & 8) != 0) *
              ((int4)round(L * 255.0) - center);
    dst[gid.xy] = (float4)result / (8.0 * 255.0);
}
)HLSL";
// clang-format on

} // namespace mapperbus::frontend::shaders
