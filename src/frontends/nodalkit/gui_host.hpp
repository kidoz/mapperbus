#pragma once

#include <functional>
#include <nk/platform/application.h>
#include <nk/style/theme.h>
#include <string_view>
#include <utility>
#include <vector>

namespace mapperbus::frontend {

/// The controller's application services. A software host uses the same event
/// loop and widgets without creating native surfaces or dialogs.
class GuiHost {
  public:
    using OpenFile = std::function<void(std::string_view,
                                        const std::vector<std::string>&,
                                        nk::Application::OpenFileDialogCallback)>;

    explicit GuiHost(nk::Application& application)
        : application_(&application), loop_(&application.event_loop()) {}
    GuiHost(nk::EventLoop& loop, OpenFile open_file)
        : loop_(&loop), open_file_(std::move(open_file)) {}

    nk::EventLoop& event_loop() {
        return *loop_;
    }
    void quit(int code) {
        if (application_)
            application_->quit(code);
        else
            loop_->quit(code);
    }
    std::string_view app_name() const {
        return application_ ? application_->app_name() : "MapperBus";
    }
    const nk::SystemPreferences& system_preferences() const {
        return application_ ? application_->system_preferences() : preferences_;
    }
    const nk::ThemeSelection& theme_selection() const {
        return application_ ? application_->theme_selection() : theme_;
    }
    void set_theme_selection(nk::ThemeSelection selection) {
        if (application_) {
            application_->set_theme_selection(std::move(selection));
        } else {
            theme_ = std::move(selection);
            nk::Theme::set_active(
                nk::make_theme(nk::resolve_theme_selection(theme_, preferences_), preferences_));
        }
    }
    bool supports_native_app_menu() const {
        return application_ && application_->supports_native_app_menu();
    }
    void set_native_app_menu(std::vector<nk::NativeMenu> menus) {
        if (application_)
            application_->set_native_app_menu(std::move(menus));
    }
    nk::Signal<const nk::SystemPreferences&>& on_system_preferences_changed() {
        return application_ ? application_->on_system_preferences_changed() : preferences_changed_;
    }
    nk::Signal<std::string_view>& on_native_app_menu_action() {
        return application_ ? application_->on_native_app_menu_action() : menu_action_;
    }
    void open_file_dialog_async(std::string_view title,
                                const std::vector<std::string>& filters,
                                nk::Application::OpenFileDialogCallback callback) {
        if (application_)
            application_->open_file_dialog_async(title, filters, std::move(callback));
        else
            open_file_(title, filters, std::move(callback));
    }
    void save_file_dialog_async(nk::SaveFileDialogOptions options,
                                nk::Application::SaveFileDialogCallback callback) {
        if (application_)
            application_->save_file_dialog_async(std::move(options), std::move(callback));
        else
            callback(nk::Unexpected(nk::FileDialogError::Unsupported));
    }

  private:
    nk::Application* application_ = nullptr;
    nk::EventLoop* loop_;
    OpenFile open_file_;
    nk::SystemPreferences preferences_;
    nk::ThemeSelection theme_;
    nk::Signal<const nk::SystemPreferences&> preferences_changed_;
    nk::Signal<std::string_view> menu_action_;
};

} // namespace mapperbus::frontend
