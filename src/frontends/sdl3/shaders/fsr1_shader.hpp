#pragma once

namespace mapperbus::frontend::shaders {

// clang-format off
inline constexpr const char* kFsr1EasuComputeMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
};

// Clamp-read from source texture (nearest)
float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

kernel void fsr1_easu(
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

    float inv_scale = 1.0 / float(scale);
    float src_x = (float(gid.x) + 0.5) * inv_scale - 0.5;
    float src_y = (float(gid.y) + 0.5) * inv_scale - 0.5;

    int ix = clamp(int(src_x), 0, sw - 1);
    int iy = clamp(int(src_y), 0, sh - 1);
    int nx = clamp(ix + 1, 0, sw - 1);
    int ny = clamp(iy + 1, 0, sh - 1);

    float fx = max(0.0, src_x - float(ix));
    float fy = max(0.0, src_y - float(iy));

    float4 c00 = sample_src(src, ix, iy, sw, sh);
    float4 c10 = sample_src(src, nx, iy, sw, sh);
    float4 c01 = sample_src(src, ix, ny, sw, sh);
    float4 c11 = sample_src(src, nx, ny, sw, sh);

    float w00 = (1.0 - fx) * (1.0 - fy);
    float w10 = fx * (1.0 - fy);
    float w01 = (1.0 - fx) * fy;
    float w11 = fx * fy;

    float4 result = c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11;
    
    // Ensure Alpha channel is 1.0
    result.a = 1.0;

    dst.write(result, gid);
}
)MSL";

inline constexpr const char* kFsr1RcasComputeMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
};

// Clamp-read from EASU texture
float4 sample_easu(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
}

float min5(float a, float b, float c, float d, float e) {
    return min(a, min(b, min(c, min(d, e))));
}

float max5(float a, float b, float c, float d, float e) {
    return max(a, max(b, max(c, max(d, e))));
}

kernel void fsr1_rcas(
    texture2d<float, access::read> easu_tex [[texture(0)]],
    texture2d<float, access::write> dst [[texture(1)]],
    constant Params& params [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int scale = params.scale_factor;
    int dw = params.src_width * scale;
    int dh = params.src_height * scale;

    if (int(gid.x) >= dw || int(gid.y) >= dh) return;
    
    int x = int(gid.x);
    int y = int(gid.y);

    int up_y = y - 1;
    int dn_y = y + 1;
    int lf_x = x - 1;
    int rt_x = x + 1;

    float4 c = sample_easu(easu_tex, x, y, dw, dh);
    float4 n = sample_easu(easu_tex, x, up_y, dw, dh);
    float4 s = sample_easu(easu_tex, x, dn_y, dw, dh);
    float4 w = sample_easu(easu_tex, lf_x, y, dw, dh);
    float4 e = sample_easu(easu_tex, rt_x, y, dw, dh);

    float min_r = min5(c.r, n.r, s.r, w.r, e.r);
    float max_r = max5(c.r, n.r, s.r, w.r, e.r);
    float min_g = min5(c.g, n.g, s.g, w.g, e.g);
    float max_g = max5(c.g, n.g, s.g, w.g, e.g);
    float min_b = min5(c.b, n.b, s.b, w.b, e.b);
    float max_b = max5(c.b, n.b, s.b, w.b, e.b);

    float limit_r = min(min_r, 1.0 - max_r);
    float limit_g = min(min_g, 1.0 - max_g);
    float limit_b = min(min_b, 1.0 - max_b);

    float lob_r = limit_r * (1.0 / (max_r + 1e-5));
    float lob_g = limit_g * (1.0 / (max_g + 1e-5));
    float lob_b = limit_b * (1.0 / (max_b + 1e-5));

    const float sharpness = 0.2;
    float w_r = sharpness * lob_r;
    float w_g = sharpness * lob_g;
    float w_b = sharpness * lob_b;

    float denom_r = 1.0 / (1.0 + 4.0 * w_r);
    float denom_g = 1.0 / (1.0 + 4.0 * w_g);
    float denom_b = 1.0 / (1.0 + 4.0 * w_b);

    float4 out;
    out.r = (c.r + w_r * (n.r + s.r + w.r + e.r)) * denom_r;
    out.g = (c.g + w_g * (n.g + s.g + w.g + e.g)) * denom_g;
    out.b = (c.b + w_b * (n.b + s.b + w.b + e.b)) * denom_b;
    out.a = 1.0;

    // Clamp the output explicitly to ensure safety
    out = clamp(out, 0.0, 1.0);
    dst.write(out, gid);
}
)MSL";
// clang-format on

} // namespace mapperbus::frontend::shaders
