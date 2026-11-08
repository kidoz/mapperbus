#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>

#include "frontends/nodalkit/rom_load_error_dialog.hpp"

TEST_CASE("ROM load recovery defaults to keeping the session and routes keyboard actions",
          "[gui][rom-source][recovery]") {
    for (bool has_game : {false, true}) {
        for (int tabs : {-1, 0, 1, 2}) {
            INFO(has_game);
            INFO(tabs);
            nk::Window window({.title = "ROM recovery", .width = 640, .height = 480});
            int underlying_activations = 0;
            auto underlying = nk::Button::create("Open another ROM");
            (void)underlying->on_clicked().connect([&] { ++underlying_activations; });
            window.set_child(underlying);
            window.present();
            auto dialog = mapperbus::frontend::make_rom_load_error_dialog(
                "/missing/archive.zip", "folder/Game.fds", "Cannot read FDS BIOS", has_game);
            int responses = 0;
            int custom_id = -1;
            auto response = nk::DialogResponse::None;
            (void)dialog->on_response().connect([&](nk::DialogResponse value) {
                ++responses;
                response = value;
                custom_id = dialog->custom_response_id();
            });
            dialog->present(window);
            nk::Button* focused = nullptr;
            for (const auto& child : dialog->children()) {
                if (nk::has_flag(child->state_flags(), nk::StateFlags::Focused))
                    focused = dynamic_cast<nk::Button*>(child.get());
            }
            REQUIRE(focused);
            CHECK(focused->label() == (has_game ? "Keep Current Game" : "Back to Start"));
            for (int tab = 0; tab < tabs; ++tab)
                window.dispatch_key_event(
                    {.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
            window.dispatch_key_event(
                {.type = nk::KeyEvent::Type::Press,
                 .key = tabs < 0 ? nk::KeyCode::Escape : nk::KeyCode::Return});
            CHECK_FALSE(dialog->is_presented());
            CHECK(responses == 1);
            CHECK(underlying_activations == 0);
            const std::array expected{
                nk::DialogResponse::Cancel, nk::DialogResponse::Custom, nk::DialogResponse::Accept};
            CHECK(response == expected[static_cast<std::size_t>(std::max(0, tabs))]);
            CHECK(custom_id == (tabs == 1 ? mapperbus::frontend::kChooseAnotherRom : -1));
        }
    }
}

TEST_CASE("Long ROM load errors scroll while recovery actions remain in the window",
          "[gui][rom-source][recovery]") {
    nk::Window window({.title = "ROM recovery", .width = 500, .height = 350});
    window.present();
    auto dialog = mapperbus::frontend::make_rom_load_error_dialog(
        "/archive.zip", std::string(4096, 'M') + ".fds", std::string(6000, 'E'), true);
    dialog->present(window);
    dialog->allocate({0, 0, 500, 350});
    nk::ScrollArea* scroll = nullptr;
    int buttons = 0;
    const std::function<void(nk::Widget&)> inspect = [&](nk::Widget& widget) {
        if (auto* area = dynamic_cast<nk::ScrollArea*>(&widget))
            scroll = area;
        if (dynamic_cast<nk::Button*>(&widget)) {
            ++buttons;
            const auto bounds = widget.allocation();
            CHECK(bounds.y >= 0);
            CHECK(bounds.bottom() <= 350);
        }
        for (const auto& child : widget.children())
            inspect(*child);
    };
    inspect(*dialog);
    CHECK(buttons == 3);
    REQUIRE(scroll);
    scroll->grab_focus();
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End});
    CHECK(scroll->v_offset() > 0);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    CHECK_FALSE(dialog->is_presented());
}
