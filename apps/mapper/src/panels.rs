//! The panels around the canvas: the menus and layer bar, the map browser, the inspector and
//! details of the map shown, the live link and the status bar.

use dwe_map::CellKind;
use dwe_map::save::SaveMode;
use dwe_pack::GameFolders;
use eframe::egui;
use mapper_core::{Action, GroupBy, Layer, Message, TargetKind, text};
use mapper_live::StoryState;

use crate::app::MapperApp;
use crate::dialogs::NewPack;

pub fn top_bar(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::top("top").show(ui, |ui| {
        ui.horizontal_wrapped(|ui| {
            ui.menu_button(text::MENU_FILE, |ui| {
                if ui.button(text::OPEN_CONTENT).clicked() {
                    ui.close();
                    if let Some(folder) = rfd::FileDialog::new().pick_folder() {
                        let team_packs = crate::options::find_team_packs(&folder);
                        let mods = app
                            .mapper
                            .content
                            .as_ref()
                            .map(|content| content.folders.mods.clone())
                            .unwrap_or_default();
                        app.apply(Action::OpenContent(GameFolders {
                            content: folder,
                            team_packs,
                            mods,
                        }));
                    }
                }
                let has_content = app.mapper.content.is_some();
                if ui
                    .add_enabled(has_content, egui::Button::new(text::RELOAD_CONTENT))
                    .clicked()
                {
                    ui.close();
                    app.apply(Action::ReloadContent);
                }
                ui.separator();
                target_menu(app, ui);
                let has_map = app.mapper.map.is_some();
                let save = egui::Button::new(text::SAVE_PATCH).shortcut_text("Ctrl+S");
                if ui.add_enabled(has_map, save).clicked() {
                    ui.close();
                    app.apply(Action::Save(SaveMode::Patch));
                }
                if ui
                    .add_enabled(has_map, egui::Button::new(text::SAVE_WHOLE))
                    .clicked()
                {
                    ui.close();
                    app.apply(Action::Save(SaveMode::WholeData));
                }
                ui.separator();
                if ui.button(text::QUIT).clicked() {
                    ui.close();
                    app.apply(Action::Quit);
                }
            });
            ui.menu_button(text::MENU_EDIT, |ui| {
                let (undo, redo) = app.mapper.map.as_ref().map_or((false, false), |map| {
                    (map.history.can_undo(), map.history.can_redo())
                });
                let button = egui::Button::new(text::UNDO).shortcut_text("Ctrl+Z");
                if ui.add_enabled(undo, button).clicked() {
                    app.apply(Action::Undo);
                }
                let button = egui::Button::new(text::REDO).shortcut_text("Ctrl+Y");
                if ui.add_enabled(redo, button).clicked() {
                    app.apply(Action::Redo);
                }
                ui.separator();
                if ui.button(text::SHORTCUTS).clicked() {
                    ui.close();
                    app.show_shortcuts = true;
                }
            });
            ui.separator();
            let back = !app.mapper.history.is_empty();
            if ui
                .add_enabled(back, egui::Button::new(text::BACK))
                .clicked()
            {
                app.apply(Action::Back);
            }
            if ui.button(text::FIT_VIEW).clicked() {
                app.apply(Action::FitView);
            }
            ui.separator();
            ui.label(text::LAYERS);
            for layer in Layer::ALL {
                let mut shown = app.mapper.layers.shows(layer);
                if ui.checkbox(&mut shown, layer.label()).changed() {
                    app.apply(Action::SetLayer(layer, shown));
                }
            }
            ui.separator();
            ui.label(text::TIME_OF_DAY);
            let variants: Vec<String> = app
                .mapper
                .map
                .as_ref()
                .map(|map| map.backgrounds.iter().map(|b| b.variant.clone()).collect())
                .unwrap_or_default();
            for variant in variants {
                let chosen = app.mapper.variant == variant;
                if ui
                    .selectable_label(chosen, text::variant_name(&variant))
                    .clicked()
                {
                    app.apply(Action::SetVariant(variant));
                }
            }
        });
    });
}

/// The pack saves go to: the writable packs of the stack, and a new mod pack.
fn target_menu(app: &mut MapperApp, ui: &mut egui::Ui) {
    ui.menu_button(text::TARGET, |ui| {
        let current = app.mapper.current_target().map(|target| target.root);
        let targets = app.mapper.targets();
        if targets.is_empty() {
            ui.weak(text::NO_TARGETS);
        }
        for target in targets {
            let chosen = current.as_ref() == Some(&target.root);
            let label = format!("{} ({})", target.id, target_kind(target.kind));
            if ui
                .selectable_label(chosen, label)
                .on_hover_text(target.root.display().to_string())
                .clicked()
            {
                ui.close();
                app.apply(Action::SetTarget(Some(target.root)));
            }
        }
        ui.separator();
        if ui
            .add_enabled(
                app.mapper.content.is_some(),
                egui::Button::new(text::NEW_PACK),
            )
            .clicked()
        {
            ui.close();
            app.new_pack = Some(NewPack::for_app(app));
        }
    });
}

