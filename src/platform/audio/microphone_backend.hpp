#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::platform {

/// Nonblocking host capture. Samples are mono float PCM at 16 kHz. Stop closes
/// the recording device and discards pending samples. No core dependency on SDL.
class MicrophoneBackend {
  public:
    virtual ~MicrophoneBackend() = default;
    virtual core::Result<std::vector<std::string>> devices() = 0;
    virtual core::Result<void> start(std::string_view device) = 0;
    virtual void stop() = 0;
    virtual core::Result<std::size_t> read(std::span<float> samples) = 0;
    virtual core::Result<void> discard() = 0;
};

} // namespace mapperbus::platform
