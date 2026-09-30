#pragma once

#include <SDL3/SDL.h>
#ifdef HAVE_SDL_SHADERCROSS
#include <SDL3_shadercross/SDL_shadercross.h>
#endif
#include <cstdint>
#include <cstring>
#include <span>

namespace mapperbus::frontend {

struct GpuParams {
    int32_t scale_factor;
    int32_t src_width;
    int32_t src_height;
    int32_t padding; // align to 16 bytes
};

#ifdef HAVE_SDL_SHADERCROSS
// Compile a compute pipeline from HLSL source using SDL_shadercross
inline SDL_GPUComputePipeline* compile_hlsl_compute(SDL_GPUDevice* device,
                                                    const char* source,
                                                    const char* entrypoint) {
    SDL_ShaderCross_HLSL_Info hlsl_info{};
    hlsl_info.source = source;
    hlsl_info.entrypoint = entrypoint;
    hlsl_info.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;

    size_t spirv_size = 0;
    void* spirv_buf = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
    if (!spirv_buf) {
        SDL_Log("Failed to compile HLSL to SPIRV: %s", SDL_GetError());
        return nullptr;
    }

    SDL_ShaderCross_SPIRV_Info spirv_info{};
    spirv_info.bytecode = static_cast<const Uint8*>(spirv_buf);
    spirv_info.bytecode_size = spirv_size;
    spirv_info.entrypoint = entrypoint;
    spirv_info.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;

    SDL_ShaderCross_ComputePipelineMetadata* metadata =
        SDL_ShaderCross_ReflectComputeSPIRV(spirv_info.bytecode, spirv_info.bytecode_size, 0);

    if (!metadata) {
        SDL_Log("Failed to reflect compute SPIRV: %s", SDL_GetError());
        SDL_free(spirv_buf);
        return nullptr;
    }

    SDL_GPUComputePipeline* pipeline =
        SDL_ShaderCross_CompileComputePipelineFromSPIRV(device, &spirv_info, metadata, 0);

    SDL_free(metadata);
    SDL_free(spirv_buf);

    return pipeline;
}
#endif

inline SDL_GPUShaderFormat gpu_shader_formats() {
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_MSL;
#ifdef HAVE_VULKAN_SHADERS
    formats |= SDL_GPU_SHADERFORMAT_SPIRV;
#endif
#ifdef HAVE_SDL_SHADERCROSS
    formats |= SDL_ShaderCross_GetHLSLShaderFormats();
#endif
    return formats;
}

inline SDL_GPUComputePipeline* create_compute_pipeline(SDL_GPUDevice* device,
                                                       const char* msl,
                                                       const char* entrypoint,
                                                       const char* hlsl,
                                                       std::span<const uint32_t> spirv = {}) {
    const auto formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUComputePipelineCreateInfo info{};
    if (!spirv.empty() && (formats & SDL_GPU_SHADERFORMAT_SPIRV)) {
        info.code = reinterpret_cast<const Uint8*>(spirv.data());
        info.code_size = spirv.size_bytes();
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = reinterpret_cast<const Uint8*>(msl);
        info.code_size = std::strlen(msl);
        info.entrypoint = entrypoint;
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
#ifdef HAVE_SDL_SHADERCROSS
        return compile_hlsl_compute(device, hlsl, entrypoint);
#else
        (void)hlsl;
        SDL_SetError("No compatible GPU upscaler shader; install glslangValidator and rebuild");
        return nullptr;
#endif
    }
    info.num_readonly_storage_textures = 1;
    info.num_readwrite_storage_textures = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 16;
    info.threadcount_y = 16;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

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
