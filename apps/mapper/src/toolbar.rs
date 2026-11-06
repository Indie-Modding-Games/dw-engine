//! The tool bar under the menus: the canvas tools, the brush size and the collision value the
//! tools paint, picked by the bands of `field/triggers`.

use eframe::egui;
use mapper_core::tools::{self, BRUSH_SIZES, ValueGroup};
use mapper_core::{Action, Tool, text};

use crate::app::MapperApp;

pub fn show(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::top("tools").show(ui, |ui| {
        ui.horizontal_wrapped(|ui| {
            let mut actions = Vec::new();
            ui.label(text::TOOLS);
            for tool in Tool::ALL {
                if ui
                    .selectable_label(app.mapper.tool == tool, text::tool_name(tool))
                    .clicked()
                {
                    actions.push(Action::SetTool(tool));
                }
            }
            if app.mapper.tool == Tool::Brush {
                ui.separator();
                ui.label(text::BRUSH_SIZE);
                for size in BRUSH_SIZES {
                    let label = format!("{size}×{size}");
                    if ui
                        .selectable_label(app.mapper.brush_size == size, label)
                        .clicked()
                    {
                        actions.push(Action::SetBrushSize(size));
                    }
                }
            }
            ui.separator();
            value_picker(app, ui, &mut actions);
            for action in actions {
                app.apply(action);
            }
        });
    });
}

/// The value the tools paint: a menu grouped by band, and the raw number.
fn value_picker(app: &MapperApp, ui: &mut egui::Ui, actions: &mut Vec<Action>) {
    let mapper = &app.mapper;
    let current = mapper.paint_value;
    ui.label(text::PAINT_VALUE);
    ui.menu_button(value_label(app, current), |ui| {
        for group in tools::value_groups(&mapper.bands) {
            let mut pick = |ui: &mut egui::Ui, label: String, value: u8| {
                if ui.selectable_label(current == value, label).clicked() {
                    actions.push(Action::SetPaintValue(value));
                    ui.close();
                }
            };
            match group {
                ValueGroup::Wall => pick(ui, text::VALUE_WALL.to_owned(), tools::WALL),
                ValueGroup::Free => pick(ui, text::VALUE_FREE.to_owned(), tools::FLOOR),
                ValueGroup::Step(values) => {
                    ui.menu_button(text::VALUE_STEP, |ui| {
                        for value in values {
                            pick(ui, value.to_string(), value);
                        }
                    });
                }
                ValueGroup::Confirm(values) => {
                    ui.menu_button(text::VALUE_CONFIRM, |ui| {
                        for value in values {
                            pick(ui, value.to_string(), value);
                        }
                    });
                }
                ValueGroup::Exit { exit, value } => {
                    pick(ui, format!("{} {exit} ({value})", text::VALUE_EXIT), value);
                }
                ValueGroup::Toilet(value) => {
                    pick(ui, format!("{} ({value})", text::VALUE_TOILET), value);
                }
            }
        }
    });
    let mut raw = i32::from(current);
    ui.label(text::VALUE_RAW);
    if ui
        .add(egui::DragValue::new(&mut raw).range(0..=255))
        .changed()
    {
        actions.push(Action::SetPaintValue(raw.clamp(0, 255) as u8));
    }
}

/// A value with the band it falls in.
pub fn value_label(app: &MapperApp, value: u8) -> String {
    let bands = &app.mapper.bands;
    if value == tools::FLOOR {
        return format!("{value} {}", text::VALUE_FREE);
    }
    match dwe_map::collision::kind_of(value) {
        dwe_map::CellKind::Blocked => format!("{value} {}", text::VALUE_WALL),
        dwe_map::CellKind::Free => format!("{value} {}", text::VALUE_FREE),
        dwe_map::CellKind::Trigger(_) => format!("{value} {}", text::band_name(bands.band(value))),
    }
}
