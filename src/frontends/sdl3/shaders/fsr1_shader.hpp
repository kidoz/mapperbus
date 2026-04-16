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
    int padding;
};

// Convert to linear space for accurate scaling/blending
float4 to_linear(float4 c) {
    return float4(pow(max(c.rgb, 0.0), float3(2.2)), c.a);
}

// Clamp-read from source texture
float4 sample_src(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return to_linear(src.read(uint2(x, y)));
}

// Lanczos-2 weight function approximation
float lanczos2_weight(float x) {
    x = abs(x);
    if (x >= 2.0) return 0.0;
    if (x < 1e-5) return 1.0;
    float pi_x = M_PI_F * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x * 0.5) / (pi_x * 0.5));
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

    int ix = int(floor(src_x));
    int iy = int(floor(src_y));

    float4 result = float4(0.0);
    float total_weight = 0.0;

    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            float dx = float(x) - (src_x - float(ix));
            float dy = float(y) - (src_y - float(iy));
            
            float weight = lanczos2_weight(dx) * lanczos2_weight(dy);
            
            result += sample_src(src, ix + x, iy + y, sw, sh) * weight;
            total_weight += weight;
        }
    }

    if (total_weight > 0.0) {
        result /= total_weight;
    }

    result.a = clamp(result.a, 0.0, 1.0);
    dst.write(max(result, 0.0), gid);
}
)MSL";

inline constexpr const char* kFsr1RcasComputeMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Params {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

// Convert from linear back to sRGB for display
float4 to_srgb(float4 c) {
    return float4(pow(max(c.rgb, 0.0), float3(1.0 / 2.2)), c.a);
}

// Clamp-read from EASU texture
float4 sample_easu(texture2d<float, access::read> src, int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return src.read(uint2(x, y));
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

    float4 c = sample_easu(easu_tex, x, y, dw, dh);
    float4 n = sample_easu(easu_tex, x, y - 1, dw, dh);
    float4 s = sample_easu(easu_tex, x, y + 1, dw, dh);
    float4 w = sample_easu(easu_tex, x - 1, y, dw, dh);
    float4 e = sample_easu(easu_tex, x + 1, y, dw, dh);

    float3 min_rgb = min(c.rgb, min(n.rgb, min(s.rgb, min(w.rgb, e.rgb))));
    float3 max_rgb = max(c.rgb, max(n.rgb, max(s.rgb, max(w.rgb, e.rgb))));

    float3 limit = min(min_rgb, 1.0 - max_rgb);
    float3 lobe = limit / (max_rgb + 1e-5); 
    
    const float sharpness = 0.2;
    float3 weight = sharpness * lobe;

    weight = min(weight, float3(0.25));

    float3 denom = 1.0 / (1.0 + 4.0 * weight);
    
    float4 out_val;
    out_val.rgb = (c.rgb + weight * (n.rgb + s.rgb + w.rgb + e.rgb)) * denom;
    out_val.a = c.a;

    dst.write(clamp(to_srgb(out_val), 0.0, 1.0), gid);
}
)MSL";

inline constexpr const char* kFsr1EasuComputeHlsl = R"HLSL(
cbuffer Params : register(b0) {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

// Convert to linear space for accurate scaling/blending
float4 to_linear(float4 c) {
    return float4(pow(max(c.rgb, 0.0), float3(2.2, 2.2, 2.2)), c.a);
}

// Clamp-read from source texture
float4 sample_src(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return to_linear(src.Load(int3(x, y, 0)));
}

// Lanczos-2 weight function approximation
float lanczos2_weight(float x) {
    x = abs(x);
    if (x >= 2.0) return 0.0;
    if (x < 1e-5) return 1.0;
    float pi_x = 3.14159265359 * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x * 0.5) / (pi_x * 0.5));
}

[numthreads(16, 16, 1)]
void fsr1_easu(uint3 gid : SV_DispatchThreadID) {
    int scale = scale_factor;
    int sw = src_width;
    int sh = src_height;
    int dw = sw * scale;
    int dh = sh * scale;

    if ((int)gid.x >= dw || (int)gid.y >= dh) return;

    float inv_scale = 1.0 / (float)scale;
    float src_x = ((float)gid.x + 0.5) * inv_scale - 0.5;
    float src_y = ((float)gid.y + 0.5) * inv_scale - 0.5;

    int ix = (int)floor(src_x);
    int iy = (int)floor(src_y);

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    float total_weight = 0.0;

    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            float dx = (float)x - (src_x - (float)ix);
            float dy = (float)y - (src_y - (float)iy);
            
            float weight = lanczos2_weight(dx) * lanczos2_weight(dy);
            
            result += sample_src(ix + x, iy + y, sw, sh) * weight;
            total_weight += weight;
        }
    }

    if (total_weight > 0.0) {
        result /= total_weight;
    }

    result.a = clamp(result.a, 0.0, 1.0);
    dst[gid.xy] = max(result, 0.0);
}
)HLSL";

inline constexpr const char* kFsr1RcasComputeHlsl = R"HLSL(
cbuffer Params : register(b0) {
    int scale_factor;
    int src_width;
    int src_height;
    int padding;
};

Texture2D<float4> easu_tex : register(t0);
RWTexture2D<float4> dst : register(u0);

// Convert from linear back to sRGB for display
float4 to_srgb(float4 c) {
    return float4(pow(max(c.rgb, 0.0), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2)), c.a);
}

// Clamp-read from EASU texture
float4 sample_easu(int x, int y, int w, int h) {
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return easu_tex.Load(int3(x, y, 0));
}

[numthreads(16, 16, 1)]
void fsr1_rcas(uint3 gid : SV_DispatchThreadID) {
    int scale = scale_factor;
    int dw = src_width * scale;
    int dh = src_height * scale;

    if ((int)gid.x >= dw || (int)gid.y >= dh) return;
    
    int x = (int)gid.x;
    int y = (int)gid.y;

    float4 c = sample_easu(x, y, dw, dh);
    float4 n = sample_easu(x, y - 1, dw, dh);
    float4 s = sample_easu(x, y + 1, dw, dh);
    float4 w = sample_easu(x - 1, y, dw, dh);
    float4 e = sample_easu(x + 1, y, dw, dh);

    float3 min_rgb = min(c.rgb, min(n.rgb, min(s.rgb, min(w.rgb, e.rgb))));
    float3 max_rgb = max(c.rgb, max(n.rgb, max(s.rgb, max(w.rgb, e.rgb))));

    float3 limit = min(min_rgb, 1.0 - max_rgb);
    float3 lobe = limit / (max_rgb + 1e-5); 
    
    const float sharpness = 0.2;
    float3 weight = sharpness * lobe;

    weight = min(weight, float3(0.25, 0.25, 0.25));

    float3 denom = 1.0 / (1.0 + 4.0 * weight);
    
    float4 out_val;
    out_val.rgb = (c.rgb + weight * (n.rgb + s.rgb + w.rgb + e.rgb)) * denom;
    out_val.a = c.a;

    dst[gid.xy] = clamp(to_srgb(out_val), 0.0, 1.0);
}
)HLSL";
// clang-format on

} // namespace mapperbus::frontend::shaders
