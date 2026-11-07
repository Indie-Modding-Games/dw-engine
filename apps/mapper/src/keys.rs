//! The keyboard shortcuts (the list the help shows is `text::SHORTCUT_LIST`). They wait while a
//! text field has the keyboard.

use dwe_map::save::SaveMode;
use eframe::egui::{self, Key, KeyboardShortcut, Modifiers};
use mapper_core::tools::{self, BRUSH_SIZES};
use mapper_core::{Action, Layer, Selection, Tool};

use crate::app::MapperApp;

/// A turn of Q or E: 45 degrees (4096 a turn).
const TURN_STEP: i32 = 512;

/// The number keys in order: 1 to 9, then 0. Each toggles a layer of `Layer::ALL`; with Alt it
/// selects the exit of its number.
const DIGITS: [Key; 10] = [
    Key::Num1,
    Key::Num2,
    Key::Num3,
    Key::Num4,
    Key::Num5,
    Key::Num6,
    Key::Num7,
    Key::Num8,
    Key::Num9,
    Key::Num0,
];

pub fn handle(app: &mut MapperApp, ctx: &egui::Context) {
    if ctx.egui_wants_keyboard_input() || app.mapper.pending.is_some() {
        return;
    }
    let command = |key| KeyboardShortcut::new(Modifiers::COMMAND, key);
    let command_shift = |key| KeyboardShortcut::new(Modifiers::COMMAND | Modifiers::SHIFT, key);
    let mut actions = Vec::new();
    ctx.input_mut(|input| {
        // The longer shortcuts first: Ctrl+Z would also take Ctrl+Shift+Z.
        if input.consume_shortcut(&command_shift(Key::Z))
            || input.consume_shortcut(&command(Key::Y))
        {
            actions.push(Action::Redo);
        }
        if input.consume_shortcut(&command(Key::Z)) {
            actions.push(Action::Undo);
        }
        if input.consume_shortcut(&command(Key::S)) {
            actions.push(Action::Save(SaveMode::Patch));
        }
        if input.consume_shortcut(&command(Key::F)) {
            app.focus_filter = true;
        }
        // Alt before the plain digits: a plain key also takes it with Alt held.
        for (place, &digit) in DIGITS.iter().enumerate() {
            let exit = (place as i32 + 1) % 10;
            if input.consume_key(Modifiers::ALT, digit) {
                actions.push(Action::Select(Some(Selection::Exit(exit))));
                actions.push(Action::CenterSelection);
            }
        }
        for (&digit, layer) in DIGITS.iter().zip(Layer::ALL) {
            if input.consume_key(Modifiers::NONE, digit) {
                actions.push(Action::SetLayer(layer, !app.mapper.layers.shows(layer)));
            }
        }
        if input.consume_key(Modifiers::NONE, Key::PageUp) {
            actions.push(Action::OpenAdjacentMap { later: false });
        }
        if input.consume_key(Modifiers::NONE, Key::PageDown) {
            actions.push(Action::OpenAdjacentMap { later: true });
        }
        let mut key = |key| input.consume_key(Modifiers::NONE, key);
        for (letter, tool) in [
            (Key::V, Tool::Select),
            (Key::B, Tool::Brush),
            (Key::R, Tool::Rectangle),
            (Key::F, Tool::Fill),
            (Key::I, Tool::Eyedropper),
        ] {
            if key(letter) {
                actions.push(Action::SetTool(tool));
            }
        }
        let size = app.mapper.brush_size;
        let place = BRUSH_SIZES
            .iter()
            .position(|&known| known == size)
            .unwrap_or(0);
        if key(Key::OpenBracket) && place > 0 {
            actions.push(Action::SetBrushSize(BRUSH_SIZES[place - 1]));
        }
        if key(Key::CloseBracket) && place + 1 < BRUSH_SIZES.len() {
            actions.push(Action::SetBrushSize(BRUSH_SIZES[place + 1]));
        }
        if key(Key::X) {
            let value = if app.mapper.paint_value == tools::WALL {
                tools::FLOOR
            } else {
                tools::WALL
            };
            actions.push(Action::SetPaintValue(value));
        }
        if key(Key::Q) {
            actions.push(Action::Rotate(-TURN_STEP));
        }
        if key(Key::E) {
            actions.push(Action::Rotate(TURN_STEP));
        }
        if key(Key::Delete) {
            actions.push(Action::DeleteSelection);
        }
        if key(Key::C) {
            actions.push(Action::CenterSelection);
        }
        if key(Key::Escape) {
            actions.push(Action::Select(None));
        }
        if key(Key::W)
            && let Some(slot) = app.mapper.selection.and_then(Selection::npc_slot)
        {
            let at = app
                .mapper
                .hover
                .map(|hover| app.mapper.view.to_canvas(hover.background));
            actions.push(Action::AddWaypoint { slot, at });
        }
    });
    for action in actions {
        app.apply(action);
    }
}
