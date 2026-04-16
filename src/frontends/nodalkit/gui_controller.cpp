#include "frontends/nodalkit/gui_controller.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <nk/widgets/label.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mappers/mapper_registry.hpp"
#include "platform/video/fsr1.hpp"
#include "platform/video/null_video.hpp"
#include "platform/video/xbrz.hpp"

namespace mapperbus::frontend {
namespace {

struct KeyBindingOption {
    nk::KeyCode key = nk::KeyCode::Unknown;
    const char* label = "";
};

constexpr std::array<core::Button, 8> kBindingOrder = {
    core::Button::Up,
    core::Button::Down,
    core::Button::Left,
    core::Button::Right,
    core::Button::A,
    core::Button::B,
    core::Button::Start,
    core::Button::Select,
};

constexpr std::array<KeyBindingOption, 17> kKeyBindingOptions = {{
    {nk::KeyCode::Up, "Up Arrow"},
    {nk::KeyCode::Down, "Down Arrow"},
    {nk::KeyCode::Left, "Left Arrow"},
    {nk::KeyCode::Right, "Right Arrow"},
    {nk::KeyCode::X, "X"},
    {nk::KeyCode::Z, "Z"},
    {nk::KeyCode::C, "C"},
    {nk::KeyCode::A, "A"},
    {nk::KeyCode::S, "S"},
    {nk::KeyCode::D, "D"},
    {nk::KeyCode::Q, "Q"},
    {nk::KeyCode::W, "W"},
    {nk::KeyCode::E, "E"},
    {nk::KeyCode::Space, "Space"},
    {nk::KeyCode::Return, "Enter"},
    {nk::KeyCode::RightShift, "Right Shift"},
    {nk::KeyCode::LeftShift, "Left Shift"},
}};

std::vector<nk::Menu> build_window_menus() {
    return {
        {"File",
         {
             nk::MenuItem::action("Open ROM...", "file.open"),
             nk::MenuItem::action("Close ROM", "file.close"),
             nk::MenuItem::make_separator(),
             nk::MenuItem::action("Quit", "file.quit"),
         }},
        {"Emulation",
         {
             nk::MenuItem::action("Pause or Resume", "emu.pause"),
             nk::MenuItem::action("Step Frame", "emu.step"),
             nk::MenuItem::action("Reset", "emu.reset"),
             nk::MenuItem::action("Power Cycle", "emu.power"),
         }},
        {"Settings",
         {
             nk::MenuItem::action("Settings...", "settings.open"),
         }},
        {"Help",
         {
             nk::MenuItem::action("About mapperbus", "help.about"),
         }},
    };
}

nk::NativeMenuShortcut command_shortcut(nk::KeyCode key) {
    return nk::NativeMenuShortcut{
        .key = key,
        .modifiers = nk::NativeMenuModifier::Super,
    };
}

std::vector<nk::NativeMenu> build_native_menus(std::string_view app_name) {
    const std::string resolved_app_name = app_name.empty() ? "mapperbus" : std::string(app_name);
    return {
        {resolved_app_name,
         {
             nk::NativeMenuItem::action("About " + resolved_app_name, "help.about"),
             nk::NativeMenuItem::make_separator(),
             nk::NativeMenuItem::action(
                 "Quit " + resolved_app_name, "file.quit", command_shortcut(nk::KeyCode::Q)),
         }},
        {"File",
         {
             nk::NativeMenuItem::action(
                 "Open ROM...", "file.open", command_shortcut(nk::KeyCode::O)),
             nk::NativeMenuItem::action("Close ROM", "file.close"),
         }},
        {"Emulation",
         {
             nk::NativeMenuItem::action("Pause or Resume", "emu.pause"),
             nk::NativeMenuItem::action("Step Frame", "emu.step"),
             nk::NativeMenuItem::action("Reset", "emu.reset"),
             nk::NativeMenuItem::action("Power Cycle", "emu.power"),
         }},
        {"Settings",
         {
             nk::NativeMenuItem::action(
                 "Settings...", "settings.open", command_shortcut(nk::KeyCode::Comma)),
         }},
        {"Help",
         {
             nk::NativeMenuItem::action("About " + resolved_app_name, "help.about"),
         }},
    };
}

constexpr std::array<std::string_view, 4> kPreviewScaleLabels = {
    "Pixel Perfect",
    "Smooth",
    "xBRZ 2x",
    "FSR 2x",
};

constexpr std::array<std::string_view, 4> kDensityLabels = {
    "System Default",
    "Standard",
    "Comfortable",
    "Compact",
};

constexpr std::array<std::string_view, 2> kAudioModeLabels = {
    "Automatic",
    "Muted",
};

std::vector<std::string> owned_labels(std::span<const std::string_view> labels) {
    std::vector<std::string> items;
    items.reserve(labels.size());
    for (const auto label : labels) {
        items.emplace_back(label);
    }
    return items;
}

int preview_scale_index(MapperBusGuiController::PreviewScaleOption option) {
    switch (option) {
    case MapperBusGuiController::PreviewScaleOption::Smooth:
        return 1;
    case MapperBusGuiController::PreviewScaleOption::Xbrz2x:
        return 2;
    case MapperBusGuiController::PreviewScaleOption::Fsr2x:
        return 3;
    case MapperBusGuiController::PreviewScaleOption::PixelPerfect:
    default:
        return 0;
    }
}

MapperBusGuiController::PreviewScaleOption preview_scale_for_index(int index) {
    switch (index) {
    case 1:
        return MapperBusGuiController::PreviewScaleOption::Smooth;
    case 2:
        return MapperBusGuiController::PreviewScaleOption::Xbrz2x;
    case 3:
        return MapperBusGuiController::PreviewScaleOption::Fsr2x;
    case 0:
    default:
        return MapperBusGuiController::PreviewScaleOption::PixelPerfect;
    }
}

std::string preview_scale_description(MapperBusGuiController::PreviewScaleOption option) {
    switch (option) {
    case MapperBusGuiController::PreviewScaleOption::Smooth:
        return "Video scale set to smooth.";
    case MapperBusGuiController::PreviewScaleOption::Xbrz2x:
        return "Video scale set to xBRZ 2x.";
    case MapperBusGuiController::PreviewScaleOption::Fsr2x:
        return "Video scale set to FSR 2x.";
    case MapperBusGuiController::PreviewScaleOption::PixelPerfect:
    default:
        return "Video scale set to pixel perfect.";
    }
}

int density_index(nk::ThemeDensity density) {
    switch (density) {
    case nk::ThemeDensity::Standard:
        return 1;
    case nk::ThemeDensity::Comfortable:
        return 2;
    case nk::ThemeDensity::Compact:
        return 3;
    case nk::ThemeDensity::SystemDefault:
    default:
        return 0;
    }
}

nk::ThemeDensity density_for_index(int index) {
    switch (index) {
    case 1:
        return nk::ThemeDensity::Standard;
    case 2:
        return nk::ThemeDensity::Comfortable;
    case 3:
        return nk::ThemeDensity::Compact;
    case 0:
    default:
        return nk::ThemeDensity::SystemDefault;
    }
}

int audio_mode_index(bool muted) {
    return muted ? 1 : 0;
}

std::string platform_family_name(nk::PlatformFamily family) {
    switch (family) {
    case nk::PlatformFamily::MacOS:
        return "macOS";
    case nk::PlatformFamily::Windows:
        return "Windows";
    case nk::PlatformFamily::Linux:
        return "Linux";
    case nk::PlatformFamily::Unknown:
    default:
        return "Unknown";
    }
}

std::string renderer_backend_label(nk::RendererBackend backend) {
    switch (backend) {
    case nk::RendererBackend::Metal:
        return "Metal";
    case nk::RendererBackend::OpenGL:
        return "OpenGL";
    case nk::RendererBackend::Vulkan:
        return "Vulkan";
    case nk::RendererBackend::Software:
    default:
        return "Software";
    }
}

std::shared_ptr<nk::TextField> read_only_value(std::string text) {
    auto field = nk::TextField::create(std::move(text));
    field->set_editable(false);
    return field;
}

std::shared_ptr<Box> labeled_row(std::string label, std::shared_ptr<nk::Widget> value) {
    auto row = Box::horizontal(14.0F);
    row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    auto name = FieldLabel::create(std::move(label));
    auto label_slot = FixedWidthSlot::create(112.0F, name);
    value->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    value->set_horizontal_stretch(1);
    row->append(label_slot);
    row->append(std::move(value));
    return row;
}

std::string basename_for_display(std::string_view path) {
    if (path.empty()) {
        return "No ROM loaded";
    }

    const auto parsed = std::filesystem::path(path);
    if (parsed.filename().empty()) {
        return std::string(path);
    }
    return parsed.filename().string();
}

std::string region_name(core::Region region) {
    switch (region) {
    case core::Region::PAL:
        return "PAL";
    case core::Region::Dendy:
        return "Dendy";
    case core::Region::Multi:
        return "Multi";
    default:
        return "NTSC";
    }
}

std::string tick_result_name(app::TickResult result) {
    switch (result) {
    case app::TickResult::FrameAdvanced:
        return "Running";
    case app::TickResult::AudioBackpressure:
        return "Audio backpressure";
    case app::TickResult::Paused:
        return "Paused";
    case app::TickResult::NoCartridge:
        return "No cartridge";
    case app::TickResult::Stopped:
    default:
        return "Stopped";
    }
}

std::string file_dialog_error_text(nk::FileDialogError error) {
    switch (error) {
    case nk::FileDialogError::Cancelled:
        return "Open ROM cancelled.";
    case nk::FileDialogError::Unsupported:
        return "Native open-file dialog is unsupported on this backend.";
    case nk::FileDialogError::Unavailable:
        return "Native open-file dialog is unavailable.";
    case nk::FileDialogError::Failed:
    default:
        return "Failed to open the native file dialog.";
    }
}

std::string button_name(core::Button button) {
    switch (button) {
    case core::Button::Up:
        return "Up";
    case core::Button::Down:
        return "Down";
    case core::Button::Left:
        return "Left";
    case core::Button::Right:
        return "Right";
    case core::Button::A:
        return "A";
    case core::Button::B:
        return "B";
    case core::Button::Start:
        return "Start";
    case core::Button::Select:
        return "Select";
    }

    return "Unknown";
}

std::vector<std::string> key_binding_labels() {
    std::vector<std::string> labels;
    labels.reserve(kKeyBindingOptions.size());
    for (const auto& option : kKeyBindingOptions) {
        labels.emplace_back(option.label);
    }
    return labels;
}

std::string key_label(nk::KeyCode key) {
    for (const auto& option : kKeyBindingOptions) {
        if (option.key == key) {
            return option.label;
        }
    }
    return "Unknown";
}

int combo_index_for_key(nk::KeyCode key) {
    for (std::size_t index = 0; index < kKeyBindingOptions.size(); ++index) {
        if (kKeyBindingOptions[index].key == key) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

nk::KeyCode key_for_combo_index(int index) {
    if (index < 0 || index >= static_cast<int>(kKeyBindingOptions.size())) {
        return kKeyBindingOptions.front().key;
    }
    return kKeyBindingOptions[static_cast<std::size_t>(index)].key;
}

} // namespace

MapperBusGuiController::MapperBusGuiController(nk::Application& app, nk::Window& window)
    : app_(app), window_(window) {
    auto audio = std::make_unique<GuiAudioBackend>();
    audio_backend_ = audio.get();

    auto input = std::make_unique<NodalKitInput>(window_);
    input_backend_ = input.get();

    session_ = std::make_unique<app::EmulationSession>(
        std::make_unique<platform::NullVideo>(), std::move(audio), std::move(input));
    actions_ = std::make_unique<app::SessionActions>(*session_);

    build_ui();
    wire_ui();
    clear_preview();
    refresh_ui();
    focus_game_surface();

    last_tick_time_ = std::chrono::steady_clock::now();
    tick_handle_ = app_.event_loop().set_interval(std::chrono::milliseconds(1), [this] {
        const auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_tick_time_);
        last_tick_time_ = now;

        const auto snapshot = actions_->snapshot();
        const auto target_frame_duration =
            std::chrono::nanoseconds(core::timing_for_region(snapshot.region).frame_duration_ns);
        const auto max_accumulated = target_frame_duration * 4;
        if (elapsed > max_accumulated) {
            elapsed = max_accumulated;
        }
        frame_accumulator_ = std::min(frame_accumulator_ + elapsed, max_accumulated);

        if (!snapshot.has_cartridge) {
            frame_accumulator_ = std::chrono::nanoseconds{0};
            return;
        }

        int steps = 0;
        while (frame_accumulator_ >= target_frame_duration && steps < 4) {
            const auto result = actions_->tick();
            if (result == app::TickResult::FrameAdvanced) {
                refresh_preview();
            }

            if (result == app::TickResult::FrameAdvanced || result == app::TickResult::Paused ||
                result == app::TickResult::NoCartridge) {
                refresh_ui();
            }

            if (result == app::TickResult::AudioBackpressure) {
                frame_accumulator_ = std::min(frame_accumulator_, target_frame_duration);
                break;
            }
            if (result == app::TickResult::Stopped) {
                frame_accumulator_ = std::chrono::nanoseconds{0};
                break;
            }

            frame_accumulator_ -= target_frame_duration;
            ++steps;
        }
    });
}

void MapperBusGuiController::open_initial_rom(std::string_view rom_path) {
    if (!rom_path.empty()) {
        attempt_open(std::string(rom_path));
    }
}

void MapperBusGuiController::build_ui() {
    root_ = Box::vertical(0.0F);
    root_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    root_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    root_->set_vertical_stretch(1);

    if (app_.supports_native_app_menu()) {
        app_.set_native_app_menu(build_native_menus(app_.app_name()));
    } else {
        menu_bar_ = nk::MenuBar::create();
        for (auto menu : build_window_menus()) {
            menu_bar_->add_menu(std::move(menu));
        }
    }

    preview_ = PreviewCanvas::create();
    preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    preview_->set_focusable(true);
    preview_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    preview_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    preview_->set_horizontal_stretch(1);
    preview_->set_vertical_stretch(1);

    status_bar_ = nk::StatusBar::create();
    status_bar_->set_segments({"Stopped", "NTSC", "Ready"});

    if (menu_bar_) {
        root_->append(menu_bar_);
    }
    root_->append(preview_);
    root_->append(status_bar_);

    window_.set_child(root_);
}

void MapperBusGuiController::wire_ui() {
    (void)window_.on_close_requested().connect([this] { app_.quit(0); });
    (void)app_.on_native_app_menu_action().connect(
        [this](std::string_view action) { handle_menu_action(action); });
    if (menu_bar_) {
        (void)menu_bar_->on_action().connect(
            [this](std::string_view action) { handle_menu_action(action); });
    }
}

void MapperBusGuiController::clear_preview() {
    update_preview_surface(
        blank_frame_.pixels.data(), mapperbus::core::kScreenWidth, mapperbus::core::kScreenHeight);
}

void MapperBusGuiController::refresh_preview() {
    const auto& frame = session_->emulator().frame_buffer();
    update_preview_surface(
        frame.pixels.data(), mapperbus::core::kScreenWidth, mapperbus::core::kScreenHeight);
}

void MapperBusGuiController::refresh_ui() {
    const app::SessionSnapshot snapshot = actions_->snapshot();
    const bool loaded = snapshot.has_cartridge;
    const bool paused = loaded && snapshot.paused;
    const std::string state_text = loaded ? (paused ? "Paused" : "Running") : "Stopped";
    const std::string loaded_media = basename_for_display(snapshot.rom_path);
    status_bar_->set_segments({
        state_text,
        region_name(snapshot.region),
        loaded ? loaded_media : "Ready",
    });

    window_.set_title("mapperbus");
}

void MapperBusGuiController::set_message(std::string message) {
    if (status_bar_) {
        status_bar_->set_segment(2, std::move(message));
    }
}

void MapperBusGuiController::focus_game_surface() {
    if (preview_) {
        preview_->grab_focus();
    }
}

void MapperBusGuiController::browse_for_rom() {
    auto dialog_result = app_.open_file_dialog("Open ROM", {"*.nes", "*.fds"});
    if (dialog_result) {
        attempt_open(*dialog_result);
        return;
    }

    if (dialog_result.error() != nk::FileDialogError::Cancelled) {
        set_message(file_dialog_error_text(dialog_result.error()));
    }
    focus_game_surface();
}

void MapperBusGuiController::attempt_open(std::string rom_path) {
    if (rom_path.empty()) {
        set_message("Open a ROM to start playback.");
        focus_game_surface();
        return;
    }

    auto result = actions_->open_rom(rom_path);
    if (!result) {
        set_message("Failed to open ROM: " + result.error());
        refresh_ui();
        clear_preview();
        focus_game_surface();
        return;
    }

    actions_->step_frame();
    frame_accumulator_ = std::chrono::nanoseconds{0};
    last_tick_time_ = std::chrono::steady_clock::now();
    refresh_preview();
    set_message("Loaded " + basename_for_display(rom_path) + ". " + gameplay_hint_text());
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::toggle_pause() {
    actions_->toggle_pause();
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::step_frame() {
    if (!actions_->snapshot().has_cartridge) {
        return;
    }

    const auto result = actions_->step_frame();
    refresh_preview();
    set_message("Stepped one frame: " + tick_result_name(result) + ".");
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::reset_session() {
    actions_->reset();
    refresh_preview();
    set_message("Reset current ROM.");
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::power_cycle_session() {
    actions_->power_cycle();
    refresh_preview();
    set_message("Power-cycled current ROM.");
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::close_current_rom() {
    actions_->close_rom();
    frame_accumulator_ = std::chrono::nanoseconds{0};
    clear_preview();
    set_message("Closed current ROM.");
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::set_preview_scale_option(PreviewScaleOption option) {
    if (preview_scale_option_ == option) {
        return;
    }

    preview_scale_option_ = option;
    switch (preview_scale_option_) {
    case PreviewScaleOption::Xbrz2x:
        preview_upscaler_ = std::make_unique<platform::XbrzUpscaler>(2);
        break;
    case PreviewScaleOption::Fsr2x:
        preview_upscaler_ = std::make_unique<platform::Fsr1Upscaler>(2);
        break;
    case PreviewScaleOption::PixelPerfect:
    case PreviewScaleOption::Smooth:
    default:
        preview_upscaler_.reset();
        break;
    }

    if (actions_->snapshot().has_cartridge) {
        refresh_preview();
    } else {
        clear_preview();
    }
}

void MapperBusGuiController::update_preview_surface(const std::uint32_t* data,
                                                    int width,
                                                    int height) {
    if (!preview_ || data == nullptr || width <= 0 || height <= 0) {
        return;
    }

    if (preview_scale_option_ == PreviewScaleOption::PixelPerfect) {
        preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
        preview_->update_pixel_buffer(data, width, height);
        return;
    }
    if (preview_scale_option_ == PreviewScaleOption::Smooth) {
        preview_->set_scale_mode(nk::ScaleMode::Bilinear);
        preview_->update_pixel_buffer(data, width, height);
        return;
    }

    if (!preview_upscaler_) {
        preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
        preview_->update_pixel_buffer(data, width, height);
        return;
    }

    const auto factor = preview_upscaler_->scale_factor();
    const auto source_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto scaled_width = width * factor;
    const auto scaled_height = height * factor;
    preview_staging_pixels_.resize(static_cast<std::size_t>(scaled_width) *
                                   static_cast<std::size_t>(scaled_height));
    preview_upscaler_->scale(std::span<const std::uint32_t>(data, source_size),
                             width,
                             height,
                             std::span<std::uint32_t>(preview_staging_pixels_));
    preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    preview_->update_pixel_buffer(preview_staging_pixels_.data(), scaled_width, scaled_height);
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_dialog_shell() {
    auto content = Box::vertical(18.0F);
    const auto page_index = [this]() {
        switch (settings_page_) {
        case SettingsPage::Video:
            return 1;
        case SettingsPage::Audio:
            return 2;
        case SettingsPage::Input:
        default:
            return 0;
        }
    };

    auto tabs_row = Box::horizontal(12.0F);
    settings_tabs_ = nk::SegmentedControl::create();
    settings_tabs_->set_segments({"Input", "Video", "Audio"});
    settings_tabs_->set_selected_index(page_index());
    (void)settings_tabs_->on_selection_changed().connect([this](int index) {
        const auto next_page = [index]() {
            switch (index) {
            case 1:
                return SettingsPage::Video;
            case 2:
                return SettingsPage::Audio;
            case 0:
            default:
                return SettingsPage::Input;
            }
        }();
        if (settings_page_ == next_page) {
            return;
        }
        settings_page_ = next_page;
        refresh_settings_dialog_sections();
    });
    tabs_row->append(settings_tabs_);
    tabs_row->append(Spacer::create());
    content->append(tabs_row);

    settings_page_slot_ = ContentSlot::create();
    settings_page_slot_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    content->append(settings_page_slot_);

    settings_footer_slot_ = ContentSlot::create();
    settings_footer_slot_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    content->append(settings_footer_slot_);

    refresh_settings_dialog_sections();
    return content;
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_page_content() {
    auto page = Box::vertical(12.0F);
    page->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::Input) {
        page->append(SecondaryText::create(
            "Configure the keyboard mapping used by the embedded game surface."));

        auto labels = key_binding_labels();
        for (const auto button : kBindingOrder) {
            auto combo = nk::ComboBox::create();
            combo->set_items(labels);
            combo->set_selected_index(combo_index_for_key(input_backend_->binding(button)));
            (void)combo->on_selection_changed().connect([this, button](int index) {
                input_backend_->set_binding(button, key_for_combo_index(index));
                set_message(button_name(button) + " mapped to " +
                            key_label(input_backend_->binding(button)) + ".");
                refresh_ui();
            });
            page->append(labeled_row(button_name(button), combo));
        }
    } else if (settings_page_ == SettingsPage::Video) {
        page->append(
            SecondaryText::create("Tune how the preview surface and interface are presented."));

        auto scale_combo = nk::ComboBox::create();
        scale_combo->set_items(owned_labels(kPreviewScaleLabels));
        scale_combo->set_selected_index(preview_scale_index(preview_scale_option_));
        (void)scale_combo->on_selection_changed().connect([this](int index) {
            const auto option = preview_scale_for_index(index);
            set_preview_scale_option(option);
            set_message(preview_scale_description(option));
        });
        page->append(labeled_row("Scale", scale_combo));

        auto density_combo = nk::ComboBox::create();
        density_combo->set_items(owned_labels(kDensityLabels));
        density_combo->set_selected_index(density_index(app_.theme_selection().density));
        (void)density_combo->on_selection_changed().connect([this](int index) {
            auto selection = app_.theme_selection();
            selection.density = density_for_index(index);
            app_.set_theme_selection(selection);
            set_message("Interface density updated.");
        });
        page->append(labeled_row("UI Density", density_combo));

        page->append(labeled_row(
            "Platform Theme",
            read_only_value(platform_family_name(app_.system_preferences().platform_family))));
        page->append(labeled_row("Features", read_only_value(video_features_text())));
    } else {
        page->append(
            SecondaryText::create("Manage audio output behavior for this frontend session."));

        auto output_combo = nk::ComboBox::create();
        output_combo->set_items(owned_labels(kAudioModeLabels));
        output_combo->set_selected_index(audio_mode_index(audio_backend_->is_muted()));
        (void)output_combo->on_selection_changed().connect([this](int index) {
            audio_backend_->set_muted(index == 1);
            set_message(index == 1 ? "Audio muted." : "Audio output restored.");
            refresh_ui();
        });
        page->append(labeled_row("Playback", output_combo));

        page->append(
            labeled_row("Backend", read_only_value(std::string(audio_backend_->status_text()))));
        page->append(labeled_row(
            "Queued Samples", read_only_value(std::to_string(audio_backend_->queued_samples()))));
    }

    return page;
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_footer_content() {
    auto footer = Box::horizontal(12.0F);
    footer->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::Input) {
        auto restore_defaults = nk::Button::create("Restore Defaults");
        (void)restore_defaults->on_clicked().connect([this] {
            input_backend_->reset_default_bindings();
            set_message("Restored default keyboard bindings. " + gameplay_hint_text());
            refresh_ui();
            refresh_settings_dialog_sections();
        });
        footer->append(restore_defaults);
    }
    footer->append(Spacer::create());
    auto close_button = nk::Button::create("Close");
    (void)close_button->on_clicked().connect([this] {
        if (settings_dialog_) {
            settings_dialog_->close(nk::DialogResponse::Close);
        }
    });
    footer->append(close_button);

    return footer;
}

void MapperBusGuiController::refresh_settings_dialog_sections() {
    if (!settings_dialog_) {
        return;
    }

    const auto page_index = [this]() {
        switch (settings_page_) {
        case SettingsPage::Video:
            return 1;
        case SettingsPage::Audio:
            return 2;
        case SettingsPage::Input:
        default:
            return 0;
        }
    };

    if (settings_tabs_) {
        settings_tabs_->set_selected_index(page_index());
    }
    if (settings_page_slot_) {
        settings_page_slot_->set_child(build_settings_page_content());
    }
    if (settings_footer_slot_) {
        settings_footer_slot_->set_child(build_settings_footer_content());
    }
    if (settings_dialog_->is_presented()) {
        settings_dialog_->queue_layout();
        settings_dialog_->queue_redraw();
    }
}

void MapperBusGuiController::open_settings_dialog() {
    settings_page_ = SettingsPage::Input;
    if (settings_dialog_ && settings_dialog_->is_presented()) {
        refresh_settings_dialog_sections();
        return;
    }

    settings_dialog_ = nk::Dialog::create("Settings");
    settings_dialog_->set_content(build_settings_dialog_shell());
    settings_dialog_->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    settings_dialog_->set_minimum_panel_width(560.0F);
    auto dialog = settings_dialog_;
    (void)settings_dialog_->on_response().connect([this, dialog](nk::DialogResponse /*response*/) {
        app_.event_loop().post(
            [this, dialog] {
                settings_tabs_.reset();
                settings_page_slot_.reset();
                settings_footer_slot_.reset();
                if (settings_dialog_ == dialog) {
                    settings_dialog_.reset();
                }
                refresh_ui();
                focus_game_surface();
            },
            "mapperbus.settings-dialog-close");
    });
    settings_dialog_->present(window_);
}

void MapperBusGuiController::handle_menu_action(std::string_view action) {
    if (action == "file.open") {
        browse_for_rom();
        return;
    }
    if (action == "file.close") {
        close_current_rom();
        return;
    }
    if (action == "file.quit") {
        app_.quit(0);
        return;
    }
    if (action == "emu.pause") {
        toggle_pause();
        return;
    }
    if (action == "emu.step") {
        step_frame();
        return;
    }
    if (action == "emu.reset") {
        reset_session();
        return;
    }
    if (action == "emu.power") {
        power_cycle_session();
        return;
    }
    if (action == "settings.open") {
        open_settings_dialog();
        return;
    }
    if (action == "help.about") {
        auto dialog = nk::Dialog::create(
            "About mapperbus", "mapperbus\nNodalKit host for NES, Famicom, and FDS sessions");
        dialog->add_button("OK", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect(
            [this](nk::DialogResponse /*response*/) { focus_game_surface(); });
        dialog->present(window_);
    }
}

std::string MapperBusGuiController::input_status_text() const {
    return input_backend_->uses_default_bindings() ? "Input default" : "Input custom";
}

std::string MapperBusGuiController::gameplay_hint_text() const {
    if (input_backend_->uses_default_bindings()) {
        return "Arrows move, X = A, Z = B, Enter = Start, Right Shift = Select.";
    }

    return "Custom map: Up " + key_label(input_backend_->binding(core::Button::Up)) + ", Down " +
           key_label(input_backend_->binding(core::Button::Down)) + ", Left " +
           key_label(input_backend_->binding(core::Button::Left)) + ", Right " +
           key_label(input_backend_->binding(core::Button::Right)) + ", A " +
           key_label(input_backend_->binding(core::Button::A)) + ", B " +
           key_label(input_backend_->binding(core::Button::B)) + ".";
}

std::string MapperBusGuiController::video_features_text() const {
    return "Renderer: " + renderer_backend_label(window_.renderer_backend());
}

} // namespace mapperbus::frontend
