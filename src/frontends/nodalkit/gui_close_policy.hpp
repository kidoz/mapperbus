#pragma once

#include <functional>
#include <nk/platform/window.h>
#include <string>
#include <utility>

#include "core/types.hpp"

namespace mapperbus::frontend {

/// Veto native close while persistence runs, then request close again once ready.
template <typename Ready, typename Request>
void guard_async_window_close(nk::Window& window, Ready ready, Request request_save) {
    window.set_close_policy([ready = std::move(ready), request_save = std::move(request_save)] {
        if (ready())
            return true;
        request_save();
        return false;
    });
}

/// Run persistence before NodalKit hides the window. A close notification is
/// too late to veto the close and keep recovery/export controls available.
inline void guard_window_close(nk::Window& window,
                               std::function<core::Result<void>()> persist,
                               std::function<void(const std::string&)> report_error) {
    window.set_close_policy([persist = std::move(persist), report_error = std::move(report_error)] {
        if (auto saved = persist(); !saved) {
            report_error(saved.error());
            return false;
        }
        return true;
    });
}

} // namespace mapperbus::frontend
