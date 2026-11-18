//! The windows for the maps a pack adds: "New map" (an id, a name, an area, a template and an
//! exit to connect it by), and renaming or deleting one the target pack added.

use std::path::PathBuf;

use dwe_map::new_map::{self, Connection, NewMap, Template};
use eframe::egui;
use mapper_core::{Action, text};

use crate::app::MapperApp;

/// The "new map" form.
#[derive(Clone, Debug, PartialEq)]
pub struct NewMapForm {
    pub name: String,
    pub title: String,
    pub area: i32,
    pub copy: bool,
    pub from: String,
    pub copy_images: bool,
    pub keep_exits: bool,
    pub width: i32,
    pub height: i32,
    pub image: Option<PathBuf>,
    pub connect: bool,
    pub connect_map: String,
    pub connect_exit: i32,
    pub own_exit: i32,
}

impl NewMapForm {
    /// The form for the map shown: a copy of it, in its area, connected to it.
    pub fn for_app(app: &MapperApp) -> Self {
        let shown = app.mapper.map.as_ref();
        let id = shown.map(|map| map.document.id.clone()).unwrap_or_default();
        Self {
            name: String::new(),
            title: String::new(),
            area: shown
                .and_then(|map| map.entry.as_ref())
                .map_or(0, |entry| entry.loading_name),
            copy: shown.is_some(),
            from: id.clone(),
            copy_images: false,
            keep_exits: false,
            width: 5,
            height: 4,
            image: None,
            connect: shown.is_some(),
            connect_map: id,
            connect_exit: 0,
            own_exit: 0,
        }
    }

