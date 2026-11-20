#include <nk/widgets/label.h>
#include <nk/widgets/spinner.h>

#include "frontends/nodalkit/gui_controller.hpp"

namespace mapperbus::frontend {

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
        return;
    }
    finish_close_rom();
}

void MapperBusGuiController::finish_close_rom() {
    // The checked worker already saved all data; retirement must do no I/O.
    session_->emulator().set_persistence_enabled(false);
    auto closed = actions_->close_rom();
    session_->emulator().set_persistence_enabled(true);
    if (!closed) {
        show_close_error(closed.error());
        return;
    }
    close_intent_.reset();
    close_ready_ = false;
    close_captured_ = false;
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

} // namespace mapperbus::frontend