fn target_kind(kind: TargetKind) -> &'static str {
    match kind {
        TargetKind::Mod => text::TARGET_MOD,
        TargetKind::Team => text::TARGET_TEAM,
    }
}

pub fn status_bar(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::bottom("status").show(ui, |ui| {
        ui.horizontal(|ui| {
            if let Some(map) = &app.mapper.map {
                if map.is_dirty() {
                    ui.colored_label(
                        egui::Color32::YELLOW,
                        format!("* {} ({})", map.document.id, text::DIRTY),
                    );
                } else {
                    ui.label(&map.document.id);
                }
                ui.separator();
            }
            let target = app.mapper.current_target().map_or_else(
                || "—".to_owned(),
                |target| format!("{} ({})", target.id, target_kind(target.kind)),
            );
            ui.label(format!("{}: {target}", text::TARGET));
            ui.separator();
            message_line(app, ui);
        });
    });
}

/// The last message, or the content and its packs.
fn message_line(app: &MapperApp, ui: &mut egui::Ui) {
    match app.mapper.messages.last() {
        Some(Message::Error(message)) => {
            ui.colored_label(egui::Color32::LIGHT_RED, message);
        }
        Some(Message::Info(message)) => {
            ui.label(message);
        }
        None => {
            if let Some(content) = &app.mapper.content {
                let packs: Vec<_> = content
                    .vfs()
                    .packs()
                    .iter()
                    .map(|pack| pack.id().to_owned())
                    .collect();
                ui.label(format!(
                    "{} · {}",
                    content.folders.content.display(),
                    packs.join(" < ")
                ));
            }
        }
    }
}

pub fn browser(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::left("browser")
        .resizable(true)
        .default_size(230.0)
        .show(ui, |ui| {
            ui.heading(text::MAPS);
            let mut filter = app.mapper.filter.clone();
            if ui
                .add(egui::TextEdit::singleline(&mut filter).hint_text(text::FILTER_HINT))
                .changed()
            {
                app.apply(Action::SetFilter(filter));
            }
            ui.horizontal(|ui| {
                ui.label(text::GROUP_BY);
                for (group_by, label) in [
                    (GroupBy::LoadingName, text::GROUP_BY_NAME),
                    (GroupBy::Prefix, text::GROUP_BY_PREFIX),
                ] {
                    if ui
                        .selectable_label(app.mapper.group_by == group_by, label)
                        .clicked()
                    {
                        app.apply(Action::SetGroupBy(group_by));
                    }
                }
            });
            ui.separator();
            let shown = app.mapper.map.as_ref().map(|map| map.document.id.clone());
            let filtering = !app.mapper.filter.trim().is_empty();
            // Once per map shown: open its group and scroll to it.
            let reveal = shown.is_some() && shown != app.revealed_map;
            let mut revealed = false;
            let mut open = None;
            egui::ScrollArea::vertical().show(ui, |ui| {
                for group in app.mapper.groups() {
                    let has_shown = group.maps.iter().any(|map| Some(&map.id) == shown.as_ref());
                    let force_open = filtering || (reveal && has_shown);
                    egui::CollapsingHeader::new(format!("{} ({})", group.title, group.maps.len()))
                        .id_salt(&group.title)
                        .open(force_open.then_some(true))
                        .show(ui, |ui| {
                            for map in &group.maps {
                                let selected = Some(&map.id) == shown.as_ref();
                                let response = ui.selectable_label(selected, &map.id);
                                if selected && reveal {
                                    response.scroll_to_me(Some(egui::Align::Center));
                                    revealed = true;
                                }
                                if response.clicked() {
                                    open = Some(map.id.clone());
                                }
                            }
                        });
                }
            });
            // Until its label is drawn: the group may take a frame to open.
            if revealed || !reveal || filtering {
                app.revealed_map = shown;
            }
            if let Some(id) = open {
                app.apply(Action::OpenMap(id));
            }
        });
}

pub fn details(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::right("details")
        .resizable(true)
        .default_size(290.0)
        .show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                crate::inspector::show(app, ui);
                ui.separator();
                map_details(app, ui);
                ui.separator();
                hover_details(app, ui);
                ui.separator();
                live_details(app, ui);
            });
        });
}

