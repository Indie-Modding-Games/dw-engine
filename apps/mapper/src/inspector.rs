//! The inspector: the fields of what is selected (an exit, an NPC with its path, a door or the
//! toilet, see [`crate::places`]), the buttons that add to the map, and the sections the editor
//! only shows (camera, lights, objects). A field dragged is one undo step.

use dwe_map::document::RawVec3;
use dwe_map::{Npc, Warp};
use eframe::egui;
use mapper_core::tools::{self, PART_RUN, PART_WAIT, PART_WALK};
use mapper_core::{Action, Selection, Tool, text};
use serde_yaml_ng::Value;

use crate::app::MapperApp;

/// How many map ids the target list shows at once.
const MOST_LISTED_MAPS: usize = 200;

pub fn show(app: &mut MapperApp, ui: &mut egui::Ui) {
    let mut actions = Vec::new();
    ui.heading(text::INSPECTOR);
    match app.mapper.selection {
        Some(Selection::Exit(exit)) => exit_fields(app, ui, exit, &mut actions),
        Some(Selection::Door(slot)) => crate::places::door_fields(app, ui, slot, &mut actions),
        Some(Selection::Toilet(_)) => crate::places::toilet_fields(app, ui, &mut actions),
        Some(selection) => {
            if let Some(slot) = selection.npc_slot() {
                npc_fields(app, ui, slot, &mut actions);
            }
        }
        None => {
            ui.weak(text::NOTHING_SELECTED);
        }
    }
    crate::places::add_buttons(app, ui, &mut actions);
    read_only(app, ui);
    for action in actions {
        app.apply(action);
    }
}

/// Wraps a field's response: a drag starts and ends one undo step.
pub fn grouped(response: &egui::Response, actions: &mut Vec<Action>) {
    if response.drag_started() {
        actions.push(Action::BeginEdit);
    }
    if response.drag_stopped() {
        actions.push(Action::EndEdit);
    }
}

/// A number field; true when it changed.
pub fn number(ui: &mut egui::Ui, value: &mut i32, actions: &mut Vec<Action>) -> bool {
    let response = ui.add(egui::DragValue::new(value));
    grouped(&response, actions);
    response.changed()
}

pub fn vector(ui: &mut egui::Ui, value: &mut RawVec3, actions: &mut Vec<Action>) -> bool {
    let mut changed = false;
    ui.horizontal(|ui| {
        changed |= number(ui, &mut value.x, actions);
        changed |= number(ui, &mut value.y, actions);
        changed |= number(ui, &mut value.z, actions);
    });
    changed
}

fn exit_fields(app: &mut MapperApp, ui: &mut egui::Ui, exit: i32, actions: &mut Vec<Action>) {
    let mapper = &app.mapper;
    let Some(map) = &mapper.map else { return };
    let Some(warp) = map.document.warps.iter().find(|warp| warp.exit == exit) else {
        return;
    };
    let mut edited: Warp = warp.clone();
    ui.strong(format!("{} {exit}", text::EXIT));
    let mut changed = false;
    egui::Grid::new("exit_fields")
        .num_columns(2)
        .show(ui, |ui| {
            ui.label(text::TARGET_MAP);
            ui.vertical(|ui| {
                ui.add(
                    egui::TextEdit::singleline(&mut app.target_filter)
                        .hint_text(text::FILTER_HINT)
                        .desired_width(150.0),
                );
                let shown = edited
                    .target_map_id
                    .clone()
                    .unwrap_or_else(|| text::TARGET_MAP_NONE.to_owned());
                egui::ComboBox::from_id_salt("target_map")
                    .selected_text(shown)
                    .show_ui(ui, |ui| {
                        if ui
                            .selectable_label(edited.target_map_id.is_none(), text::TARGET_MAP_NONE)
                            .clicked()
                        {
                            edited.target_map_id = None;
                            edited.target_map = -1;
                            changed = true;
                        }
                        let Some(content) = &mapper.content else {
                            return;
                        };
                        let filter = app.target_filter.trim().to_lowercase();
                        let maps = content
                            .table
                            .maps
                            .iter()
                            .filter(|entry| entry.has_file() && entry.id.contains(&filter))
                            .take(MOST_LISTED_MAPS);
                        for entry in maps {
                            let chosen = edited.target_map_id.as_deref() == Some(entry.id.as_str());
                            if ui.selectable_label(chosen, &entry.id).clicked() {
                                edited.target_map_id = Some(entry.id.clone());
                                // The original's index too, so both name the same map.
                                edited.target_map = if entry.original_index {
                                    entry.index
                                } else {
                                    -1
                                };
                                changed = true;
                            }
                        }
                    });
            });
            ui.end_row();
            ui.label(text::TARGET_INDEX);
            ui.monospace(edited.target_map.to_string());
            ui.end_row();
            ui.label(text::TARGET_EXIT);
            let response = ui.add(egui::DragValue::new(&mut edited.target_exit).range(-1..=9));
            grouped(&response, actions);
            changed |= response.changed();
            ui.end_row();
            ui.label(text::ARRIVAL);
            changed |= vector(ui, &mut edited.spawn, actions);
            ui.end_row();
            ui.label(text::FACING);
            let response = ui.add(egui::DragValue::new(&mut edited.rotation).range(0..=4095));
            grouped(&response, actions);
            changed |= response.changed();
            ui.end_row();
        });
    if changed {
        actions.push(Action::SetWarp(edited.clone()));
    }
    ui.horizontal_wrapped(|ui| {
        let trigger = u8::try_from(exit)
            .ok()
            .and_then(|exit| mapper.bands.exit_trigger(exit));
        if let Some(value) = trigger
            && ui.button(text::PAINT_EXIT_CELLS).clicked()
        {
            actions.push(Action::SetPaintValue(value));
            actions.push(Action::SetTool(Tool::Brush));
        }
        if ui.button(text::ERASE_CELLS).clicked() {
            actions.push(Action::SetPaintValue(tools::FLOOR));
            actions.push(Action::SetTool(Tool::Brush));
        }
        let leads = map
            .overlay
            .exits
            .iter()
            .any(|mark| mark.exit == exit && mark.target.destination().is_some());
        if ui
            .add_enabled(leads, egui::Button::new(text::OPEN_TARGET))
            .clicked()
        {
            actions.push(Action::FollowExit(exit));
        }
    });
    live_exit_tests(app, ui, exit);
}

