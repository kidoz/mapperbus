#pragma once

#include "platform/video/video_backend.hpp"

namespace mapperbus::platform {

class NullVideo : public VideoBackend {
  public:
    bool initialize(int /*width*/, int /*height*/) override {
        return true;
    }
    void render(const core::FrameBuffer& /*frame*/) override {}
    void shutdown() override {}
};

} // namespace mapperbus::platform
