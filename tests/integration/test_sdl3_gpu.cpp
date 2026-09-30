#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frontends/sdl3/gpu_common.hpp"
#include "frontends/sdl3/gpu_fsr_upscaler.hpp"
#include "frontends/sdl3/gpu_upscaler.hpp"
#include "frontends/sdl3/sdl3_video.hpp"

namespace mapperbus::frontend {
namespace {

struct SdlVideoScope {
    ~SdlVideoScope() {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

void require_gpu_device() {
    const char* flag = std::getenv("MAPPERBUS_REQUIRE_GPU");
    const bool required = flag && std::string_view(flag) == "1";
    const bool video_initialized = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (required) {
        INFO(SDL_GetError());
        REQUIRE(video_initialized);
    }
    if (!video_initialized) {
        SKIP("SDL video unavailable: " << SDL_GetError());
    }
    auto* device = SDL_CreateGPUDevice(gpu_shader_formats(), false, nullptr);
    if (required) {
        INFO(SDL_GetError());
        REQUIRE(device != nullptr);
    }
    if (!device) {
        SKIP("Compatible SDL GPU device unavailable: " << SDL_GetError());
    }
    SDL_DestroyGPUDevice(device);
}

TEST_CASE("GPU initialization failure falls back on every frame", "[sdl3][gpu][fallback]") {
    struct RestoreGpuDriverHint {
        std::optional<std::string> original;
        RestoreGpuDriverHint() {
            if (const char* hint = SDL_GetHint(SDL_HINT_GPU_DRIVER)) {
                original = hint;
            }
        }
        ~RestoreGpuDriverHint() {
            if (original) {
                (void)SDL_SetHintWithPriority(
                    SDL_HINT_GPU_DRIVER, original->c_str(), SDL_HINT_OVERRIDE);
            } else {
                SDL_ResetHint(SDL_HINT_GPU_DRIVER);
            }
        }
    } restore;
    REQUIRE(SDL_SetHintWithPriority(
        SDL_HINT_GPU_DRIVER, "mapperbus-invalid-driver", SDL_HINT_OVERRIDE));

    const auto check_fallback = [](platform::Upscaler& upscaler) {
        constexpr int kWidth = 3;
        constexpr int kHeight = 2;
        constexpr int kFactor = 2;
        std::vector<uint32_t> source = {
            0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU, 0xFF7193B5U, 0xFFFFFFFFU, 0xFF000000U};
        std::vector<uint32_t> target(source.size() * kFactor * kFactor, 0);
        for (int frame = 0; frame < 2; ++frame) {
            upscaler.scale(source, kWidth, kHeight, target);
            for (int y = 0; y < kHeight * kFactor; ++y) {
                for (int x = 0; x < kWidth * kFactor; ++x) {
                    REQUIRE(target[y * kWidth * kFactor + x] ==
                            source[(y / kFactor) * kWidth + x / kFactor]);
                }
            }
            std::reverse(source.begin(), source.end());
        }
    };
    GpuUpscaler xbrz(2);
    check_fallback(xbrz);
    REQUIRE_FALSE(xbrz.is_gpu_available());
    GpuFsr1Upscaler fsr(2);
    check_fallback(fsr);
    REQUIRE_FALSE(fsr.is_gpu_available());
}

TEST_CASE("GPU xBRZ preserves colors across partial workgroups", "[sdl3][gpu]") {
    SdlVideoScope sdl;
    require_gpu_device();

    // Odd dimensions exercise bounds checks in the final 16x16 workgroup.
    constexpr int kWidth = 7;
    constexpr int kHeight = 5;
    for (int factor : {2, 3, 6}) {
        GpuUpscaler upscaler(factor);
        std::vector<uint32_t> source(kWidth * kHeight);
        std::vector<uint32_t> target(source.size() * factor * factor, 0);
        for (uint32_t color : {0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU, 0xFF7193B5U}) {
            std::fill(source.begin(), source.end(), color);
            upscaler.scale(source, kWidth, kHeight, target);
            INFO(SDL_GetError());
            REQUIRE(upscaler.is_gpu_available());
            REQUIRE(std::all_of(
                target.begin(), target.end(), [color](uint32_t pixel) { return pixel == color; }));
        }
        // A contrasting top-left neighbor triggers the filter's corner blend,
        // distinguishing the compute result from the CPU nearest-neighbor fallback.
        std::fill(source.begin(), source.end(), 0xFF000000U);
        source[1 * kWidth + 2] = 0xFFFFFFFFU;
        upscaler.scale(source, kWidth, kHeight, target);
        REQUIRE(target[(2 * factor) * (kWidth * factor) + 3 * factor] == 0xFF404040U);
    }
}

TEST_CASE("GPU FSR returns the previous frame with correct channels", "[sdl3][gpu]") {
    SdlVideoScope sdl;
    require_gpu_device();

    constexpr int kWidth = 7;
    constexpr int kHeight = 5;
    GpuFsr1Upscaler upscaler(3);
    std::vector<uint32_t> source(kWidth * kHeight, 0xFFFF0000U);
    std::vector<uint32_t> target(source.size() * 9, 0);
    upscaler.scale(source, kWidth, kHeight, target);
    INFO(SDL_GetError());
    REQUIRE(upscaler.is_gpu_available());

    // FSR readback is pipelined by one frame; check frame order as well as
    // upload/download channel conversion and both compute passes.
    for (uint32_t next_color : {0xFF00FF00U, 0xFF0000FFU, 0xFFFF0000U}) {
        const auto previous_color = source.front();
        std::fill(source.begin(), source.end(), next_color);
        upscaler.scale(source, kWidth, kHeight, target);
        REQUIRE(std::all_of(target.begin(), target.end(), [previous_color](uint32_t pixel) {
            return pixel == previous_color;
        }));
    }
}

TEST_CASE("Changing vsync preserves the renderer's borrowed GPU device", "[sdl3][gpu]") {
    SdlVideoScope sdl;
    require_gpu_device();

    Sdl3Video video;
    auto upscaler = std::make_unique<GpuUpscaler>(2);
    auto* scaler = upscaler.get();
    video.set_upscaler(std::move(upscaler));
    REQUIRE(video.initialize(core::kScreenWidth, core::kScreenHeight));
    core::FrameBuffer frame;
    frame.pixels.fill(0xFF7193B5U);
    video.render(frame);
    INFO(SDL_GetError());
    REQUIRE(scaler->is_gpu_available());

    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    REQUIRE(windows != nullptr);
    REQUIRE(count == 1);
    SDL_Window* window = windows[0];
    SDL_free(windows);
    auto* renderer = SDL_GetRenderer(window);
    REQUIRE(renderer != nullptr);
    const auto properties = SDL_GetRendererProperties(renderer);
    auto* device =
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr);
    if (!device) {
        SKIP("SDL GPU renderer unavailable; only GPU readback was exercised");
    }
    auto* texture = scaler->get_gpu_texture();
    REQUIRE(texture != nullptr);
    for (bool enabled : {true, false, true}) {
        video.set_vsync(enabled);
        REQUIRE(SDL_GetRenderer(window) == renderer);
        REQUIRE(SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
                                       SDL_PROP_RENDERER_GPU_DEVICE_POINTER,
                                       nullptr) == device);
        video.render(frame);
        REQUIRE(scaler->get_gpu_texture() == texture);
    }
    video.shutdown();
}

} // namespace
} // namespace mapperbus::frontend