    pub fn new_map(&self) -> NewMap {
        NewMap {
            name: self.name.trim().to_owned(),
            title: self.title.trim().to_owned(),
            loading_name: self.area,
            template: if self.copy {
                Template::Copy {
                    from: self.from.clone(),
                    copy_images: self.copy_images,
                    keep_exits: self.keep_exits,
                }
            } else {
                Template::Blank {
                    width: self.width,
                    height: self.height,
                    image: self.image.clone(),
                }
            },
            connect: self.connect.then(|| Connection {
                map: self.connect_map.clone(),
                exit: self.connect_exit,
                own_exit: self.own_exit,
            }),
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct MapForms {
    pub new: Option<NewMapForm>,
    /// The map being renamed and the name typed.
    pub rename: Option<(String, String)>,
    /// The map asked to be deleted.
    pub delete: Option<String>,
}

pub fn show(app: &mut MapperApp, ctx: &egui::Context) {
    new_map_window(app, ctx);
    rename_window(app, ctx);
    delete_window(app, ctx);
}

/// A combo of the table's maps with data, by id.
fn map_combo(app: &MapperApp, ui: &mut egui::Ui, salt: &str, chosen: &mut String) {
    let Some(content) = &app.mapper.content else {
        return;
    };
    egui::ComboBox::from_id_salt(salt)
        .selected_text(chosen.clone())
        .height(360.0)
        .show_ui(ui, |ui| {
            for entry in content.table.maps.iter().filter(|entry| entry.has_file()) {
                ui.selectable_value(chosen, entry.id.clone(), &entry.id);
            }
        });
}

fn new_map_window(app: &mut MapperApp, ctx: &egui::Context) {
    let Some(mut form) = app.map_forms.new.clone() else {
        return;
    };
    let target = app.mapper.current_target();
    let mut open = true;
    let mut create = false;
    let mut cancel = false;
    egui::Window::new(text::new_map_title())
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .default_width(460.0)
        .show(ctx, |ui| {
            let Some(target) = &target else {
                ui.label(text::new_map_needs_target());
                return;
            };
            let Some(content) = &app.mapper.content else {
                return;
            };
            egui::Grid::new("new_map").num_columns(2).show(ui, |ui| {
                ui.label(text::new_map_name());
                ui.horizontal(|ui| {
                    ui.monospace(format!("{}:", target.id));
                    ui.add(
                        egui::TextEdit::singleline(&mut form.name)
                            .hint_text(text::new_map_name_hint())
                            .desired_width(180.0),
                    );
                });
                ui.end_row();
                ui.label(text::new_map_title_field());
                ui.add(
                    egui::TextEdit::singleline(&mut form.title)
                        .hint_text(text::new_map_title_hint())
                        .desired_width(240.0),
                );
                ui.end_row();
                ui.label(text::new_map_area());
                let names = &content.table.map_names;
                let shown = names
                    .get(usize::try_from(form.area).unwrap_or(0))
                    .map_or("", |name| name.trim());
                egui::ComboBox::from_id_salt("new_map_area")
                    .selected_text(format!("{} {shown}", form.area))
                    .height(360.0)
                    .show_ui(ui, |ui| {
                        for (index, name) in names.iter().enumerate() {
                            let index = index as i32;
                            ui.selectable_value(
                                &mut form.area,
                                index,
                                format!("{index} {}", name.trim()),
                            );
                        }
                    });
                ui.end_row();
                ui.label(text::new_map_template());
                ui.horizontal(|ui| {
                    ui.radio_value(&mut form.copy, true, text::new_map_copy());
                    ui.radio_value(&mut form.copy, false, text::new_map_blank());
                });
                ui.end_row();
            });
            ui.separator();
            if form.copy {
                ui.horizontal(|ui| {
                    ui.label(text::new_map_copy());
                    map_combo(app, ui, "new_map_from", &mut form.from);
                });
                ui.checkbox(&mut form.copy_images, text::new_map_copy_images());
                ui.checkbox(&mut form.keep_exits, text::new_map_keep_exits());
            } else {
                ui.horizontal(|ui| {
                    ui.label(text::new_map_size());
                    let fixed = form.image.is_some();
                    ui.add_enabled(
                        !fixed,
                        egui::DragValue::new(&mut form.width).range(1..=new_map::MOST_TILES),
                    );
                    ui.label("×");
                    ui.add_enabled(
                        !fixed,
                        egui::DragValue::new(&mut form.height).range(1..=new_map::MOST_TILES),
                    );
                });
                ui.horizontal(|ui| {
                    ui.label(text::new_map_image());
                    let shown = form.image.as_ref().map_or_else(
                        || text::new_map_image_none().to_owned(),
                        |file| file.display().to_string(),
                    );
                    ui.weak(shown);
                    if ui.button(text::new_map_pick_image()).clicked()
                        && let Some(file) = rfd::FileDialog::new()
                            .add_filter("PNG", &["png"])
                            .pick_file()
                    {
                        form.image = Some(file);
                    }
                    if form.image.is_some() && ui.button(text::dwx_clear()).clicked() {
                        form.image = None;
                    }
                });
            }
            ui.separator();
            ui.checkbox(&mut form.connect, text::new_map_connect());
            ui.add_enabled_ui(form.connect, |ui| {
                ui.horizontal(|ui| {
                    map_combo(app, ui, "new_map_connect", &mut form.connect_map);
                    ui.label(text::new_map_connect_exit());
                    ui.add(egui::DragValue::new(&mut form.connect_exit).range(0..=9));
                    ui.label(text::new_map_own_exit());
                    ui.add(egui::DragValue::new(&mut form.own_exit).range(0..=9));
                });
            });
            ui.separator();
            let name = form.name.trim();
            let id = new_map::added_map_id(&target.id, name);
            let problem = if !new_map::is_map_name(name) {
                Some(text::new_map_bad_name())
            } else if content.table.find(&id).is_some() {
                Some(text::new_map_id_taken())
            } else {
                None
            };
            if let Some(problem) = problem
                && !name.is_empty()
            {
                ui.colored_label(egui::Color32::from_rgb(235, 90, 70), problem);
            }
            ui.horizontal(|ui| {
                let ready = problem.is_none() && !form.title.trim().is_empty();
                if ui
                    .add_enabled(ready, egui::Button::new(text::new_map_create()))
                    .clicked()
                {
                    create = true;
                }
                if ui.button(text::unsaved_cancel()).clicked() {
                    cancel = true;
                }
            });
        });
    if create {
        app.map_forms.new = None;
        app.world_ui.shown = false;
        app.apply(Action::NewMap(form.new_map()));
    } else if cancel || !open {
        app.map_forms.new = None;
    } else {
        app.map_forms.new = Some(form);
    }
}

fn rename_window(app: &mut MapperApp, ctx: &egui::Context) {
    let Some((id, mut name)) = app.map_forms.rename.clone() else {
        return;
    };
    let mut open = true;
    let mut done = None;
    egui::Window::new(text::rename_map_title())
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .show(ctx, |ui| {
            ui.monospace(&id);
            let pack = id.split_once(':').map_or("", |(pack, _)| pack);
            ui.horizontal(|ui| {
                ui.monospace(format!("{pack}:"));
                ui.add(egui::TextEdit::singleline(&mut name).hint_text(text::new_map_name_hint()));
            });
            let valid = new_map::is_map_name(name.trim());
            if !valid && !name.is_empty() {
                ui.colored_label(
                    egui::Color32::from_rgb(235, 90, 70),
                    text::new_map_bad_name(),
                );
            }
            ui.horizontal(|ui| {
                if ui
                    .add_enabled(valid, egui::Button::new(text::rename_map_title()))
                    .clicked()
                {
                    done = Some(true);
                }
                if ui.button(text::unsaved_cancel()).clicked() {
                    done = Some(false);
                }
            });
        });
    match (done, open) {
        (Some(true), _) => {
            app.map_forms.rename = None;
            app.apply(Action::RenameMap {
                id,
                name: name.trim().to_owned(),
            });
        }
        (Some(false), _) | (None, false) => app.map_forms.rename = None,
        (None, true) => app.map_forms.rename = Some((id, name)),
    }
}

fn delete_window(app: &mut MapperApp, ctx: &egui::Context) {
    let Some(id) = app.map_forms.delete.clone() else {
        return;
    };
    let mut done = None;
    let modal = egui::Modal::new(egui::Id::unique("delete_map")).show(ctx, |ui| {
        ui.heading(text::delete_map_title());
        ui.monospace(&id);
        ui.label(text::delete_map_question());
        ui.horizontal(|ui| {
            if ui.button(text::delete_map_title()).clicked() {
                done = Some(true);
            }
            if ui.button(text::unsaved_cancel()).clicked() {
                done = Some(false);
            }
        });
    });
    if modal.should_close() && done.is_none() {
        done = Some(false);
    }
    if let Some(delete) = done {
        app.map_forms.delete = None;
        if delete {
            app.apply(Action::DeleteMap(id));
        }
    }
}

/// The "Mapas" menu: new map, and rename or delete one the target pack added.
pub fn menu(app: &mut MapperApp, ui: &mut egui::Ui) {
    ui.menu_button(text::menu_maps(), |ui| {
        let has_content = app.mapper.content.is_some();
        if ui
            .add_enabled(has_content, egui::Button::new(text::new_map()))
            .clicked()
        {
            ui.close();
            app.map_forms.new = Some(NewMapForm::for_app(app));
        }
        let own = app.mapper.own_maps();
        if own.is_empty() {
            ui.weak(text::no_own_maps());
        }
        ui.add_enabled_ui(!own.is_empty(), |ui| {
            ui.menu_button(text::rename_map(), |ui| {
                for id in &own {
                    if ui.button(id).clicked() {
                        ui.close();
                        let name = id.split_once(':').map_or("", |(_, name)| name);
                        app.map_forms.rename = Some((id.clone(), name.to_owned()));
                    }
                }
            });
            ui.menu_button(text::delete_map(), |ui| {
                for id in &own {
                    if ui.button(id).clicked() {
                        ui.close();
                        app.map_forms.delete = Some(id.clone());
                    }
                }
            });
        });
    });
}
