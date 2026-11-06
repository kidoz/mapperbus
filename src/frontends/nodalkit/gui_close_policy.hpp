#pragma once

#include <functional>
#include <nk/platform/window.h>
#include <string>
#include <utility>

#include "core/types.hpp"

namespace mapperbus::frontend {

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