/// With a game on this map: put the player on the exit's first trigger cell and let the game run
/// (it takes the exit), or where the exit leaves the player.
fn live_exit_tests(app: &MapperApp, ui: &mut egui::Ui, exit: i32) {
    let Some(view) = app.live.shows(&app.mapper) else {
        return;
    };
    let rotation = view
        .player
        .map_or(0, |player| mapper_core::live::rotation_of_yaw(player.yaw));
    ui.horizontal_wrapped(|ui| {
        let cell = app.mapper.exit_cell(exit);
        if ui
            .add_enabled(cell.is_some(), egui::Button::new(text::LIVE_TEST_EXIT))
            .on_hover_text(text::LIVE_TEST_EXIT_HINT)
            .clicked()
            && let Some(cell) = cell
        {
            let at = dwe_map::world::cell_center(cell);
            app.live
                .place(mapper_core::live::place_player(at, rotation), true);
        }
        if let Some(at) = app.mapper.place_at_arrival(exit)
            && ui.button(text::LIVE_TO_ARRIVAL).clicked()
        {
            app.live.place(at, false);
        }
    });
}

fn npc_fields(app: &MapperApp, ui: &mut egui::Ui, slot: usize, actions: &mut Vec<Action>) {
    let Some(map) = &app.mapper.map else { return };
    let Some(npc) = map.document.npcs.get(slot) else {
        return;
    };
    let mut edited: Npc = npc.clone();
    ui.strong(format!("{} {slot}", text::NPC));
    let mut changed = false;
    egui::Grid::new("npc_fields").num_columns(2).show(ui, |ui| {
        for (label, value) in [
            ("type_id", &mut edited.type_id),
            ("follow_mode", &mut edited.follow_mode),
            ("tracking_range", &mut edited.tracking_range),
            ("script_id", &mut edited.script_id),
        ] {
            ui.monospace(label);
            changed |= number(ui, value, actions);
            ui.end_row();
        }
        ui.label(text::POSITION);
        changed |= vector(ui, &mut edited.position, actions);
        ui.end_row();
        ui.label(text::FACING);
        let response = ui.add(egui::DragValue::new(&mut edited.rotation.y).range(0..=4095));
        grouped(&response, actions);
        changed |= response.changed();
        ui.end_row();
    });
    egui::CollapsingHeader::new(text::NPC_FIELDS)
        .id_salt("npc_other")
        .show(ui, |ui| {
            egui::Grid::new("npc_other").num_columns(2).show(ui, |ui| {
                // Only the whole numbers: the other fields stay as the document has them.
                for (key, value) in edited.other.iter_mut() {
                    let (Some(key), Some(mut number_value)) = (key.as_str(), value.as_i64()) else {
                        continue;
                    };
                    ui.monospace(key);
                    let response = ui.add(egui::DragValue::new(&mut number_value));
                    grouped(&response, actions);
                    if response.changed() {
                        *value = Value::from(number_value);
                        changed = true;
                    }
                    ui.end_row();
                }
            });
        });
    if changed {
        actions.push(Action::SetNpc {
            slot,
            npc: edited.clone(),
        });
    }
    path_fields(app, ui, slot, npc, actions);
    let last = slot + 1 == map.document.npcs.len();
    if ui
        .add_enabled(last, egui::Button::new(text::REMOVE_NPC))
        .on_disabled_hover_text(text::REMOVE_NPC_HINT)
        .clicked()
    {
        actions.push(Action::RemoveNpc);
    }
}

