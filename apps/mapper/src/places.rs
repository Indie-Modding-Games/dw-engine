//! The inspector's fields for the map's doors and toilet, and the buttons that add NPC slots,
//! doors and the toilet. The doors and the toilet live in the map table (`data/maps`); a save
//! writes them into the target pack.

use dwe_map::document::RawVec3;
use dwe_map::table::{MapDoor, MapToilet};
use eframe::egui;
use mapper_core::{Action, Selection, text};

use crate::app::MapperApp;
use crate::inspector::{grouped, number, vector};

/// How far apart a new toilet's two points are, in the original's units.
const NEW_TOILET_SPAN: i32 = 200;

/// A door slot's fields: its model ("sin modelo" leaves the slot unused), place and turn.
pub fn door_fields(app: &MapperApp, ui: &mut egui::Ui, slot: usize, actions: &mut Vec<Action>) {
    let doors = app.mapper.places().door_slots();
    let Some(door) = doors.get(slot) else {
        return;
    };
    let mut edited: MapDoor = door.clone();
    ui.strong(format!("{} {slot}", text::DOOR));
    let mut changed = false;
    egui::Grid::new("door_fields")
        .num_columns(2)
        .show(ui, |ui| {
            ui.label(text::DOOR_MODEL);
            ui.horizontal(|ui| {
                let mut none = edited.model_id.is_none();
                if ui.checkbox(&mut none, text::DOOR_NO_MODEL).changed() {
                    edited.model_id = if none { None } else { Some(0) };
                    changed = true;
                }
                if let Some(model) = &mut edited.model_id {
                    changed |= number(ui, model, actions);
                }
            });
            ui.end_row();
            ui.label(text::POSITION);
            changed |= vector(ui, &mut edited.position, actions);
            ui.end_row();
            ui.label(text::ROTATION);
            let response = ui.add(egui::DragValue::new(&mut edited.rotation).range(0..=4095));
            grouped(&response, actions);
            changed |= response.changed();
            ui.end_row();
        });
    if changed {
        actions.push(Action::SetDoor { slot, door: edited });
    }
}

/// The toilet's two points.
pub fn toilet_fields(app: &MapperApp, ui: &mut egui::Ui, actions: &mut Vec<Action>) {
    let Some(toilet) = app.mapper.places().toilet else {
        return;
    };
    let mut edited: MapToilet = toilet.clone();
    ui.strong(text::TOILET).on_hover_text(text::TOILET_HINT);
    let mut changed = false;
    egui::Grid::new("toilet_fields")
        .num_columns(2)
        .show(ui, |ui| {
            for (label, x, z) in [
                (text::TOILET_FIRST, &mut edited.x1, &mut edited.z1),
                (text::TOILET_SECOND, &mut edited.x2, &mut edited.z2),
            ] {
                ui.label(label);
                ui.horizontal(|ui| {
                    changed |= number(ui, x, actions);
                    changed |= number(ui, z, actions);
                });
                ui.end_row();
            }
        });
    if changed {
        actions.push(Action::SetToilet(Some(edited)));
    }
}

/// The buttons that add to the map: an NPC slot, a door in the first unused slot, the toilet
/// (or take it away).
pub fn add_buttons(app: &MapperApp, ui: &mut egui::Ui, actions: &mut Vec<Action>) {
    if app.mapper.map.is_none() {
        return;
    }
    let places = app.mapper.places();
    ui.add_space(4.0);
    ui.horizontal_wrapped(|ui| {
        ui.weak(text::ADD_THINGS);
        if ui.button(text::ADD_NPC).clicked() {
            actions.push(Action::AddNpc(None));
        }
        let free = places
            .door_slots()
            .iter()
            .position(|door| door.model_id.is_none());
        if ui
            .add_enabled(free.is_some(), egui::Button::new(text::ADD_DOOR))
            .on_disabled_hover_text(text::NO_FREE_DOOR)
            .clicked()
            && let Some(slot) = free
        {
            let door = MapDoor {
                model_id: Some(0),
                position: view_centre(app),
                rotation: 0,
            };
            actions.push(Action::SetDoor { slot, door });
            actions.push(Action::Select(Some(Selection::Door(slot))));
        }
        if places.toilet.is_some() {
            if ui.button(text::REMOVE_TOILET).clicked() {
                actions.push(Action::SetToilet(None));
                actions.push(Action::Select(None));
            }
        } else if ui.button(text::ADD_TOILET).clicked() {
            let at = view_centre(app);
            actions.push(Action::SetToilet(Some(MapToilet {
                index: 0,
                x1: at.x,
                z1: at.z,
                x2: at.x + NEW_TOILET_SPAN,
                z2: at.z,
            })));
            actions.push(Action::Select(Some(Selection::Toilet(0))));
        }
    });
}

/// The ground under the middle of the view, in the original's units, at the height of the map's
/// first NPC or arrival (0 without either).
fn view_centre(app: &MapperApp) -> RawVec3 {
    let mapper = &app.mapper;
    let Some(map) = &mapper.map else {
        return RawVec3 { x: 0, y: 0, z: 0 };
    };
    let document = &map.document;
    let height = document
        .npcs
        .first()
        .map(|npc| npc.position.y)
        .or_else(|| document.warps.first().map(|warp| warp.spawn.y))
        .unwrap_or(0);
    let world_height = dwe_map::world::to_world(
        RawVec3 {
            x: 0,
            y: height,
            z: 0,
        }
        .into(),
    )
    .y;
    map.camera
        .ground_at_background(mapper.view.center, world_height)
        .map_or(
            RawVec3 {
                x: 0,
                y: height,
                z: 0,
            },
            |ground| {
                let original = dwe_map::world::from_original_axes(ground);
                RawVec3 {
                    x: original.x.round() as i32,
                    y: height,
                    z: original.z.round() as i32,
                }
            },
        )
}
