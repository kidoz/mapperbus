#include "frontends/nodalkit/gui_controller.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <functional>
#include <nk/widgets/spinner.h>

#if __has_include(<nk/actions/shortcut_registry.h>)
#include <nk/actions/shortcut_registry.h>
#define MAPPERBUS_HAVE_NODALKIT_SHORTCUTS 1
#endif
#include <nk/controllers/event_controller.h>
#include <nk/platform/drag_drop.h>
#include <nk/widgets/headerbar.h>
#include <nk/widgets/label.h>
#include <nk/widgets/preferences.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/slider.h>
#include <nk/widgets/switch_widget.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/rom_source.hpp"
#include "core/binary_file.hpp"
#include "core/logger.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_close_policy.hpp"
#include "frontends/nodalkit/input_binding_table.hpp"
#include "frontends/nodalkit/rom_load_error_dialog.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/input/null_input.hpp"
#include "platform/video/fsr1.hpp"
#include "platform/video/null_video.hpp"
#include "platform/video/xbrz.hpp"
#if defined(MAPPERBUS_HAVE_SDL3_AUDIO)
#include "frontends/sdl3/sdl3_microphone.hpp"
#endif

#ifndef MAPPERBUS_VERSION
#define MAPPERBUS_VERSION "dev"
#endif