fn map_details(app: &mut MapperApp, ui: &mut egui::Ui) {
    let mut action = None;
    {
        let mapper = &app.mapper;
        let Some(map) = &mapper.map else {
            ui.label(crate::app::status_text(mapper));
            return;
        };
        ui.heading(format!("{} {}", text::MAP_INFO, map.document.id));
        if let (Some(content), Some(entry)) = (&mapper.content, &map.entry) {
            let name = content.table.display_name(entry);
            ui.label(format!("#{} · {name}", entry.index));
        }
        let size = map.background_size();
        ui.label(format!("{} × {} px", size.x, size.y));
        if !map.document.patches.is_empty() {
            ui.label(format!(
                "{}: {}",
                text::PATCHED_BY,
                map.document.patches.join(", ")
            ));
        }

        ui.add_space(6.0);
        ui.strong(text::EXITS);
        egui::Grid::new("exits").striped(true).show(ui, |ui| {
            for exit in map.overlay.exits.iter().filter(|exit| exit.is_used()) {
                ui.monospace(exit.exit.to_string());
                let label = ui.monospace(crate::canvas::exit_label(exit.exit, &exit.target));
                if let Some(patch) = map.document.exit_patch(exit.exit) {
                    label.on_hover_text(format!("{} {}", text::EXIT_PATCHED_BY, patch.pack));
                }
                if exit.cell_count == 0 {
                    ui.weak(text::NO_TRIGGER_CELLS);
                } else if exit.target.destination().is_some() && ui.button(text::GO).clicked() {
                    action = Some(Action::FollowExit(exit.exit));
                } else {
                    ui.label("");
                }
                ui.end_row();
            }
        });

        ui.add_space(6.0);
        ui.strong(text::NPCS);
        egui::Grid::new("npcs").striped(true).show(ui, |ui| {
            for (npc, record) in map.overlay.npcs.iter().zip(&map.document.npcs) {
                ui.monospace(format!("N{}", npc.slot));
                ui.label(format!(
                    "t{} s{} ({}, {}, {})",
                    npc.type_id,
                    npc.script_id,
                    record.position.x,
                    record.position.y,
                    record.position.z
                ));
                ui.end_row();
            }
        });
    }
    if let Some(action) = action {
        app.apply(action);
    }
}

fn hover_details(app: &MapperApp, ui: &mut egui::Ui) {
    ui.strong(text::HOVER);
    let Some(hover) = app.mapper.hover else {
        ui.weak("—");
        return;
    };
    let kind = match hover.kind {
        CellKind::Free => text::KIND_FREE.to_owned(),
        CellKind::Blocked => text::KIND_BLOCKED.to_owned(),
        CellKind::Trigger(_) => hover.band.map(text::band_name).unwrap_or_default(),
    };
    egui::Grid::new("hover").show(ui, |ui| {
        ui.label(text::CELL);
        ui.monospace(format!("{}, {}", hover.cell.column, hover.cell.row));
        ui.end_row();
        ui.label(text::VALUE);
        ui.monospace(format!("{} ({kind})", hover.value));
        ui.end_row();
        ui.label(text::WORLD);
        ui.monospace(format!("{:.0}, {:.0}", hover.world.x, hover.world.z));
        ui.end_row();
        let original = hover.original();
        ui.label(text::ORIGINAL);
        ui.monospace(format!("{:.0}, {:.0}", original.x, original.z));
        ui.end_row();
    });
}

fn live_details(app: &mut MapperApp, ui: &mut egui::Ui) {
    ui.strong(text::LIVE);
    let live = &mut app.live;
    ui.add(egui::TextEdit::singleline(&mut live.endpoint).hint_text(text::LIVE_ENDPOINT_HINT));
    let mut open = None;
    ui.horizontal(|ui| {
        if live.client.is_none() {
            if ui.button(text::LIVE_CONNECT).clicked() {
                live.connect();
            }
        } else {
            if ui.button(text::LIVE_DISCONNECT).clicked() {
                live.client = None;
                live.story = None;
                live.state = text::LIVE_OFF.to_owned();
            }
            if ui.button(text::LIVE_FOLLOW).clicked() {
                open = live.game_map();
            }
        }
    });
    let mut reload_on_save = app.mapper.reload_on_save();
    let reload_toggled = ui
        .checkbox(&mut reload_on_save, text::RELOAD_ON_SAVE)
        .changed();
    if live.client.is_some() {
        if ui.button(text::LIVE_RELOAD).clicked() {
            live.reload();
        }
        let mut overlay = live.overlay;
        if ui.checkbox(&mut overlay, text::LIVE_OVERLAY).changed() {
            live.set_overlay(overlay);
        }
        if ui.button(text::LIVE_STORY).clicked() {
            live.read_story();
        }
    }
    ui.weak(&live.state);
    if let Some(story) = &live.story {
        story_details(&app.mapper.story_names, story, ui);
    }
    if reload_toggled {
        app.apply(Action::SetReloadOnSave(reload_on_save));
    }
    if let Some(map) = open {
        app.apply(Action::OpenMap(map));
    }
}

/// The story's state read from the game, with the labels of `story/names`.
fn story_details(names: &dwe_map::StoryNames, story: &StoryState, ui: &mut egui::Ui) {
    ui.add_space(6.0);
    let map = story.map.as_deref().unwrap_or("—");
    ui.strong(format!("{} · {map} · tick {}", text::STORY, story.tick));
    ui.label(text::STORY_TRIGGERS);
    egui::Grid::new("story_triggers")
        .striped(true)
        .show(ui, |ui| {
            for &trigger in &story.triggers {
                ui.monospace(text::named(trigger, names.trigger(trigger)));
                ui.end_row();
            }
        });
    ui.label(text::STORY_VARIABLES);
    egui::Grid::new("story_variables")
        .striped(true)
        .show(ui, |ui| {
            for &(variable, value) in &story.variables {
                ui.monospace(text::named(variable, names.variable(variable)));
                ui.monospace(value.to_string());
                ui.end_row();
            }
        });
}
