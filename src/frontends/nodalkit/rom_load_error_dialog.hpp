#pragma once

#include <memory>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <string>

#include "frontends/nodalkit/gui_widgets.hpp"

namespace mapperbus::frontend {

inline constexpr int kChooseAnotherRom = 1;

inline std::shared_ptr<nk::Dialog> make_rom_load_error_dialog(
    const std::string& path,
    const std::string& member,
    const std::string& error,
    bool has_current_game,
    const std::string& chooser_error = {}) {
    auto content = Box::vertical(12.0F);
    const auto append = [&](const std::string& text) {
        auto label = nk::Label::create(text);
        label->set_wrapping(true);
        content->append(label);
    };
    append(error);
    append("File: " + path);
    if (!member.empty())
        append("ROM: " + member);
    if (!chooser_error.empty())
        append(chooser_error);
    auto scroll = rectangular_scroll_area();
    scroll->set_focusable(true);
    scroll->set_h_scroll_policy(nk::ScrollPolicy::Never);
    scroll->set_content(content);
    auto dialog = nk::Dialog::create("Could Not Open ROM");
    dialog->set_content(FixedWidthSlot::create(480.0F, FixedHeightSlot::create(240.0F, scroll)));
    dialog->add_button(has_current_game ? "Keep Current Game" : "Back to Start",
                       nk::DialogResponse::Cancel);
    dialog->add_custom_button("Choose Another File…", kChooseAnotherRom);
    dialog->add_button("Retry", nk::DialogResponse::Accept);
    dialog->set_default_response(nk::DialogResponse::Cancel);
    return dialog;
}

} // namespace mapperbus::frontend
