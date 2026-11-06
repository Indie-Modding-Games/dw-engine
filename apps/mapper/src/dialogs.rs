//! The windows over the editor: the unsaved-changes question, the new mod pack, the shortcuts
//! help, and the panel with what a save found.

use std::path::PathBuf;

use eframe::egui;
use mapper_core::{Action, PendingChoice, text};

use crate::app::MapperApp;

/// The "new mod pack" form.
#[derive(Clone, Debug, Default)]
pub struct NewPack {
    pub folder: String,
    pub id: String,
}

impl NewPack {
    /// The form for the content open: its first mods folder, if it has one.
    pub fn for_app(app: &MapperApp) -> Self {
        let folder = app
            .mapper
            .content
            .as_ref()
            .and_then(|content| content.folders.mods.first())
            .map(|folder| folder.display().to_string())
            .unwrap_or_default();
        Self {
            folder,
            id: String::new(),
        }
    }
}

pub fn show(app: &mut MapperApp, ctx: &egui::Context) {
    unsaved(app, ctx);
    new_pack(app, ctx);
    shortcuts(app, ctx);
}

/// Asks what to do with the changes before an action that would lose them.
fn unsaved(app: &mut MapperApp, ctx: &egui::Context) {
    if app.mapper.pending.is_none() {
        return;
    }
    let mut choice = None;
    let modal = egui::Modal::new(egui::Id::unique("unsaved")).show(ctx, |ui| {
        ui.heading(text::UNSAVED_TITLE);
        ui.label(text::UNSAVED_QUESTION);
        if let Some(target) = app.mapper.current_target() {
            ui.weak(format!("{}: {}", text::TARGET, target.id));
        }
        ui.horizontal(|ui| {
            let can_save = app.mapper.current_target().is_some();
            if ui
                .add_enabled(can_save, egui::Button::new(text::UNSAVED_SAVE))
                .clicked()
            {
                choice = Some(PendingChoice::Save);
            }
            if ui.button(text::UNSAVED_DISCARD).clicked() {
                choice = Some(PendingChoice::Discard);
            }
            if ui.button(text::UNSAVED_CANCEL).clicked() {
                choice = Some(PendingChoice::Cancel);
            }
        });
    });
    if modal.should_close() && choice.is_none() {
        choice = Some(PendingChoice::Cancel);
    }
    if let Some(choice) = choice {
        app.apply(Action::Resolve(choice));
    }
}

fn new_pack(app: &mut MapperApp, ctx: &egui::Context) {
    let Some(form) = &mut app.new_pack else {
        return;
    };
    let mut open = true;
    let mut create = None;
    egui::Window::new(text::NEW_PACK)
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .show(ctx, |ui| {
            egui::Grid::new("new_pack").num_columns(2).show(ui, |ui| {
                ui.label(text::NEW_PACK_FOLDER);
                ui.horizontal(|ui| {
                    ui.text_edit_singleline(&mut form.folder);
                    if ui.button(text::PICK_FOLDER).clicked()
                        && let Some(folder) = rfd::FileDialog::new().pick_folder()
                    {
                        form.folder = folder.display().to_string();
                    }
                });
                ui.end_row();
                ui.label(text::NEW_PACK_ID);
                ui.text_edit_singleline(&mut form.id);
                ui.end_row();
            });
            let ready =
                !form.folder.trim().is_empty() && mapper_core::targets::is_pack_id(form.id.trim());
            if ui
                .add_enabled(ready, egui::Button::new(text::NEW_PACK_CREATE))
                .clicked()
            {
                create = Some((PathBuf::from(form.folder.trim()), form.id.trim().to_owned()));
            }
        });
    if let Some((folder, id)) = create {
        let root = folder.join(&id);
        app.apply(Action::CreateModPack { folder, id });
        if app
            .mapper
            .current_target()
            .is_some_and(|target| target.root == root)
        {
            open = false;
        }
    }
    if !open {
        app.new_pack = None;
    }
}

fn shortcuts(app: &mut MapperApp, ctx: &egui::Context) {
    let mut open = app.show_shortcuts;
    egui::Window::new(text::SHORTCUTS)
        .open(&mut open)
        .collapsible(false)
        .show(ctx, |ui| {
            egui::Grid::new("shortcuts").striped(true).show(ui, |ui| {
                for (keys, what) in text::SHORTCUT_LIST {
                    ui.monospace(keys);
                    ui.label(what);
                    ui.end_row();
                }
            });
        });
    app.show_shortcuts = open;
}

/// What the last save found (errors stop it; warnings do not).
pub fn problems(app: &mut MapperApp, ui: &mut egui::Ui) {
    if app.mapper.save_problems.is_empty() {
        return;
    }
    egui::Panel::bottom("problems").show(ui, |ui| {
        ui.strong(text::SAVE_PROBLEMS);
        for problem in &app.mapper.save_problems {
            let line = text::problem(problem);
            if problem.is_error() {
                ui.colored_label(egui::Color32::LIGHT_RED, line);
            } else {
                ui.colored_label(egui::Color32::YELLOW, line);
            }
        }
    });
}
