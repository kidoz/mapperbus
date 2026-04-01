#include "frontends/nodalkit/gui_controller.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <nk/widgets/label.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mappers/mapper_registry.hpp"
#include "platform/video/null_video.hpp"

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

    tick_handle_ = app_.event_loop().set_interval(std::chrono::milliseconds(16), [this] {
        const auto result = actions_->tick();
        if (result == app::TickResult::FrameAdvanced) {
            refresh_preview();
        }

        if (result == app::TickResult::FrameAdvanced || result == app::TickResult::Paused ||
            result == app::TickResult::NoCartridge) {
            refresh_ui();
        }
    });
}

void MapperBusGuiController::open_initial_rom(std::string_view rom_path) {
    if (!rom_path.empty()) {
        attempt_open(std::string(rom_path));
    }
}

void MapperBusGuiController::build_ui() {
    root_ = Box::vertical(10.0F);
    root_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    root_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    root_->set_vertical_stretch(1);

    menu_bar_ = nk::MenuBar::create();
    menu_bar_->add_menu({
        "File",
        {
            nk::MenuItem::action("Open ROM...", "file.open"),
            nk::MenuItem::action("Close ROM", "file.close"),
            nk::MenuItem::make_separator(),
            nk::MenuItem::action("Quit", "file.quit"),
        },
    });
    menu_bar_->add_menu({
        "Emulation",
        {
            nk::MenuItem::action("Pause / Resume", "emu.pause"),
            nk::MenuItem::action("Step Frame", "emu.step"),
            nk::MenuItem::action("Reset", "emu.reset"),
            nk::MenuItem::action("Power Cycle", "emu.power"),
        },
    });
    menu_bar_->add_menu({
        "Input",
        {
            nk::MenuItem::action("Configure...", "input.configure"),
        },
    });
    menu_bar_->add_menu({
        "Help",
        {
            nk::MenuItem::action("About mapperbus GUI", "help.about"),
        },
    });

    preview_ = PreviewCanvas::create();
    preview_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    preview_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    preview_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    preview_->set_horizontal_stretch(1);
    preview_->set_vertical_stretch(1);

    auto preview_stage = InsetStage::create(preview_, 640.0F, 820.0F, 8.0F);
    preview_stage->set_horizontal_stretch(1);
    preview_stage->set_vertical_stretch(1);

    auto body = Box::vertical(8.0F);
    body->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    body->set_vertical_size_policy(nk::SizePolicy::Expanding);
    body->set_vertical_stretch(1);
    body->append(preview_stage);

    status_bar_ = nk::StatusBar::create();
    status_bar_->set_segments({"Stopped", "NTSC", "Ready"});

    root_->append(menu_bar_);
    root_->append(body);
    root_->append(status_bar_);

    window_.set_child(root_);
}

void MapperBusGuiController::wire_ui() {
    (void)window_.on_close_request().connect([this] { app_.quit(0); });
    (void)menu_bar_->on_action().connect(
        [this](std::string_view action) { handle_menu_action(action); });
}

void MapperBusGuiController::clear_preview() {
    preview_->update_pixel_buffer(
        blank_frame_.pixels.data(), mapperbus::core::kScreenWidth, mapperbus::core::kScreenHeight);
}

void MapperBusGuiController::refresh_preview() {
    const auto& frame = session_->emulator().frame_buffer();
    preview_->update_pixel_buffer(
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

void MapperBusGuiController::browse_for_rom() {
    auto dialog_result = app_.open_file_dialog("Open ROM", {"*.nes", "*.fds"});
    if (dialog_result) {
        attempt_open(*dialog_result);
        return;
    }

    if (dialog_result.error() != nk::FileDialogError::Cancelled) {
        set_message(file_dialog_error_text(dialog_result.error()));
    }
}

void MapperBusGuiController::attempt_open(std::string rom_path) {
    if (rom_path.empty()) {
        set_message("Open a ROM to start playback.");
        return;
    }

    auto result = actions_->open_rom(rom_path);
    if (!result) {
        set_message("Failed to open ROM: " + result.error());
        refresh_ui();
        clear_preview();
        return;
    }

    actions_->step_frame();
    refresh_preview();
    set_message("Loaded " + basename_for_display(rom_path) + ". " + gameplay_hint_text());
    refresh_ui();
}

void MapperBusGuiController::toggle_pause() {
    actions_->toggle_pause();
    refresh_ui();
}

void MapperBusGuiController::step_frame() {
    if (!actions_->snapshot().has_cartridge) {
        return;
    }

    const auto result = actions_->step_frame();
    refresh_preview();
    set_message("Stepped one frame: " + tick_result_name(result) + ".");
    refresh_ui();
}

void MapperBusGuiController::reset_session() {
    actions_->reset();
    refresh_preview();
    set_message("Reset current ROM.");
    refresh_ui();
}

void MapperBusGuiController::power_cycle_session() {
    actions_->power_cycle();
    refresh_preview();
    set_message("Power-cycled current ROM.");
    refresh_ui();
}

void MapperBusGuiController::close_current_rom() {
    actions_->close_rom();
    clear_preview();
    set_message("Closed current ROM.");
    refresh_ui();
}

void MapperBusGuiController::open_input_dialog() {
    if (input_dialog_ && input_dialog_->is_presented()) {
        return;
    }

    auto content = Box::vertical(10.0F);
    auto intro = nk::Label::create("Remap player 1 keyboard controls. Changes apply immediately.");
    content->append(intro);

    auto labels = key_binding_labels();
    for (const auto button : kBindingOrder) {
        auto row = Box::horizontal(8.0F);
        auto name = nk::Label::create(button_name(button));
        auto combo = nk::ComboBox::create();
        combo->set_items(labels);
        combo->set_selected_index(combo_index_for_key(input_backend_->binding(button)));
        (void)combo->on_selection_changed().connect([this, button](int index) {
            input_backend_->set_binding(button, key_for_combo_index(index));
            set_message(button_name(button) + " mapped to " +
                        key_label(input_backend_->binding(button)) + ".");
            refresh_ui();
        });
        row->append(name);
        row->append(combo);
        row->append(Spacer::create());
        content->append(row);
    }

    auto reset_defaults = nk::Button::create("Restore Defaults");
    (void)reset_defaults->on_clicked().connect([this] {
        input_backend_->reset_default_bindings();
        if (input_dialog_) {
            input_dialog_->close(nk::DialogResponse::Custom);
        }
        set_message("Restored default keyboard bindings. " + gameplay_hint_text());
        refresh_ui();
        open_input_dialog();
    });
    content->append(reset_defaults);

    input_dialog_ = nk::Dialog::create("Input Bindings");
    input_dialog_->set_content(content);
    input_dialog_->add_button("Close", nk::DialogResponse::Close);
    (void)input_dialog_->on_response().connect([this](nk::DialogResponse /*response*/) {
        input_dialog_.reset();
        refresh_ui();
    });
    input_dialog_->present(window_);
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
    if (action == "input.configure") {
        open_input_dialog();
        return;
    }
    if (action == "help.about") {
        auto dialog =
            nk::Dialog::create("About mapperbus GUI",
                               "mapperbus GUI\nNodalKit host for NES, Famicom, and FDS sessions");
        dialog->add_button("OK", nk::DialogResponse::Accept);
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

} // namespace mapperbus::frontend
