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

TEST_CASE("Keyboard focus reveals binding controls through nested viewports",
          "[gui][focus][scroll]") {
    MacTheme theme;
    nk::Window window({.title = "Nested settings", .width = 400, .height = 180});
    auto table = mapperbus::frontend::InputBindingTable::create("Disabled");
    for (int i = 0; i < 8; ++i) {
        table->add_binding("Action", "Key", "Pad", [] {}, [] {}, false, "Disabled");
    }
    auto inner = mapperbus::frontend::rectangular_scroll_area();
    inner->set_h_scroll_policy(nk::ScrollPolicy::Never);
    inner->set_content(table);
    auto column = mapperbus::frontend::Box::vertical();
    column->append(mapperbus::frontend::FixedHeightSlot::create(100));
    column->append(mapperbus::frontend::BoundedHeightSlot::create(120, inner));
    auto outer = mapperbus::frontend::rectangular_scroll_area();
    outer->set_h_scroll_policy(nk::ScrollPolicy::Never);
    outer->set_content(column);
    window.set_child(outer);
    outer->allocate({0, 0, 400, 180});
    // The outer viewport gets the column's natural height; the inner viewport
    // still has to scroll within its own 120-point allocation.
    for (int i = 0; i < 8; ++i) {
        window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
        const auto cell = table->children()[i * 2]->allocation();
        CHECK(cell.y >= inner->allocation().y);
        CHECK(cell.bottom() <= inner->allocation().bottom());
        CHECK(cell.y >= outer->allocation().y);
        CHECK(cell.bottom() <= outer->allocation().bottom());
    }
    CHECK(inner->v_offset() > 0);
    CHECK(outer->v_offset() > 0);
    CHECK(inner->h_offset() == 0);
    CHECK(outer->h_offset() == 0);
    for (int i = 6; i >= 0; --i) {
        window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                                   .key = nk::KeyCode::Tab,
                                   .modifiers = nk::Modifiers::Shift});
        const auto cell = table->children()[i * 2]->allocation();
        CHECK(cell.y >= inner->allocation().y);
        CHECK(cell.bottom() <= outer->allocation().bottom());
    }
}

TEST_CASE("Settings viewport yields space to its navigation and footer", "[gui][layout]") {
    MacTheme theme;
    auto root = mapperbus::frontend::Box::vertical(16);
    auto heading = mapperbus::frontend::FixedHeightSlot::create(28);
    auto scroll = mapperbus::frontend::rectangular_scroll_area();
    scroll->set_content(mapperbus::frontend::FixedHeightSlot::create(900));
    auto viewport = mapperbus::frontend::BoundedHeightSlot::create(520, scroll);
    auto footer = mapperbus::frontend::FixedHeightSlot::create(28);
    root->append(heading);
    root->append(viewport);
    root->append(footer);
    for (const float height : {220.0F, 400.0F, 600.0F}) {
        root->allocate({0, 0, 600, height});
        CHECK(viewport->allocation().height > 0);
        CHECK(viewport->allocation().bottom() <= footer->allocation().y);
        CHECK(footer->allocation().bottom() <= height);
    }
}

TEST_CASE("Settings groups render one themed card in both appearances", "[gui][settings][render]") {
    for (const auto scheme : {nk::ColorScheme::Light, nk::ColorScheme::Dark}) {
        MacTheme theme(scheme);
        auto group = mapperbus::frontend::SettingsGroup::create("Display");
        group->add(nk::PreferencesRow::create("Scale"));
        group->add(nk::PreferencesRow::create("Density"));
        const auto height = group->measure(nk::Constraints::unbounded()).natural_height;
        group->allocate({0, 0, 400, height});
        nk::SnapshotContext snapshot;
        static_cast<nk::Widget&>(*group).snapshot(snapshot);
        const auto root = snapshot.take_root();
        nk::SoftwareRenderer renderer;
        renderer.begin_frame({400, height}, 1);
        renderer.render(*root);
        renderer.end_frame();
        const auto* rgba = renderer.pixel_data();
        const auto first = group->rows().front()->allocation();
        const auto second = group->rows().back()->allocation();
        CHECK(first.bottom() == second.y);
        const int y = static_cast<int>(first.y + 10);
        const auto background = rgba[(y * 400 + 390) * 4];
        if (scheme == nk::ColorScheme::Light) {
            CHECK(background > 200);
        } else {
            CHECK(background < 100);
        }
    }
}
