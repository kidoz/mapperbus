#include "frontends/sdl3/macos/microphone_permission.hpp"

#import <AVFoundation/AVFoundation.h>
#include <atomic>

namespace mapperbus::frontend {
namespace {

class MacMicrophonePermission final : public platform::MicrophonePermission {
  public:
    [[nodiscard]] platform::MicrophoneAccess status() const override {
        using platform::MicrophoneAccess;
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized:
            return MicrophoneAccess::Authorized;
        case AVAuthorizationStatusNotDetermined:
            return pending_->load() ? MicrophoneAccess::Pending : MicrophoneAccess::NotDetermined;
        case AVAuthorizationStatusDenied:
            return MicrophoneAccess::Denied;
        case AVAuthorizationStatusRestricted:
            return MicrophoneAccess::Restricted;
        }
        return MicrophoneAccess::Restricted;
    }

    void request() override {
        if (status() != platform::MicrophoneAccess::NotDetermined || pending_->exchange(true))
            return;
        // AVFoundation may finish after the session/backend has been destroyed.
        // The block owns only the pending flag, never this or a UI object.
        const auto pending = pending_;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL) {
                                   pending->store(false);
                                 }];
    }

  private:
    std::shared_ptr<std::atomic<bool>> pending_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace

std::unique_ptr<platform::MicrophonePermission> make_macos_microphone_permission() {
    return std::make_unique<MacMicrophonePermission>();
}

} // namespace mapperbus::frontend
