#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/button.h>
#include <string>
#include <utility>
#include <vector>

namespace mapperbus::frontend {

inline nk::FontDescriptor binding_font() {
    return {.family = {}, .size = 13.5F, .weight = nk::FontWeight::Regular};
}

// The value is the action target, so its label and text position stay stable
// across hover/focus instead of shifting inside a separate Rebind button.
class BindingCell final : public nk::Button {
  public:
    static std::shared_ptr<BindingCell> create(std::string value) {
        return std::shared_ptr<BindingCell>(new BindingCell(std::move(value)));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints&) const override {
        const float height = theme_number("control-height", 28.0F);
        const float width = std::max(120.0F,
                                     measure_text(label(), binding_font()).width +
                                         theme_number("spacing-xl", 24.0F));
        return {width, height, width, height};
    }

    bool handle_mouse_event(const nk::MouseEvent& event) override {
        return is_sensitive() && Button::handle_mouse_event(event);
    }

    bool handle_key_event(const nk::KeyEvent& event) override {
        return is_sensitive() && Button::handle_key_event(event);
    }

    [[nodiscard]] nk::CursorShape cursor_shape() const override {
        return is_sensitive() ? nk::CursorShape::PointingHand : nk::CursorShape::Default;
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto bounds = allocation();
        const float radius = theme_number("radius-control", 6.0F);
        const bool disabled = !is_sensitive();
        auto border = theme_color("border-subtle", nk::Color::from_rgb(210, 210, 214));
        auto text = theme_color(disabled ? "text-secondary" : "text-primary",
                                nk::Color::from_rgb(96, 96, 100));
        if (disabled) {
            border.a *= 0.45F;
            text.a *= 0.65F;
        }
        if (nk::has_flag(state_flags(), nk::StateFlags::FocusVisible)) {
            ctx.add_border({bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4},
                           theme_color("focus-ring-color"),
                           2.0F,
                           radius + 2);
        }
        const bool hovered = nk::has_flag(state_flags(), nk::StateFlags::Hovered);
        ctx.add_rounded_rect(bounds,
                             theme_color(!disabled && hovered ? "surface-hover" : "field-bg",
                                         nk::Color::from_rgb(250, 250, 252)),
                             radius);
        ctx.add_border(bounds, border, 1.0F, radius);
        const auto font = binding_font();
        const auto size = measure_text(label(), font);
        const float inset = theme_number("spacing-md", 12.0F);
        add_text_elided(ctx,
                        {bounds.x + inset, bounds.y + (bounds.height - size.height) * 0.5F},
                        label(),
                        std::max(0.0F, bounds.width - inset * 2),
                        text,
                        font);
    }

  private:
    explicit BindingCell(std::string value) : Button(std::move(value)) {
        add_style_class("binding-cell");
    }
};

class InputBindingTable final : public nk::Widget {
  public:
    static std::shared_ptr<InputBindingTable> create(std::string gamepad_status) {
        return std::shared_ptr<InputBindingTable>(new InputBindingTable(std::move(gamepad_status)));
    }

    void add_binding(std::string action,
                     std::string keyboard_value,
                     std::string gamepad_value,
                     std::function<void()> keyboard_action,
                     std::function<void()> gamepad_action,
                     bool gamepad_available,
                     std::string gamepad_help) {
        auto keyboard = BindingCell::create(std::move(keyboard_value));
        keyboard->ensure_accessible().set_name("Rebind " + action + " keyboard");
        keyboard->ensure_accessible().set_description(
            "Current binding: " + std::string(keyboard->label()) +
            ". Click or press Return to change.");
        (void)keyboard->on_clicked().connect(std::move(keyboard_action));
        auto gamepad = BindingCell::create(std::move(gamepad_value));
        gamepad->ensure_accessible().set_name("Rebind " + action + " gamepad");
        gamepad->ensure_accessible().set_description(
            gamepad_available
                ? "Current binding: " + std::string(gamepad->label()) + ". Click to change."
                : std::move(gamepad_help));
        gamepad->set_sensitive(gamepad_available);
        (void)gamepad->on_clicked().connect(std::move(gamepad_action));
        append_child(keyboard);
        append_child(gamepad);
        rows_.push_back({std::move(action), std::move(keyboard), std::move(gamepad)});
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints&) const override {
        const float height = header_height() + row_height() * static_cast<float>(rows_.size());
        return {360, height, 560, height};
    }

