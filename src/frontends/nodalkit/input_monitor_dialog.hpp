#pragma once

#include <array>
#include <memory>
#include <nk/platform/events.h>
#include <nk/widgets/dialog.h>

namespace mapperbus::frontend {

/// Modal windows intentionally clear polled keys. Keep diagnostic keys local,
/// and let Return/Space be tested instead of activating the default response.
class InputMonitorDialog final : public nk::Dialog {
  public:
    InputMonitorDialog() : Dialog("Multiplayer Input Monitor", "") {
        set_focusable(true);
    }
    static std::shared_ptr<InputMonitorDialog> create() {
        return std::make_shared<InputMonitorDialog>();
    }
    bool handle_key_event(const nk::KeyEvent& event) override {
        if (event.key == nk::KeyCode::Escape)
            return Dialog::handle_key_event(event);
        const auto key = static_cast<std::size_t>(event.key);
        if (key < held_.size())
            held_[key] = event.type == nk::KeyEvent::Type::Press;
        return true;
    }
    void on_focus_changed(bool focused) override {
        Dialog::on_focus_changed(focused);
        if (!focused)
            clear_keys();
    }
    void clear_keys() {
        held_.fill(false);
    }
    [[nodiscard]] bool key_pressed(nk::KeyCode key) const {
        const auto index = static_cast<std::size_t>(key);
        return key != nk::KeyCode::Unknown && index < held_.size() && held_[index];
    }

  private:
    std::array<bool, 256> held_{};
};
} // namespace mapperbus::frontend
