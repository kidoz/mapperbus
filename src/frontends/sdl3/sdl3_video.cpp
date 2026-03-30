#include "frontends/sdl3/sdl3_video.hpp"

#include "core/types.hpp"

namespace mapperbus::frontend {

Sdl3Video::~Sdl3Video() {
    shutdown();
}

bool Sdl3Video::initialize(int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    src_width_ = width;
    src_height_ = height;

    int display_scale = upscaler_ ? upscaler_->scale_factor() : 2;
    int win_w = width * display_scale;
    int win_h = height * display_scale;

    window_ = SDL_CreateWindow("MapperBus", win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    // VSync disabled — frame pacing is handled by the software timer in App::run()
    // at exact NTSC rate (~60.0988 Hz). Using both causes double-pacing jitter.
    SDL_SetRenderVSync(renderer_, 0);

    int tex_w = width;
    int tex_h = height;

    if (upscaler_) {
        tex_w = width * upscaler_->scale_factor();
        tex_h = height * upscaler_->scale_factor();
        scaled_buffer_.resize(static_cast<std::size_t>(tex_w) * tex_h);

        // Let SDL fit the upscaled texture into the window
        SDL_SetRenderLogicalPresentation(
            renderer_, tex_w, tex_h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    } else {
        SDL_SetRenderLogicalPresentation(
            renderer_, width, height, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    }

    texture_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
    if (!texture_) {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        return false;
    }

    return true;
}

void Sdl3Video::render(const core::FrameBuffer& frame) {
    if (upscaler_) {
        int scale = upscaler_->scale_factor();
        int dst_w = src_width_ * scale;
        upscaler_->scale(frame.pixels, src_width_, src_height_, scaled_buffer_);
        SDL_UpdateTexture(texture_,
                          nullptr,
                          scaled_buffer_.data(),
                          dst_w * static_cast<int>(sizeof(std::uint32_t)));
    } else {
        SDL_UpdateTexture(texture_,
                          nullptr,
                          frame.pixels.data(),
                          core::kScreenWidth * static_cast<int>(sizeof(std::uint32_t)));
    }
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void Sdl3Video::set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) {
    upscaler_ = std::move(upscaler);
}

void Sdl3Video::shutdown() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    scaled_buffer_.clear();
}

} // namespace mapperbus::frontend
