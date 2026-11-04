#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/platform/window.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/style/theme.h>
#include <nk/widgets/scroll_area.h>
#include <string>
#include <vector>

#include "frontends/nodalkit/gui_widgets.hpp"
#include "frontends/nodalkit/input_binding_table.hpp"

namespace {

struct MacTheme {
    std::shared_ptr<nk::Theme> previous = nk::Theme::active();
    explicit MacTheme(nk::ColorScheme scheme = nk::ColorScheme::Light) {
        nk::Theme::set_active(nk::Theme::make_macos_26(scheme));
    }
    ~MacTheme() {
        nk::Theme::set_active(previous);
    }
};

void collect_text(const nk::RenderNode& node, std::vector<const nk::TextNode*>& text) {
    if (node.kind() == nk::RenderNodeKind::Text) {
        text.push_back(static_cast<const nk::TextNode*>(&node));
    }
    for (const auto& child : node.children()) {
        collect_text(*child, text);
    }
}

} // namespace

TEST_CASE("Binding values share a text baseline and aligned columns", "[gui][bindings]") {
    for (const auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        MacTheme theme(scheme);
        auto table = mapperbus::frontend::InputBindingTable::create("Disabled");
        table->add_binding("Up", "Up Arrow", "D-pad Up", [] {}, [] {}, false, "Gamepad disabled.");
        table->add_binding(
            "Down", "Down Arrow", "D-pad Down", [] {}, [] {}, false, "Gamepad disabled.");
        for (const float width : {360.0F, 576.0F}) {
            const auto height = table->measure(nk::Constraints::unbounded()).natural_height;
            table->allocate({10, 20, width, height});
            const auto children = table->children();
            REQUIRE(children.size() == 4);
            CHECK(children[0]->allocation().x == children[2]->allocation().x);
            CHECK(children[1]->allocation().x == children[3]->allocation().x);
            for (const auto& child : children) {
                CHECK(child->allocation().x >= table->allocation().x);
                CHECK(child->allocation().right() <= table->allocation().right());
            }
            nk::SnapshotContext snapshot;
            static_cast<nk::Widget&>(*table).snapshot(snapshot);
            const auto root = snapshot.take_root();
            std::vector<const nk::TextNode*> texts;
            collect_text(*root, texts);
            const auto y = [&](std::string_view value) {
                for (const auto* text : texts) {
                    if (text->text() == value) {
                        return text->bounds().y;
                    }
                }
                FAIL("Missing binding text: " << value);
                return 0.0F;
            };
            CHECK(y("Up") == Catch::Approx(y("Up Arrow")));
            CHECK(y("Up") == Catch::Approx(y("D-pad Up")));
            CHECK(y("Down") == Catch::Approx(y("Down Arrow")));
            CHECK(y("Down") == Catch::Approx(y("D-pad Down")));
        }
    }
}

TEST_CASE("Keyboard can activate binding cells and skip unavailable gamepad cells",
          "[gui][bindings]") {
    MacTheme theme;
    nk::Window window({.title = "Bindings", .width = 600, .height = 300});
    auto table = mapperbus::frontend::InputBindingTable::create("Disabled");
    int keyboard = 0;
    int gamepad = 0;
    for (const auto* action : {"Up", "Down"}) {
        table->add_binding(
            action,
            "Up Arrow",
            "D-pad Up",
            [&] { ++keyboard; },
            [&] { ++gamepad; },
            false,
            "Gamepad disabled.");
    }
    window.set_child(table);
    table->allocate({0, 0, 576, 120});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    CHECK(nk::has_flag(table->children()[0]->state_flags(), nk::StateFlags::Focused));
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    CHECK(keyboard == 1);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    CHECK(nk::has_flag(table->children()[2]->state_flags(), nk::StateFlags::Focused));
    const auto rect = table->children()[1]->allocation();
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = rect.x + 10, .y = rect.y + 10, .button = 1});
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = rect.x + 10, .y = rect.y + 10, .button = 1});
    CHECK(gamepad == 0);
    CHECK(table->children()[0]->accessible()->name() == "Rebind Up keyboard");
    CHECK(table->children()[1]->accessible()->description() == "Gamepad disabled.");
}

TEST_CASE("Settings viewport leaves both bottom stripe corners square", "[gui][bindings][render]") {
    for (const auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        MacTheme theme(scheme);
        auto table = mapperbus::frontend::InputBindingTable::create("Disabled");
        for (int i = 0; i < 8; ++i) {
            table->add_binding("Action", "Key", "Pad", [] {}, [] {}, false, "Disabled");
        }
        auto scroll = mapperbus::frontend::rectangular_scroll_area();
        scroll->set_h_scroll_policy(nk::ScrollPolicy::Never);
        scroll->set_content(mapperbus::frontend::PaddingSlot::create({0, 24, 0, 0}, table));
        scroll->allocate({0, 0, 600, 120});
        scroll->scroll_to(0, 10000);
        scroll->allocate({0, 0, 600, 120});
        nk::SnapshotContext snapshot;
        static_cast<nk::Widget&>(*scroll).snapshot(snapshot);
        auto root = snapshot.take_root();
        nk::SoftwareRenderer renderer;
        renderer.begin_frame({600, 120}, 1);
        renderer.render(*root);
        renderer.end_frame();
        const auto* rgba = renderer.pixel_data();
        const auto pixel = [&](int x, int y) { return rgba + (y * 600 + x) * 4; };
        const auto* left = pixel(0, 119);
        const auto* right = pixel(575, 119);
        const auto* gutter = pixel(599, 119);
        CHECK(left[0] == right[0]);
        CHECK(left[1] == right[1]);
        CHECK(left[2] == right[2]);
        CHECK(left[0] != gutter[0]);
    }
}
