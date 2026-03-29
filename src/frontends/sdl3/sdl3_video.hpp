#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <vector>

#include "platform/video/upscaler.hpp"
#include "platform/video/video_backend.hpp"

namespace mapperbus::frontend {

class Sdl3Video : public platform::VideoBackend {
  public:
    ~Sdl3Video() override;

    bool initialize(int width, int height) override;
    void render(const core::FrameBuffer& frame) override;
    void shutdown() override;

    void set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) override;

  private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    std::unique_ptr<platform::Upscaler> upscaler_;
    std::vector<std::uint32_t> scaled_buffer_;
    int src_width_ = 0;
    int src_height_ = 0;
};

} // namespace mapperbus::frontend
