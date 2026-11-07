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
    float dr = a.r - b.r;
    float dg = a.g - b.g;
    float db = a.b - b.b;
    float rmean = (a.r + b.r) * 0.5;
    float wr = (rmean >= 0.5) ? 3.0 : 2.0;
    float wg = 4.0;
    float wb = (rmean >= 0.5) ? 2.0 : 3.0;
    return sqrt(wr * dr * dr + wg * dg * dg + wb * db * db);
}

float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

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
        blend_tl = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_tr = false;
    if (bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        blend_tr = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_bl = false;
    if (bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        blend_bl = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_br = false;
    if (bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        blend_br = d > 30.0/255.0 &&
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

float4 blend_3_1(float4 a, float4 b) {
    return mix(b, a, 0.75);
}

float4 blend_7_1(float4 a, float4 b) {
    return mix(b, a, 0.875);
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
    float mid = float(scale) * 0.5;
    float px = float(sub_x);
    float py = float(sub_y);
    float4 result = H;

    if (px < mid && py < mid && (active & 1) != 0) {
        if (sub_x == 0 && sub_y == 0) {
            result = blend_3_1(H, D);
        } else if ((sub_x == 0 || sub_y == 0) && (blends & 3) == 2) {
            result = blend_7_1(H, D);
        }
    }
    if (px >= mid && py < mid && (active & 2) != 0) {
        if (sub_x == scale-1 && sub_y == 0) {
            result = blend_3_1(H, F);
        } else if ((sub_x == scale-1 || sub_y == 0) && ((blends >> 2) & 3) == 2) {
            result = blend_7_1(H, F);
        }
    }
    if (px < mid && py >= mid && (active & 4) != 0) {
        if (sub_x == 0 && sub_y == scale-1) {
            result = blend_3_1(H, J);
        } else if ((sub_x == 0 || sub_y == scale-1) && ((blends >> 4) & 3) == 2) {
            result = blend_7_1(H, J);
        }
    }
    if (px >= mid && py >= mid && (active & 8) != 0) {
        if (sub_x == scale-1 && sub_y == scale-1) {
            result = blend_3_1(H, L);
        } else if ((sub_x == scale-1 || sub_y == scale-1) && ((blends >> 6) & 3) == 2) {
            result = blend_7_1(H, L);
        }
    }
    dst.write(result, gid);
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
    float dr = a.r - b.r;
    float dg = a.g - b.g;
    float db = a.b - b.b;
    float rmean = (a.r + b.r) * 0.5;
    float wr = (rmean >= 0.5) ? 3.0 : 2.0;
    float wg = 4.0;
    float wb = (rmean >= 0.5) ? 2.0 : 3.0;
    return sqrt(wr * dr * dr + wg * dg * dg + wb * db * db);
}

float4 sample_src(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.Load(int3(x, y, 0));
}

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
        blend_tl = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_tr = false;
    if (bl_tr > 0) {
        float d = color_dist(H, F);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, E);
        blend_tr = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_bl = false;
    if (bl_bl > 0) {
        float d = color_dist(H, J);
        float dv = color_dist(H, G);
        float dh_val = color_dist(H, K);
        blend_bl = d > 30.0/255.0 &&
                   (dv < d * steep_threshold || dh_val < d * steep_threshold);
    }
    bool blend_br = false;
    if (bl_br > 0) {
        float d = color_dist(H, L);
        float dv = color_dist(H, I);
        float dh_val = color_dist(H, K);
        blend_br = d > 30.0/255.0 &&
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

float4 blend_3_1(float4 a, float4 b) {
    return lerp(b, a, 0.75);
}

float4 blend_7_1(float4 a, float4 b) {
    return lerp(b, a, 0.875);
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
    float mid = (float)scale * 0.5;
    float px = (float)sub_x;
    float py = (float)sub_y;
    float4 result = H;

    if (px < mid && py < mid && (active & 1) != 0) {
        if (sub_x == 0 && sub_y == 0) {
            result = blend_3_1(H, D);
        } else if ((sub_x == 0 || sub_y == 0) && (blends & 3) == 2) {
            result = blend_7_1(H, D);
        }
    }
    if (px >= mid && py < mid && (active & 2) != 0) {
        if (sub_x == scale-1 && sub_y == 0) {
            result = blend_3_1(H, F);
        } else if ((sub_x == scale-1 || sub_y == 0) && ((blends >> 2) & 3) == 2) {
            result = blend_7_1(H, F);
        }
    }
    if (px < mid && py >= mid && (active & 4) != 0) {
        if (sub_x == 0 && sub_y == scale-1) {
            result = blend_3_1(H, J);
        } else if ((sub_x == 0 || sub_y == scale-1) && ((blends >> 4) & 3) == 2) {
            result = blend_7_1(H, J);
        }
    }
    if (px >= mid && py >= mid && (active & 8) != 0) {
        if (sub_x == scale-1 && sub_y == scale-1) {
            result = blend_3_1(H, L);
        } else if ((sub_x == scale-1 || sub_y == scale-1) && ((blends >> 6) & 3) == 2) {
            result = blend_7_1(H, L);
        }
    }
    dst[gid.xy] = result;
}
)HLSL";
// clang-format on

} // namespace mapperbus::frontend::shaders
