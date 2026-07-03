#include "frontends/nodalkit/gui_controller.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <nk/controllers/event_controller.h>
#include <nk/widgets/label.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/switch_widget.h>
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

struct GamepadControlOption {
    platform::GamepadControl control{};
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

constexpr std::array<GamepadControlOption, 22> kGamepadControlOptions = {{
    {platform::gamepad_button_control(platform::GamepadButton::DpadUp), "D-pad Up"},
    {platform::gamepad_button_control(platform::GamepadButton::DpadDown), "D-pad Down"},
    {platform::gamepad_button_control(platform::GamepadButton::DpadLeft), "D-pad Left"},
    {platform::gamepad_button_control(platform::GamepadButton::DpadRight), "D-pad Right"},
    {platform::gamepad_button_control(platform::GamepadButton::East), "East Face"},
    {platform::gamepad_button_control(platform::GamepadButton::South), "South Face"},
    {platform::gamepad_button_control(platform::GamepadButton::West), "West Face"},
    {platform::gamepad_button_control(platform::GamepadButton::North), "North Face"},
    {platform::gamepad_button_control(platform::GamepadButton::Start), "Start"},
    {platform::gamepad_button_control(platform::GamepadButton::Back), "Back / Select"},
    {platform::gamepad_button_control(platform::GamepadButton::LeftShoulder), "Left Shoulder"},
    {platform::gamepad_button_control(platform::GamepadButton::RightShoulder), "Right Shoulder"},
    {platform::gamepad_button_control(platform::GamepadButton::LeftStick), "Left Stick"},
    {platform::gamepad_button_control(platform::GamepadButton::RightStick), "Right Stick"},
    {platform::gamepad_axis_control(platform::GamepadAxis::LeftX,
                                    platform::GamepadControlKind::AxisNegative),
     "Left Stick Left"},
    {platform::gamepad_axis_control(platform::GamepadAxis::LeftX,
                                    platform::GamepadControlKind::AxisPositive),
     "Left Stick Right"},
    {platform::gamepad_axis_control(platform::GamepadAxis::LeftY,
                                    platform::GamepadControlKind::AxisNegative),
     "Left Stick Up"},
    {platform::gamepad_axis_control(platform::GamepadAxis::LeftY,
                                    platform::GamepadControlKind::AxisPositive),
     "Left Stick Down"},
    {platform::gamepad_axis_control(platform::GamepadAxis::RightX,
                                    platform::GamepadControlKind::AxisNegative),
     "Right Stick Left"},
    {platform::gamepad_axis_control(platform::GamepadAxis::RightX,
                                    platform::GamepadControlKind::AxisPositive),
     "Right Stick Right"},
    {platform::gamepad_axis_control(platform::GamepadAxis::RightY,
                                    platform::GamepadControlKind::AxisNegative),
     "Right Stick Up"},
    {platform::gamepad_axis_control(platform::GamepadAxis::RightY,
                                    platform::GamepadControlKind::AxisPositive),
     "Right Stick Down"},
}};

constexpr std::array<std::string_view, 4> kGamepadDeadzoneLabels = {
    "Low",
    "Standard",
    "High",
    "Firm",
};

constexpr std::array<int, 4> kGamepadDeadzoneValues = {
    8000,
    12000,
    16000,
    22000,
};

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
             nk::MenuItem::make_separator(),
             nk::MenuItem::action("Save State", "emu.save-state"),
             nk::MenuItem::action("Load State", "emu.load-state"),
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
             nk::NativeMenuItem::make_separator(),
             nk::NativeMenuItem::action(
                 "Save State", "emu.save-state", command_shortcut(nk::KeyCode::S)),
             nk::NativeMenuItem::action(
                 "Load State", "emu.load-state", command_shortcut(nk::KeyCode::L)),
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

constexpr std::array<std::string_view, 3> kAudioSampleRateLabels = {
    "44.1 kHz",
    "48 kHz",
    "96 kHz",
};

constexpr std::array<int, 3> kAudioSampleRateValues = {
    44100,
    48000,
    96000,
};

constexpr std::array<std::string_view, 2> kAudioResamplingLabels = {
    "Blip",
    "Cubic",
};

constexpr std::array<std::string_view, 3> kAudioFilterModeLabels = {
    "Unfiltered",
    "Hardware Accurate",
    "Enhanced",
};

constexpr std::array<std::string_view, 2> kAudioFilterProfileLabels = {
    "NES",
    "Famicom",
};

constexpr std::array<std::string_view, 2> kAudioStereoLabels = {
    "Mono",
    "Pseudo Stereo",
};

constexpr std::array<std::string_view, 2> kAudioExpansionMixingLabels = {
    "Simple",
    "Resistance",
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

int audio_sample_rate_index(int sample_rate) {
    int best_index = 0;
    int best_distance = std::abs(sample_rate - kAudioSampleRateValues.front());
    for (std::size_t index = 1; index < kAudioSampleRateValues.size(); ++index) {
        const int distance = std::abs(sample_rate - kAudioSampleRateValues[index]);
        if (distance < best_distance) {
            best_index = static_cast<int>(index);
            best_distance = distance;
        }
    }
    return best_index;
}

int audio_sample_rate_for_index(int index) {
    if (index < 0 || index >= static_cast<int>(kAudioSampleRateValues.size())) {
        return kAudioSampleRateValues.back();
    }
    return kAudioSampleRateValues[static_cast<std::size_t>(index)];
}

int audio_resampling_index(core::ResamplingMode mode) {
    return mode == core::ResamplingMode::CubicHermite ? 1 : 0;
}

core::ResamplingMode audio_resampling_for_index(int index) {
    return index == 1 ? core::ResamplingMode::CubicHermite : core::ResamplingMode::BlipBuffer;
}

int audio_filter_mode_index(core::FilterMode mode) {
    switch (mode) {
    case core::FilterMode::HardwareAccurate:
        return 1;
    case core::FilterMode::Enhanced:
        return 2;
    case core::FilterMode::Unfiltered:
    default:
        return 0;
    }
}

core::FilterMode audio_filter_mode_for_index(int index) {
    switch (index) {
    case 1:
        return core::FilterMode::HardwareAccurate;
    case 2:
        return core::FilterMode::Enhanced;
    case 0:
    default:
        return core::FilterMode::Unfiltered;
    }
}

int audio_filter_profile_index(core::FilterProfile profile) {
    return profile == core::FilterProfile::Famicom ? 1 : 0;
}

core::FilterProfile audio_filter_profile_for_index(int index) {
    return index == 1 ? core::FilterProfile::Famicom : core::FilterProfile::NES;
}

int audio_stereo_index(core::StereoMode mode) {
    return mode == core::StereoMode::PseudoStereo ? 1 : 0;
}

core::StereoMode audio_stereo_for_index(int index) {
    return index == 1 ? core::StereoMode::PseudoStereo : core::StereoMode::Mono;
}

int audio_expansion_mixing_index(core::ExpansionMixingMode mode) {
    return mode == core::ExpansionMixingMode::ResistanceModeled ? 1 : 0;
}

core::ExpansionMixingMode audio_expansion_mixing_for_index(int index) {
    return index == 1 ? core::ExpansionMixingMode::ResistanceModeled
                      : core::ExpansionMixingMode::SimpleSum;
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

std::shared_ptr<Box> value_row(std::string label, std::string value) {
    return labeled_row(std::move(label), ValueText::create(std::move(value)));
}

constexpr float kBindingActionColumnWidth = 76.0F;
constexpr float kBindingValueColumnWidth = 150.0F;

std::shared_ptr<Box> input_binding_row(std::string label,
                                       std::string keyboard_value,
                                       std::string gamepad_value,
                                       std::function<void()> keyboard_action,
                                       std::function<void()> gamepad_action,
                                       bool gamepad_available) {
    auto row = Box::horizontal(10.0F);
    row->set_horizontal_size_policy(nk::SizePolicy::Expanding);

    row->append(
        FixedWidthSlot::create(kBindingActionColumnWidth, FieldLabel::create(std::move(label))));

    auto keyboard_field = ValueText::create(std::move(keyboard_value));
    row->append(FixedWidthSlot::create(kBindingValueColumnWidth, keyboard_field));

    auto gamepad_field = ValueText::create(std::move(gamepad_value));
    gamepad_field->set_dimmed(!gamepad_available);
    row->append(FixedWidthSlot::create(kBindingValueColumnWidth, gamepad_field));

    row->append(Spacer::create());

    auto key_button = nk::Button::create("Key");
    (void)key_button->on_clicked().connect(std::move(keyboard_action));
    row->append(FixedWidthSlot::create(64.0F, key_button));

    auto pad_button = nk::Button::create("Pad");
    pad_button->set_sensitive(gamepad_available);
    (void)pad_button->on_clicked().connect(std::move(gamepad_action));
    row->append(FixedWidthSlot::create(64.0F, pad_button));

    return row;
}

std::shared_ptr<Box> input_bindings_header(bool gamepad_online) {
    auto row = Box::horizontal(10.0F);
    row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    row->append(FixedWidthSlot::create(kBindingActionColumnWidth, FieldLabel::create("Action")));

    row->append(FixedWidthSlot::create(kBindingValueColumnWidth, FieldLabel::create("Keyboard")));

    row->append(FixedWidthSlot::create(
        kBindingValueColumnWidth,
        FieldLabel::create(gamepad_online ? "Gamepad" : "Gamepad (offline)")));

    row->append(Spacer::create());
    row->append(FixedWidthSlot::create(138.0F, FieldLabel::create("Rebind")));
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

std::string key_label(nk::KeyCode key) {
    for (const auto& option : kKeyBindingOptions) {
        if (option.key == key) {
            return option.label;
        }
    }
    switch (key) {
    case nk::KeyCode::Num1:
        return "1";
    case nk::KeyCode::Num2:
        return "2";
    case nk::KeyCode::Num3:
        return "3";
    case nk::KeyCode::Num4:
        return "4";
    case nk::KeyCode::Num5:
        return "5";
    case nk::KeyCode::Num6:
        return "6";
    case nk::KeyCode::Num7:
        return "7";
    case nk::KeyCode::Num8:
        return "8";
    case nk::KeyCode::Num9:
        return "9";
    case nk::KeyCode::Num0:
        return "0";
    case nk::KeyCode::Escape:
        return "Escape";
    case nk::KeyCode::Backspace:
        return "Backspace";
    case nk::KeyCode::Tab:
        return "Tab";
    case nk::KeyCode::Minus:
        return "-";
    case nk::KeyCode::Equals:
        return "=";
    case nk::KeyCode::LeftBracket:
        return "[";
    case nk::KeyCode::RightBracket:
        return "]";
    case nk::KeyCode::Backslash:
        return "\\";
    case nk::KeyCode::Semicolon:
        return ";";
    case nk::KeyCode::Apostrophe:
        return "'";
    case nk::KeyCode::Grave:
        return "`";
    case nk::KeyCode::Comma:
        return ",";
    case nk::KeyCode::Period:
        return ".";
    case nk::KeyCode::Slash:
        return "/";
    case nk::KeyCode::CapsLock:
        return "Caps Lock";
    case nk::KeyCode::F1:
        return "F1";
    case nk::KeyCode::F2:
        return "F2";
    case nk::KeyCode::F3:
        return "F3";
    case nk::KeyCode::F4:
        return "F4";
    case nk::KeyCode::F5:
        return "F5";
    case nk::KeyCode::F6:
        return "F6";
    case nk::KeyCode::F7:
        return "F7";
    case nk::KeyCode::F8:
        return "F8";
    case nk::KeyCode::F9:
        return "F9";
    case nk::KeyCode::F10:
        return "F10";
    case nk::KeyCode::F11:
        return "F11";
    case nk::KeyCode::F12:
        return "F12";
    case nk::KeyCode::PrintScreen:
        return "Print Screen";
    case nk::KeyCode::ScrollLock:
        return "Scroll Lock";
    case nk::KeyCode::Pause:
        return "Pause";
    case nk::KeyCode::Insert:
        return "Insert";
    case nk::KeyCode::Home:
        return "Home";
    case nk::KeyCode::PageUp:
        return "Page Up";
    case nk::KeyCode::Delete:
        return "Delete";
    case nk::KeyCode::End:
        return "End";
    case nk::KeyCode::PageDown:
        return "Page Down";
    case nk::KeyCode::NumpadDivide:
        return "Numpad /";
    case nk::KeyCode::NumpadMultiply:
        return "Numpad *";
    case nk::KeyCode::NumpadMinus:
        return "Numpad -";
    case nk::KeyCode::NumpadPlus:
        return "Numpad +";
    case nk::KeyCode::NumpadEnter:
        return "Numpad Enter";
    case nk::KeyCode::Numpad1:
        return "Numpad 1";
    case nk::KeyCode::Numpad2:
        return "Numpad 2";
    case nk::KeyCode::Numpad3:
        return "Numpad 3";
    case nk::KeyCode::Numpad4:
        return "Numpad 4";
    case nk::KeyCode::Numpad5:
        return "Numpad 5";
    case nk::KeyCode::Numpad6:
        return "Numpad 6";
    case nk::KeyCode::Numpad7:
        return "Numpad 7";
    case nk::KeyCode::Numpad8:
        return "Numpad 8";
    case nk::KeyCode::Numpad9:
        return "Numpad 9";
    case nk::KeyCode::Numpad0:
        return "Numpad 0";
    case nk::KeyCode::NumpadPeriod:
        return "Numpad .";
    case nk::KeyCode::LeftCtrl:
        return "Left Ctrl";
    case nk::KeyCode::LeftAlt:
        return "Left Alt";
    case nk::KeyCode::LeftSuper:
        return "Left Super";
    case nk::KeyCode::RightCtrl:
        return "Right Ctrl";
    case nk::KeyCode::RightAlt:
        return "Right Alt";
    case nk::KeyCode::RightSuper:
        return "Right Super";
    case nk::KeyCode::Unknown:
    default:
        break;
    }
    return "Unknown";
}

std::string gamepad_control_label(const platform::GamepadControl& control) {
    for (const auto& option : kGamepadControlOptions) {
        if (option.control == control) {
            return option.label;
        }
    }
    return platform::gamepad_control_token(control);
}

int gamepad_deadzone_index(std::int16_t deadzone) {
    int best_index = 0;
    int best_distance = std::abs(static_cast<int>(deadzone) - kGamepadDeadzoneValues.front());
    for (std::size_t index = 1; index < kGamepadDeadzoneValues.size(); ++index) {
        const int distance = std::abs(static_cast<int>(deadzone) - kGamepadDeadzoneValues[index]);
        if (distance < best_distance) {
            best_index = static_cast<int>(index);
            best_distance = distance;
        }
    }
    return best_index;
}

std::int16_t gamepad_deadzone_for_index(int index) {
    if (index < 0 || index >= static_cast<int>(kGamepadDeadzoneValues.size())) {
        return static_cast<std::int16_t>(kGamepadDeadzoneValues[1]);
    }
    return static_cast<std::int16_t>(kGamepadDeadzoneValues[static_cast<std::size_t>(index)]);
}

const std::array<platform::GamepadControl, kGamepadControlOptions.size()>&
gamepad_capture_controls() {
    static const auto controls = [] {
        std::array<platform::GamepadControl, kGamepadControlOptions.size()> values{};
        for (std::size_t index = 0; index < kGamepadControlOptions.size(); ++index) {
            values[index] = kGamepadControlOptions[index].control;
        }
        return values;
    }();
    return controls;
}

} // namespace

MapperBusGuiController::MapperBusGuiController(nk::Application& app, nk::Window& window)
    : app_(app), window_(window), configuration_(app::load_mapperbus_configuration()) {
    auto theme_selection = app_.theme_selection();
    theme_selection.density = density_for_index(configuration_.frontend.ui_density_index);
    app_.set_theme_selection(theme_selection);

    preview_scale_option_ = preview_scale_for_index(configuration_.frontend.preview_scale_index);

    auto audio = std::make_unique<GuiAudioBackend>();
    audio_backend_ = audio.get();
    audio_backend_->set_muted(configuration_.frontend.audio_muted);

    auto input = std::make_unique<NodalKitInput>(window_, configuration_.input.gamepad);
    input_backend_ = input.get();
    for (const auto button : kBindingOrder) {
        input_backend_->set_binding(
            button,
            static_cast<nk::KeyCode>(
                configuration_.input.keyboard_bindings[platform::button_index(button)]));
    }

    session_ = std::make_unique<app::EmulationSession>(std::make_unique<platform::NullVideo>(),
                                                       std::move(audio),
                                                       std::move(input),
                                                       configuration_.audio);
    actions_ = std::make_unique<app::SessionActions>(*session_);

    build_ui();
    wire_ui();
    if (preview_scale_option_ == PreviewScaleOption::Xbrz2x) {
        preview_upscaler_ = std::make_unique<platform::XbrzUpscaler>(2);
    } else if (preview_scale_option_ == PreviewScaleOption::Fsr2x) {
        preview_upscaler_ = std::make_unique<platform::Fsr1Upscaler>(2);
    }

    clear_preview();
    refresh_ui();
    focus_game_surface();

    last_tick_time_ = std::chrono::steady_clock::now();
    tick_handle_ = app_.event_loop().set_interval(std::chrono::milliseconds(1), [this] {
        poll_pending_rebind();
        update_input_test_status();

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

        const auto needs_audio_preroll = [this, &snapshot]() {
            return snapshot.running && !snapshot.paused && audio_backend_->uses_realtime_output() &&
                   actions_->audio_queued_samples() < actions_->audio_low_watermark_samples();
        };

        int steps = 0;
        while ((frame_accumulator_ >= target_frame_duration || needs_audio_preroll()) &&
               steps < 4) {
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
    status_bar_->set_segments({"Stopped", "NTSC", "No ROM", status_message_});

    if (menu_bar_) {
        root_->append(menu_bar_);
    }
    root_->append(preview_);
    root_->append(status_bar_);

    window_.set_child(root_);
}

void MapperBusGuiController::wire_ui() {
    (void)window_.on_close_requested().connect([this] {
        save_configuration_state();
        app_.quit(0);
    });
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
        loaded ? loaded_media : "No ROM",
        status_message_,
    });

    window_.set_title("mapperbus");
}

void MapperBusGuiController::set_message(std::string message) {
    status_message_ = std::move(message);
    if (status_bar_) {
        status_bar_->set_segment(3, status_message_);
    }
}

void MapperBusGuiController::focus_game_surface() {
    if (preview_) {
        preview_->grab_focus();
    }
}

void MapperBusGuiController::browse_for_rom() {
    app_.open_file_dialog_async(
        "Open ROM", {"*.nes", "*.fds"}, [this](nk::OpenFileDialogResult dialog_result) {
            if (dialog_result) {
                attempt_open(*dialog_result);
                return;
            }

            if (dialog_result.error() != nk::FileDialogError::Cancelled) {
                set_message(file_dialog_error_text(dialog_result.error()));
            }
            focus_game_surface();
        });
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

void MapperBusGuiController::save_session_state() {
    if (!actions_->snapshot().has_cartridge) {
        set_message("No ROM loaded to save state from.");
        return;
    }

    const auto result = actions_->save_state();
    set_message(result ? "Saved state to " + basename_for_display(actions_->state_path()) + "."
                       : result.error());
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::load_session_state() {
    if (!actions_->snapshot().has_cartridge) {
        set_message("No ROM loaded to load state into.");
        return;
    }

    const auto result = actions_->load_state();
    if (result) {
        refresh_preview();
        set_message("Loaded state from " + basename_for_display(actions_->state_path()) + ".");
    } else {
        set_message(result.error());
    }
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

void MapperBusGuiController::update_configuration_from_state() {
    for (const auto button : kBindingOrder) {
        configuration_.input.keyboard_bindings[platform::button_index(button)] =
            static_cast<int>(input_backend_->binding(button));
    }
    configuration_.input.gamepad = input_backend_->gamepad_config();
    configuration_.audio = actions_->snapshot().audio_settings;
    configuration_.frontend.preview_scale_index = preview_scale_index(preview_scale_option_);
    configuration_.frontend.ui_density_index = density_index(app_.theme_selection().density);
    configuration_.frontend.audio_muted = audio_backend_->is_muted();
}

void MapperBusGuiController::save_configuration_state() {
    update_configuration_from_state();
    auto result = app::save_mapperbus_configuration(configuration_);
    if (!result) {
        const std::string message = "Could not save settings: " + result.error();
        set_message(message);
        update_settings_save_status(message);
        return;
    }
    update_settings_save_status("Saved automatically");
}

void MapperBusGuiController::open_rebind_dialog(RebindDevice device, core::Button button) {
    pending_rebind_ = PendingInputRebind{
        .device = device,
        .button = button,
        .started_at = std::chrono::steady_clock::now(),
    };
    if (device == RebindDevice::Gamepad) {
        pending_rebind_->initially_pressed_gamepad_controls =
            input_backend_->pressed_gamepad_controls(gamepad_capture_controls());
    }

    const std::string target = button_name(button);
    const bool keyboard = device == RebindDevice::Keyboard;
    rebind_dialog_ = nk::Dialog::create("Rebind " + target);
    rebind_dialog_->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    rebind_dialog_->set_minimum_panel_width(420.0F);
    rebind_dialog_->add_button("Cancel", nk::DialogResponse::Cancel);

    auto content = Box::vertical(10.0F);
    content->set_focusable(true);
    content->append(SecondaryText::create(keyboard ? "Press a keyboard key. Escape cancels."
                                                   : "Press a gamepad button or move a stick."));
    content->append(SecondaryText::create("Conflicts are swapped with the previous binding."));

    if (keyboard) {
        auto keyboard_controller = std::make_shared<nk::KeyboardController>();
        (void)keyboard_controller->on_key_pressed().connect(
            [this](int key_code, int /*modifiers*/) {
                if (!pending_rebind_ || pending_rebind_->device != RebindDevice::Keyboard) {
                    return;
                }
                const auto key = static_cast<nk::KeyCode>(key_code);
                if (key == nk::KeyCode::Escape) {
                    cancel_rebind_dialog();
                    return;
                }
                if (key == nk::KeyCode::Unknown) {
                    return;
                }
                apply_keyboard_rebind(pending_rebind_->button, key);
            });
        content->add_controller(std::move(keyboard_controller));
    }

    rebind_dialog_->set_content(content);
    auto dialog = rebind_dialog_;
    (void)rebind_dialog_->on_response().connect([this, dialog](nk::DialogResponse /*response*/) {
        if (rebind_dialog_ == dialog && pending_rebind_) {
            pending_rebind_.reset();
            set_message("Input rebinding cancelled.");
        }
        if (rebind_dialog_ == dialog) {
            rebind_dialog_.reset();
            focus_game_surface();
        }
    });
    rebind_dialog_->present(window_);
    app_.event_loop().post([content] { content->grab_focus(); }, "mapperbus.rebind-focus");
}

void MapperBusGuiController::close_rebind_dialog() {
    pending_rebind_.reset();
    if (rebind_dialog_) {
        auto dialog = rebind_dialog_;
        rebind_dialog_.reset();
        dialog->close(nk::DialogResponse::Accept);
    }
}

void MapperBusGuiController::cancel_rebind_dialog() {
    pending_rebind_.reset();
    if (rebind_dialog_) {
        auto dialog = rebind_dialog_;
        rebind_dialog_.reset();
        dialog->close(nk::DialogResponse::Cancel);
    }
    set_message("Input rebinding cancelled.");
}

void MapperBusGuiController::poll_pending_rebind() {
    if (!pending_rebind_ || pending_rebind_->device != RebindDevice::Gamepad) {
        return;
    }

    const auto elapsed = std::chrono::steady_clock::now() - pending_rebind_->started_at;
    if (elapsed < std::chrono::milliseconds(150)) {
        return;
    }

    const auto pressed = input_backend_->pressed_gamepad_controls(gamepad_capture_controls());
    auto& initially_pressed = pending_rebind_->initially_pressed_gamepad_controls;
    std::erase_if(initially_pressed, [&pressed](const platform::GamepadControl& control) {
        return std::find(pressed.begin(), pressed.end(), control) == pressed.end();
    });
    for (const auto& control : pressed) {
        if (std::find(initially_pressed.begin(), initially_pressed.end(), control) ==
            initially_pressed.end()) {
            apply_gamepad_rebind(pending_rebind_->button, control);
            return;
        }
    }
}

void MapperBusGuiController::apply_keyboard_rebind(core::Button button, nk::KeyCode key) {
    const auto previous = input_backend_->binding(button);
    std::optional<core::Button> conflict_button;
    for (const auto other : kBindingOrder) {
        if (other != button && input_backend_->binding(other) == key) {
            conflict_button = other;
            break;
        }
    }

    const auto commit = [this, button, key, previous, conflict_button] {
        if (conflict_button) {
            input_backend_->set_binding(*conflict_button, previous);
        }
        input_backend_->set_binding(button, key);
        save_configuration_state();

        std::string message = button_name(button) + " mapped to " + key_label(key) + ".";
        if (conflict_button) {
            message += " Replaced " + button_name(*conflict_button) + ".";
        }
        set_message(std::move(message));
        refresh_settings_dialog_sections();
        refresh_ui();
    };

    if (conflict_button) {
        close_rebind_dialog();
        auto dialog = nk::Dialog::create("Replace binding?",
                                         key_label(key) + " is already bound to " +
                                             button_name(*conflict_button) +
                                             ". Replace it and move the previous binding to " +
                                             button_name(*conflict_button) + "?");
        dialog->add_button("Cancel", nk::DialogResponse::Cancel);
        dialog->add_button("Replace", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect([this, dialog, commit](nk::DialogResponse response) {
            if (response == nk::DialogResponse::Accept) {
                commit();
            } else {
                set_message("Input rebinding cancelled.");
            }
            focus_game_surface();
        });
        dialog->present(window_);
        return;
    }

    commit();
    close_rebind_dialog();
}

void MapperBusGuiController::apply_gamepad_rebind(core::Button button,
                                                  platform::GamepadControl control) {
    const auto previous = input_backend_->gamepad_binding(button);
    std::optional<core::Button> conflict_button;
    for (const auto other : kBindingOrder) {
        if (other != button && input_backend_->gamepad_binding(other) == control) {
            conflict_button = other;
            break;
        }
    }

    const auto commit = [this, button, control, previous, conflict_button] {
        if (conflict_button) {
            input_backend_->set_gamepad_binding(*conflict_button, previous);
        }
        input_backend_->set_gamepad_binding(button, control);
        save_configuration_state();

        std::string message = button_name(button) + " gamepad input mapped to " +
                              gamepad_control_label(control) + ".";
        if (conflict_button) {
            message += " Replaced " + button_name(*conflict_button) + ".";
        }
        set_message(std::move(message));
        refresh_settings_dialog_sections();
        refresh_ui();
    };

    if (conflict_button) {
        close_rebind_dialog();
        auto dialog = nk::Dialog::create("Replace binding?",
                                         gamepad_control_label(control) + " is already bound to " +
                                             button_name(*conflict_button) +
                                             ". Replace it and move the previous binding to " +
                                             button_name(*conflict_button) + "?");
        dialog->add_button("Cancel", nk::DialogResponse::Cancel);
        dialog->add_button("Replace", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect([this, dialog, commit](nk::DialogResponse response) {
            if (response == nk::DialogResponse::Accept) {
                commit();
            } else {
                set_message("Input rebinding cancelled.");
            }
            focus_game_surface();
        });
        dialog->present(window_);
        return;
    }

    commit();
    close_rebind_dialog();
}

void MapperBusGuiController::apply_audio_settings_change(std::string message) {
    auto result = actions_->apply_audio_settings(configuration_.audio);
    if (!result) {
        configuration_.audio = actions_->snapshot().audio_settings;
        set_message("Audio settings failed: " + result.error());
        refresh_settings_dialog_sections();
        refresh_ui();
        return;
    }

    save_configuration_state();
    set_message(std::move(message));
    refresh_ui();
}

void MapperBusGuiController::update_input_test_status() {
    if (!input_test_label_) {
        return;
    }

    input_backend_->poll();
    input_test_label_->set_text(input_test_status_text());
}

void MapperBusGuiController::update_settings_save_status(std::string text) {
    settings_save_status_ = std::move(text);
    if (settings_save_label_) {
        settings_save_label_->set_text(settings_save_status_);
    }
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_dialog_shell() {
    auto content = Box::vertical(18.0F);
    content->set_vertical_size_policy(nk::SizePolicy::Expanding);
    content->set_vertical_stretch(1);
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
    settings_page_slot_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    settings_page_slot_->set_vertical_stretch(1);
    content->append(settings_page_slot_);

    settings_footer_slot_ = ContentSlot::create();
    settings_footer_slot_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    settings_footer_slot_->set_margin({12.0F, 32.0F, 0.0F, 0.0F});
    content->append(settings_footer_slot_);

    refresh_settings_dialog_sections();
    return RightBleedSlot::create(20.0F, FixedWidthSlot::create(640.0F, content));
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_page_content() {
    auto page = Box::vertical(16.0F);
    page->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::Input) {
        page->append(SecondaryText::create("Configure controller input for the game surface."));

        page->append(SectionTitle::create("Controller"));
        page->append(value_row("Keyboard", "Ready"));
        if (!input_backend_->gamepad_support_available()) {
            page->append(SecondaryText::create("Gamepad support is unavailable in this build."));
        } else {
            const bool gamepad_enabled = input_backend_->gamepad_config().enabled;
            auto enabled_row = Box::horizontal(10.0F);
            enabled_row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
            auto enabled_switch = nk::Switch::create();
            enabled_switch->set_active(gamepad_enabled);
            (void)enabled_switch->on_toggled().connect([this](bool active) {
                input_backend_->set_gamepad_enabled(active);
                save_configuration_state();
                set_message(active ? "Gamepad input enabled." : "Gamepad input disabled.");
                refresh_ui();
                refresh_settings_dialog_sections();
            });
            enabled_row->append(enabled_switch);
            enabled_row->append(Spacer::create());
            page->append(labeled_row("Gamepad input", enabled_row));

            auto index_combo = nk::ComboBox::create();
            auto device_labels = input_backend_->gamepad_device_labels();
            index_combo->set_items(device_labels);
            index_combo->set_selected_index(
                std::clamp(input_backend_->gamepad_config().gamepad_index,
                           0,
                           std::max(0, static_cast<int>(device_labels.size()) - 1)));
            index_combo->set_sensitive(input_backend_->gamepad_config().enabled);
            (void)index_combo->on_selection_changed().connect([this](int index) {
                input_backend_->set_gamepad_index(index);
                save_configuration_state();
                set_message("Preferred gamepad updated.");
                refresh_ui();
                refresh_settings_dialog_sections();
            });
            page->append(labeled_row("Preferred device", index_combo));

            auto deadzone_control = nk::SegmentedControl::create();
            deadzone_control->set_segments(owned_labels(kGamepadDeadzoneLabels));
            deadzone_control->set_selected_index(
                gamepad_deadzone_index(input_backend_->gamepad_config().axis_deadzone));
            deadzone_control->set_sensitive(input_backend_->gamepad_config().enabled);
            (void)deadzone_control->on_selection_changed().connect([this](int index) {
                input_backend_->set_gamepad_deadzone(gamepad_deadzone_for_index(index));
                save_configuration_state();
                set_message(
                    "Gamepad deadzone set to " +
                    std::string(
                        kGamepadDeadzoneLabels[static_cast<std::size_t>(std::clamp(index, 0, 3))]) +
                    ".");
                refresh_ui();
            });
            page->append(labeled_row("Deadzone", deadzone_control));

            page->append(SecondaryText::create(input_backend_->gamepad_status_text()));
        }

        page->append(SectionTitle::create("Bindings"));
        input_test_label_ = SecondaryText::create(input_test_status_text());
        page->append(input_test_label_);
        const bool gamepad_available = input_backend_->gamepad_support_available() &&
                                       input_backend_->gamepad_config().enabled &&
                                       input_backend_->gamepad_device_count() > 0;
        auto bindings_table = Box::vertical(0.0F);
        bindings_table->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        bindings_table->set_margin({0.0F, 0.0F, 18.0F, 0.0F});
        auto bindings_header = StripedRow::wrap(input_bindings_header(gamepad_available), false);
        bindings_header->set_margin({0.0F, 0.0F, 6.0F, 0.0F});
        bindings_table->append(std::move(bindings_header));
        int binding_row_index = 0;
        for (const auto button : kBindingOrder) {
            auto binding_row = input_binding_row(
                button_name(button),
                key_label(input_backend_->binding(button)),
                gamepad_control_label(input_backend_->gamepad_binding(button)),
                [this, button] { open_rebind_dialog(RebindDevice::Keyboard, button); },
                [this, button] { open_rebind_dialog(RebindDevice::Gamepad, button); },
                gamepad_available);
            bindings_table->append(
                StripedRow::wrap(std::move(binding_row), (binding_row_index % 2) == 1));
            ++binding_row_index;
        }
        page->append(bindings_table);
    } else if (settings_page_ == SettingsPage::Video) {
        input_test_label_.reset();
        page->append(
            SecondaryText::create("Tune how the preview surface and interface are presented."));

        auto scale_combo = nk::ComboBox::create();
        scale_combo->set_items(owned_labels(kPreviewScaleLabels));
        scale_combo->set_selected_index(preview_scale_index(preview_scale_option_));
        (void)scale_combo->on_selection_changed().connect([this](int index) {
            const auto option = preview_scale_for_index(index);
            set_preview_scale_option(option);
            save_configuration_state();
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
            save_configuration_state();
            set_message("Interface density updated.");
        });
        page->append(labeled_row("UI Density", density_combo));

        page->append(labeled_row(
            "Platform Theme",
            read_only_value(platform_family_name(app_.system_preferences().platform_family))));
        page->append(labeled_row("Renderer", read_only_value(video_features_text())));
    } else {
        input_test_label_.reset();
        page->append(
            SecondaryText::create("Manage audio output behavior for this frontend session."));

        auto sample_rate_combo = nk::ComboBox::create();
        sample_rate_combo->set_items(owned_labels(kAudioSampleRateLabels));
        sample_rate_combo->set_selected_index(
            audio_sample_rate_index(configuration_.audio.sample_rate));
        (void)sample_rate_combo->on_selection_changed().connect([this](int index) {
            configuration_.audio.sample_rate = audio_sample_rate_for_index(index);
            apply_audio_settings_change("Audio sample rate updated.");
        });
        page->append(labeled_row("Sample Rate", sample_rate_combo));

        auto resampling_control = nk::SegmentedControl::create();
        resampling_control->set_segments(owned_labels(kAudioResamplingLabels));
        resampling_control->set_selected_index(
            audio_resampling_index(configuration_.audio.resampling));
        (void)resampling_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.resampling = audio_resampling_for_index(index);
            apply_audio_settings_change("Audio resampling updated.");
        });
        page->append(labeled_row("Resampling", resampling_control));

        auto filter_mode_combo = nk::ComboBox::create();
        filter_mode_combo->set_items(owned_labels(kAudioFilterModeLabels));
        filter_mode_combo->set_selected_index(
            audio_filter_mode_index(configuration_.audio.filter_mode));
        (void)filter_mode_combo->on_selection_changed().connect([this](int index) {
            configuration_.audio.filter_mode = audio_filter_mode_for_index(index);
            apply_audio_settings_change("Audio filter mode updated.");
        });
        page->append(labeled_row("Filter", filter_mode_combo));

        auto filter_profile_control = nk::SegmentedControl::create();
        filter_profile_control->set_segments(owned_labels(kAudioFilterProfileLabels));
        filter_profile_control->set_selected_index(
            audio_filter_profile_index(configuration_.audio.filter_profile));
        (void)filter_profile_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.filter_profile = audio_filter_profile_for_index(index);
            apply_audio_settings_change("Audio filter profile updated.");
        });
        page->append(labeled_row("Profile", filter_profile_control));

        auto stereo_control = nk::SegmentedControl::create();
        stereo_control->set_segments(owned_labels(kAudioStereoLabels));
        stereo_control->set_selected_index(audio_stereo_index(configuration_.audio.stereo_mode));
        (void)stereo_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.stereo_mode = audio_stereo_for_index(index);
            apply_audio_settings_change("Audio channel mode updated.");
        });
        page->append(labeled_row("Channels", stereo_control));

        auto dither_row = Box::horizontal(10.0F);
        dither_row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        auto dither_switch = nk::Switch::create();
        dither_switch->set_active(configuration_.audio.dithering_enabled);
        (void)dither_switch->on_toggled().connect([this](bool active) {
            configuration_.audio.dithering_enabled = active;
            apply_audio_settings_change(active ? "Audio dithering enabled."
                                               : "Audio dithering disabled.");
        });
        dither_row->append(dither_switch);
        dither_row->append(Spacer::create());
        page->append(labeled_row("Dithering", dither_row));

        auto mixing_control = nk::SegmentedControl::create();
        mixing_control->set_segments(owned_labels(kAudioExpansionMixingLabels));
        mixing_control->set_selected_index(
            audio_expansion_mixing_index(configuration_.audio.expansion_mixing));
        (void)mixing_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.expansion_mixing = audio_expansion_mixing_for_index(index);
            apply_audio_settings_change("Expansion audio mixing updated.");
        });
        page->append(labeled_row("Expansion", mixing_control));

        auto output_combo = nk::ComboBox::create();
        output_combo->set_items(owned_labels(kAudioModeLabels));
        output_combo->set_selected_index(audio_mode_index(audio_backend_->is_muted()));
        (void)output_combo->on_selection_changed().connect([this](int index) {
            audio_backend_->set_muted(index == 1);
            save_configuration_state();
            set_message(index == 1 ? "Audio muted." : "Audio output restored.");
            refresh_ui();
        });
        page->append(labeled_row("Playback", output_combo));

        page->append(
            labeled_row("Backend", read_only_value(std::string(audio_backend_->status_text()))));
    }

    return page;
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_footer_content() {
    auto container = Box::vertical(6.0F);
    container->set_horizontal_size_policy(nk::SizePolicy::Expanding);

    settings_save_label_.reset();

    auto footer = Box::horizontal(12.0F);
    footer->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::Input) {
        auto restore_keyboard = nk::Button::create("Restore Keyboard");
        (void)restore_keyboard->on_clicked().connect([this] {
            input_backend_->reset_default_bindings();
            save_configuration_state();
            set_message("Restored default keyboard bindings. " + gameplay_hint_text());
            refresh_ui();
            refresh_settings_dialog_sections();
        });
        footer->append(restore_keyboard);

        auto restore_gamepad = nk::Button::create("Restore Gamepad");
        restore_gamepad->set_sensitive(input_backend_->gamepad_support_available());
        (void)restore_gamepad->on_clicked().connect([this] {
            input_backend_->reset_default_gamepad_bindings();
            save_configuration_state();
            set_message("Restored default gamepad bindings. " + gameplay_hint_text());
            refresh_ui();
            refresh_settings_dialog_sections();
        });
        footer->append(restore_gamepad);
    }
    footer->append(Spacer::create());
    auto close_button = nk::Button::create("Close");
    (void)close_button->on_clicked().connect([this] {
        if (settings_dialog_) {
            settings_dialog_->close(nk::DialogResponse::Close);
        }
    });
    footer->append(close_button);
    container->append(footer);

    return container;
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
        constexpr float kSettingsPageMaxHeight = 440.0F;
        const float previous_h_offset =
            settings_scroll_area_ ? settings_scroll_area_->h_offset() : 0.0F;
        const float previous_v_offset =
            settings_scroll_area_ ? settings_scroll_area_->v_offset() : 0.0F;
        auto page = build_settings_page_content();
        page->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        page->set_horizontal_stretch(1);
        page->set_vertical_size_policy(nk::SizePolicy::Preferred);
        page->set_vertical_stretch(0);

        auto padded = PaddingSlot::create({12.0F, 32.0F, 32.0F, 0.0F}, std::move(page));
        padded->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        padded->set_vertical_size_policy(nk::SizePolicy::Preferred);
        padded->set_vertical_stretch(0);

        auto scroll = nk::ScrollArea::create();
        scroll->set_h_scroll_policy(nk::ScrollPolicy::Never);
        scroll->set_v_scroll_policy(nk::ScrollPolicy::Automatic);
        scroll->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        scroll->set_vertical_size_policy(nk::SizePolicy::Expanding);
        scroll->set_vertical_stretch(1);
        scroll->set_content(std::move(padded));
        settings_scroll_area_ = scroll;
        settings_page_slot_->set_child(BoundedHeightSlot::create(kSettingsPageMaxHeight, scroll));
        settings_scroll_area_->scroll_to(previous_h_offset, previous_v_offset);
        app_.event_loop().post(
            [scroll, previous_h_offset, previous_v_offset] {
                scroll->scroll_to(previous_h_offset, previous_v_offset);
            },
            "mapperbus.settings-scroll-restore");
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
    settings_dialog_->set_minimum_panel_width(640.0F);
    auto dialog = settings_dialog_;
    (void)settings_dialog_->on_response().connect([this, dialog](nk::DialogResponse /*response*/) {
        app_.event_loop().post(
            [this, dialog] {
                settings_tabs_.reset();
                settings_page_slot_.reset();
                settings_footer_slot_.reset();
                settings_scroll_area_.reset();
                input_test_label_.reset();
                settings_save_label_.reset();
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
    if (action == "emu.save-state") {
        save_session_state();
        return;
    }
    if (action == "emu.load-state") {
        load_session_state();
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
    return input_backend_->uses_default_bindings() &&
                   input_backend_->uses_default_gamepad_bindings()
               ? "Input default"
               : "Input custom";
}

std::string MapperBusGuiController::input_test_status_text() const {
    std::vector<std::string> pressed;
    for (const auto button : kBindingOrder) {
        if (input_backend_->is_button_pressed(0, button)) {
            pressed.push_back(button_name(button));
        }
    }

    if (pressed.empty()) {
        return "Pressed: none";
    }

    std::string text = "Pressed: ";
    for (std::size_t index = 0; index < pressed.size(); ++index) {
        if (index > 0) {
            text += " + ";
        }
        text += pressed[index];
    }
    return text;
}

std::string MapperBusGuiController::gameplay_hint_text() const {
    const std::string gamepad =
        input_backend_->gamepad_config().enabled ? " Gamepad input is enabled." : "";

    if (input_backend_->uses_default_bindings() &&
        input_backend_->uses_default_gamepad_bindings()) {
        return "Arrows move, X = A, Z = B, Enter = Start, Right Shift = Select." + gamepad;
    }

    return "Custom map: Up " + key_label(input_backend_->binding(core::Button::Up)) + ", Down " +
           key_label(input_backend_->binding(core::Button::Down)) + ", Left " +
           key_label(input_backend_->binding(core::Button::Left)) + ", Right " +
           key_label(input_backend_->binding(core::Button::Right)) + ", A " +
           key_label(input_backend_->binding(core::Button::A)) + ", B " +
           key_label(input_backend_->binding(core::Button::B)) + "." + gamepad;
}

std::string MapperBusGuiController::video_features_text() const {
    return renderer_backend_label(window_.renderer_backend());
}

} // namespace mapperbus::frontend
