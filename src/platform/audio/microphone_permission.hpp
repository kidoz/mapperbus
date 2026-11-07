#pragma once

namespace mapperbus::platform {

enum class MicrophoneAccess { Authorized, NotDetermined, Pending, Denied, Restricted };

/// Host permission policy, separate from capture and machine state. Request
/// returns immediately; completion must not reference a destroyed backend.
class MicrophonePermission {
  public:
    virtual ~MicrophonePermission() = default;
    [[nodiscard]] virtual MicrophoneAccess status() const = 0;
    virtual void request() = 0;
};

} // namespace mapperbus::platform