namespace mapperbus::frontend {
namespace {

// NodalKit 0.2.0 lacks the dismissal signal proposed in NK-03. Connect it
// when available without requiring a locally patched toolkit checkout.
template <typename Menu> void connect_menu_dismissed(Menu& menu, std::function<void()> callback) {
    if constexpr (requires { menu.on_dismissed(); }) {
        (void)menu.on_dismissed().connect(std::move(callback));
    }
}

struct KeyBindingOption {
    nk::KeyCode key = nk::KeyCode::Unknown;
    const char* label = "";
};

struct GamepadControlOption {
    platform::GamepadControl control{};
    const char* label = "";
};

constexpr auto kBindingOrder = std::array{platform::InputAction::Up,
                                          platform::InputAction::Down,
                                          platform::InputAction::Left,
                                          platform::InputAction::Right,
                                          platform::InputAction::A,
                                          platform::InputAction::B,
                                          platform::InputAction::TurboA,
                                          platform::InputAction::TurboB,
                                          platform::InputAction::Start,
                                          platform::InputAction::Select,
                                          platform::InputAction::Microphone};

constexpr std::array<KeyBindingOption, 18> kKeyBindingOptions = {{
    {nk::KeyCode::M, "M"},
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
    {platform::gamepad_button_control(platform::GamepadButton::East), "East · B"},
    {platform::gamepad_button_control(platform::GamepadButton::South), "South · A"},
    {platform::gamepad_button_control(platform::GamepadButton::West), "West · X"},
    {platform::gamepad_button_control(platform::GamepadButton::North), "North · Y"},
    {platform::gamepad_button_control(platform::GamepadButton::Start), "Start"},
    {platform::gamepad_button_control(platform::GamepadButton::Back), "Back"},
    {platform::gamepad_button_control(platform::GamepadButton::LeftShoulder), "L"},
    {platform::gamepad_button_control(platform::GamepadButton::RightShoulder), "R"},
    {platform::gamepad_button_control(platform::GamepadButton::LeftStick), "L3"},
    {platform::gamepad_button_control(platform::GamepadButton::RightStick), "R3"},
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

nk::NativeMenuShortcut command_shortcut(nk::KeyCode key) {
    // macOS menus use Command. Other desktops spell the same chords with Ctrl.
#if defined(__APPLE__)
    const auto modifier = nk::NativeMenuModifier::Super;
#else
    const auto modifier = nk::NativeMenuModifier::Ctrl;
#endif
    return nk::NativeMenuShortcut{
        .key = key,
        .modifiers = modifier,
    };
}

nk::NativeMenuShortcut plain_shortcut(
    nk::KeyCode key, nk::NativeMenuModifier modifiers = nk::NativeMenuModifier::None) {
    return nk::NativeMenuShortcut{
        .key = key,
        .modifiers = modifiers,
    };
}

std::vector<nk::NativeMenu> build_native_menus(std::string_view app_name, int active_slot = 0) {
    std::vector<nk::NativeMenuItem> slots;
    slots.reserve(app::kSaveStateSlotCount + 3);
    for (int slot = 0; slot < app::kSaveStateSlotCount; ++slot) {
        slots.push_back(nk::NativeMenuItem::action("Slot " + std::to_string(slot + 1) +
                                                       (slot == active_slot ? " (selected)" : ""),
                                                   "emu.slot-" + std::to_string(slot)));
    }
    slots.push_back(nk::NativeMenuItem::make_separator());
    slots.push_back(nk::NativeMenuItem::action("Choose State Slot…", "emu.choose-slot"));
    slots.push_back(nk::NativeMenuItem::action("Open States Folder", "emu.open-states-folder"));
    const std::string resolved_app_name = app_name.empty() ? "mapperbus" : std::string(app_name);
    return {
        {resolved_app_name,
         {
             nk::NativeMenuItem::action("About " + resolved_app_name, "help.about"),
             nk::NativeMenuItem::make_separator(),
#if defined(__APPLE__)
             nk::NativeMenuItem::action(
                 "Settings…", "settings.open", command_shortcut(nk::KeyCode::Comma)),
             nk::NativeMenuItem::make_separator(),
#endif
             nk::NativeMenuItem::action(
                 "Quit " + resolved_app_name, "file.quit", command_shortcut(nk::KeyCode::Q)),
         }},
        {"File",
         {
             nk::NativeMenuItem::action("Open ROM…", "file.open", command_shortcut(nk::KeyCode::O)),
             nk::NativeMenuItem::action("Close ROM", "file.close"),
         }},
        {"Emulation",
         {
             nk::NativeMenuItem::action(
                 "Pause or Resume", "emu.pause", plain_shortcut(nk::KeyCode::P)),
             nk::NativeMenuItem::action("Reset", "emu.reset", plain_shortcut(nk::KeyCode::R)),
             nk::NativeMenuItem::make_separator(),
             nk::NativeMenuItem::action("Save State — Slot " + std::to_string(active_slot + 1),
                                        "emu.save-state",
                                        plain_shortcut(nk::KeyCode::F5)),
             nk::NativeMenuItem::action("Load State — Slot " + std::to_string(active_slot + 1),
                                        "emu.load-state",
                                        plain_shortcut(nk::KeyCode::F7)),
             nk::NativeMenuItem::submenu("State Slot", std::move(slots)),
             nk::NativeMenuItem::make_separator(),
             nk::NativeMenuItem::action("Step Frame", "emu.step"),
             nk::NativeMenuItem::action("Power Cycle", "emu.power"),
             nk::NativeMenuItem::make_separator(),
             nk::NativeMenuItem::action("Disk Controls…", "fds.controls"),
             nk::NativeMenuItem::action(
                 "Next Disk Side", "fds.next", plain_shortcut(nk::KeyCode::F6)),
             nk::NativeMenuItem::action("Eject or Insert Disk", "fds.eject"),
             nk::NativeMenuItem::action(
                 "Toggle Disk Write Protection", "fds.protect", plain_shortcut(nk::KeyCode::F8)),
         }},
#if !defined(__APPLE__)
        {"Settings",
         {
             nk::NativeMenuItem::action(
                 "Settings…", "settings.open", command_shortcut(nk::KeyCode::Comma)),
         }},
#endif
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

/// Build a GNOME boxed-list preference row: title on the left, an arbitrary
/// suffix control on the right. Replaces the flat labeled_row() for settings
/// that live inside a PreferencesGroup.
std::shared_ptr<nk::PreferencesRow> pref_row(std::string title,
                                             std::shared_ptr<nk::Widget> suffix,
                                             std::string subtitle = {}) {
    auto row = nk::PreferencesRow::create(std::move(title), std::move(subtitle));
    if (suffix != nullptr) {
        // ComboBox uses its accessible name for the current selection.
        // Preserve that value and expose the row label as its description.
        if (dynamic_cast<nk::ComboBox*>(suffix.get()) != nullptr) {
            suffix->ensure_accessible().set_description(std::string(row->title()));
        } else {
            suffix->ensure_accessible().set_name(std::string(row->title()));
        }
        suffix->set_horizontal_size_policy(nk::SizePolicy::Preferred);
        row->set_suffix(std::move(suffix));
    }
    return row;
}

/// Read-only preference row showing a static value text as the suffix.
std::shared_ptr<nk::PreferencesRow> pref_value_row(std::string title, std::string value) {
    auto text = ValueText::create(std::move(value));
    text->set_dimmed(true);
    return pref_row(std::move(title), text);
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

std::string title_for_display(std::string_view path) {
    if (path.empty()) {
        return "MapperBus";
    }

    const auto parsed = std::filesystem::path(path);
    auto title = parsed.stem().string();
    if (title.empty()) {
        title = basename_for_display(path);
    }

    std::string cleaned;
    cleaned.reserve(title.size());
    bool pending_space = false;
    for (const auto character : title) {
        if (character == '-' || character == '_' || character == ' ') {
            pending_space = !cleaned.empty();
            continue;
        }
        if (pending_space) {
            cleaned += ' ';
            pending_space = false;
        }
        cleaned += character;
    }
    return cleaned.empty() ? "MapperBus" : cleaned;
}

std::string recent_media_type(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& c : ext) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (ext == ".NES") {
        return "NES";
    }
    if (ext == ".FDS") {
        return "Famicom Disk System";
    }
    return ext.empty() ? "ROM" : ext.substr(1);
}

std::string recent_opened_date(std::int64_t unix_seconds) {
    const auto opened = static_cast<std::time_t>(unix_seconds);
    std::tm opened_local{};
    if (localtime_r(&opened, &opened_local) == nullptr) {
        return "earlier";
    }

    const auto now = std::time(nullptr);
    std::tm today{};
    if (localtime_r(&now, &today) == nullptr) {
        return "earlier";
    }
    if (opened_local.tm_year == today.tm_year && opened_local.tm_yday == today.tm_yday) {
        return "today";
    }

    std::tm yesterday = today;
    yesterday.tm_mday -= 1;
    if (std::mktime(&yesterday) != static_cast<std::time_t>(-1) &&
        opened_local.tm_year == yesterday.tm_year && opened_local.tm_yday == yesterday.tm_yday) {
        return "yesterday";
    }

    static constexpr std::array<const char*, 12> kMonths = {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec",
    };
    const int month = std::clamp(opened_local.tm_mon, 0, 11);
    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%d %s %04d",
                  opened_local.tm_mday,
                  kMonths[static_cast<std::size_t>(month)],
                  opened_local.tm_year + 1900);
    return buffer;
}

std::string nominal_fps_label(core::Region region) {
    // The number is the region's fixed frame rate, not a measured pace.
    switch (region) {
    case core::Region::PAL:
        return "Target 50.01 FPS";
    case core::Region::Dendy:
        return "Target 50.00 FPS";
    case core::Region::Multi:
    case core::Region::NTSC:
    default:
        return "Target 60.10 FPS";
    }
}

std::string reserved_key_message(nk::KeyCode key) {
    switch (key) {
    case nk::KeyCode::P:
        return "P pauses the game. Choose another key.";
    case nk::KeyCode::R:
        return "R resets the game. Choose another key.";
    case nk::KeyCode::F5:
        return "F5 saves a state. Choose another key.";
    case nk::KeyCode::F6:
        return "F6 controls FDS disk sides. Choose another key.";
    case nk::KeyCode::F8:
        return "F8 controls FDS write protection. Choose another key.";
    case nk::KeyCode::F7:
        return "F7 loads a state. Choose another key.";
    case nk::KeyCode::F11:
        return "F11 toggles fullscreen. Choose another key.";
    default:
        return "That key is reserved. Choose another key.";
    }
}

bool is_rom_path(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    for (auto& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == ".nes" || extension == ".fds" || app::is_rom_archive(path);
}

std::string menu_label(std::string_view name, std::string_view shortcut) {
    if (shortcut.empty()) {
        return std::string(name);
    }
    return std::string(name) + " (" + std::string(shortcut) + ")";
}

core::Region region_for_preference(app::RegionPreference preference, core::Region detected) {
    switch (preference) {
    case app::RegionPreference::Ntsc:
        return core::Region::NTSC;
    case app::RegionPreference::Pal:
        return core::Region::PAL;
    case app::RegionPreference::Dendy:
        return core::Region::Dendy;
    case app::RegionPreference::Automatic:
        break;
    }
    return detected;
}

std::string mapper_detail(std::optional<std::uint16_t> mapper_number) {
    if (!mapper_number.has_value()) {
        return "No ROM";
    }

    std::string_view name;
    switch (*mapper_number) {
    case 0:
        name = "NROM";
        break;
    case 1:
        name = "MMC1";
        break;
    case 2:
        name = "UxROM";
        break;
    case 3:
        name = "CNROM";
        break;
    case 4:
        name = "MMC3";
        break;
    case 5:
        name = "MMC5";
        break;
    case 7:
        name = "AxROM";
        break;
    case 9:
        name = "MMC2";
        break;
    case 11:
        name = "Color Dreams";
        break;
    case 19:
        name = "Namco 163";
        break;
    case 24:
    case 26:
        name = "VRC6";
        break;
    case 69:
        name = "Sunsoft 5B";
        break;
    case 85:
        name = "VRC7";
        break;
    default:
        break;
    }

    auto detail = "Mapper " + std::to_string(*mapper_number);
    if (!name.empty()) {
        detail += " · ";
        detail += name;
    }
    return detail;
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

std::string button_name(platform::InputAction button) {
    switch (button) {
    case platform::InputAction::Microphone:
        return "Microphone";
    case platform::InputAction::Up:
        return "Up";
    case platform::InputAction::Down:
        return "Down";
    case platform::InputAction::Left:
        return "Left";
    case platform::InputAction::Right:
        return "Right";
    case platform::InputAction::TurboA:
        return "Turbo A";
    case platform::InputAction::TurboB:
        return "Turbo B";
    case platform::InputAction::A:
        return "A";
    case platform::InputAction::B:
        return "B";
    case platform::InputAction::Start:
        return "Start";
    case platform::InputAction::Select:
        return "Select";
    }

    return "Unknown";
}

std::string key_label(nk::KeyCode key) {
    if (key == nk::KeyCode::Unknown)
        return "Unbound";
    if (key >= nk::KeyCode::A && key <= nk::KeyCode::Z) {
        return std::string(
            1, static_cast<char>('A' + static_cast<int>(key) - static_cast<int>(nk::KeyCode::A)));
    }
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
    if (control.kind == platform::GamepadControlKind::None)
        return "Unbound";
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

// Noncancellable file writes keep their sheet visible until
// completion, including when Escape or Return reaches the dialog itself.
class FileOperationDialog final : public nk::Dialog {
  public:
    FileOperationDialog(std::string title, bool cancellable)
        : nk::Dialog(std::move(title), {}), cancellable_(cancellable) {}
    bool handle_key_event(const nk::KeyEvent& event) override {
        return !cancellable_ || nk::Dialog::handle_key_event(event);
    }

  private:
    bool cancellable_;
};

std::shared_ptr<nk::Dialog> make_bios_validation_dialog() {
    auto content = Box::vertical(12.0F);
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    content->append(spinner);
    auto label = nk::Label::create("Reading and checking the selected 8 KiB BIOS…");
    label->set_wrapping(true);
    content->append(label);
    auto dialog = std::make_shared<FileOperationDialog>("Checking BIOS", true);
    dialog->set_content(FixedWidthSlot::create(480.0F, content));
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    return dialog;
}

} // namespace

MapperBusGuiController::MapperBusGuiController(
    nk::Application& app,
    nk::Window& window,
    app::AsyncDiskExport::Write disk_export_write,
    app::AsyncConfigurationSave::Write configuration_write,
    app::AsyncConfigurationLoad::Read configuration_read,
    app::AsyncRomLoader::CancellablePrepare rom_prepare,
    app::FdsBiosRead bios_read)
    : app_(app), window_(window), configuration_loader_(std::move(configuration_read)),
      configuration_saver_(app::mapperbus_configuration_path(), std::move(configuration_write)),
      rom_loader_(std::move(rom_prepare)), bios_validation_(bios_read),
      settings_bios_validation_(std::move(bios_read)), disk_export_(std::move(disk_export_write)) {
    // Loading has no live host devices or media. Install configured backends
    // only after the owned settings result is adopted on the UI thread.
    session_ = std::make_unique<app::EmulationSession>(std::make_unique<platform::NullVideo>(),
                                                       std::make_unique<platform::NullAudio>(),
                                                       std::make_unique<platform::NullInput>());
    actions_ = std::make_unique<app::SessionActions>(*session_);

    build_ui();
    wire_ui();
    start_configuration_load();

    last_tick_time_ = std::chrono::steady_clock::now();
    tick_handle_ = app_.event_loop().set_interval(std::chrono::milliseconds(1), [this] {
        if (!startup_ready_) {
            poll_configuration_load();
            return;
        }
        poll_rom_load();
        poll_recovery_bios();
        poll_settings_bios();
        poll_state_operation();
        poll_disk_export();
        if (!machine_worker_active())
            poll_configuration_saves();
        poll_session_close();
        if (!machine_worker_active()) {
            session_->poll_disk_saves();
            const auto& error = session_->disk_save_error();
            if (error != announced_disk_save_error_) {
                announced_disk_save_error_ = error;
                if (!error.empty())
                    set_message("Failed to save disk progress: " + error +
                                ". Open Disk Controls to retry.");
            }
        }
        if (close_intent_ || loading_request_ || pending_state_operation_) {
            last_tick_time_ = std::chrono::steady_clock::now();
            frame_accumulator_ = std::chrono::nanoseconds{0};
            return;
        }
        poll_state_storage();
        poll_gamepad_assignment();
        poll_pending_rebind();
        update_input_test_status();
        update_input_monitor();

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
        bool preview_changed = false;
        bool state_changed = false;
        while ((frame_accumulator_ >= target_frame_duration || needs_audio_preroll()) &&
               steps < 4) {
            const auto result = actions_->tick();
            if (result == app::TickResult::FrameAdvanced) {
                preview_changed = true;
            }

            if (result == app::TickResult::FrameAdvanced || result == app::TickResult::Paused ||
                result == app::TickResult::NoCartridge) {
                state_changed = true;
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
        // Catch-up/preroll may advance several frames before the event loop
        // can paint. Scale and upload only the final frame that can be shown.
        if (preview_changed) {
            refresh_preview();
        }
        if (state_changed) {
            refresh_ui();
        }
    });
}

void MapperBusGuiController::start_configuration_load() {
    if (startup_ready_ || startup_loading_ || close_authorized_)
        return;
    const auto primary = app::mapperbus_configuration_path();
    startup_error_.clear();
    if (auto started = configuration_loader_.start({primary, primary.parent_path() / "config.ini"});
        !started) {
        startup_error_ = started.error();
    } else {
        startup_loading_ = true;
    }
    show_startup_view();
}

void MapperBusGuiController::poll_configuration_load() {
    auto loaded = configuration_loader_.poll();
    if (!loaded)
        return;
    startup_loading_ = false;
    if (!*loaded) {
        startup_error_ = loaded->error();
        show_startup_view();
        return;
    }
    finish_startup(std::move(**loaded), true);
}

void MapperBusGuiController::show_startup_view() {
    auto content = Box::vertical(14.0F);
    content->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    content->set_vertical_size_policy(nk::SizePolicy::Expanding);
    auto top = Spacer::create();
    top->set_vertical_size_policy(nk::SizePolicy::Expanding);
    top->set_vertical_stretch(1);
    content->append(top);
    auto column = Box::vertical(14.0F);
    std::shared_ptr<nk::Button> initial_focus;
    if (startup_error_.empty()) {
        auto spinner = nk::Spinner::create();
        spinner->set_spinning(true);
        column->append(spinner);
        auto title = nk::Label::create("Loading settings…");
        title->add_style_class("heading");
        column->append(title);
        auto explanation = nk::Label::create(
            initial_rom_request_ ? "Your game will open when settings are ready."
                                 : "Please wait while your saved preferences are loaded.");
        explanation->add_style_class("muted");
        explanation->set_wrapping(true);
        column->append(explanation);
    } else {
        auto title = nk::Label::create("Could Not Load Settings");
        title->add_style_class("heading");
        column->append(title);
        auto error = nk::Label::create(startup_error_);
        error->set_wrapping(true);
        column->append(error);
        auto help = nk::Label::create("Retry, or continue with defaults without saving settings "
                                      "for this session. Your saved settings will be preserved.");
        help->set_wrapping(true);
        column->append(help);
        auto retry = nk::Button::create("Retry");
        initial_focus = retry;
        (void)retry->on_clicked().connect([this] {
            app_.event_loop().post([this] { start_configuration_load(); },
                                   "mapperbus.startup-retry");
        });
        column->append(retry);
        auto defaults = nk::Button::create("Continue Without Saving");
        (void)defaults->on_clicked().connect([this] {
            app_.event_loop().post([this] { finish_startup({}, false); },
                                   "mapperbus.startup-defaults");
        });
        column->append(defaults);
    }
    auto quit = nk::Button::create("Quit");
    (void)quit->on_clicked().connect([this] {
        app_.event_loop().post([this] { request_session_close(true); }, "mapperbus.startup-quit");
    });
    column->append(quit);
    auto row = Box::horizontal(0.0F);
    row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    row->append(Spacer::create());
    row->append(FixedWidthSlot::create(480.0F, column));
    row->append(Spacer::create());
    content->append(row);
    auto bottom = Spacer::create();
    bottom->set_vertical_size_policy(nk::SizePolicy::Expanding);
    bottom->set_vertical_stretch(1);
    content->append(bottom);
    main_slot_->set_child(content);
    status_bar_->set_segments(
        {startup_error_.empty() ? "Loading settings…" : "Settings unavailable"});
    update_native_toolbar();
    (initial_focus ? initial_focus : quit)->grab_focus();
}

void MapperBusGuiController::finish_startup(app::LoadedConfiguration loaded, bool allow_saving) {
    if (startup_ready_ || close_authorized_)
        return;
    (void)configuration_loader_.cancel();
    startup_loading_ = false;
    configuration_ = std::move(loaded.configuration);
    configuration_saves_enabled_ = allow_saving;
    settings_page_ = static_cast<SettingsPage>(configuration_.frontend.settings_page_index);
    preview_scale_option_ = preview_scale_for_index(configuration_.frontend.preview_scale_index);
    auto theme_selection = app_.theme_selection();
    theme_selection.density = density_for_index(configuration_.frontend.ui_density_index);
    app_.set_theme_selection(theme_selection);
    const bool replaced_reserved_binding = configure_session();
    preview_upscaler_.reset();
    if (preview_scale_option_ == PreviewScaleOption::Xbrz2x)
        preview_upscaler_ = std::make_unique<platform::XbrzUpscaler>(2);
    else if (preview_scale_option_ == PreviewScaleOption::Fsr2x)
        preview_upscaler_ = std::make_unique<platform::Fsr1Upscaler>(2);
    preview_->set_integer_scaling(preview_scale_option_ == PreviewScaleOption::PixelPerfect);
    startup_ready_ = true;
    header_open_button_->set_sensitive(true);
    refresh_welcome_view();
    clear_preview();
    refresh_ui();
    refresh_configuration_save_status();
    if (replaced_reserved_binding)
        save_configuration_state();
    if (!allow_saving)
        set_message("Using defaults. Settings changes will not be saved in this session.");
    else if (loaded.missing)
        set_message("No saved settings found. Using defaults.");
    focus_game_surface();
    if (auto request = std::exchange(initial_rom_request_, {}))
        attempt_open(std::move(request->first), std::move(request->second));
}

bool MapperBusGuiController::configure_session() {
    actions_.reset();
    session_.reset();
    auto audio = std::make_unique<GuiAudioBackend>();
    audio_backend_ = audio.get();
    audio_backend_->set_muted(configuration_.frontend.audio_muted);
    audio_backend_->set_volume(static_cast<float>(configuration_.frontend.audio_volume_percent) /
                               100.0F);

    auto input =
        std::make_unique<NodalKitInput>(window_,
                                        std::array{configuration_.input.gamepad_profile(0),
                                                   configuration_.input.gamepad_profile(1),
                                                   configuration_.input.gamepad_profile(2),
                                                   configuration_.input.gamepad_profile(3),
                                                   configuration_.input.gamepad_profile(4),
                                                   configuration_.input.gamepad_profile(5)});
    input_backend_ = input.get();
    const app::MapperBusInputConfiguration default_input;
    bool replaced_reserved_binding = false;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        auto& bindings = configuration_.input.keyboard_profile(player);
        const auto& defaults = default_input.keyboard_profile(player);
        for (const auto button : kBindingOrder) {
            const auto index = platform::button_index(button);
            auto key = static_cast<nk::KeyCode>(bindings[index]);
            if (NodalKitInput::is_reserved_game_key(key)) {
                key = static_cast<nk::KeyCode>(defaults[index]);
                bindings[index] = static_cast<int>(key);
                replaced_reserved_binding = true;
            }
            input_backend_->player_input(player).set_binding(button, key);
        }
    }

    session_ = std::make_unique<app::EmulationSession>(std::make_unique<platform::NullVideo>(),
                                                       std::move(audio),
                                                       std::move(input),
                                                       configuration_.audio);
    session_->enable_background_disk_saves();
    actions_ = std::make_unique<app::SessionActions>(*session_);
    session_->set_turbo_period(configuration_.input.turbo_period_frames);
    session_->set_state_storage(configuration_.save_states);
    session_->set_fds_bios_path(configuration_.fds_bios_path);
    session_->set_controller_timing(configuration_.input.controller_timing);
    session_->set_controller_wiring(configuration_.input.controller_wiring);
    session_->set_expansion_controllers(configuration_.input.expansion_controllers);
    session_->set_controller_mode(configuration_.input.controller_mode);
#if defined(MAPPERBUS_HAVE_SDL3_AUDIO)
    session_->set_microphone_backend(std::make_unique<Sdl3Microphone>());
#endif
    session_->configure_live_microphone(configuration_.microphone);
    return replaced_reserved_binding;
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

    // Client-side headerbar: GNOME-native titlebar with window controls.
    // Unified style makes the Wayland surface negotiate CSD and reserve the
    // top inset the headerbar paints into.
    headerbar_ = nk::Headerbar::create("MapperBus");
    headerbar_->set_centering_policy(nk::HeaderbarCenteringPolicy::Strict);
    headerbar_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    window_.set_titlebar_style(nk::TitlebarStyle::Unified);

    // GNOME-style headerbar: keep the primary Open action visible and collect
    // secondary commands in one compact menu instead of duplicating them in an
    // in-window menu bar.
    auto open_button = nk::Button::create("Open…");
    header_open_button_ = open_button;
    open_button->set_sensitive(startup_ready_);
    open_button->add_style_class("flat");
    (void)open_button->on_clicked().connect([this] { browse_for_rom(); });
    headerbar_->add_leading(open_button);

    pause_button_ = SessionIconButton::create(SessionIcon::Pause, "Pause");
    pause_button_->set_visible(false);
    (void)pause_button_->on_clicked().connect([this] { toggle_pause(); });

    fullscreen_button_ = SessionIconButton::create(SessionIcon::Fullscreen, "Fullscreen");
    fullscreen_button_->set_visible(false);
    (void)fullscreen_button_->on_clicked().connect([this] { toggle_fullscreen(); });

    auto session_actions = Box::horizontal(4.0F);
    session_actions->append(pause_button_);
    session_actions->append(fullscreen_button_);
    headerbar_->add_trailing(session_actions);

    if (!app_.supports_native_app_menu()) {
        header_menu_button_ = nk::Button::create("☰");
        header_menu_button_->add_style_class("flat");
        header_menu_button_->add_style_class("primary-menu-button");
        header_menu_button_->ensure_accessible().set_name("Main menu");
        header_menu_button_->ensure_accessible().set_description("Open the application menu");
        headerbar_->add_trailing(FixedWidthSlot::create(40.0F, header_menu_button_));

        header_menu_ = nk::ContextMenu::create();
        header_menu_->ensure_accessible().set_name("Application menu");
    }

    if (app_.system_preferences().platform_family == nk::PlatformFamily::MacOS) {
        // Headerbar is client-side chrome and collapses under AppKit. Keep
        // its invisible controls out of the macOS focus/accessibility tree.
        headerbar_->set_visible(false);
    } else {
        root_->append(headerbar_);
    }

    update_native_menu();

    preview_ = PreviewCanvas::create();
    preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    preview_->set_focusable(true);
    preview_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    preview_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    preview_->set_horizontal_stretch(1);
    preview_->set_vertical_stretch(1);
    preview_->set_display_scale(window_.scale_factor());
    (void)window_.on_scale_factor_changed().connect(
        [this](float scale) { preview_->set_display_scale(scale); });
    preview_->set_integer_scaling(preview_scale_option_ == PreviewScaleOption::PixelPerfect);

    welcome_view_ = build_welcome_view();
    main_slot_ = ContentSlot::create();
    main_slot_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    main_slot_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    main_slot_->set_horizontal_stretch(1);
    main_slot_->set_vertical_stretch(1);
    main_slot_->set_child(welcome_view_);

    toast_overlay_ = nk::ToastOverlay::create();
    toast_overlay_->set_child(main_slot_);
    toast_overlay_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    toast_overlay_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    toast_overlay_->set_horizontal_stretch(1);
    toast_overlay_->set_vertical_stretch(1);

    status_bar_ = nk::StatusBar::create();
    status_bar_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    status_bar_->set_segments({"Ready"});
    if constexpr (requires { status_bar_->set_trailing(std::string{}); }) {
        status_bar_->set_trailing("");
    }

    root_->append(toast_overlay_);
    root_->append(status_bar_);
    if (header_menu_) {
        root_->append(header_menu_);
    }

    // F11 as a declared shortcut when the toolkit provides the registry;
    // the key-controller fallback keeps older NodalKit revisions working.
#if defined(MAPPERBUS_HAVE_NODALKIT_SHORTCUTS)
    auto& shortcuts = window_.shortcuts();
    shortcuts.add_action("win.toggle-fullscreen", [this] { toggle_fullscreen(); });
    shortcuts.add_shortcut({nk::Shortcut{static_cast<int>(nk::KeyCode::F11), nk::Modifiers::None},
                            "win.toggle-fullscreen"});

    // Session hotkeys match the SDL frontend: F5/F7 for the selected slot, Shift+F5
    // and Shift+F7 for slot 2. P, R, and F11 stay with the game commands.
    // They stay inert while a modal sheet is presented — most importantly
    // the rebind sheet, which captures keys and must not also pause or reset.
    const auto bind_session_key =
        [this, &shortcuts](nk::KeyCode key, nk::Modifiers modifiers, std::string_view action) {
            shortcuts.add_action(std::string(action), [this, a = std::string(action)] {
                if (modal_dialog_presented() && a != "file.quit") {
                    return;
                }
                handle_menu_action(a);
            });
            shortcuts.add_shortcut(
                {nk::Shortcut{static_cast<int>(key), modifiers}, std::string(action)});
        };
    bind_session_key(nk::KeyCode::P, nk::Modifiers::None, "emu.pause");
    bind_session_key(nk::KeyCode::R, nk::Modifiers::None, "emu.reset");
    bind_session_key(nk::KeyCode::F5, nk::Modifiers::None, "emu.save-state");
    bind_session_key(nk::KeyCode::F7, nk::Modifiers::None, "emu.load-state");
    bind_session_key(nk::KeyCode::F6, nk::Modifiers::None, "fds.next");
    bind_session_key(nk::KeyCode::F6, nk::Modifiers::Shift, "fds.eject");
    bind_session_key(nk::KeyCode::F8, nk::Modifiers::None, "fds.protect");
    bind_session_key(nk::KeyCode::F5, nk::Modifiers::Shift, "emu.save-state-1");
    bind_session_key(nk::KeyCode::F7, nk::Modifiers::Shift, "emu.load-state-1");
    // macOS menu equivalents already bind Ctrl/Command chords. Register
    // them here for desktops whose native menu does not consume the key.
#if !defined(__APPLE__)
    bind_session_key(nk::KeyCode::O, nk::Modifiers::Ctrl, "file.open");
    bind_session_key(nk::KeyCode::Q, nk::Modifiers::Ctrl, "file.quit");
    bind_session_key(nk::KeyCode::Comma, nk::Modifiers::Ctrl, "settings.open");
#endif
#else
    {
        auto root_keys = std::make_shared<nk::KeyboardController>();
        (void)root_keys->on_key_pressed().connect([this](int key_code, int modifiers) {
            const auto key = static_cast<nk::KeyCode>(key_code);
            const auto mods = static_cast<nk::Modifiers>(modifiers);
            if (modal_dialog_presented() &&
                !(key == nk::KeyCode::Q && mods == nk::Modifiers::Ctrl)) {
                return;
            }
            if (key == nk::KeyCode::F11 && mods == nk::Modifiers::None) {
                toggle_fullscreen();
            } else if (key == nk::KeyCode::P && mods == nk::Modifiers::None) {
                handle_menu_action("emu.pause");
            } else if (key == nk::KeyCode::R && mods == nk::Modifiers::None) {
                handle_menu_action("emu.reset");
            } else if (key == nk::KeyCode::F6) {
                handle_menu_action(mods == nk::Modifiers::Shift ? "fds.eject" : "fds.next");
            } else if (key == nk::KeyCode::F8 && mods == nk::Modifiers::None) {
                handle_menu_action("fds.protect");
            } else if (key == nk::KeyCode::F5 && mods == nk::Modifiers::None) {
                handle_menu_action("emu.save-state");
            } else if (key == nk::KeyCode::F7 && mods == nk::Modifiers::None) {
                handle_menu_action("emu.load-state");
            } else if (key == nk::KeyCode::F5 && mods == nk::Modifiers::Shift) {
                handle_menu_action("emu.save-state-1");
            } else if (key == nk::KeyCode::F7 && mods == nk::Modifiers::Shift) {
                handle_menu_action("emu.load-state-1");
            } else if (key == nk::KeyCode::O && mods == nk::Modifiers::Ctrl) {
                handle_menu_action("file.open");
            } else if (key == nk::KeyCode::Q && mods == nk::Modifiers::Ctrl) {
                handle_menu_action("file.quit");
            } else if (key == nk::KeyCode::Comma && mods == nk::Modifiers::Ctrl) {
                handle_menu_action("settings.open");
            }
        });
        root_->add_controller(std::move(root_keys));
    }
#endif

    window_.set_child(root_);
}

void MapperBusGuiController::wire_ui() {
    (void)app_.on_system_preferences_changed().connect([this](const nk::SystemPreferences&) {
        app_.event_loop().post([this] { refresh_settings_dialog_sections(); },
                               "mapperbus.settings-theme");
    });
    guard_async_window_close(
        window_, [this] { return close_authorized_; }, [this] { request_session_close(true); });
    (void)window_.on_close_requested().connect([this] { app_.quit(0); });
    (void)root_->on_drop().connect([this](nk::DragDropEvent& event) {
        if (event.payload == nullptr || !event.payload->has_files()) {
            return;
        }
        event.accept(nk::DragOperation::Copy);
        open_dropped_files(event.payload->files);
    });
    (void)window_.on_window_state_changed().connect([this] { update_fullscreen_chrome(); });
    (void)app_.on_native_app_menu_action().connect([this](std::string_view action) {
        // AppKit must finish dispatching before a slot change rebuilds its menu.
        app_.event_loop().post([this, action = std::string(action)] { handle_menu_action(action); },
                               "mapperbus.native-menu-action");
    });
    if (header_menu_button_ && header_menu_) {
        (void)header_menu_button_->on_clicked().connect([this] {
            if (header_menu_->is_open()) {
                header_menu_->dismiss();
                focus_game_surface();
                return;
            }
            rebuild_header_menu();
            const auto& button = header_menu_button_->allocation();
            // Labels include shortcut hints, so the menu is wider than the button.
            constexpr float menu_width = 280.0F;
            header_menu_->show_at(
                {std::max(8.0F, button.right() - menu_width), button.bottom() + 4.0F});
            header_menu_->grab_focus();
        });
        connect_menu_dismissed(*header_menu_, [this] { focus_game_surface(); });
        (void)header_menu_->on_item_activated().connect([this](int index) {
            if (index < 0 || index >= static_cast<int>(header_menu_actions_.size())) {
                return;
            }
            const auto& action = header_menu_actions_[static_cast<std::size_t>(index)];
            if (!action.empty()) {
                handle_menu_action(action);
            }
        });
    }
}

void MapperBusGuiController::clear_preview() {
    update_preview_surface(
        blank_frame_.pixels.data(), mapperbus::core::kScreenWidth, mapperbus::core::kScreenHeight);
}

void MapperBusGuiController::refresh_preview() {
    if (machine_worker_active())
        return;
    const auto& frame = session_->emulator().frame_buffer();
    update_preview_surface(
        frame.pixels.data(), mapperbus::core::kScreenWidth, mapperbus::core::kScreenHeight);
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_welcome_view() {
    auto welcome = Box::vertical(0.0F);
    welcome->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    welcome->set_vertical_size_policy(nk::SizePolicy::Expanding);
    welcome->set_horizontal_stretch(1);
    welcome->set_vertical_stretch(1);

    auto column = Box::vertical(14.0F);
    column->append(TitleText::create("Open a game"));
    column->append(SecondaryText::create(
        "Choose a .nes, .fds, .zip, or .7z file, or drop one on this window."));

    auto open_button = nk::Button::create("Open ROM…");
    open_button->add_style_class("suggested");
    (void)open_button->on_clicked().connect([this] { browse_for_rom(); });
    welcome_open_button_ = open_button;
    column->append(open_button);

    column->append(SectionTitle::create("Recent"));
    if (configuration_.recent_roms.empty()) {
        column->append(SecondaryText::create("Your recently opened games will appear here."));
    } else {
        auto recents = nk::PreferencesGroup::create();
        recents->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        for (const auto& entry : configuration_.recent_roms) {
            auto row = nk::PreferencesRow::create(title_for_display(
                entry.archive_member.empty() ? entry.path.string() : entry.archive_member));
            row->set_subtitle((entry.archive_member.empty() ? recent_media_type(entry.path)
                                                            : entry.path.filename().string()) +
                              " · last opened " + recent_opened_date(entry.last_opened_unix));
            row->set_suffix(SecondaryText::create("\u203A"));
            row->set_activatable(true);
            (void)row->on_activated().connect(
                [this, entry] { attempt_open(entry.path.string(), entry.archive_member); });
            recents->add(std::move(row));
        }
        column->append(recents);

        auto clear_button = nk::Button::create("Clear Recents");
        clear_button->add_style_class("flat");
        (void)clear_button->on_clicked().connect([this] { confirm_clear_recents(); });
        column->append(clear_button);
    }

    auto center_row = Box::horizontal(0.0F);
    center_row->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    auto left = Spacer::create();
    auto right = Spacer::create();
    center_row->append(left);
    center_row->append(column);
    center_row->append(right);

    auto top = Spacer::create();
    auto bottom = Spacer::create();
    top->set_vertical_size_policy(nk::SizePolicy::Expanding);
    top->set_vertical_stretch(1);
    bottom->set_vertical_size_policy(nk::SizePolicy::Expanding);
    bottom->set_vertical_stretch(1);
    welcome->append(top);
    welcome->append(center_row);
    welcome->append(bottom);
    return welcome;
}

void MapperBusGuiController::refresh_welcome_view() {
    welcome_view_ = build_welcome_view();
    if (welcome_active_ && main_slot_) {
        main_slot_->set_child(welcome_view_);
    }
}

void MapperBusGuiController::refresh_ui() {
    if (!startup_ready_) {
        update_native_toolbar();
        return;
    }
    if (machine_worker_active())
        return;
    const bool assignments_changed = input_backend_->consume_gamepad_assignment_changes();
    const bool devices_changed = input_backend_->consume_gamepad_inventory_changes();
    if (assignments_changed)
        save_configuration_state();
    if ((assignments_changed || devices_changed) && settings_page_ == SettingsPage::Input)
        refresh_settings_dialog_sections();
    const app::SessionSnapshot snapshot = actions_->snapshot();
    const bool loaded = snapshot.has_cartridge;
    const bool paused = loaded && snapshot.paused;
    const std::string state_text = loaded ? (paused ? "Paused" : "Running") : "Ready";
    const std::string display_title =
        title_for_display(snapshot.rom_member.empty() ? snapshot.rom_path : snapshot.rom_member);
    if (loaded == welcome_active_) {
        welcome_active_ = !loaded;
        if (welcome_active_) {
            refresh_welcome_view();
            main_slot_->set_child(welcome_view_);
        } else {
            main_slot_->set_child(preview_);
        }
        if (welcome_active_ && welcome_open_button_) {
            welcome_open_button_->grab_focus();
        } else {
            focus_game_surface();
        }
    }
    std::vector<std::string> segments;
    if (loaded) {
        segments = {state_text,
                    region_name(snapshot.region),
                    nominal_fps_label(snapshot.region),
                    "Slot " + std::to_string(active_state_slot_ + 1)};
    } else {
        segments = {state_text};
    }
    if (snapshot.is_fds) {
        segments.push_back(snapshot.disk_side < 0
                               ? "Disk ejected"
                               : "Disk " + std::to_string(snapshot.disk_side / 2 + 1) + " " +
                                     (snapshot.disk_side % 2 == 0 ? "A" : "B"));
        segments.push_back(snapshot.disk_active ? "Disk active" : "Disk idle");
        if (snapshot.disk_write_protected)
            segments.push_back("Write protected");
        if (session_->disk_save_pending())
            segments.push_back("Saving disk changes");
        else if (snapshot.disk_dirty)
            segments.push_back("Unsaved disk changes");
        if (auto status = disk_save_status_.lock())
            status->set_text(!session_->disk_save_error().empty() ? session_->disk_save_error()
                             : session_->disk_save_pending()      ? "Saving disk changes…"
                             : snapshot.disk_dirty ? "Unsaved disk changes remain in memory."
                                                   : "Disk persistence is up to date.");
        if (auto retry = disk_save_retry_.lock())
            retry->set_sensitive(!session_->disk_save_pending());
        if (!session_->disk_save_error().empty())
            segments.push_back("Disk save failed");
    }
    status_bar_->set_segments(std::move(segments));
    // Handoff composition: session state leads, mapper identification trails
    // right-aligned in the secondary text color. The capability probe keeps
    // the frontend buildable against NodalKit revisions without the slot.
    if constexpr (requires { status_bar_->set_trailing(std::string{}); }) {
        status_bar_->set_trailing(loaded ? mapper_detail(snapshot.mapper_number) : "");
    } else if (loaded) {
        status_bar_->set_segments({state_text,
                                   region_name(snapshot.region),
                                   nominal_fps_label(snapshot.region),
                                   mapper_detail(snapshot.mapper_number),
                                   "Slot " + std::to_string(active_state_slot_ + 1)});
    }
    if (pause_button_) {
        const bool show_session_actions = loaded;
        pause_button_->set_visible(show_session_actions);
        fullscreen_button_->set_visible(show_session_actions);
        pause_button_->set_icon(paused ? SessionIcon::Play : SessionIcon::Pause);
        pause_button_->set_accessible_name(paused ? "Resume" : "Pause");
        fullscreen_button_->set_accessible_name(window_.is_fullscreen() ? "Leave fullscreen"
                                                                        : "Fullscreen");
    }

    // Keep the native task-switcher title identifiable while the visible
    // headerbar presents a clean media title without the file extension.
    const std::string window_title = loaded ? (display_title + " — MapperBus") : "MapperBus";
    window_.set_title(window_title);
    if (headerbar_) {
        headerbar_->set_title(display_title);
        headerbar_->set_subtitle(loaded ? "MapperBus" : "");
    }
    update_native_menu();
    update_native_toolbar();
}

void MapperBusGuiController::update_native_menu() {
    if (machine_worker_active())
        return;
    if (!app_.supports_native_app_menu() || native_menu_slot_ == active_state_slot_) {
        return;
    }
    // Replacing AppKit's menu bar interrupts an open menu. Frame refreshes must
    // preserve it; the selected slot is the only changing input to this model.
    app_.set_native_app_menu(build_native_menus(app_.app_name(), active_state_slot_));
    native_menu_slot_ = active_state_slot_;
}

void MapperBusGuiController::update_native_toolbar() {
    if (machine_worker_active())
        return;
    if (app_.system_preferences().platform_family != nk::PlatformFamily::MacOS) {
        return;
    }
    const auto snapshot = actions_->snapshot();
    const bool loaded = snapshot.has_cartridge;
    const bool paused = snapshot.paused;
    const bool fullscreen = window_.is_fullscreen();
    const bool modal = modal_dialog_presented();
    const int state = (loaded ? 1 : 0) | (paused ? 2 : 0) | (fullscreen ? 4 : 0) | (modal ? 8 : 0);
    if (native_toolbar_state_ == state) {
        return;
    }
    native_toolbar_state_ = state;
    nk::NativeToolbarConfig toolbar;
    toolbar.identifier = "dev.mapperbus.main-toolbar";
    toolbar.allows_user_customization = false;
    const auto add = [&](std::string id,
                         std::string label,
                         std::string symbol,
                         std::string action,
                         bool enabled) {
        // NodalKit 0.3.0 has no disabled state for native toolbar items.
        if (!enabled) {
            return;
        }
        toolbar.items.push_back({
            .identifier = std::move(id),
            .label = label,
            .tooltip = std::move(label),
            .symbol_name = std::move(symbol),
            .on_activate =
                [this, action = std::move(action)] {
                    // Mutating AppKit's toolbar or presenting a sheet must happen
                    // after the native action finishes dispatching.
                    app_.event_loop().post([this, action] { handle_menu_action(action); },
                                           "mapperbus.native-toolbar-action");
                },
            .on_search_submit = {},
        });
    };
    add("open", "Open ROM…", "folder", "file.open", !modal);
    add("pause",
        paused ? "Resume" : "Pause",
        paused ? "play.fill" : "pause.fill",
        "emu.pause",
        loaded && !modal);
    add("fullscreen",
        fullscreen ? "Leave fullscreen" : "Fullscreen",
        fullscreen ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right",
        "win.toggle-fullscreen",
        !modal);
    window_.set_native_toolbar(std::move(toolbar));
}

void MapperBusGuiController::set_message(std::string message) {
    if (toast_overlay_ && !message.empty()) {
        const bool high_priority = message.starts_with("Failed") || message.starts_with("Unable");
        toast_overlay_->add_toast({
            .title = std::move(message),
            .action_label = {},
            .priority = high_priority ? nk::ToastPriority::High : nk::ToastPriority::Normal,
            .timeout =
                high_priority ? std::chrono::milliseconds(8000) : std::chrono::milliseconds(5000),
        });
    }
}

void MapperBusGuiController::rebuild_header_menu() {
    if (machine_worker_active())
        return;
    if (!header_menu_) {
        return;
    }

    header_menu_->clear();
    header_menu_actions_.clear();
    const auto add_action = [this](std::string label, std::string action) {
        header_menu_->add_item(std::move(label));
        header_menu_actions_.push_back(std::move(action));
    };
    const auto add_separator = [this] {
        header_menu_->add_separator();
        header_menu_actions_.emplace_back();
    };

    const auto snapshot = actions_->snapshot();
    if (snapshot.has_cartridge) {
        add_action(menu_label(snapshot.paused ? "Resume" : "Pause", "P"), "emu.pause");
        add_action(menu_label("Reset", "R"), "emu.reset");
        add_separator();
        add_action(menu_label("Save State — Slot " + std::to_string(active_state_slot_ + 1), "F5"),
                   "emu.save-state");
        add_action(menu_label("Load State — Slot " + std::to_string(active_state_slot_ + 1), "F7"),
                   "emu.load-state");
        add_action("Choose State Slot…", "emu.choose-slot");
        add_action("Open States Folder", "emu.open-states-folder");
        add_separator();
        add_action("Step Frame", "emu.step");
        add_action("Power Cycle", "emu.power");
        if (snapshot.is_fds) {
            add_action("Disk Controls…", "fds.controls");
            add_action("Next Disk Side (F6)", "fds.next");
            add_action("Eject or Insert Disk", "fds.eject");
            add_action("Toggle Write Protection (F8)", "fds.protect");
        }
        add_separator();
        add_action("Close ROM", "file.close");
    }
    add_action(menu_label(window_.is_fullscreen() ? "Leave Fullscreen" : "Fullscreen", "F11"),
               "win.toggle-fullscreen");
    add_action(menu_label("Settings…", "Ctrl+,"), "settings.open");
    add_action("About MapperBus", "help.about");
    add_separator();
    add_action(menu_label("Quit", "Ctrl+Q"), "file.quit");
}

void MapperBusGuiController::focus_game_surface() {
    if (settings_window_ && settings_window_->is_visible() && settings_window_->is_focused()) {
        return;
    }
    if (preview_) {
        preview_->grab_focus();
    }
}

void MapperBusGuiController::toggle_fullscreen() {
    window_.set_fullscreen(!window_.is_fullscreen());
    // Chrome and the OSD notice follow the window-state signal, so this path
    // and compositor-initiated changes (window menu, Super-based shortcuts)
    // behave identically.
}

void MapperBusGuiController::update_fullscreen_chrome() {
    const bool fullscreen = window_.is_fullscreen();
    if (headerbar_) {
        headerbar_->set_visible(!fullscreen && app_.system_preferences().platform_family !=
                                                   nk::PlatformFamily::MacOS);
    }
    if (status_bar_) {
        status_bar_->set_visible(!fullscreen);
    }
    if (fullscreen != announced_fullscreen_) {
        announced_fullscreen_ = fullscreen;
        set_message(fullscreen ? "Fullscreen — press F11 to leave." : "Left fullscreen.");
    }
    focus_game_surface();
    update_native_toolbar();
}

void MapperBusGuiController::browse_for_rom(bool recovering) {
    if (!startup_ready_ || close_intent_ || close_authorized_)
        return;
    auto failure = recovering ? failed_rom_load_ : nullptr;
    if (recovering) {
        if (!failure || failure->choosing_file)
            return;
        failure->choosing_file = true;
    } else if (modal_dialog_presented()) {
        return;
    }
    app_.open_file_dialog_async(
        "Open ROM",
        {"*.nes", "*.fds", "*.zip", "*.7z"},
        [this, failure](nk::OpenFileDialogResult dialog_result) {
            if (failure) {
                if (failed_rom_load_ != failure || close_intent_ || close_authorized_)
                    return;
                failure->choosing_file = false;
                if (!dialog_result) {
                    if (dialog_result.error() != nk::FileDialogError::Cancelled)
                        failure->chooser_error = file_dialog_error_text(dialog_result.error());
                    present_rom_load_error();
                    return;
                }
                dismiss_rom_load_error();
            }
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

void MapperBusGuiController::attempt_open(std::string rom_path, std::string member) {
    if (close_authorized_)
        return;
    if (!startup_ready_) {
        if (!rom_path.empty()) {
            initial_rom_request_ = std::pair{std::move(rom_path), std::move(member)};
            show_startup_view();
        }
        return;
    }
    if (loading_request_ || modal_dialog_presented())
        return;
    if (rom_path.empty()) {
        set_message("Open a ROM to start playback.");
        focus_game_surface();
        return;
    }
    if (auto initialized = session_->initialize(); !initialized) {
        set_message(initialized.error());
        return;
    }
    // Capture the old game's assignment scope before the content identity changes.
    update_configuration_from_state();
    close_settings();
    paused_before_rom_load_ = session_->paused();
    session_->set_paused(true);
    loading_request_ = app::RomLoadRequest{
        .path = std::move(rom_path),
        .member = std::move(member),
        .fds_bios_path = configuration_.fds_bios_path,
        .audio_settings = session_->audio_settings(),
        .state_storage = configuration_.save_states,
        .persistence_directory = {},
    };
    if (session_->has_cartridge()) {
        // Save before reading replacement persistence, including reopening the same game.
        present_rom_loading(true);
        auto fence = session_->begin_background_persistence();
        if (auto started = rom_loader_.save_current(session_->emulator_handle(), std::move(fence));
            !started) {
            session_->cancel_background_persistence();
            show_rom_load_error(started.error());
        }
    } else {
        start_rom_read();
    }
}

void MapperBusGuiController::present_rom_loading(bool saving) {
    dismiss_rom_loading();
    auto content = Box::vertical(12.0F);
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    content->append(spinner);
    auto explanation = nk::Label::create(saving ? "Saving progress before opening the next game…"
                                                : "Reading and checking the selected ROM…");
    explanation->set_wrapping(true);
    content->append(explanation);
    if (!saving) {
        loading_status_ = explanation;
        if (app::is_rom_archive(loading_request_->path))
            explanation->set_text("Opening archive…");
    }
    auto filename =
        nk::Label::create(std::filesystem::path(loading_request_->path).filename().string());
    filename->set_wrapping(true);
    content->append(filename);
    loading_dialog_ = std::make_shared<FileOperationDialog>(
        saving ? "Saving Current Game" : "Opening ROM", !saving);
    loading_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    if (!saving)
        loading_dialog_->add_button("Cancel", nk::DialogResponse::Cancel);
    (void)loading_dialog_->on_response().connect([this](nk::DialogResponse) {
        if (!loading_dialog_ || !rom_loader_.cancel())
            return;
        // Invalidate the result during dispatch, before a timer can adopt it.
        // Widget retirement still waits for dispatch to unwind.
        auto dialog = loading_dialog_;
        app_.event_loop().post(
            [this, dialog] {
                if (loading_dialog_ != dialog)
                    return;
                abandon_rom_load("Opening ROM cancelled.");
            },
            "mapperbus.rom-cancel");
    });
    loading_dialog_->present(window_);
}

void MapperBusGuiController::dismiss_rom_loading() {
    loading_status_.reset();
    auto dialog = std::exchange(loading_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::start_rom_read() {
    present_rom_loading(false);
    if (auto started = rom_loader_.start(*loading_request_); !started)
        show_rom_load_error(started.error());
}

void MapperBusGuiController::abandon_rom_load(std::string message) {
    loading_request_.reset();
    dismiss_rom_loading();
    session_->set_paused(paused_before_rom_load_);
    frame_accumulator_ = std::chrono::nanoseconds{0};
    last_tick_time_ = std::chrono::steady_clock::now();
    set_message(std::move(message));
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::show_rom_load_error(std::string error,
                                                 std::optional<std::string> bios_member) {
    if (close_intent_) {
        abandon_rom_load("Failed to open ROM: " + error);
        return;
    }
    failed_rom_load_ = std::make_shared<FailedRomLoad>(FailedRomLoad{
        .path = loading_request_->path,
        .member = bios_member.value_or(loading_request_->member),
        .error = std::move(error),
        .chooser_error = {},
        .needs_bios = bios_member.has_value(),
    });
    abandon_rom_load("Failed to open ROM: " + failed_rom_load_->error);
    present_rom_load_error();
}

void MapperBusGuiController::dismiss_rom_load_error(bool keep_request) {
    if (!keep_request) {
        (void)bios_validation_.cancel();
        failed_rom_load_.reset();
    }
    auto dialog = std::exchange(rom_error_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::present_rom_load_error() {
    if (!failed_rom_load_ || close_intent_ || close_authorized_)
        return;
    rom_error_dialog_ = make_rom_load_error_dialog(failed_rom_load_->path,
                                                   failed_rom_load_->member,
                                                   failed_rom_load_->error,
                                                   session_->has_cartridge(),
                                                   failed_rom_load_->chooser_error,
                                                   failed_rom_load_->needs_bios);
    const std::weak_ptr<nk::Dialog> weak_dialog = rom_error_dialog_;
    (void)rom_error_dialog_->on_response().connect(
        [this, weak_dialog](nk::DialogResponse response) {
            auto dialog = weak_dialog.lock();
            if (!dialog || rom_error_dialog_ != dialog || !failed_rom_load_)
                return;
            const int custom_id = dialog->custom_response_id();
            app_.event_loop().post(
                [this, dialog, response, custom_id] {
                    if (rom_error_dialog_ != dialog || !failed_rom_load_ || close_intent_ ||
                        close_authorized_)
                        return;
                    const auto failure = failed_rom_load_;
                    if (response == nk::DialogResponse::Custom && custom_id == kChooseAnotherRom) {
                        dismiss_rom_load_error(true);
                        browse_for_rom(true);
                    } else if (response == nk::DialogResponse::Custom &&
                               custom_id == kChooseRecoveryBios && failure->needs_bios) {
                        dismiss_rom_load_error(true);
                        choose_recovery_bios();
                    } else {
                        dismiss_rom_load_error();
                        if (response == nk::DialogResponse::Accept) {
                            // Recheck outgoing persistence and use current BIOS/settings.
                            attempt_open(failure->path, failure->member);
                        } else {
                            set_message(session_->has_cartridge() ? "Kept current game."
                                                                  : "Opening ROM cancelled.");
                            refresh_ui();
                            focus_game_surface();
                        }
                    }
                },
                "mapperbus.rom-error-response");
        });
    rom_error_dialog_->present(window_);
    update_native_toolbar();
}

void MapperBusGuiController::choose_recovery_bios() {
    const auto failure = failed_rom_load_;
    if (!failure || !failure->needs_bios || failure->choosing_file || failure->checking_bios ||
        close_intent_ || close_authorized_)
        return;
    failure->choosing_file = true;
    app_.open_file_dialog_async(
        "Choose 8 KiB FDS BIOS",
        {"*.rom", "*.bin"},
        [this, failure](nk::OpenFileDialogResult result) {
            if (failed_rom_load_ != failure || close_intent_ || close_authorized_)
                return;
            failure->choosing_file = false;
            if (!result) {
                if (result.error() != nk::FileDialogError::Cancelled)
                    failure->chooser_error = file_dialog_error_text(result.error());
                present_rom_load_error();
                return;
            }
            auto started = bios_validation_.start(*result);
            if (!started) {
                failure->chooser_error = started.error();
                present_rom_load_error();
                return;
            }
            failure->checking_bios = true;
            rom_error_dialog_ = make_bios_validation_dialog();
            const std::weak_ptr<nk::Dialog> weak_dialog = rom_error_dialog_;
            (void)rom_error_dialog_->on_response().connect(
                [this, failure, weak_dialog](nk::DialogResponse) {
                    auto dialog = weak_dialog.lock();
                    if (!dialog || rom_error_dialog_ != dialog || failed_rom_load_ != failure ||
                        !bios_validation_.cancel())
                        return;
                    app_.event_loop().post(
                        [this, failure, dialog] {
                            if (failed_rom_load_ != failure || rom_error_dialog_ != dialog ||
                                close_intent_)
                                return;
                            dismiss_rom_load_error(true);
                            failure->checking_bios = false;
                            present_rom_load_error();
                        },
                        "mapperbus.bios-cancel");
                });
            rom_error_dialog_->present(window_);
        });
}

void MapperBusGuiController::poll_recovery_bios() {
    auto checked = bios_validation_.poll();
    if (!checked)
        return;
    const auto failure = failed_rom_load_;
    if (!failure || !failure->checking_bios || close_intent_ || close_authorized_)
        return;
    failure->checking_bios = false;
    dismiss_rom_load_error(true);
    if (!*checked) {
        failure->chooser_error = checked->error();
        present_rom_load_error();
        return;
    }
    update_configuration_from_state();
    auto proposed = configuration_;
    proposed.fds_bios_path = **checked;
    begin_configuration_change(
        std::move(proposed),
        [this, failure] {
            session_->set_fds_bios_path(configuration_.fds_bios_path);
            // Let configuration polling finish before a retry can take machine ownership.
            app_.event_loop().post(
                [this, failure] {
                    if (failed_rom_load_ != failure || close_intent_ || close_authorized_)
                        return;
                    dismiss_rom_load_error();
                    attempt_open(failure->path, failure->member);
                },
                "mapperbus.bios-retry");
        },
        {},
        [this, failure] {
            if (failed_rom_load_ == failure && !close_intent_ && !close_authorized_) {
                failure->chooser_error =
                    "The BIOS setting was not saved. The previous BIOS remains selected.";
                present_rom_load_error();
            }
        });
}

void MapperBusGuiController::update_settings_bios_status(std::string text) {
    settings_bios_status_text_ = std::move(text);
    if (auto status = settings_bios_status_.lock()) {
        status->set_text(settings_bios_status_text_);
        status->set_visible(!settings_bios_status_text_.empty());
    }
}

void MapperBusGuiController::cancel_settings_bios() {
    (void)settings_bios_validation_.cancel();
    const bool changed = settings_bios_selection_ != nullptr;
    if (auto selection = std::exchange(settings_bios_selection_, {})) {
        if (auto row = selection->row.lock())
            row->suffix()->set_sensitive(true);
    }
    auto dialog = std::exchange(settings_bios_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
    if (changed)
        update_native_toolbar();
}

void MapperBusGuiController::choose_settings_bios(std::weak_ptr<nk::PreferencesRow> weak_row) {
    if (settings_bios_selection_ || configuration_change_ || close_intent_ || close_authorized_ ||
        loading_request_ || failed_rom_load_ || settings_page_ != SettingsPage::General)
        return;
    const auto row = weak_row.lock();
    if (!row)
        return;
    auto selection = std::make_shared<SettingsBiosSelection>(SettingsBiosSelection{weak_row});
    settings_bios_selection_ = selection;
    row->suffix()->set_sensitive(false);
    update_settings_bios_status({});
    update_native_toolbar();
    app_.open_file_dialog_async(
        "Choose 8 KiB FDS BIOS",
        {"*.rom", "*.bin"},
        [this, selection](nk::OpenFileDialogResult result) {
            if (settings_bios_selection_ != selection || close_intent_ || close_authorized_)
                return;
            if (settings_window_ && settings_window_->is_visible())
                settings_window_->present();
            if (!result) {
                cancel_settings_bios();
                if (result.error() != nk::FileDialogError::Cancelled)
                    update_settings_bios_status(file_dialog_error_text(result.error()));
                update_native_toolbar();
                return;
            }
            if (auto started = settings_bios_validation_.start(*result); !started) {
                cancel_settings_bios();
                update_settings_bios_status(started.error());
                update_native_toolbar();
                return;
            }
            settings_bios_dialog_ = make_bios_validation_dialog();
            const std::weak_ptr<nk::Dialog> weak_dialog = settings_bios_dialog_;
            (void)settings_bios_dialog_->on_response().connect([this, selection, weak_dialog](
                                                                   nk::DialogResponse) {
                auto dialog = weak_dialog.lock();
                if (!dialog || settings_bios_dialog_ != dialog ||
                    settings_bios_selection_ != selection || !settings_bios_validation_.cancel())
                    return;
                app_.event_loop().post(
                    [this, selection, dialog] {
                        if (settings_bios_selection_ != selection ||
                            settings_bios_dialog_ != dialog)
                            return;
                        cancel_settings_bios();
                        update_settings_bios_status("BIOS check cancelled.");
                        update_native_toolbar();
                    },
                    "mapperbus.settings-bios-cancel");
            });
            settings_bios_dialog_->present(settings_host_window());
        });
}

void MapperBusGuiController::poll_settings_bios() {
    auto checked = settings_bios_validation_.poll();
    if (!checked)
        return;
    const bool visible = (settings_window_ && settings_window_->is_visible()) ||
                         (settings_dialog_ && settings_dialog_->is_presented());
    if (!settings_bios_selection_ || !visible || settings_page_ != SettingsPage::General ||
        close_intent_ || close_authorized_) {
        cancel_settings_bios();
        return;
    }
    cancel_settings_bios();
    if (!*checked) {
        update_settings_bios_status(checked->error());
        update_native_toolbar();
        return;
    }
    update_configuration_from_state();
    auto proposed = configuration_;
    proposed.fds_bios_path = **checked;
    begin_configuration_change(
        std::move(proposed),
        [this] { session_->set_fds_bios_path(configuration_.fds_bios_path); },
        "FDS BIOS selected for the next disk load.",
        [this] { update_settings_bios_status("The previous BIOS remains selected."); });
    update_native_toolbar();
}

void MapperBusGuiController::poll_rom_load() {
    if (auto status = loading_status_.lock()) {
        if (auto progress = rom_loader_.archive_progress()) {
            switch (progress->phase) {
            case app::ArchiveReadPhase::Open:
                status->set_text("Opening archive…");
                break;
            case app::ArchiveReadPhase::Header:
            case app::ArchiveReadPhase::Skip:
                status->set_text(
                    std::format("{}\n{} {} scanned",
                                progress->bytes == 0 ? "Scanning archive…" : "Checking archive…",
                                progress->entries,
                                progress->entries == 1 ? "entry" : "entries"));
                break;
            case app::ArchiveReadPhase::Data:
                status->set_text(std::format("Loading ROM…\n{} bytes decoded", progress->bytes));
                break;
            case app::ArchiveReadPhase::Close:
                status->set_text(progress->bytes == 0 ? "Checking archive…" : "Checking ROM…");
                break;
            }
        }
    }
    const auto phase = rom_loader_.phase();
    auto completed = rom_loader_.poll();
    if (!completed)
        return;
    if (phase == app::AsyncRomLoader::Phase::Saving) {
        core::Result<void> saved;
        if (!*completed)
            saved = std::unexpected(completed->error());
        session_->finish_background_persistence(saved);
    }
    if (!*completed) {
        show_rom_load_error(completed->error(),
                            phase == app::AsyncRomLoader::Phase::Loading
                                ? rom_loader_.fds_bios_error_member()
                                : std::nullopt);
        return;
    }
    if (phase == app::AsyncRomLoader::Phase::Saving) {
        if (close_intent_) {
            abandon_rom_load("Opening ROM cancelled.");
            return;
        }
        start_rom_read();
        return;
    }
    auto media = std::move(**completed);
    if (!media.members.empty()) {
        dismiss_rom_loading();
        choose_archive_rom(std::move(media.path), std::move(media.members));
        return;
    }
    auto path = media.path;
    auto member = media.member;
    session_->adopt_rom(std::move(media));
    loading_request_.reset();
    dismiss_rom_loading();
    finish_rom_open(std::move(path), std::move(member));
}

void MapperBusGuiController::finish_rom_open(std::string rom_path, std::string member) {
    input_backend_->set_gamepad_assignments(
        app::gamepad_assignments_for_game(configuration_, session_->game_identity()));
    active_state_slot_ = app::selected_state_slot(configuration_, session_->game_identity());
    touch_recent_rom(configuration_, rom_path, session_->current_rom_member());
    save_configuration_state();
    detected_region_ = actions_->snapshot().region;
    app::apply_controller_hardware(
        session_->emulator(),
        app::controller_hardware_for_game(configuration_, session_->game_identity()));
    apply_region_preference();
    refresh_settings_dialog_sections();
    actions_->step_frame();
    frame_accumulator_ = std::chrono::nanoseconds{0};
    last_tick_time_ = std::chrono::steady_clock::now();
    refresh_preview();
    set_message("Loaded " + title_for_display(member.empty() ? rom_path : member) + ".");
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::choose_archive_rom(std::string rom_path,
                                                std::vector<std::string> members) {
    auto combo = nk::ComboBox::create();
    combo->set_items(members);
    combo->set_selected_index(0);
    auto content = Box::vertical(12.0F);
    content->append(SecondaryText::create("Choose a ROM from " +
                                          std::filesystem::path(rom_path).filename().string()));
    content->append(combo);
    archive_dialog_ = nk::Dialog::create("Open Archived ROM");
    archive_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    archive_dialog_->add_button("Cancel", nk::DialogResponse::Cancel);
    archive_dialog_->add_button("Open", nk::DialogResponse::Accept);
    (void)archive_dialog_->on_response().connect(
        [this, combo, members = std::move(members)](nk::DialogResponse response) {
            auto dialog = archive_dialog_;
            const int index = combo->selected_index();
            // Release the dialog after its dispatch has unwound.
            app_.event_loop().post(
                [this, dialog, response, index, members] {
                    if (archive_dialog_ != dialog) {
                        return;
                    }
                    archive_dialog_.reset();
                    if (response == nk::DialogResponse::Accept && index >= 0 &&
                        static_cast<std::size_t>(index) < members.size()) {
                        loading_request_->member = members[static_cast<std::size_t>(index)];
                        start_rom_read();
                    } else {
                        abandon_rom_load("Opening ROM cancelled.");
                    }
                    focus_game_surface();
                    update_native_toolbar();
                },
                "mapperbus.archive-close");
        });
    archive_dialog_->present(window_);
    combo->grab_focus();
    update_native_toolbar();
}

void MapperBusGuiController::perform_disk_action(std::string_view action) {
    const auto snapshot = actions_->snapshot();
    core::Result<void> result;
    if (action == "fds.next") {
        result = session_->insert_disk_side((snapshot.disk_side + 1) %
                                            std::max(1, snapshot.disk_side_count));
    } else if (action == "fds.eject") {
        result = snapshot.disk_side < 0 ? session_->insert_disk_side(0) : session_->eject_disk();
    } else if (action == "fds.protect") {
        result = session_->set_disk_write_protected(!snapshot.disk_write_protected);
    }
    set_message(result ? "Disk control updated." : result.error());
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::open_disk_controls() {
    if (modal_dialog_presented())
        return;
    const auto snapshot = actions_->snapshot();
    auto content = Box::vertical(12.0F);
    if (snapshot.is_fds) {
        std::vector<std::string> labels;
        for (int side = 0; side < snapshot.disk_side_count; ++side) {
            labels.push_back("Disk " + std::to_string(side / 2 + 1) + " " +
                             (side % 2 == 0 ? "A" : "B"));
        }
        auto sides = nk::ComboBox::create();
        sides->set_items(std::move(labels));
        sides->set_selected_index(std::max(0, snapshot.disk_side));
        content->append(sides);
        auto insert = nk::Button::create("Insert Selected Side");
        (void)insert->on_clicked().connect([this, sides] {
            auto result = session_->insert_disk_side(sides->selected_index());
            set_message(result ? "Selected disk side inserted." : result.error());
            refresh_ui();
        });
        content->append(insert);
        auto eject = nk::Button::create("Eject Disk");
        (void)eject->on_clicked().connect([this] {
            auto result = session_->eject_disk();
            set_message(result ? "Disk ejected." : result.error());
            refresh_ui();
        });
        content->append(eject);
        auto protect = nk::Button::create(
            snapshot.disk_write_protected ? "Allow Disk Writes" : "Protect Disk Against Writes");
        (void)protect->on_clicked().connect([this, protect] {
            const bool was_protected = actions_->snapshot().disk_write_protected;
            auto result = session_->set_disk_write_protected(!was_protected);
            if (result)
                protect->set_label(was_protected ? "Protect Disk Against Writes"
                                                 : "Allow Disk Writes");
            set_message(result ? "Disk write protection updated." : result.error());
            refresh_ui();
        });
        content->append(protect);
        auto save_status = nk::Label::create("");
        save_status->set_wrapping(true);
        disk_save_status_ = save_status;
        content->append(save_status);
        auto retry = nk::Button::create("Retry Disk Save");
        disk_save_retry_ = retry;
        (void)retry->on_clicked().connect([this] {
            auto saved = session_->retry_disk_save();
            set_message(saved ? "Disk save requested." : saved.error());
            refresh_ui();
        });
        content->append(retry);
        auto export_button = nk::Button::create("Export Modified Disk…");
        (void)export_button->on_clicked().connect([this] { choose_disk_export(); });
        content->append(export_button);
    } else
        content->append(SecondaryText::create("Open a .fds image to use disk controls."));
    fds_dialog_ = nk::Dialog::create("Disk Controls");
    fds_dialog_->set_content(FixedWidthSlot::create(480.0F, content));
    fds_dialog_->add_button("Done", nk::DialogResponse::Close);
    (void)fds_dialog_->on_response().connect([this](nk::DialogResponse) {
        auto dialog = fds_dialog_;
        app_.event_loop().post(
            [this, dialog] {
                if (fds_dialog_ == dialog)
                    fds_dialog_.reset();
                focus_game_surface();
                update_native_toolbar();
            },
            "mapperbus.fds-close");
    });
    fds_dialog_->present(window_);
    refresh_ui();
}

void MapperBusGuiController::choose_disk_export() {
    if (choosing_disk_export_ || disk_export_.busy() || close_intent_)
        return;
    choosing_disk_export_ = true;
    const std::weak_ptr<int> lifetime = disk_export_lifetime_;
    app_.save_file_dialog_async({.title = "Export FDS Disk",
                                 .suggested_filename = "modified-disk.fds",
                                 .filters = {"*.fds"}},
                                [this, lifetime](nk::SaveFileDialogResult result) {
                                    if (lifetime.expired())
                                        return;
                                    choosing_disk_export_ = false;
                                    if (close_intent_)
                                        return;
                                    if (!result) {
                                        if (result.error() != nk::FileDialogError::Cancelled)
                                            set_message(file_dialog_error_text(result.error()));
                                        return;
                                    }
                                    // Retire Disk Controls before presenting the progress sheet.
                                    if (fds_dialog_ && fds_dialog_->is_presented())
                                        fds_dialog_->close();
                                    choosing_disk_export_ = true;
                                    app_.event_loop().post(
                                        [this, path = std::move(*result)] {
                                            choosing_disk_export_ = false;
                                            if (!close_intent_)
                                                start_disk_export(path);
                                        },
                                        "mapperbus.export-start");
                                });
}

void MapperBusGuiController::dismiss_disk_export_dialog() {
    auto dialog = std::exchange(disk_export_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::start_disk_export(std::string path) {
    auto data = session_->emulator().disk_export_data();
    if (!data) {
        set_message(data.error());
        return;
    }
    disk_export_path_ = std::move(path);
    auto content = Box::vertical(12.0F);
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    content->append(spinner);
    auto label = nk::Label::create("Exporting a copy of the current disk…");
    label->set_wrapping(true);
    disk_export_status_ = label;
    content->append(label);
    auto destination = nk::Label::create(disk_export_path_);
    destination->set_wrapping(true);
    content->append(destination);
    disk_export_dialog_ = std::make_shared<FileOperationDialog>("Exporting Disk", false);
    disk_export_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    disk_export_dialog_->present(window_);
    auto started =
        disk_export_.start({disk_export_path_, session_->current_rom_path(), std::move(*data)});
    if (!started) {
        dismiss_disk_export_dialog();
        set_message("Could not start disk export: " + started.error());
    }
    refresh_ui();
}

void MapperBusGuiController::poll_disk_export() {
    auto completed = disk_export_.poll();
    if (!completed)
        return;
    dismiss_disk_export_dialog();
    if (!*completed) {
        // Keep the failed export visible instead of hiding its error on exit.
        close_intent_.reset();
        close_ready_ = false;
        close_captured_ = false;
        auto content = Box::vertical(12.0F);
        auto error = nk::Label::create(completed->error());
        error->set_wrapping(true);
        content->append(error);
        auto destination = nk::Label::create(disk_export_path_);
        destination->set_wrapping(true);
        content->append(destination);
        auto help = nk::Label::create("Your game remains open. Choose Export Modified Disk "
                                      "again to retry or select another destination.");
        help->set_wrapping(true);
        content->append(help);
        disk_export_dialog_ = nk::Dialog::create("Could Not Export Disk");
        disk_export_dialog_->set_content(FixedWidthSlot::create(480.0F, content));
        disk_export_dialog_->add_button("Done", nk::DialogResponse::Close);
        (void)disk_export_dialog_->on_response().connect([this](nk::DialogResponse) {
            auto dialog = disk_export_dialog_;
            app_.event_loop().post(
                [this, dialog] {
                    if (disk_export_dialog_ == dialog) {
                        dismiss_disk_export_dialog();
                        refresh_ui();
                        focus_game_surface();
                    }
                },
                "mapperbus.export-error-close");
        });
        disk_export_dialog_->present(window_);
        set_message("Failed to export disk: " + completed->error());
    } else {
        set_message("Modified disk exported to " + **completed + ".");
        focus_game_surface();
    }
    refresh_ui();
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
    set_message(result == app::TickResult::FrameAdvanced ? "Stepped one frame."
                                                         : "Could not step a frame.");
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

void MapperBusGuiController::select_state_slot(int slot) {
    if (!actions_->snapshot().has_cartridge || slot < 0 || slot >= app::kSaveStateSlotCount ||
        slot == active_state_slot_) {
        return;
    }
    active_state_slot_ = slot;
    if (auto control = state_slot_control_.lock(); control && control->selected_index() != slot) {
        control->set_selected_index(slot);
    }
    app::remember_state_slot(configuration_, session_->game_identity(), slot);
    save_configuration_state();
    refresh_ui();
}

void MapperBusGuiController::choose_state_slot() {
    if (!actions_->snapshot().has_cartridge || state_slot_dialog_) {
        return;
    }
    auto combo = nk::ComboBox::create();
    state_slot_control_ = combo;
    (void)combo->on_selection_changed().connect([this](int slot) { select_state_slot(slot); });
    auto group = SettingsGroup::create("Save states");
    group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    group->add(pref_row("Slot", combo));
    auto content = Box::vertical(12.0F);
    content->append(group);
    auto scan_row = Box::horizontal(12.0F);
    auto spinner = nk::Spinner::create();
    state_storage_spinner_ = spinner;
    scan_row->append(spinner);
    auto slots_status = nk::Label::create("Checking slots…");
    slots_status->set_wrapping(true);
    state_slots_status_ = slots_status;
    scan_row->append(slots_status);
    content->append(scan_row);
    content->append(SecondaryText::create("Remembered for this game. F5 saves; F7 loads."));
    auto storage_group = SettingsGroup::create("Storage");
    storage_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    auto mode = nk::ComboBox::create();
    mode->set_items({"User data directory", "Portable (beside ROM)"});
    mode->set_selected_index(
        configuration_.save_states.mode == app::StateStorageMode::Portable ? 1 : 0);
    storage_group->add(pref_row("Location", mode));
    state_storage_mode_ = mode;
    content->append(storage_group);
    auto directory = nk::TextField::create(configuration_.save_states.directory.string());
    directory->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    directory->set_placeholder("Optional absolute folder override");
    directory->ensure_accessible().set_name("Save-state folder override");
    content->append(directory);
    state_storage_directory_ = directory;
    content->append(SecondaryText::create("Folder overrides take priority over Location."));
    auto location_status =
        nk::Label::create("Current location is unchanged until validation and saving succeed.");
    location_status->set_wrapping(true);
    state_location_status_ = location_status;
    content->append(location_status);
    auto apply = nk::Button::create("Apply Storage Location");
    state_storage_apply_ = apply;
    const auto apply_storage = [this] {
        const auto mode_control = state_storage_mode_.lock();
        const auto directory_control = state_storage_directory_.lock();
        if (!mode_control || !directory_control || storage_tasks_.preparing() ||
            configuration_change_)
            return;
        app::StateStorageSettings settings;
        settings.mode = mode_control->selected_index() == 1 ? app::StateStorageMode::Portable
                                                            : app::StateStorageMode::UserData;
        settings.directory = std::filesystem::path(directory_control->text());
        auto started = storage_tasks_.prepare(
            std::move(settings), session_->current_rom_path(), session_->game_identity());
        if (auto status = state_location_status_.lock())
            status->set_text(started ? "Checking storage location…" : started.error());
        update_state_storage_controls();
    };
    (void)apply->on_clicked().connect(apply_storage);
    (void)directory->on_activate().connect(apply_storage);
    auto open_folder = nk::Button::create("Open States Folder");
    state_storage_open_ = open_folder;
    (void)open_folder->on_clicked().connect([this] { open_states_folder(); });
    auto storage_actions = Box::horizontal(12.0F);
    storage_actions->append(apply);
    storage_actions->append(open_folder);
    content->append(storage_actions);
    state_slot_dialog_ = nk::Dialog::create("Choose State Slot");
    state_slot_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    state_slot_dialog_->add_button("Done", nk::DialogResponse::Close);
    (void)state_slot_dialog_->on_response().connect([this](nk::DialogResponse) {
        // Invalidate results immediately; widget retirement waits for dispatch.
        storage_tasks_.cancel_chooser();
        auto dialog = state_slot_dialog_;
        app_.event_loop().post(
            [this, dialog] {
                if (state_slot_dialog_ == dialog) {
                    state_slot_dialog_.reset();
                    state_slot_control_.reset();
                    state_slots_status_.reset();
                    state_location_status_.reset();
                    state_storage_spinner_.reset();
                    state_storage_mode_.reset();
                    state_storage_directory_.reset();
                    state_storage_apply_.reset();
                    state_storage_open_.reset();
                }
                focus_game_surface();
                update_native_toolbar();
            },
            "mapperbus.state-slot-close");
    });
    state_slot_dialog_->present(window_);
    request_state_slot_scan();
    update_state_storage_controls();
    combo->grab_focus();
    update_native_toolbar();
}

void MapperBusGuiController::request_state_slot_scan() {
    auto control = state_slot_control_.lock();
    if (!control)
        return;
    auto started = storage_tasks_.scan(
        {session_->state_directory(),
         session_->current_rom_path(),
         session_->current_rom_member().empty() && !session_->emulator().is_fds()});
    std::vector<std::string> labels;
    for (int slot = 0; slot < app::kSaveStateSlotCount; ++slot)
        labels.push_back("Slot " + std::to_string(slot + 1) +
                         (started ? " — Checking…" : " — Unavailable"));
    control->set_items(std::move(labels));
    control->set_selected_index(active_state_slot_);
    if (auto status = state_slots_status_.lock())
        status->set_text(started ? "Checking slots…" : started.error());
    update_state_storage_controls();
}

void MapperBusGuiController::update_state_storage_controls() {
    const bool checking = storage_tasks_.preparing() || configuration_change_.has_value();
    if (auto mode = state_storage_mode_.lock())
        mode->set_sensitive(!checking);
    if (auto directory = state_storage_directory_.lock())
        directory->set_sensitive(!checking);
    if (auto apply = state_storage_apply_.lock())
        apply->set_sensitive(!checking);
    if (auto open = state_storage_open_.lock())
        open->set_sensitive(!checking && !storage_tasks_.opening());
    if (auto spinner = state_storage_spinner_.lock()) {
        const bool busy = storage_tasks_.scanning() || checking;
        spinner->set_spinning(busy);
        spinner->set_visible(busy);
    }
}

void MapperBusGuiController::poll_state_storage() {
    if (auto completed = storage_tasks_.poll_slots()) {
        if (auto control = state_slot_control_.lock()) {
            std::vector<std::string> labels;
            for (int slot = 0; slot < app::kSaveStateSlotCount; ++slot) {
                const auto status = *completed ? (**completed)[static_cast<std::size_t>(slot)]
                                               : app::StateSlotStatus::Unavailable;
                labels.push_back("Slot " + std::to_string(slot + 1) +
                                 (status == app::StateSlotStatus::Saved   ? " — Saved"
                                  : status == app::StateSlotStatus::Empty ? " — Empty"
                                                                          : " — Unavailable"));
            }
            control->set_items(std::move(labels));
            control->set_selected_index(active_state_slot_);
        }
        if (auto status = state_slots_status_.lock())
            status->set_text(*completed ? "Slots checked." : completed->error());
        update_state_storage_controls();
    }
    if (auto completed = storage_tasks_.poll_location()) {
        auto status = state_location_status_.lock();
        if (!*completed) {
            if (status)
                status->set_text(completed->error());
        } else if (state_slot_dialog_ && state_slot_dialog_->is_presented()) {
            update_configuration_from_state();
            auto proposed = configuration_;
            proposed.save_states = (**completed).settings;
            auto prepared = std::move(**completed);
            // Validation is complete; the noncancellable save owns this proposal.
            state_slot_dialog_->close();
            begin_configuration_change(
                std::move(proposed),
                [this, prepared = std::move(prepared)] {
                    session_->set_prepared_state_storage(prepared.settings, prepared.directory);
                },
                "Storage location updated. Existing files stay in their current folders.");
        }
        update_state_storage_controls();
    }
    if (auto completed = storage_tasks_.poll_open()) {
        set_message(*completed ? "States folder opened." : completed->error());
        update_state_storage_controls();
    }
}

void MapperBusGuiController::open_states_folder() {
    if (!session_->has_cartridge()) {
        set_message("Open a ROM to view its save-state folder.");
        return;
    }
    if (storage_tasks_.preparing())
        return;
    auto started = storage_tasks_.open(session_->state_directory());
    set_message(started ? "Opening states folder…" : started.error());
    update_state_storage_controls();
}

bool MapperBusGuiController::machine_worker_active() const {
    return close_saving_ || rom_loader_.phase() == app::AsyncRomLoader::Phase::Saving ||
           state_io_.phase() == app::AsyncStateIO::Phase::Saving;
}

void MapperBusGuiController::begin_state_operation(int slot, bool save) {
    if (modal_dialog_presented())
        return;
    if (!session_->has_cartridge()) {
        set_message(save ? "No ROM loaded to save state from."
                         : "No ROM loaded to load state into.");
        return;
    }
    if (slot < 0 || slot >= app::kSaveStateSlotCount) {
        set_message("Choose a state slot between 1 and 10.");
        return;
    }
    update_configuration_from_state();
    close_settings();
    pending_state_operation_ =
        PendingStateOperation{slot, save, session_->paused(), session_->state_path_for_slot(slot)};
    session_->set_paused(true);

    auto content = Box::vertical(12.0F);
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    content->append(spinner);
    auto label = nk::Label::create(save ? "Saving the paused game. Please wait…"
                                        : "Reading the selected save-state…");
    label->set_wrapping(true);
    content->append(label);
    content->append(nk::Label::create("Slot " + std::to_string(slot + 1)));
    state_operation_dialog_ =
        std::make_shared<FileOperationDialog>(save ? "Saving State" : "Loading State", !save);
    state_operation_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    if (!save)
        state_operation_dialog_->add_button("Cancel", nk::DialogResponse::Cancel);
    (void)state_operation_dialog_->on_response().connect([this](nk::DialogResponse) {
        if (!state_operation_dialog_ || !state_io_.cancel())
            return;
        auto dialog = state_operation_dialog_;
        app_.event_loop().post(
            [this, dialog] {
                if (state_operation_dialog_ == dialog)
                    finish_state_operation("Loading state cancelled.", false);
            },
            "mapperbus.state-cancel");
    });
    state_operation_dialog_->present(window_);
    // Publish Paused before the writer takes exclusive machine access.
    refresh_ui();

    core::Result<void> started;
    if (save) {
        auto fence = session_->begin_background_persistence();
        started = state_io_.save(
            session_->emulator_handle(), pending_state_operation_->path, std::move(fence));
        if (!started)
            session_->cancel_background_persistence();
    } else {
        app::StateLoadRequest request{pending_state_operation_->path, {}};
        if (session_->current_rom_member().empty() && !session_->emulator().is_fds())
            request.legacy = app::legacy_state_path(session_->current_rom_path(), slot);
        started = state_io_.load(std::move(request));
    }
    if (!started)
        finish_state_operation(started.error(), false);
}

void MapperBusGuiController::dismiss_state_operation() {
    auto dialog = std::exchange(state_operation_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::finish_state_operation(std::string message, bool success) {
    const auto operation = std::move(*pending_state_operation_);
    pending_state_operation_.reset();
    dismiss_state_operation();
    session_->set_paused(operation.previously_paused);
    frame_accumulator_ = std::chrono::nanoseconds{0};
    last_tick_time_ = std::chrono::steady_clock::now();
    if (success) {
        select_state_slot(operation.slot);
        if (!operation.save) {
            refresh_preview();
            refresh_settings_dialog_sections();
        }
    }
    set_message(std::move(message));
    refresh_ui();
    focus_game_surface();
}

void MapperBusGuiController::poll_state_operation() {
    auto completed = state_io_.poll();
    if (!completed)
        return;
    if (pending_state_operation_->save) {
        core::Result<void> saved;
        if (!*completed)
            saved = std::unexpected(completed->error());
        session_->finish_background_persistence(saved);
    }
    if (!*completed) {
        const bool cancelled_close = pending_state_operation_->save && close_intent_.has_value();
        if (cancelled_close) {
            close_intent_.reset();
            close_ready_ = false;
            close_captured_ = false;
        }
        finish_state_operation(
            "Failed to " + std::string(pending_state_operation_->save ? "save" : "load") +
                " state: " + completed->error() + (cancelled_close ? ". Closing cancelled." : ""),
            false);
        return;
    }
    auto& loaded = **completed;
    if (!pending_state_operation_->save) {
        if (loaded.legacy)
            core::logger::warn(
                "Importing a legacy sibling state; old versions cannot verify ROM identity: {}",
                loaded.path);
        if (auto applied = session_->load_state_data(loaded.data, loaded.legacy); !applied) {
            finish_state_operation("Failed to load state: " + applied.error(), false);
            return;
        }
    }
    finish_state_operation(
        std::string(pending_state_operation_->save ? "Saved state to " : "Loaded state from ") +
            basename_for_display(loaded.path) + ".",
        true);
}

void MapperBusGuiController::save_session_state(int slot) {
    begin_state_operation(slot, true);
}

void MapperBusGuiController::load_session_state(int slot) {
    begin_state_operation(slot, false);
}

void MapperBusGuiController::apply_region_preference() {
    if (!actions_->snapshot().has_cartridge || !detected_region_) {
        return;
    }
    actions_->set_region(region_for_preference(configuration_.frontend.region, *detected_region_));
}

void MapperBusGuiController::confirm_clear_recents() {
    auto dialog = nk::Dialog::create(
        "Clear recents?",
        "This removes the list of recently opened games. It does not delete any ROM files.");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    dialog->add_button("Clear", nk::DialogResponse::Accept);
    (void)dialog->on_response().connect([this, dialog](nk::DialogResponse response) {
        if (response == nk::DialogResponse::Accept) {
            clear_recent_roms(configuration_);
            save_configuration_state();
            set_message("Cleared recent games.");
            refresh_welcome_view();
        }
        focus_game_surface();
    });
    dialog->present(window_);
}

void MapperBusGuiController::open_dropped_files(const std::vector<std::filesystem::path>& files) {
    for (const auto& file : files) {
        if (!is_rom_path(file)) {
            continue;
        }
        attempt_open(file.string());
        return;
    }
    set_message("Drop a .nes, .fds, .zip, or .7z file.");
    focus_game_surface();
}

void MapperBusGuiController::close_current_rom() {
    if (!session_->has_cartridge()) {
        set_message("No ROM is open.");
        return;
    }
    request_session_close(false);
}

void MapperBusGuiController::dismiss_close_dialog() {
    auto dialog = std::exchange(close_save_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::request_session_close(bool quit) {
    if (close_authorized_)
        return;
    if (!startup_ready_) {
        (void)configuration_loader_.cancel();
        startup_loading_ = false;
        initial_rom_request_.reset();
        close_authorized_ = true;
        window_.request_close();
        return;
    }
    if (close_intent_) {
        if (quit)
            close_intent_ = true;
        return;
    }
    close_intent_ = quit;
    close_ready_ = false;
    close_captured_ = false;
    close_data_saved_ = false;
    close_configuration_submitted_ = false;
    close_waiting_configuration_ = false;
    dismiss_configuration_change();
    dismiss_rom_load_error();
    if (disk_export_.busy()) {
        if (auto status = disk_export_status_.lock())
            status->set_text(quit ? "Finishing disk export before quitting…"
                                  : "Finishing disk export before closing the game…");
    } else {
        dismiss_disk_export_dialog();
    }
    // Cancel reads immediately. Existing exclusive saves finish before closing.
    if (state_io_.cancel())
        finish_state_operation("Loading state cancelled.", false);
    if (rom_loader_.cancel())
        abandon_rom_load("Opening ROM cancelled.");
    if (archive_dialog_ && archive_dialog_->is_presented())
        archive_dialog_->close(nk::DialogResponse::Cancel);
    cancel_gamepad_assignment();
    close_input_monitor();
    if (rebind_dialog_)
        cancel_rebind_dialog();
    close_settings();
    if (settings_window_ && settings_window_->is_visible())
        settings_window_->request_close();
    if (settings_dialog_ && settings_dialog_->is_presented())
        settings_dialog_->close();
    if (state_slot_dialog_ && state_slot_dialog_->is_presented())
        state_slot_dialog_->close();
    if (fds_dialog_ && fds_dialog_->is_presented())
        fds_dialog_->close();
    // Run after the modal-close callbacks have restored their prior pause flags.
    app_.event_loop().post([this] { close_ready_ = true; }, "mapperbus.close-ready");
}

void MapperBusGuiController::start_close_save() {
    if (!close_captured_) {
        update_configuration_from_state();
        close_previously_paused_ = session_->paused();
        close_captured_ = true;
        session_->set_paused(true);
    }
    dismiss_close_dialog();
    auto content = Box::vertical(12.0F);
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    content->append(spinner);
    auto message = nk::Label::create(*close_intent_ ? "Saving progress before quitting…"
                                                    : "Saving progress before closing the game…");
    message->set_wrapping(true);
    content->append(message);
    close_save_dialog_ = std::make_shared<FileOperationDialog>("Saving Progress", false);
    close_save_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
    close_save_dialog_->present(window_);
    refresh_ui();
    auto fence = session_->begin_background_persistence();
    auto started = close_save_task_.start(
        [machine = session_->emulator_handle(), fence = std::move(fence)] {
            fence();
            return machine->flush_persistent_data();
        },
        false);
    close_saving_ = started.has_value();
    if (!started) {
        session_->cancel_background_persistence();
        show_close_error(started.error());
    }
}

void MapperBusGuiController::show_close_error(std::string error) {
    dismiss_close_dialog();
    auto content = Box::vertical(12.0F);
    auto label = nk::Label::create(std::move(error));
    label->set_wrapping(true);
    content->append(label);
    auto help = nk::Label::create("MapperBus remains open. Fix the save location and retry, or "
                                  "keep the application open to recover your changes.");
    help->set_wrapping(true);
    content->append(help);
    close_save_dialog_ = nk::Dialog::create("Could Not Save Progress");
    close_save_dialog_->set_content(FixedWidthSlot::create(480.0F, content));
    close_save_dialog_->add_button("Keep Open", nk::DialogResponse::Cancel);
    close_save_dialog_->add_button("Retry", nk::DialogResponse::Accept);
    close_save_dialog_->set_default_response(nk::DialogResponse::Cancel);
    (void)close_save_dialog_->on_response().connect([this](nk::DialogResponse response) {
        if (!close_save_dialog_)
            return;
        auto dialog = close_save_dialog_;
        app_.event_loop().post(
            [this, dialog, response] {
                if (close_save_dialog_ != dialog)
                    return;
                dismiss_close_dialog();
                if (response == nk::DialogResponse::Accept) {
                    if (!configuration_saver_.error().empty())
                        retry_configuration_save();
                    close_data_saved_ = false;
                    close_configuration_submitted_ = false;
                } else {
                    close_intent_.reset();
                    close_ready_ = false;
                    if (close_captured_)
                        session_->set_paused(close_previously_paused_);
                    close_captured_ = false;
                    close_data_saved_ = false;
                    close_configuration_submitted_ = false;
                    frame_accumulator_ = std::chrono::nanoseconds{0};
                    last_tick_time_ = std::chrono::steady_clock::now();
                    refresh_ui();
                    focus_game_surface();
                }
            },
            "mapperbus.close-response");
    });
    close_save_dialog_->present(window_);
}

void MapperBusGuiController::poll_session_close() {
    if (!close_intent_)
        return;
    if (close_saving_) {
        auto completed = close_save_task_.poll();
        if (!completed)
            return;
        close_saving_ = false;
        session_->finish_background_persistence(*completed);
        if (!*completed) {
            show_close_error(completed->error());
            return;
        }
        close_data_saved_ = true;
        dismiss_close_dialog();
    }
    if (!close_ready_ || loading_request_ || pending_state_operation_ || disk_export_.busy())
        return;
    if (close_waiting_configuration_) {
        if (configuration_saver_.busy())
            return;
        close_waiting_configuration_ = false;
        dismiss_close_dialog();
    }
    if (close_save_dialog_) // An error waits for Keep Open or Retry.
        return;
    const auto wait_for_settings = [this] {
        auto content = Box::vertical(12.0F);
        auto spinner = nk::Spinner::create();
        spinner->set_spinning(true);
        content->append(spinner);
        auto label = nk::Label::create(*close_intent_ ? "Saving settings before quitting…"
                                                      : "Saving settings before closing the game…");
        label->set_wrapping(true);
        content->append(label);
        close_save_dialog_ = std::make_shared<FileOperationDialog>("Saving Settings", false);
        close_save_dialog_->set_content(FixedWidthSlot::create(440.0F, content));
        close_save_dialog_->present(window_);
        close_waiting_configuration_ = true;
    };
    // Proposed hardware/storage must commit before an exclusive machine writer.
    if (configuration_change_) {
        if (!configuration_saver_.error().empty())
            show_close_error("Could not save settings: " + configuration_saver_.error());
        else
            wait_for_settings();
        return;
    }
    if (!close_data_saved_) {
        start_close_save();
        return;
    }
    if (!close_configuration_submitted_) {
        save_configuration_state();
        close_configuration_submitted_ = true;
    }
    if (configuration_saver_.busy()) {
        wait_for_settings();
        return;
    }
    if (!configuration_saver_.error().empty()) {
        show_close_error("Could not save settings: " + configuration_saver_.error());
        return;
    }
    if (*close_intent_) {
        session_->emulator().set_persistence_enabled(false);
        close_authorized_ = true;
        window_.request_close();
    } else {
        close_intent_.reset();
        close_ready_ = false;
        close_captured_ = false;
        finish_close_rom();
    }
}

void MapperBusGuiController::finish_close_rom() {
    // The checked worker already saved all data; retirement must do no I/O.
    session_->emulator().set_persistence_enabled(false);
    actions_->close_rom();
    session_->emulator().set_persistence_enabled(true);
    input_backend_->set_gamepad_assignments(app::gamepad_assignments_for_game(configuration_, {}));
    app::apply_controller_hardware(session_->emulator(),
                                   app::controller_hardware(configuration_.input));
    detected_region_.reset();
    frame_accumulator_ = std::chrono::nanoseconds{0};
    clear_preview();
    set_message("Closed current ROM.");
    refresh_ui();
    refresh_settings_dialog_sections();
    focus_game_surface();
}

void MapperBusGuiController::set_preview_scale_option(PreviewScaleOption option) {
    if (preview_scale_option_ == option) {
        return;
    }

    preview_scale_option_ = option;
    // R03: whole physical-pixel scaling applies to the unfiltered picture.
    preview_->set_integer_scaling(option == PreviewScaleOption::PixelPerfect);
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
    if (!startup_ready_ || machine_worker_active())
        return;
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        const auto& input = input_backend_->player_input(player);
        auto& keyboard = configuration_.input.keyboard_profile(player);
        for (const auto button : kBindingOrder) {
            keyboard[platform::button_index(button)] = static_cast<int>(input.binding(button));
        }
        app::remember_gamepad_profile(
            configuration_, session_->game_identity(), player, input.gamepad_config());
    }
    configuration_.audio = actions_->snapshot().audio_settings;
    configuration_.frontend.preview_scale_index = preview_scale_index(preview_scale_option_);
    configuration_.frontend.ui_density_index = density_index(app_.theme_selection().density);
    configuration_.frontend.audio_muted = audio_backend_->is_muted();
}

void MapperBusGuiController::save_configuration_state() {
    if (!startup_ready_)
        return;
    update_configuration_from_state();
    if (configuration_change_)
        configuration_save_deferred_ = true;
    else if (configuration_saves_enabled_)
        (void)configuration_saver_.submit(configuration_);
    refresh_configuration_save_status();
}

void MapperBusGuiController::refresh_configuration_save_status() {
    if (!configuration_saves_enabled_) {
        update_settings_save_status("Changes are not saved in this session.");
        if (settings_save_retry_)
            settings_save_retry_->set_visible(false);
        return;
    }
    const auto& error = configuration_saver_.error();
    update_settings_save_status(!error.empty()                ? "Failed to save settings: " + error
                                : configuration_saver_.busy() ? "Saving settings…"
                                                              : "Saved automatically");
    if (settings_save_retry_) {
        settings_save_retry_->set_visible(!error.empty());
        settings_save_retry_->set_sensitive(!configuration_saver_.busy());
    }
}

void MapperBusGuiController::retry_configuration_save() {
    if (!startup_ready_ || !configuration_saves_enabled_)
        return;
    if (!configuration_change_) {
        update_configuration_from_state();
        (void)configuration_saver_.submit(configuration_);
    }
    configuration_saver_.retry();
    refresh_configuration_save_status();
    if (configuration_change_ && !close_intent_)
        show_configuration_change(false);
}

void MapperBusGuiController::dismiss_configuration_change() {
    auto dialog = std::exchange(configuration_change_dialog_, {});
    if (dialog && dialog->is_presented())
        dialog->close();
}

void MapperBusGuiController::show_configuration_change(bool failed) {
    dismiss_configuration_change();
    auto content = Box::vertical(12.0F);
    if (!failed) {
        auto spinner = nk::Spinner::create();
        spinner->set_spinning(true);
        content->append(spinner);
    }
    auto message = nk::Label::create(failed ? "Could not save the proposed settings: " +
                                                  configuration_saver_.error()
                                            : "Saving settings before applying this change…");
    message->set_wrapping(true);
    content->append(message);
    if (failed) {
        auto help = nk::Label::create("The previous settings remain active. Fix the save location "
                                      "and retry, or keep the current settings.");
        help->set_wrapping(true);
        content->append(help);
        configuration_change_dialog_ = nk::Dialog::create("Could Not Save Settings");
        configuration_change_dialog_->add_button("Keep Current Settings",
                                                 nk::DialogResponse::Cancel);
        configuration_change_dialog_->add_button("Retry", nk::DialogResponse::Accept);
        configuration_change_dialog_->set_default_response(nk::DialogResponse::Cancel);
        (void)configuration_change_dialog_->on_response().connect(
            [this](nk::DialogResponse response) {
                if (!configuration_change_dialog_)
                    return;
                auto dialog = configuration_change_dialog_;
                app_.event_loop().post(
                    [this, dialog, response] {
                        if (configuration_change_dialog_ != dialog)
                            return;
                        dismiss_configuration_change();
                        if (response == nk::DialogResponse::Accept)
                            retry_configuration_save();
                        else
                            discard_configuration_change();
                    },
                    "mapperbus.configuration-response");
            });
    } else {
        configuration_change_dialog_ =
            std::make_shared<FileOperationDialog>("Saving Settings", false);
    }
    configuration_change_dialog_->set_content(FixedWidthSlot::create(480.0F, content));
    configuration_change_dialog_->present(settings_host_window());
}

void MapperBusGuiController::begin_configuration_change(app::MapperBusConfiguration proposed,
                                                        std::function<void()> apply,
                                                        std::string success_message,
                                                        std::function<void()> discarded) {
    if (!startup_ready_ || configuration_change_ || close_intent_)
        return;
    const auto revision = configuration_saves_enabled_ ? configuration_saver_.submit(proposed) : 0;
    configuration_change_ = PendingConfigurationChange{revision,
                                                       std::move(proposed),
                                                       std::move(apply),
                                                       std::move(success_message),
                                                       std::move(discarded)};
    configuration_save_deferred_ = false;
    if (!configuration_saves_enabled_)
        return; // Applied on the next UI poll; no file writer is started.
    // An explicit new proposal is also an explicit retry of failed storage.
    configuration_saver_.retry();
    refresh_configuration_save_status();
    app_.event_loop().post(
        [this, revision] {
            if (configuration_change_ && configuration_change_->revision == revision &&
                !close_intent_)
                show_configuration_change(!configuration_saver_.error().empty());
        },
        "mapperbus.configuration-start");
}

void MapperBusGuiController::discard_configuration_change() {
    if (configuration_saver_.busy())
        return;
    dismiss_configuration_change();
    auto discarded = configuration_change_ ? std::move(configuration_change_->discarded)
                                           : std::function<void()>{};
    configuration_change_.reset();
    configuration_save_deferred_ = false;
    update_configuration_from_state();
    if (configuration_saves_enabled_)
        configuration_saver_.replace_failed(configuration_);
    refresh_configuration_save_status();
    refresh_settings_dialog_sections();
    refresh_ui();
    if (discarded)
        discarded();
}

void MapperBusGuiController::poll_configuration_saves() {
    auto completed = configuration_saver_.poll();
    if (!configuration_saves_enabled_ && configuration_change_)
        completed = app::ConfigurationSaveCompletion{0, {}};
    if (completed && configuration_change_ &&
        completed->revision == configuration_change_->revision && completed->result) {
        dismiss_configuration_change();
        auto change = std::move(*configuration_change_);
        configuration_change_.reset();
        configuration_ = std::move(change.proposed);
        change.apply();
        if (!change.success_message.empty())
            set_message(std::move(change.success_message));
        if (std::exchange(configuration_save_deferred_, false))
            save_configuration_state();
        refresh_ui();
        refresh_settings_dialog_sections();
    }
    if (configuration_change_ && !configuration_saver_.error().empty() && !close_intent_ &&
        (!configuration_change_dialog_ || !configuration_change_dialog_->is_presented() ||
         dynamic_cast<FileOperationDialog*>(configuration_change_dialog_.get())))
        show_configuration_change(true);
    if (completed && !completed->result)
        set_message("Could not save settings: " + completed->result.error());
    refresh_configuration_save_status();
}

void MapperBusGuiController::save_controller_hardware_change(
    app::MapperBusConfiguration proposed,
    const app::ControllerHardwareSettings& settings,
    std::string success_message) {
    begin_configuration_change(
        std::move(proposed),
        [this, settings] { app::apply_controller_hardware(session_->emulator(), settings); },
        std::move(success_message));
}

void MapperBusGuiController::change_controller_hardware(
    const app::ControllerHardwareOverrides& change) {
    update_configuration_from_state();
    auto proposed = configuration_;
    const auto identity = session_->game_identity();
    auto saved = app::controller_hardware_for_game(proposed, identity, change);
    if (proposed.game_controllers.contains(identity))
        app::remember_controller_hardware(proposed, identity, saved);
    else
        app::set_controller_hardware(proposed.input, saved);
    // A state may have restored different hardware. Only the edited field changes
    // in the running machine; unrelated saved preferences remain intact too.
    auto active = app::controller_hardware(session_->emulator());
    change.apply_to(active);
    save_controller_hardware_change(std::move(proposed), active);
}

void MapperBusGuiController::remember_game_controller_hardware(bool remember) {
    const auto identity = session_->game_identity();
    if (identity.empty())
        return;
    update_configuration_from_state();
    auto proposed = configuration_;
    auto settings = app::controller_hardware(session_->emulator());
    if (remember)
        app::remember_controller_hardware(proposed, identity, settings);
    else {
        proposed.game_controllers.erase(identity);
        settings = app::controller_hardware(proposed.input);
    }
    save_controller_hardware_change(std::move(proposed),
                                    settings,
                                    remember ? "Controller hardware remembered for this game."
                                             : "Using global controller hardware defaults.");
}

void MapperBusGuiController::remember_game_assignments(bool remember) {
    const auto identity = session_->game_identity();
    if (identity.empty())
        return;
    update_configuration_from_state();
    auto proposed = configuration_;
    if (remember) {
        platform::GamepadAssignments assignments;
        for (int player = 0; player < platform::kInputProfileCount; ++player)
            assignments[player] =
                platform::gamepad_assignment(input_backend_->player_input(player).gamepad_config());
        proposed.game_gamepads[identity] = std::move(assignments);
    } else {
        proposed.game_gamepads.erase(identity);
    }
    begin_configuration_change(std::move(proposed), [this, identity] {
        input_backend_->set_gamepad_assignments(
            app::gamepad_assignments_for_game(configuration_, identity));
    });
}

void MapperBusGuiController::open_gamepad_assignment() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    if (gamepad_assignment_dialog_ || rebind_dialog_ || input_monitor_dialog_)
        return;
    refresh_ui();
    update_configuration_from_state();
    const auto snapshot = actions_->snapshot();
    resume_after_gamepad_assignment_ = snapshot.has_cartridge && !snapshot.paused;
    if (resume_after_gamepad_assignment_)
        actions_->pause();
    input_backend_->set_assignment_capture(true);
    gamepad_assignment_profiles_ = NodalKitInput::gamepad_assignment_profiles(
        snapshot.controller_mode, snapshot.expansion_controllers);
    std::vector<std::string> roles;
    for (int player : gamepad_assignment_profiles_)
        roles.push_back(input_profile_name(player, snapshot.controller_mode));
    gamepad_assignment_ = std::make_unique<Sdl3GamepadAssignment>(std::move(roles));
    auto dialog = nk::Dialog::create("Assign Gamepads");
    gamepad_assignment_dialog_ = dialog;
    dialog->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    dialog->set_minimum_panel_width(460.0F);
    dialog->set_default_response(nk::DialogResponse::Cancel);
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    auto content = Box::vertical(10.0F);
    auto instructions =
        nk::Label::create("Press a button for each role below, in order. Bindings are kept.");
    instructions->set_wrapping(true);
    content->append(instructions);
    gamepad_assignment_status_ = nk::Label::create(gamepad_assignment_->status());
    gamepad_assignment_status_->set_wrapping(true);
    content->append(gamepad_assignment_status_);
    for (std::size_t player = 0; player < gamepad_assignment_profiles_.size(); ++player) {
        auto label = nk::Label::create(gamepad_assignment_->role_name(player) + ": Not assigned");
        label->set_wrapping(true);
        gamepad_assignment_labels_[player] = label;
        content->append(label);
    }
    auto finish_help =
        nk::Label::create("Changes are saved after all listed controllers are assigned and buttons "
                          "are released. Cancel keeps existing assignments.");
    finish_help->set_wrapping(true);
    content->append(finish_help);
    dialog->set_content(content);
    (void)dialog->on_response().connect(
        [this, weak_dialog = std::weak_ptr<nk::Dialog>(dialog)](nk::DialogResponse response) {
            const auto closed = weak_dialog.lock();
            if (!closed || gamepad_assignment_dialog_ != closed)
                return;
            gamepad_assignment_dialog_.reset();
            gamepad_assignment_.reset();
            gamepad_assignment_profiles_.clear();
            gamepad_assignment_status_.reset();
            gamepad_assignment_labels_.fill(nullptr);
            input_backend_->set_assignment_capture(false);
            if (response == nk::DialogResponse::Accept) {
                (void)input_backend_->consume_gamepad_assignment_changes();
                set_message("Gamepads assigned. Button bindings retained.");
            } else if (response == nk::DialogResponse::Custom) {
                set_message("Saving controller assignments…");
            } else {
                set_message("Gamepad assignment cancelled.");
            }
            if (resume_after_gamepad_assignment_)
                actions_->resume();
            resume_after_gamepad_assignment_ = false;
            refresh_ui();
            refresh_settings_dialog_sections();
            if (settings_tabs_)
                settings_tabs_->grab_focus();
        });
    gamepad_assignment_last_poll_ = std::chrono::steady_clock::now();
    dialog->present(settings_host_window());
#else
    set_message("Gamepad support is unavailable in this build.");
#endif
}

void MapperBusGuiController::cancel_gamepad_assignment() {
    if (gamepad_assignment_dialog_) {
        const auto dialog = gamepad_assignment_dialog_;
        dialog->close(nk::DialogResponse::Cancel);
    }
}

void MapperBusGuiController::poll_gamepad_assignment() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    if (!gamepad_assignment_ || !gamepad_assignment_dialog_)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - gamepad_assignment_last_poll_ < std::chrono::milliseconds(16))
        return;
    gamepad_assignment_last_poll_ = now;
    gamepad_assignment_->poll();
    const auto& status = gamepad_assignment_->status();
    if (gamepad_assignment_status_->text() != status)
        gamepad_assignment_status_->set_text(status);
    for (std::size_t player = 0; player < gamepad_assignment_profiles_.size(); ++player) {
        const auto text = gamepad_assignment_->role_name(player) + ": " +
                          gamepad_assignment_->device_name(player);
        if (gamepad_assignment_labels_[player]->text() != text)
            gamepad_assignment_labels_[player]->set_text(text);
    }
    if (!gamepad_assignment_->ready())
        return;
    if (configuration_change_)
        return;
    std::optional<app::MapperBusConfiguration> proposal;
    // Stage and validate devices using the existing atomic assignment seam,
    // but reject live commit until the owned configuration write succeeds.
    (void)input_backend_->assign_gamepads(
        gamepad_assignment_profiles_,
        gamepad_assignment_->assignments(),
        [this, &proposal](auto profiles) {
            auto updated = configuration_;
            for (std::size_t player = 0; player < gamepad_assignment_profiles_.size(); ++player)
                app::remember_gamepad_profile(updated,
                                              session_->game_identity(),
                                              gamepad_assignment_profiles_[player],
                                              profiles[player]);
            proposal = std::move(updated);
            return false;
        });
    const auto dialog = gamepad_assignment_dialog_;
    if (!proposal) {
        dialog->close(nk::DialogResponse::Cancel);
        set_message(
            "A controller changed before assignment. Existing assignments were kept; try again.");
        return;
    }
    const auto profiles = gamepad_assignment_profiles_;
    const auto chosen_span = gamepad_assignment_->assignments();
    const std::vector<SDL_JoystickID> chosen(chosen_span.begin(), chosen_span.end());
    dialog->close(nk::DialogResponse::Custom);
    begin_configuration_change(std::move(*proposal), [this, profiles, chosen] {
        if (input_backend_->assign_gamepads(profiles, chosen)) {
            (void)input_backend_->consume_gamepad_assignment_changes();
            set_message("Gamepads assigned. Button bindings retained.");
        } else {
            set_message("Settings saved, but a controller disconnected before assignment. "
                        "Reconnect it and run Assign Gamepads again.");
        }
    });
#endif
}

void MapperBusGuiController::open_rebind_dialog(RebindDevice device, platform::InputAction button) {
    pending_rebind_ = PendingInputRebind{
        .device = device,
        .button = button,
        .started_at = std::chrono::steady_clock::now(),
    };
    if (device == RebindDevice::Gamepad) {
        pending_rebind_->initially_pressed_gamepad_controls =
            input_backend_->player_input(settings_input_player_)
                .pressed_gamepad_controls(gamepad_capture_controls());
    }

    const std::string target =
        input_profile_name(settings_input_player_, actions_->snapshot().controller_mode) + " · " +
        button_name(button);
    const bool keyboard = device == RebindDevice::Keyboard;
    rebind_dialog_ = nk::Dialog::create("Rebind " + target);
    rebind_dialog_->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    rebind_dialog_->set_minimum_panel_width(420.0F);
    rebind_dialog_->add_button("Cancel", nk::DialogResponse::Cancel);
    rebind_dialog_->add_button("Clear", nk::DialogResponse::Custom);

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
                if (NodalKitInput::is_reserved_game_key(key)) {
                    pending_rebind_.reset();
                    if (rebind_dialog_) {
                        auto dialog = rebind_dialog_;
                        rebind_dialog_.reset();
                        dialog->close(nk::DialogResponse::Cancel);
                    }
                    set_message(reserved_key_message(key));
                    focus_game_surface();
                    return;
                }
                apply_keyboard_rebind(pending_rebind_->button, key);
            });
        // Capture at the dialog boundary so keys arrive from either its content
        // or the modal root during native focus restoration.
        rebind_dialog_->add_controller(std::move(keyboard_controller));
    }

    rebind_dialog_->set_content(content);
    auto dialog = rebind_dialog_;
    (void)rebind_dialog_->on_response().connect([this, dialog](nk::DialogResponse response) {
        if (rebind_dialog_ == dialog && pending_rebind_ && response == nk::DialogResponse::Custom) {
            const auto pending = *pending_rebind_;
            if (pending.device == RebindDevice::Keyboard) {
                apply_keyboard_rebind(pending.button, nk::KeyCode::Unknown);
            } else {
                apply_gamepad_rebind(pending.button, {.kind = platform::GamepadControlKind::None});
            }
            return;
        }
        if (rebind_dialog_ == dialog && pending_rebind_) {
            pending_rebind_.reset();
            set_message("Input rebinding cancelled.");
        }
        if (rebind_dialog_ == dialog) {
            rebind_dialog_.reset();
            focus_game_surface();
        }
    });
    rebind_dialog_->present(settings_host_window());
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

    const auto pressed = input_backend_->player_input(settings_input_player_)
                             .pressed_gamepad_controls(gamepad_capture_controls());
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

void MapperBusGuiController::apply_keyboard_rebind(platform::InputAction button, nk::KeyCode key) {
    const auto previous = input_backend_->player_input(settings_input_player_).binding(button);
    std::optional<platform::InputAction> conflict_button;
    for (const auto other : kBindingOrder) {
        if (key != nk::KeyCode::Unknown && other != button &&
            input_backend_->player_input(settings_input_player_).binding(other) == key) {
            conflict_button = other;
            break;
        }
    }

    const auto commit =
        [this, player = settings_input_player_, button, key, previous, conflict_button] {
            if (conflict_button) {
                input_backend_->player_input(player).set_binding(*conflict_button, previous);
            }
            input_backend_->player_input(player).set_binding(button, key);
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
        auto dialog = nk::Dialog::create(
            "Replace binding?",
            key_label(key) + " is already used by the " + button_name(*conflict_button) +
                " button. Replace swaps them: " + button_name(*conflict_button) + " moves to " +
                key_label(previous) + ".");
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
        dialog->present(settings_host_window());
        return;
    }

    commit();
    close_rebind_dialog();
}

void MapperBusGuiController::apply_gamepad_rebind(platform::InputAction button,
                                                  platform::GamepadControl control) {
    const auto previous =
        input_backend_->player_input(settings_input_player_).gamepad_binding(button);
    std::optional<platform::InputAction> conflict_button;
    for (const auto other : kBindingOrder) {
        if (control.kind != platform::GamepadControlKind::None && other != button &&
            input_backend_->player_input(settings_input_player_).gamepad_binding(other) ==
                control) {
            conflict_button = other;
            break;
        }
    }

    const auto commit =
        [this, player = settings_input_player_, button, control, previous, conflict_button] {
            if (conflict_button) {
                input_backend_->player_input(player).set_gamepad_binding(*conflict_button,
                                                                         previous);
            }
            input_backend_->player_input(player).set_gamepad_binding(button, control);
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
        auto dialog =
            nk::Dialog::create("Replace binding?",
                               gamepad_control_label(control) + " is already used by the " +
                                   button_name(*conflict_button) +
                                   " button. Replace swaps them: " + button_name(*conflict_button) +
                                   " moves to " + gamepad_control_label(previous) + ".");
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
        dialog->present(settings_host_window());
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
    if (!input_test_label_ || input_monitor_dialog_ ||
        (settings_window_ && !settings_window_->is_visible())) {
        return;
    }

    input_backend_->poll();
    input_test_label_->set_text(input_test_status_text());
    if (live_microphone_status_)
        live_microphone_status_->set_text(session_->live_microphone().status());
}

void MapperBusGuiController::open_input_monitor() {
    if (input_monitor_dialog_ || gamepad_assignment_dialog_ || rebind_dialog_)
        return;
    const auto snapshot = actions_->snapshot();
    resume_after_input_monitor_ = snapshot.has_cartridge && !snapshot.paused;
    if (resume_after_input_monitor_)
        actions_->pause();
    auto dialog = InputMonitorDialog::create();
    input_monitor_dialog_ = dialog;
    dialog->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    dialog->set_minimum_panel_width(500.0F);
    dialog->set_default_response(nk::DialogResponse::Close);
    dialog->add_button("Done", nk::DialogResponse::Close);
    auto content = Box::vertical(10.0F);
    auto help =
        nk::Label::create("Test keyboard and gamepad bindings for every active controller. "
                          "Held bindings are shown before turbo pulses and hardware filtering. "
                          "A running game pauses until this panel closes.");
    help->set_wrapping(true);
    content->append(help);
    auto rows = Box::vertical(10.0F);
    for (auto& label : input_monitor_labels_) {
        label = nk::Label::create();
        label->set_wrapping(true);
        label->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        label->set_visible(false);
        rows->append(label);
    }
    auto scroll = rectangular_scroll_area();
    scroll->set_h_scroll_policy(nk::ScrollPolicy::Never);
    scroll->set_v_scroll_policy(nk::ScrollPolicy::Automatic);
    scroll->set_content(rows);
    const auto count =
        active_controller_profiles(snapshot.controller_mode, snapshot.expansion_controllers).size();
    content->append(BoundedHeightSlot::create(
        400.0F, FixedHeightSlot::create(56.0F * static_cast<float>(count), scroll)));
    dialog->set_content(content);
    (void)dialog->on_response().connect(
        [this, weak_dialog = std::weak_ptr<nk::Dialog>(dialog)](nk::DialogResponse) {
            const auto closed = weak_dialog.lock();
            if (!closed || input_monitor_dialog_ != closed)
                return;
            input_monitor_dialog_.reset();
            input_monitor_labels_.fill(nullptr);
            if (resume_after_input_monitor_)
                actions_->resume();
            resume_after_input_monitor_ = false;
            refresh_ui();
            if (settings_tabs_)
                settings_tabs_->grab_focus();
        });
    input_monitor_last_poll_ = {};
    update_input_monitor();
    refresh_ui();
    dialog->present(settings_host_window());
    dialog->grab_focus();
}

void MapperBusGuiController::close_input_monitor() {
    if (input_monitor_dialog_) {
        const auto dialog = input_monitor_dialog_;
        dialog->close(nk::DialogResponse::Close);
    }
}

void MapperBusGuiController::update_input_monitor() {
    if (!input_monitor_dialog_)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - input_monitor_last_poll_ < std::chrono::milliseconds(33))
        return;
    input_monitor_last_poll_ = now;
    if (!settings_host_window().is_focused())
        input_monitor_dialog_->clear_keys();
    input_backend_->poll();
    const auto snapshot = actions_->snapshot();
    auto profiles = input_backend_->monitor_profiles();
    for (int player = 0; player < platform::kInputProfileCount; ++player)
        for (auto action : platform::input_actions()) {
            const auto key = input_backend_->player_input(player).binding(action);
            if (!NodalKitInput::is_reserved_game_key(key) &&
                input_monitor_dialog_->key_pressed(key))
                profiles[player].held[platform::button_index(action)] = true;
        }
    const auto rows =
        input_monitor_rows(snapshot.controller_mode, snapshot.expansion_controllers, profiles);
    for (std::size_t index = 0; index < input_monitor_labels_.size(); ++index) {
        const auto& label = input_monitor_labels_[index];
        label->set_visible(index < rows.size());
        if (index < rows.size() && label->text() != rows[index].text)
            label->set_text(rows[index].text);
    }
}

void MapperBusGuiController::update_settings_save_status(std::string text) {
    settings_save_status_ = std::move(text);
    if (settings_save_label_ && settings_save_label_->text() != settings_save_status_)
        settings_save_label_->set_text(settings_save_status_);
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_dialog_shell() {
    auto content = Box::vertical(16.0F);
    content->set_vertical_size_policy(nk::SizePolicy::Expanding);
    content->set_vertical_stretch(1);
    const auto page_index = [this]() {
        switch (settings_page_) {
        case SettingsPage::Video:
            return 1;
        case SettingsPage::Audio:
            return 2;
        case SettingsPage::General:
            return 3;
        case SettingsPage::Input:
        default:
            return 0;
        }
    };

    auto tabs_row = Box::horizontal(12.0F);
    settings_tabs_ = nk::SegmentedControl::create();
    settings_tabs_->ensure_accessible().set_name("Settings categories");
    settings_tabs_->set_segments({"Input", "Video", "Audio", "General"});
    settings_tabs_->set_selected_index(page_index());
    (void)settings_tabs_->on_selection_changed().connect([this](int index) {
        const auto next_page = [index]() {
            switch (index) {
            case 1:
                return SettingsPage::Video;
            case 2:
                return SettingsPage::Audio;
            case 3:
                return SettingsPage::General;
            case 0:
            default:
                return SettingsPage::Input;
            }
        }();
        if (settings_page_ == next_page) {
            return;
        }
        settings_page_ = next_page;
        configuration_.frontend.settings_page_index = index;
        save_configuration_state();
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
    settings_footer_slot_->set_margin({4.0F, 0.0F, 0.0F, 0.0F});
    content->append(settings_footer_slot_);

    refresh_settings_dialog_sections();
    // A width constraint does not add an empty layout row or shift the
    // content column relative to the dialog's title and footer.
    return content;
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_page_content() {
    live_microphone_status_.reset();
    auto page = Box::vertical(16.0F);
    page->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::General) {
        input_test_label_.reset();
        auto hardware_group = SettingsGroup::create("Hardware");
        hardware_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        const auto identity = session_->game_identity();
        const bool remembered = configuration_.game_controllers.contains(identity);
        auto remember = nk::Switch::create();
        remember->set_active(remembered);
        remember->set_sensitive(!identity.empty());
        remember->ensure_accessible().set_name("Remember for this game");
        (void)remember->on_toggled().connect(
            [this](bool active) { remember_game_controller_hardware(active); });
        hardware_group->add(pref_row(
            "Remember for this game",
            remember,
            identity.empty() ? "Open a game to save its controller hardware settings."
            : remembered
                ? "Changes below apply only to this game. Turn off to use global defaults."
                : "Changes below update global defaults. Turn on to save only for this game."));
        const bool adapter = actions_->snapshot().controller_mode != core::ControllerMode::Standard;
        auto mode_combo = nk::ComboBox::create();
        mode_combo->set_items({"Standard", "Four Score", "Hori 4 Players"});
        mode_combo->set_selected_index(static_cast<int>(actions_->snapshot().controller_mode));
        mode_combo->ensure_accessible().set_name("Controller mode");
        (void)mode_combo->on_selection_changed().connect([this](int index) {
            app::ControllerHardwareOverrides change;
            change.mode = index == 2   ? core::ControllerMode::HoriFourPlayer
                          : index == 1 ? core::ControllerMode::FourScore
                                       : core::ControllerMode::Standard;
            change_controller_hardware(change);
        });
        hardware_group->add(
            pref_row("Controller mode", mode_combo, "NES Four Score or Famicom Hori adapter."));
        auto wiring_combo = nk::ComboBox::create();
        wiring_combo->set_items({"NES", "Original RF Famicom"});
        wiring_combo->set_selected_index(
            actions_->snapshot().controller_wiring == core::ControllerWiring::RfFamicom ? 1 : 0);
        wiring_combo->ensure_accessible().set_name("Controller wiring");
        (void)wiring_combo->on_selection_changed().connect([this](int index) {
            app::ControllerHardwareOverrides change;
            change.wiring =
                index == 1 ? core::ControllerWiring::RfFamicom : core::ControllerWiring::Nes;
            change_controller_hardware(change);
        });
        hardware_group->add(pref_row(
            "Controller wiring", wiring_combo, "RF built-in pad 2 has no Start or Select."));
        auto timing_combo = nk::ComboBox::create();
        timing_combo->set_items({"NES / AV Famicom", "Original RF Famicom"});
        timing_combo->set_selected_index(
            actions_->snapshot().controller_timing == core::ControllerTiming::RfFamicom ? 1 : 0);
        timing_combo->ensure_accessible().set_name("Controller timing");
        (void)timing_combo->on_selection_changed().connect([this](int index) {
            app::ControllerHardwareOverrides change;
            change.timing = index == 1 ? core::ControllerTiming::RfFamicom
                                       : core::ControllerTiming::NesAvFamicom;
            change_controller_hardware(change);
        });
        hardware_group->add(
            pref_row("Controller timing", timing_combo, "Controller ports in NTSC mode."));
        for (int port = 0; port < 2; ++port) {
            auto attached = nk::Switch::create();
            attached->set_sensitive(!adapter);
            attached->set_active((actions_->snapshot().expansion_controllers & (1U << port)) != 0);
            attached->ensure_accessible().set_name("External controller " +
                                                   std::to_string(port + 1));
            (void)attached->on_toggled().connect([this, port](bool active) {
                const auto mask = static_cast<core::Byte>(1U << port);
                auto ports = actions_->snapshot().expansion_controllers;
                ports = active ? static_cast<core::Byte>(ports | mask)
                               : static_cast<core::Byte>(ports & ~mask);
                change_controller_hardware({.expansion_controllers = ports});
            });
            hardware_group->add(
                pref_row("External controller " + std::to_string(port + 1),
                         attached,
                         adapter ? "Available in Standard controller mode."
                                 : "Famicom expansion pad; bindings are under Input > External " +
                                       std::to_string(port + 1) + "."));
        }
        page->append(hardware_group);
        auto bios_group = SettingsGroup::create("BIOS");
        bios_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        const auto bios_path = configuration_.fds_bios_path.empty()
                                   ? app::mapperbus_data_directory() / "bios" / "disksys.rom"
                                   : std::filesystem::path(configuration_.fds_bios_path);
        auto bios_status = nk::Label::create(settings_bios_status_text_);
        bios_status->set_wrapping(true);
        bios_status->set_visible(!settings_bios_status_text_.empty());
        settings_bios_status_ = bios_status;
        auto browse = nk::Button::create("Choose BIOS…");
        auto bios_row = pref_row("Famicom Disk System", browse, bios_path.filename().string());
        bios_row->ensure_accessible().set_description(bios_path.string());
        browse->ensure_accessible().set_name("Choose FDS BIOS");
        browse->ensure_accessible().set_description(bios_path.string());
        const std::weak_ptr<nk::PreferencesRow> weak_bios_row = bios_row;
        (void)browse->on_clicked().connect(
            [this, weak_bios_row] { choose_settings_bios(weak_bios_row); });
        bios_group->add(bios_row);
        page->append(bios_group);
        page->append(bios_status);
    } else if (settings_page_ == SettingsPage::Input) {
        auto player_selector = nk::SegmentedControl::create();
        const auto mode = actions_->snapshot().controller_mode;
        const int profile_count =
            mode == core::ControllerMode::HoriFourPlayer ? platform::kInputProfileCount : 4;
        std::vector<std::string> profile_names;
        for (int player = 0; player < profile_count; ++player)
            profile_names.push_back(input_profile_name(player, mode));
        settings_input_player_ = std::clamp(settings_input_player_, 0, profile_count - 1);
        player_selector->set_segments(profile_names);
        player_selector->set_selected_index(settings_input_player_);
        player_selector->ensure_accessible().set_name("Input player");
        (void)player_selector->on_selection_changed().connect([this, profile_count](int player) {
            settings_input_player_ = std::clamp(player, 0, profile_count - 1);
            refresh_settings_dialog_sections();
        });
        page->append(player_selector);
        const bool gamepad_support =
            input_backend_->player_input(settings_input_player_).gamepad_support_available();
        const int gamepad_count =
            input_backend_->player_input(settings_input_player_).gamepad_device_count();
        const bool gamepad_plugged_in =
            input_backend_->player_input(settings_input_player_).gamepad_connected();
        const bool gamepad_enabled =
            input_backend_->player_input(settings_input_player_).gamepad_config().enabled;
        const bool gamepad_available = gamepad_support && gamepad_enabled && gamepad_plugged_in;
        const std::string gamepad_state = !gamepad_support      ? "Unavailable"
                                          : !gamepad_enabled    ? "Disabled"
                                          : !gamepad_plugged_in ? "Not connected"
                                                                : "";
        const std::string gamepad_help =
            input_backend_->player_input(settings_input_player_).gamepad_status_text();

        auto setup_group = SettingsGroup::create(
            mode == core::ControllerMode::Standard ? "Controller setup" : "Four-player setup");
        setup_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        const auto identity = session_->game_identity();
        const bool remembered = configuration_.game_gamepads.contains(identity);
        auto remember = nk::Switch::create();
        remember->set_active(remembered);
        remember->set_sensitive(!identity.empty());
        (void)remember->on_toggled().connect(
            [this](bool active) { remember_game_assignments(active); });
        setup_group->add(pref_row(
            "Remember assignments for this game",
            remember,
            identity.empty() ? "Open a game to remember its player assignments."
            : remembered
                ? "Device choices and enabled switches are saved for this game. Bindings stay "
                  "shared."
                : "Device choices use global defaults. Turn on to keep this game's player order."));
        remember->ensure_accessible().set_name("Remember assignments for this game");
        if (mode == core::ControllerMode::HoriFourPlayer) {
            auto configure = nk::Button::create("Configure Four Gamepads");
            configure->set_sensitive(gamepad_support);
            (void)configure->on_clicked().connect([this] {
                update_configuration_from_state();
                for (int player = 2; player < platform::kInputProfileCount; ++player) {
                    auto& input = input_backend_->player_input(player);
                    input.set_gamepad_index(player - 2);
                    input.set_gamepad_enabled(true);
                }
                set_message("Hori 1–4 assigned to Gamepads 1–4. Button bindings retained.");
                save_configuration_state();
                refresh_ui();
                refresh_settings_dialog_sections();
            });
            auto setup_row =
                pref_row("Hori 1–4",
                         configure,
                         gamepad_support ? "Enable Gamepads 1–4."
                                         : "Gamepad support is unavailable in this build.");
            configure->ensure_accessible().set_name("Configure Four Hori Gamepads");
            configure->ensure_accessible().set_description(
                "Assign and enable Gamepads 1–4 for Hori 1–4. Keep bindings and deadzones.");
            setup_group->add(setup_row);
        }
        auto assign_by_button = nk::Button::create("Assign by Button…");
        assign_by_button->set_sensitive(gamepad_support);
        (void)assign_by_button->on_clicked().connect([this] { open_gamepad_assignment(); });
        const auto assignment_count = NodalKitInput::gamepad_assignment_profiles(
                                          mode, actions_->snapshot().expansion_controllers)
                                          .size();
        auto by_button_row = pref_row("Player order",
                                      assign_by_button,
                                      "Assign " + std::to_string(assignment_count) +
                                          " controllers in player order.");
        assign_by_button->ensure_accessible().set_name("Assign Gamepads by Button");
        setup_group->add(by_button_row);
        auto monitor = nk::Button::create("Test All Controllers…");
        (void)monitor->on_clicked().connect([this] { open_input_monitor(); });
        setup_group->add(
            pref_row("Input monitor", monitor, "See devices, held bindings and shared gamepads."));
        monitor->ensure_accessible().set_name("Test All Controllers");
        page->append(setup_group);

        auto bindings = Box::vertical(6.0F);
        bindings->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        bindings->append(SectionTitle::create("Bindings", true));
        input_test_label_ = SecondaryText::create(input_test_status_text());
        bindings->append(input_test_label_);
        auto table = InputBindingTable::create(gamepad_state);
        for (const auto button : kBindingOrder) {
            table->add_binding(
                button_name(button),
                key_label(input_backend_->player_input(settings_input_player_).binding(button)),
                gamepad_control_label(
                    input_backend_->player_input(settings_input_player_).gamepad_binding(button)),
                [this, button] { open_rebind_dialog(RebindDevice::Keyboard, button); },
                [this, button] { open_rebind_dialog(RebindDevice::Gamepad, button); },
                gamepad_available,
                gamepad_help);
        }
        bindings->append(table);
        bindings->append(
            SecondaryText::create("Microphone: hold to simulate sound with RF Famicom wiring."));
        page->append(bindings);

        auto microphone_group = SettingsGroup::create("Live microphone");
        auto capture_switch = nk::Switch::create();
        capture_switch->set_active(configuration_.microphone.enabled);
        (void)capture_switch->on_toggled().connect([this](bool enabled) {
            configuration_.microphone.enabled = enabled;
            session_->configure_live_microphone(configuration_.microphone);
            save_configuration_state();
            refresh_settings_dialog_sections();
        });
        microphone_group->add(
            pref_row("Capture input",
                     capture_switch,
                     "Uses host sound as microphone activity during RF Famicom play."));
        std::vector<std::string> device_names{""};
        auto devices = session_->live_microphone().devices();
        if (devices)
            device_names.insert(device_names.end(), devices->begin(), devices->end());
        if (!configuration_.microphone.device.empty() &&
            std::ranges::find(device_names, configuration_.microphone.device) == device_names.end())
            device_names.push_back(configuration_.microphone.device);
        auto capture_device_labels = device_names;
        capture_device_labels[0] = "System default";
        auto device_combo = nk::ComboBox::create();
        device_combo->set_items(capture_device_labels);
        const auto selected = std::ranges::find(device_names, configuration_.microphone.device);
        device_combo->set_selected_index(static_cast<int>(selected - device_names.begin()));
        (void)device_combo->on_selection_changed().connect([this, device_names](int index) {
            if (index < 0 || static_cast<std::size_t>(index) >= device_names.size())
                return;
            configuration_.microphone.device = device_names[static_cast<std::size_t>(index)];
            session_->configure_live_microphone(configuration_.microphone);
            save_configuration_state();
            refresh_settings_dialog_sections();
        });
        microphone_group->add(pref_row("Input device", device_combo));
        auto sensitivity = nk::Slider::create();
        sensitivity->ensure_accessible().set_name("Microphone sensitivity");
        sensitivity->set_range(1.0, 100.0);
        sensitivity->set_step(1.0);
        sensitivity->set_value(configuration_.microphone.sensitivity);
        (void)sensitivity->on_value_changed().connect([this](double value) {
            configuration_.microphone.sensitivity =
                std::clamp(static_cast<int>(std::lround(value)), 1, 100);
            session_->configure_live_microphone(configuration_.microphone);
            save_configuration_state();
        });
        microphone_group->add(pref_row("Sensitivity",
                                       FixedWidthSlot::create(180.0F, sensitivity),
                                       "Raise to detect quieter sounds."));
        auto microphone_audio = nk::Switch::create();
        microphone_audio->set_active(configuration_.microphone.audio_enabled);
        (void)microphone_audio->on_toggled().connect([this](bool enabled) {
            configuration_.microphone.audio_enabled = enabled;
            session_->configure_live_microphone(configuration_.microphone);
            save_configuration_state();
        });
        microphone_group->add(pref_row("Microphone audio",
                                       microphone_audio,
                                       "Play captured sound. Headphones avoid feedback."));
        auto microphone_volume = nk::Slider::create();
        microphone_volume->ensure_accessible().set_name("Microphone volume");
        microphone_volume->set_range(0.0, 100.0);
        microphone_volume->set_step(1.0);
        microphone_volume->set_value(configuration_.microphone.volume_percent);
        (void)microphone_volume->on_value_changed().connect([this](double value) {
            configuration_.microphone.volume_percent =
                std::clamp(static_cast<int>(std::lround(value)), 0, 100);
            session_->configure_live_microphone(configuration_.microphone);
            save_configuration_state();
        });
        microphone_group->add(pref_row("Microphone volume",
                                       FixedWidthSlot::create(180.0F, microphone_volume),
                                       "Requires Capture input."));
        auto retry = nk::Button::create("Retry / Refresh Devices");
        (void)retry->on_clicked().connect([this] {
            session_->retry_live_microphone();
            refresh_settings_dialog_sections();
        });
        microphone_group->add(pref_row("Connection", retry));
        page->append(microphone_group);
        live_microphone_status_ = nk::Label::create(session_->live_microphone().status());
        live_microphone_status_->set_wrapping(true);
        live_microphone_status_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        page->append(live_microphone_status_);
        if (!devices)
            page->append(SecondaryText::create(devices.error()));

        auto turbo_group = SettingsGroup::create("Turbo");
        auto turbo_speed = nk::SegmentedControl::create();
        turbo_speed->set_segments({"Slow", "Normal", "Fast"});
        const int period = configuration_.input.turbo_period_frames;
        turbo_speed->set_selected_index(period == 6 ? 0 : period == 2 ? 2 : 1);
        (void)turbo_speed->on_selection_changed().connect([this](int index) {
            const int frames = index == 0 ? 6 : index == 2 ? 2 : 4;
            configuration_.input.turbo_period_frames = frames;
            session_->set_turbo_period(frames);
            save_configuration_state();
            set_message("Turbo speed updated.");
        });
        turbo_group->add(pref_row(
            "Speed",
            turbo_speed,
            "Hold Turbo A or Turbo B for repeated presses. Use Slow if a game misses presses."));
        page->append(turbo_group);

        if (gamepad_support) {
            auto controller_group = SettingsGroup::create("Controller");
            controller_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);
            auto enabled_switch = nk::Switch::create();
            enabled_switch->set_active(gamepad_enabled);
            (void)enabled_switch->on_toggled().connect([this](bool active) {
                input_backend_->player_input(settings_input_player_).set_gamepad_enabled(active);
                save_configuration_state();
                set_message(active ? "Gamepad input enabled." : "Gamepad input disabled.");
                refresh_ui();
                refresh_settings_dialog_sections();
            });
            controller_group->add(
                pref_row("Gamepad input", enabled_switch, gamepad_available ? "" : gamepad_help));

            if (gamepad_enabled && gamepad_count > 0) {
                auto index_combo = nk::ComboBox::create();
                auto device_labels =
                    input_backend_->player_input(settings_input_player_).gamepad_device_labels();
                index_combo->set_items(device_labels);
                index_combo->set_selected_index(
                    std::clamp(input_backend_->player_input(settings_input_player_)
                                   .gamepad_config()
                                   .gamepad_index,
                               0,
                               std::max(0, static_cast<int>(device_labels.size()) - 1)));
                // Nothing to choose until a device is actually connected.
                index_combo->set_sensitive(gamepad_enabled);
                (void)index_combo->on_selection_changed().connect([this](int index) {
                    input_backend_->player_input(settings_input_player_).set_gamepad_index(index);
                    save_configuration_state();
                    set_message("Preferred gamepad updated.");
                    refresh_ui();
                    refresh_settings_dialog_sections();
                });
                controller_group->add(pref_row("Preferred device", index_combo));

                auto assign_device = nk::Button::create("Use Selected Gamepad");
                (void)assign_device->on_clicked().connect(
                    [this, weak_combo = std::weak_ptr<nk::ComboBox>(index_combo)] {
                        const auto combo = weak_combo.lock();
                        if (!combo)
                            return;
                        input_backend_->player_input(settings_input_player_)
                            .set_gamepad_index(combo->selected_index());
                        set_message("Selected gamepad assigned. Button bindings retained.");
                        save_configuration_state();
                        refresh_ui();
                        refresh_settings_dialog_sections();
                    });
                auto assignment_row =
                    pref_row("Assignment", assign_device, "Confirm this player's controller.");
                assign_device->ensure_accessible().set_name("Use Selected Gamepad");
                controller_group->add(assignment_row);

                auto deadzone_control = nk::SegmentedControl::create();
                deadzone_control->set_segments(owned_labels(kGamepadDeadzoneLabels));
                deadzone_control->set_selected_index(
                    gamepad_deadzone_index(input_backend_->player_input(settings_input_player_)
                                               .gamepad_config()
                                               .axis_deadzone));
                deadzone_control->set_sensitive(gamepad_enabled);
                (void)deadzone_control->on_selection_changed().connect([this](int index) {
                    input_backend_->player_input(settings_input_player_)
                        .set_gamepad_deadzone(gamepad_deadzone_for_index(index));
                    save_configuration_state();
                    set_message("Gamepad deadzone set to " +
                                std::string(kGamepadDeadzoneLabels[static_cast<std::size_t>(
                                    std::clamp(index, 0, 3))]) +
                                ".");
                    refresh_ui();
                });
                controller_group->add(pref_row("Deadzone", deadzone_control));
            }
            page->append(controller_group);
            if (gamepad_enabled && !gamepad_plugged_in &&
                !input_backend_->player_input(settings_input_player_)
                     .gamepad_config()
                     .identity.guid.empty()) {
                auto assignment_status = nk::Label::create(gamepad_help);
                assignment_status->set_wrapping(true);
                assignment_status->set_horizontal_size_policy(nk::SizePolicy::Expanding);
                page->append(assignment_status);
            }
        }
    } else if (settings_page_ == SettingsPage::Video) {
        input_test_label_.reset();
        page->append(SecondaryText::create("Choose how the picture and the window look."));

        auto display_group = SettingsGroup::create("Display");
        display_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);

        auto scale_combo = nk::ComboBox::create();
        scale_combo->set_items(owned_labels(kPreviewScaleLabels));
        scale_combo->set_selected_index(preview_scale_index(preview_scale_option_));
        (void)scale_combo->on_selection_changed().connect([this](int index) {
            const auto option = preview_scale_for_index(index);
            set_preview_scale_option(option);
            save_configuration_state();
            set_message(preview_scale_description(option));
        });
        display_group->add(pref_row("Scale", scale_combo));

        auto density_combo = nk::ComboBox::create();
        density_combo->set_items(owned_labels(kDensityLabels));
        density_combo->set_selected_index(density_index(app_.theme_selection().density));
        (void)density_combo->on_selection_changed().connect([this](int index) {
            auto selection = app_.theme_selection();
            selection.density = density_for_index(index);
            app_.set_theme_selection(selection);
            save_configuration_state();
            set_message("Interface density updated.");
            app_.event_loop().post([this] { refresh_settings_dialog_sections(); },
                                   "mapperbus.settings-density");
        });
        display_group->add(pref_row("Density", density_combo));

        auto region_combo = nk::ComboBox::create();
        region_combo->set_items({"Automatic", "NTSC", "PAL", "Dendy"});
        const auto region_index = [this] {
            switch (configuration_.frontend.region) {
            case app::RegionPreference::Ntsc:
                return 1;
            case app::RegionPreference::Pal:
                return 2;
            case app::RegionPreference::Dendy:
                return 3;
            case app::RegionPreference::Automatic:
                break;
            }
            return 0;
        }();
        region_combo->set_selected_index(region_index);
        (void)region_combo->on_selection_changed().connect([this](int index) {
            switch (index) {
            case 1:
                configuration_.frontend.region = app::RegionPreference::Ntsc;
                break;
            case 2:
                configuration_.frontend.region = app::RegionPreference::Pal;
                break;
            case 3:
                configuration_.frontend.region = app::RegionPreference::Dendy;
                break;
            case 0:
            default:
                configuration_.frontend.region = app::RegionPreference::Automatic;
                break;
            }
            apply_region_preference();
            save_configuration_state();
            set_message(configuration_.frontend.region == app::RegionPreference::Automatic
                            ? "Region follows the cartridge."
                            : "Region set to " + region_name(actions_->snapshot().region) + ".");
            refresh_ui();
        });
        display_group->add(pref_row("Region", region_combo, "Automatic follows the cartridge."));

        display_group->add(pref_value_row(
            "Platform Theme", platform_family_name(app_.system_preferences().platform_family)));
        display_group->add(pref_value_row("Renderer", video_features_text()));
        page->append(display_group);
    } else {
        input_test_label_.reset();
        page->append(SecondaryText::create("Set the volume and the sound."));

        auto audio_group = SettingsGroup::create("Audio Output");
        audio_group->set_horizontal_size_policy(nk::SizePolicy::Expanding);

        auto mute_switch = nk::Switch::create();
        mute_switch->set_active(audio_backend_->is_muted());
        (void)mute_switch->on_toggled().connect([this](bool active) {
            audio_backend_->set_muted(active);
            save_configuration_state();
            set_message(active ? "Audio muted." : "Audio output restored.");
            refresh_ui();
        });
        audio_group->add(pref_row("Mute", mute_switch));

        auto volume_slider = nk::Slider::create();
        volume_slider->ensure_accessible().set_name("Volume");
        volume_slider->set_range(0.0, 100.0);
        volume_slider->set_step(5.0);
        volume_slider->set_value(static_cast<double>(configuration_.frontend.audio_volume_percent));
        volume_slider->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        (void)volume_slider->on_value_changed().connect([this](double value) {
            const int percent = std::clamp(static_cast<int>(std::lround(value)), 0, 100);
            if (percent == configuration_.frontend.audio_volume_percent) {
                return;
            }
            configuration_.frontend.audio_volume_percent = percent;
            audio_backend_->set_volume(static_cast<float>(percent) / 100.0F);
            save_configuration_state();
        });
        audio_group->add(pref_row("Volume", FixedWidthSlot::create(180.0F, volume_slider)));

        auto sample_rate_combo = nk::ComboBox::create();
        sample_rate_combo->set_items(owned_labels(kAudioSampleRateLabels));
        sample_rate_combo->set_selected_index(
            audio_sample_rate_index(configuration_.audio.sample_rate));
        (void)sample_rate_combo->on_selection_changed().connect([this](int index) {
            configuration_.audio.sample_rate = audio_sample_rate_for_index(index);
            apply_audio_settings_change("Audio sample rate updated.");
        });
        audio_group->add(pref_row("Sample rate", sample_rate_combo));

        auto resampling_control = nk::SegmentedControl::create();
        resampling_control->set_segments(owned_labels(kAudioResamplingLabels));
        resampling_control->set_selected_index(
            audio_resampling_index(configuration_.audio.resampling));
        (void)resampling_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.resampling = audio_resampling_for_index(index);
            apply_audio_settings_change("Audio resampling updated.");
        });
        audio_group->add(pref_row("Resampling", resampling_control));

        auto filter_mode_combo = nk::ComboBox::create();
        filter_mode_combo->set_items(owned_labels(kAudioFilterModeLabels));
        filter_mode_combo->set_selected_index(
            audio_filter_mode_index(configuration_.audio.filter_mode));
        (void)filter_mode_combo->on_selection_changed().connect([this](int index) {
            configuration_.audio.filter_mode = audio_filter_mode_for_index(index);
            apply_audio_settings_change("Audio filter mode updated.");
        });
        audio_group->add(pref_row("Filter", filter_mode_combo));

        auto filter_profile_control = nk::SegmentedControl::create();
        filter_profile_control->set_segments(owned_labels(kAudioFilterProfileLabels));
        filter_profile_control->set_selected_index(
            audio_filter_profile_index(configuration_.audio.filter_profile));
        (void)filter_profile_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.filter_profile = audio_filter_profile_for_index(index);
            apply_audio_settings_change("Audio filter profile updated.");
        });
        audio_group->add(pref_row("Console", filter_profile_control));

        auto stereo_control = nk::SegmentedControl::create();
        stereo_control->set_segments(owned_labels(kAudioStereoLabels));
        stereo_control->set_selected_index(audio_stereo_index(configuration_.audio.stereo_mode));
        (void)stereo_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.stereo_mode = audio_stereo_for_index(index);
            apply_audio_settings_change("Audio channel mode updated.");
        });
        audio_group->add(pref_row("Channels", stereo_control));

        auto dither_switch = nk::Switch::create();
        dither_switch->set_active(configuration_.audio.dithering_enabled);
        (void)dither_switch->on_toggled().connect([this](bool active) {
            configuration_.audio.dithering_enabled = active;
            apply_audio_settings_change(active ? "Audio dithering enabled."
                                               : "Audio dithering disabled.");
        });
        audio_group->add(pref_row("Dithering", dither_switch));

        auto mixing_control = nk::SegmentedControl::create();
        mixing_control->set_segments(owned_labels(kAudioExpansionMixingLabels));
        mixing_control->set_selected_index(
            audio_expansion_mixing_index(configuration_.audio.expansion_mixing));
        (void)mixing_control->on_selection_changed().connect([this](int index) {
            configuration_.audio.expansion_mixing = audio_expansion_mixing_for_index(index);
            apply_audio_settings_change("Expansion audio mixing updated.");
        });
        audio_group->add(pref_row("Extra audio", mixing_control));

        std::string backend_text = std::string(audio_backend_->status_text());
        if (backend_text.starts_with("Audio: ")) {
            backend_text = backend_text.substr(7);
        }
        audio_group->add(pref_value_row("Backend", std::move(backend_text)));
        page->append(audio_group);
    }

    return page;
}

std::shared_ptr<nk::Widget> MapperBusGuiController::build_settings_footer_content() {
    auto container = Box::vertical(6.0F);
    container->set_horizontal_size_policy(nk::SizePolicy::Expanding);

    settings_save_label_ = nk::Label::create(settings_save_status_);
    settings_save_label_->set_wrapping(true);
    settings_save_label_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    container->append(settings_save_label_);
    settings_save_retry_ = nk::Button::create("Retry Settings Save");
    settings_save_retry_->set_visible(configuration_saves_enabled_ &&
                                      !configuration_saver_.error().empty());
    settings_save_retry_->set_sensitive(!configuration_saver_.busy());
    (void)settings_save_retry_->on_clicked().connect([this] { retry_configuration_save(); });
    container->append(settings_save_retry_);

    auto footer = Box::horizontal(12.0F);
    footer->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    if (settings_page_ == SettingsPage::Input) {
        auto restore_keyboard = nk::Button::create("Keyboard Defaults");
        restore_keyboard->add_style_class("flat");
        restore_keyboard->ensure_accessible().set_name("Restore Keyboard Defaults");
        (void)restore_keyboard->on_clicked().connect([this] {
            input_backend_->player_input(settings_input_player_).reset_default_bindings();
            save_configuration_state();
            set_message("Restored default keyboard bindings.");
            refresh_ui();
            refresh_settings_dialog_sections();
        });
        footer->append(restore_keyboard);

        auto restore_gamepad = nk::Button::create("Gamepad Defaults");
        restore_gamepad->add_style_class("flat");
        restore_gamepad->ensure_accessible().set_name("Restore Gamepad Defaults");
        restore_gamepad->set_sensitive(
            input_backend_->player_input(settings_input_player_).gamepad_support_available());
        (void)restore_gamepad->on_clicked().connect([this] {
            input_backend_->player_input(settings_input_player_).reset_default_gamepad_bindings();
            save_configuration_state();
            set_message("Restored default gamepad bindings.");
            refresh_ui();
            refresh_settings_dialog_sections();
        });
        footer->append(restore_gamepad);
    }
    footer->append(Spacer::create());
    auto close_button = nk::Button::create("Close");
    (void)close_button->on_clicked().connect([this] { close_settings(); });
    footer->append(close_button);
    container->append(footer);

    return container;
}

void MapperBusGuiController::refresh_settings_dialog_sections() {
    if (machine_worker_active())
        return;
    if (!settings_dialog_ && !settings_window_) {
        return;
    }
    cancel_settings_bios();

    const auto page_index = [this]() {
        switch (settings_page_) {
        case SettingsPage::Video:
            return 1;
        case SettingsPage::Audio:
            return 2;
        case SettingsPage::General:
            return 3;
        case SettingsPage::Input:
        default:
            return 0;
        }
    };

    if (settings_tabs_) {
        settings_tabs_->set_selected_index(page_index());
    }
    if (settings_window_) {
        const std::array<std::string_view, 4> names{"Input", "Video", "Audio", "General"};
        settings_window_->set_title(std::string(names[page_index()]) + " — Settings");
    }
    if (settings_page_slot_) {
        constexpr float kSettingsPageMaxHeight = 520.0F;
        // Keep the reading position within a page, but start a freshly
        // switched page at the top instead of inheriting the old offset.
        const bool same_page_as_before = settings_built_page_ == settings_page_;
        settings_built_page_ = settings_page_;
        const float previous_h_offset =
            same_page_as_before && settings_scroll_area_ ? settings_scroll_area_->h_offset() : 0.0F;
        const float previous_v_offset =
            same_page_as_before && settings_scroll_area_ ? settings_scroll_area_->v_offset() : 0.0F;
        auto page = build_settings_page_content();
        page->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        page->set_horizontal_stretch(1);
        page->set_vertical_size_policy(nk::SizePolicy::Preferred);
        page->set_vertical_stretch(0);

        // Keep the scrollbar lane outside the content column. The matching
        // bleed below cancels this inset so cards retain equal outer margins.
        auto padded = PaddingSlot::create({0.0F, 24.0F, 4.0F, 0.0F}, std::move(page));
        padded->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        padded->set_vertical_size_policy(nk::SizePolicy::Preferred);
        padded->set_vertical_stretch(0);

        auto scroll = rectangular_scroll_area();
        scroll->set_h_scroll_policy(nk::ScrollPolicy::Never);
        scroll->set_v_scroll_policy(nk::ScrollPolicy::Automatic);
        scroll->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        scroll->set_vertical_size_policy(nk::SizePolicy::Expanding);
        scroll->set_vertical_stretch(1);
        scroll->set_content(std::move(padded));
        settings_scroll_area_ = scroll;
        settings_page_slot_->set_child(BoundedHeightSlot::create(
            kSettingsPageMaxHeight, RightBleedSlot::create(24.0F, scroll)));
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
    if (settings_dialog_ && settings_dialog_->is_presented()) {
        settings_dialog_->queue_layout();
        settings_dialog_->queue_redraw();
    }
}

void MapperBusGuiController::open_settings_dialog() {
    if (app_.system_preferences().platform_family == nk::PlatformFamily::MacOS) {
        if (!settings_window_) {
            settings_window_ = std::make_unique<nk::Window>(nk::WindowConfig{
                .title = "Settings",
                .width = 640,
                .height = 660,
            });
            auto content = build_settings_dialog_shell();
            auto root =
                PaddingSlot::create({.top = 20, .right = 20, .bottom = 20, .left = 20}, content);
            settings_window_->set_child(root);
            auto actions = std::make_shared<nk::ActionGroup>();
            auto close = std::make_shared<nk::Action>("settings.close");
            (void)close->on_activated().connect([this] { close_settings(); });
            actions->add(close);
            settings_window_->set_action_group(actions);
            // Let controls consume Escape (for example, to dismiss a dropdown)
            // before treating it as a request to close the Settings window.
            auto keys = std::make_shared<nk::KeyboardController>();
            (void)keys->on_key_pressed().connect([this](int key, int modifiers) {
                if (static_cast<nk::KeyCode>(key) == nk::KeyCode::Escape &&
                    static_cast<nk::Modifiers>(modifiers) == nk::Modifiers::None) {
                    close_settings();
                }
            });
            root->add_controller(std::move(keys));
            settings_window_->add_shortcut({
                .shortcut = {.key_code = static_cast<int>(nk::KeyCode::W),
                             .modifiers = nk::Modifiers::Super},
                .action_name = "settings.close",
            });
            (void)settings_window_->on_close_requested().connect([this] {
                cancel_settings_bios();
                cancel_gamepad_assignment();
                close_input_monitor();
                if (rebind_dialog_) {
                    cancel_rebind_dialog();
                }
            });
        }
        settings_window_->present();
        settings_tabs_->grab_focus();
        return;
    }
    if (settings_dialog_ && settings_dialog_->is_presented()) {
        refresh_settings_dialog_sections();
        return;
    }

    settings_dialog_ = nk::Dialog::create("Settings");
    settings_dialog_->set_content(FixedWidthSlot::create(600.0F, build_settings_dialog_shell()));
    settings_dialog_->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    settings_dialog_->set_minimum_panel_width(640.0F);
    auto dialog = settings_dialog_;
    (void)settings_dialog_->on_response().connect([this, dialog](nk::DialogResponse /*response*/) {
        if (settings_dialog_ == dialog)
            cancel_settings_bios();
        app_.event_loop().post(
            [this, dialog] {
                // Reopening Settings before this deferred task runs replaces
                // settings_dialog_; a stale task must not tear down the new
                // dialog's widgets.
                if (settings_dialog_ != dialog) {
                    return;
                }
                cancel_gamepad_assignment();
                close_input_monitor();
                settings_tabs_.reset();
                settings_page_slot_.reset();
                settings_footer_slot_.reset();
                settings_scroll_area_.reset();
                input_test_label_.reset();
                settings_save_label_.reset();
                settings_save_retry_.reset();
                settings_dialog_.reset();
                refresh_ui();
                focus_game_surface();
            },
            "mapperbus.settings-dialog-close");
    });
    settings_dialog_->present(window_);
    update_native_toolbar();
}

void MapperBusGuiController::close_settings() {
    cancel_settings_bios();
    if (input_monitor_dialog_) {
        close_input_monitor();
        return;
    }
    if (gamepad_assignment_dialog_) {
        cancel_gamepad_assignment();
        return;
    }
    if (settings_window_) {
        settings_window_->request_close();
    } else if (settings_dialog_) {
        settings_dialog_->close(nk::DialogResponse::Close);
    }
}

nk::Window& MapperBusGuiController::settings_host_window() {
    return settings_window_ && settings_window_->is_visible() ? *settings_window_ : window_;
}

bool MapperBusGuiController::modal_dialog_presented() const {
    return !startup_ready_ || configuration_change_.has_value() || close_intent_.has_value() ||
           loading_request_.has_value() || failed_rom_load_ || settings_bios_selection_ ||
           choosing_disk_export_ || disk_export_.busy() ||
           (disk_export_dialog_ && disk_export_dialog_->is_presented()) ||
           pending_state_operation_.has_value() ||
           (input_monitor_dialog_ && input_monitor_dialog_->is_presented()) ||
           (gamepad_assignment_dialog_ && gamepad_assignment_dialog_->is_presented()) ||
           (settings_dialog_ && settings_dialog_->is_presented()) ||
           (rebind_dialog_ && rebind_dialog_->is_presented()) ||
           (state_slot_dialog_ && state_slot_dialog_->is_presented()) ||
           (archive_dialog_ && archive_dialog_->is_presented()) ||
           (fds_dialog_ && fds_dialog_->is_presented());
}

void MapperBusGuiController::handle_menu_action(std::string_view action) {
    if (!startup_ready_ && action != "file.quit")
        return;
    if ((configuration_change_ || close_intent_ || loading_request_ || failed_rom_load_ ||
         settings_bios_selection_ || pending_state_operation_ || choosing_disk_export_ ||
         disk_export_.busy() || (disk_export_dialog_ && disk_export_dialog_->is_presented())) &&
        action != "file.quit")
        return;
    if ((gamepad_assignment_dialog_ || input_monitor_dialog_) && action != "file.quit")
        return;
    if (((archive_dialog_ && archive_dialog_->is_presented()) ||
         (fds_dialog_ && fds_dialog_->is_presented())) &&
        action != "file.quit") {
        return;
    }
    if (state_slot_dialog_ && state_slot_dialog_->is_presented() &&
        (action == "file.open" || action == "file.close" || action == "settings.open")) {
        return;
    }
    // The menu and chooser edit the same selection. Allow that action while
    // the chooser is open, but keep emulation commands and other dialogs guarded.
    const bool choosing_slot =
        action.starts_with("emu.slot-") && state_slot_dialog_ && state_slot_dialog_->is_presented();
    const bool other_modal = (settings_dialog_ && settings_dialog_->is_presented()) ||
                             (rebind_dialog_ && rebind_dialog_->is_presented());
    if (action.starts_with("emu.") && modal_dialog_presented() && (!choosing_slot || other_modal)) {
        return;
    }
    if (action == "fds.controls") {
        open_disk_controls();
        return;
    }
    if (action.starts_with("fds.")) {
        perform_disk_action(action);
        return;
    }
    if (action == "file.open") {
        browse_for_rom();
        return;
    }
    if (action == "file.close") {
        close_current_rom();
        return;
    }
    if (action == "file.quit") {
        request_session_close(true);
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
    if (action == "emu.open-states-folder") {
        open_states_folder();
        return;
    }
    if (action == "emu.choose-slot") {
        choose_state_slot();
        return;
    }
    for (int slot = 0; slot < app::kSaveStateSlotCount; ++slot) {
        if (action == "emu.slot-" + std::to_string(slot)) {
            select_state_slot(slot);
            set_message("Selected state slot " + std::to_string(slot + 1) +
                        ". F5 saves; F7 loads.");
            if (!choosing_slot) {
                focus_game_surface();
            }
            return;
        }
    }
    if (action == "emu.save-state") {
        save_session_state(active_state_slot_);
        return;
    }
    if (action == "emu.load-state") {
        load_session_state(active_state_slot_);
        return;
    }
    if (action == "emu.save-state-1") {
        save_session_state(1);
        return;
    }
    if (action == "emu.load-state-1") {
        load_session_state(1);
        return;
    }
    if (action == "win.toggle-fullscreen") {
        toggle_fullscreen();
        return;
    }
    if (action == "settings.open") {
        open_settings_dialog();
        return;
    }
    if (action == "help.about") {
        auto dialog =
            nk::Dialog::create("About MapperBus",
                               "Version " MAPPERBUS_VERSION
                               " · © 2026 Aleksandr Pavlov. A NES, Famicom, and FDS emulator.");
        dialog->add_button("OK", nk::DialogResponse::Accept);
        (void)dialog->on_response().connect(
            [this](nk::DialogResponse /*response*/) { focus_game_surface(); });
        dialog->present(window_);
    }
}

std::string MapperBusGuiController::input_status_text() const {
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        const auto& input = input_backend_->player_input(player);
        if (!input.uses_default_bindings() || !input.uses_default_gamepad_bindings()) {
            return "Input custom";
        }
    }
    return "Input default";
}

std::string MapperBusGuiController::input_test_status_text() const {
    std::vector<std::string> pressed;
    for (const auto button : kBindingOrder) {
        if (input_backend_->is_action_pressed(settings_input_player_, button) ||
            (settings_window_ && settings_window_->is_visible() && settings_window_->is_focused() &&
             settings_window_->is_key_pressed(
                 input_backend_->player_input(settings_input_player_).binding(button)))) {
            pressed.push_back(button_name(button));
        }
    }

    if (pressed.empty()) {
        return "Click a binding to change it. Hold a key to test.";
    }

    std::string text = "Holding: ";
    for (std::size_t index = 0; index < pressed.size(); ++index) {
        if (index > 0) {
            text += " + ";
        }
        text += pressed[index];
    }
    return text;
}

std::string MapperBusGuiController::video_features_text() const {
    return renderer_backend_label(window_.renderer_backend());
}

} // namespace mapperbus::frontend
