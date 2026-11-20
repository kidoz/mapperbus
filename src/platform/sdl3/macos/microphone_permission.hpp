#pragma once

#include <memory>

#include "platform/audio/microphone_permission.hpp"

namespace mapperbus::frontend {

[[nodiscard]] std::unique_ptr<platform::MicrophonePermission> make_macos_microphone_permission();

} // namespace mapperbus::frontend