/// The NPC's path: each part's kind and point (or frames, for a wait), in order.
fn path_fields(
    app: &MapperApp,
    ui: &mut egui::Ui,
    slot: usize,
    npc: &Npc,
    actions: &mut Vec<Action>,
) {
    ui.add_space(4.0);
    ui.strong(text::PATH);
    let length = tools::path_length(npc);
    let selected = match app.mapper.selection {
        Some(Selection::Waypoint { part, .. }) => Some(part),
        _ => None,
    };
    egui::Grid::new("npc_path").striped(true).show(ui, |ui| {
        for part in 0..length {
            let chosen = selected == Some(part);
            if ui
                .selectable_label(chosen, format!("{slot}.{part}"))
                .clicked()
            {
                actions.push(Action::Select(Some(Selection::Waypoint { slot, part })));
            }
            let kind = npc.ai_sections[part];
            let kind_name = |kind: i32| match kind {
                PART_WAIT => text::PART_WAIT,
                PART_RUN => text::PART_RUN,
                _ => text::PART_WALK,
            };
            egui::ComboBox::from_id_salt(("part_kind", part))
                .selected_text(kind_name(kind))
                .width(80.0)
                .show_ui(ui, |ui| {
                    for choice in [PART_WAIT, PART_WALK, PART_RUN] {
                        if ui
                            .selectable_label(kind == choice, kind_name(choice))
                            .clicked()
                            && let Some(npc) = tools::set_part_kind(npc, part, choice)
                        {
                            actions.push(Action::SetNpc { slot, npc });
                        }
                    }
                });
            let mut point = npc.waypoints.get(part).copied().unwrap_or_default();
            let changed = if kind == PART_WAIT {
                ui.horizontal(|ui| {
                    let changed = number(ui, &mut point.x, actions);
                    ui.weak(text::FRAMES);
                    changed
                })
                .inner
            } else {
                vector(ui, &mut point, actions)
            };
            if changed {
                let mut edited = npc.clone();
                if let Some(waypoint) = edited.waypoints.get_mut(part) {
                    *waypoint = point;
                    actions.push(Action::SetNpc { slot, npc: edited });
                }
            }
            ui.horizontal(|ui| {
                if ui
                    .add_enabled(part > 0, egui::Button::new(text::UP))
                    .clicked()
                {
                    actions.push(Action::MoveWaypoint {
                        slot,
                        part,
                        later: false,
                    });
                }
                if ui
                    .add_enabled(part + 1 < length, egui::Button::new(text::DOWN))
                    .clicked()
                {
                    actions.push(Action::MoveWaypoint {
                        slot,
                        part,
                        later: true,
                    });
                }
                if ui.button(text::REMOVE_POINT).clicked() {
                    actions.push(Action::RemoveWaypoint { slot, part });
                }
            });
            ui.end_row();
        }
    });
    let full = length >= dwe_map::save::NPC_PATH_PARTS;
    if ui
        .add_enabled(!full, egui::Button::new(text::ADD_POINT))
        .clicked()
    {
        actions.push(Action::AddWaypoint { slot, at: None });
    }
}

/// The sections the editor does not change yet.
fn read_only(app: &MapperApp, ui: &mut egui::Ui) {
    let Some(map) = &app.mapper.map else { return };
    let document = &map.document;
    ui.add_space(6.0);
    let vector = |value: RawVec3| format!("{}, {}, {}", value.x, value.y, value.z);
    egui::CollapsingHeader::new(format!("{} · {}", text::CAMERA, text::READ_ONLY))
        .id_salt("camera")
        .show(ui, |ui| {
            let camera = &document.camera;
            egui::Grid::new("camera").show(ui, |ui| {
                ui.monospace("viewpoint");
                ui.monospace(vector(camera.viewpoint));
                ui.end_row();
                ui.monospace("reference");
                ui.monospace(vector(camera.reference));
                ui.end_row();
                ui.monospace("projection");
                ui.monospace(camera.projection.to_string());
                ui.end_row();
            });
        });
    egui::CollapsingHeader::new(format!(
        "{} ({}) · {}",
        text::LIGHTS,
        document.lights.len(),
        text::READ_ONLY
    ))
    .id_salt("lights")
    .show(ui, |ui| {
        egui::Grid::new("lights").striped(true).show(ui, |ui| {
            for (index, light) in document.lights.iter().enumerate() {
                ui.monospace(index.to_string());
                ui.monospace(vector(light.direction));
                let color = light.color;
                ui.monospace(format!("rgb {} {} {}", color.r, color.g, color.b));
                ui.end_row();
            }
        });
    });
    let objects = document.objects.as_ref();
    let (sprites, instances) = objects.map_or((0, 0), |objects| {
        (objects.sprites.len(), objects.instances.len())
    });
    egui::CollapsingHeader::new(format!(
        "{} ({sprites} / {instances}) · {}",
        text::OBJECTS,
        text::READ_ONLY
    ))
    .id_salt("objects")
    .show(ui, |ui| {
        let Some(objects) = objects else { return };
        egui::Grid::new("objects").striped(true).show(ui, |ui| {
            for (index, instance) in objects.instances.iter().enumerate() {
                ui.monospace(index.to_string());
                ui.monospace(format!("{}, {}", instance.x, instance.y));
                ui.monospace(format!("flag {}", instance.flag));
                ui.monospace(format!("{} {}", instance.frames.len(), text::FRAMES));
                ui.end_row();
            }
        });
    });
}