    void allocate(const nk::Rect& bounds) override {
        Widget::allocate(bounds);
        const auto columns = column_bounds(bounds);
        float y = bounds.y + header_height();
        const float height = row_height();
        for (auto& row : rows_) {
            const auto place = [&](const std::shared_ptr<BindingCell>& cell, float x) {
                const auto request = cell->measure(nk::Constraints::unbounded());
                cell->allocate({x,
                                y + (height - request.natural_height) * 0.5F,
                                columns.width,
                                request.natural_height});
            };
            place(row.keyboard, columns.keyboard);
            place(row.gamepad, columns.gamepad);
            y += height;
        }
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto bounds = allocation();
        const auto columns = column_bounds(bounds);
        const float inset = theme_number("spacing-md", 12.0F);
        const auto heading_font =
            nk::FontDescriptor{.family = {}, .size = 12.0F, .weight = nk::FontWeight::Medium};
        const auto heading = [&](float x, std::string_view text, float width) {
            const auto size = measure_text(text, heading_font);
            add_text_elided(ctx,
                            {x, bounds.y + (header_height() - size.height) * 0.5F},
                            text,
                            width,
                            theme_color("text-secondary", nk::Color::from_rgb(96, 96, 100)),
                            heading_font);
        };
        heading(bounds.x + inset, "Action", columns.keyboard - bounds.x - inset);
        heading(columns.keyboard + inset, "Keyboard", columns.width - inset);
        heading(columns.gamepad + inset,
                gamepad_status_.empty() ? "Gamepad" : "Gamepad · " + gamepad_status_,
                columns.width - inset);

        const auto font = binding_font();
        const float height = row_height();
        float y = bounds.y + header_height();
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (i % 2 == 1) {
                auto stripe = theme_color("surface-hover", nk::Color::from_rgb(232, 232, 236));
                stripe.a *= 0.45F;
                ctx.add_color_rect({bounds.x, y, bounds.width, height}, stripe);
            }
            const auto size = measure_text(rows_[i].action, font);
            ctx.add_text({bounds.x + inset, y + (height - size.height) * 0.5F},
                         rows_[i].action,
                         theme_color("text-primary", nk::Color::from_rgb(24, 24, 28)),
                         font);
            y += height;
        }
        Widget::snapshot(ctx);
    }

  private:
    explicit InputBindingTable(std::string status) : gamepad_status_(std::move(status)) {
        set_horizontal_size_policy(nk::SizePolicy::Expanding);
        ensure_accessible().set_role(nk::AccessibleRole::Group);
        ensure_accessible().set_name("Input bindings");
    }

    struct Row {
        std::string action;
        std::shared_ptr<BindingCell> keyboard;
        std::shared_ptr<BindingCell> gamepad;
    };
    struct Columns {
        float keyboard;
        float gamepad;
        float width;
    };

    [[nodiscard]] float header_height() const {
        return theme_number("spacing-xl", 24.0F) + 4;
    }
    [[nodiscard]] float row_height() const {
        return theme_number("control-height", 28.0F) + theme_number("spacing-sm", 8.0F);
    }
    [[nodiscard]] Columns column_bounds(nk::Rect bounds) const {
        const float inset = theme_number("spacing-md", 12.0F);
        const float gap = theme_number("spacing-lg", 16.0F);
        const float action_width = 72.0F;
        const float width =
            std::max(0.0F, (bounds.width - inset * 2 - gap * 2 - action_width) * 0.5F);
        const float keyboard = bounds.x + inset + action_width + gap;
        return {keyboard, keyboard + width + gap, width};
    }

    std::string gamepad_status_;
    std::vector<Row> rows_;
};

} // namespace mapperbus::frontend
