//! The map canvas: the background with the layers over it. The primary button works with the
//! tool chosen (select and drag, paint, rectangle, fill, eyedropper; Ctrl+click follows an exit);
//! the right or middle button pans, the wheel (or a pinch) zooms, and a double click with
//! Seleccionar fits the map. A right click (without dragging) opens a menu that adds an NPC there
//! or opens the section of the event under it; with a game connected on the same map, the game's
//! characters are drawn where they are now, and the menu also puts the player there or tests an
//! exit. The Eventos layer is drawn by [`crate::events`].

use dwe_map::world::{self, Cell};
use dwe_map::{CellKind, TriggerBand};
use eframe::egui::{self, Color32, PointerButton, Pos2, Shape, Stroke};
use glam::Vec2;
use mapper_core::overlay::Arrow;
use mapper_core::tools::Drag;
use mapper_core::{Action, ExitTarget, Layer, Mapper, Modifiers, NewCue, Selection, Tool, text};

use crate::app::MapperApp;

/// How fast the wheel zooms: the factor per pixel of scroll.
const WHEEL_ZOOM: f32 = 0.0015;

pub mod colors {
    use eframe::egui::Color32;

    pub const WALL: Color32 = Color32::from_rgba_premultiplied(110, 20, 20, 110);
    pub const STEP: Color32 = Color32::from_rgba_premultiplied(130, 110, 0, 130);
    pub const CONFIRM: Color32 = Color32::from_rgba_premultiplied(0, 100, 130, 130);
    pub const EXIT: Color32 = Color32::from_rgba_premultiplied(0, 130, 40, 140);
    pub const TOILET: Color32 = Color32::from_rgba_premultiplied(100, 40, 130, 140);
    pub const OTHER: Color32 = Color32::from_rgba_premultiplied(90, 90, 90, 120);
    pub const GRID: Color32 = Color32::from_rgba_premultiplied(40, 40, 40, 60);
    pub const EXIT_TEXT: Color32 = Color32::from_rgb(140, 255, 160);
    pub const ARRIVAL: Color32 = Color32::from_rgb(120, 255, 140);
    pub const NPC: Color32 = Color32::from_rgb(255, 170, 60);
    pub const WAYPOINT: Color32 = Color32::from_rgb(255, 220, 140);
    pub const DOOR: Color32 = Color32::from_rgb(200, 150, 100);
    pub const TOILET_LINE: Color32 = Color32::from_rgb(210, 140, 255);
    pub const HOVER: Color32 = Color32::WHITE;
    pub const LABEL_BACK: Color32 = Color32::from_rgba_premultiplied(0, 0, 0, 170);
    pub const SELECTED: Color32 = Color32::from_rgb(80, 200, 255);
    pub const PREVIEW: Color32 = Color32::from_rgba_premultiplied(40, 120, 160, 70);
    pub const LIVE_PLAYER: Color32 = Color32::from_rgb(90, 230, 255);
    pub const LIVE_PARTNER: Color32 = Color32::from_rgb(255, 120, 220);
    pub const LIVE_NPC: Color32 = Color32::from_rgb(255, 255, 120);
    pub const LIVE_VIEW: Color32 = Color32::from_rgb(90, 230, 255);
}

/// The length of a live character's facing arrow, in world units.
const LIVE_ARROW: f32 = 90.0;

pub fn show(app: &mut MapperApp, ui: &mut egui::Ui) {
    let (response, painter) =
        ui.allocate_painter(ui.available_size(), egui::Sense::click_and_drag());
    app.canvas_rect = Some(response.rect);
    let origin = response.rect.min;
    let to_local = |pos: Pos2| Vec2::new(pos.x - origin.x, pos.y - origin.y);

    if response.dragged_by(PointerButton::Secondary) || response.dragged_by(PointerButton::Middle) {
        let delta = response.drag_delta();
        app.apply(Action::Pan(Vec2::new(delta.x, delta.y)));
    }
    pointer_gesture(app, ui, &response);
    if let Some(pointer) = response.hover_pos() {
        let (scroll, pinch) = ui.input(|input| (input.smooth_scroll_delta.y, input.zoom_delta()));
        let factor = pinch * (scroll * WHEEL_ZOOM).exp();
        if (factor - 1.0).abs() > 1e-4 {
            app.apply(Action::Zoom {
                factor,
                anchor: to_local(pointer),
            });
        }
    }
    app.apply(Action::Hover(response.hover_pos().map(to_local)));
    if response.secondary_clicked() {
        app.context_point = context_point(&app.mapper);
    }
    remember_event(app);
    if response.double_clicked() && app.mapper.tool == Tool::Select {
        app.apply(Action::FitView);
    }

    let painter = painter.with_clip_rect(response.rect);
    let mapper = &app.mapper;
    let Some(map) = &mapper.map else {
        painter.text(
            response.rect.center(),
            egui::Align2::CENTER_CENTER,
            crate::app::status_text(mapper),
            egui::FontId::proportional(16.0),
            Color32::GRAY,
        );
        return;
    };
    let to_screen = |background: Vec2| {
        let canvas = mapper.view.to_canvas(background);
        Pos2::new(origin.x + canvas.x, origin.y + canvas.y)
    };

    let size = map.background_size();
    let corners = egui::Rect::from_min_max(to_screen(Vec2::ZERO), to_screen(size));
    match app.background_texture() {
        Some(texture) => {
            painter.image(
                texture.id(),
                corners,
                egui::Rect::from_min_max(Pos2::ZERO, Pos2::new(1.0, 1.0)),
                Color32::WHITE,
            );
        }
        None => {
            painter.rect_filled(corners, 0.0, Color32::from_gray(40));
            painter.text(
                corners.center(),
                egui::Align2::CENTER_CENTER,
                text::NO_BACKGROUND,
                egui::FontId::proportional(14.0),
                Color32::GRAY,
            );
        }
    }

    let overlay = &map.overlay;
    let layers = mapper.layers;
    if layers.shows(Layer::Grid) {
        for line in &overlay.grid {
            painter.add(Shape::line(
                line.iter().map(|&point| to_screen(point)).collect(),
                Stroke::new(1.0, colors::GRID),
            ));
        }
    }
    draw_cells(&painter, mapper, &to_screen);
    draw_hover(&painter, mapper, &to_screen);
    draw_tool_preview(&painter, mapper, &to_screen);
    if layers.shows(Layer::Toilet)
        && let Some([first, second]) = overlay.toilet
    {
        let (first, second) = (to_screen(first), to_screen(second));
        painter.line_segment([first, second], Stroke::new(2.0, colors::TOILET_LINE));
        painter.circle_filled(first, 4.0, colors::TOILET_LINE);
        painter.circle_stroke(second, 6.0, Stroke::new(2.0, colors::TOILET_LINE));
        label(&painter, second, text::LAYER_TOILET, colors::TOILET_LINE);
    }
    if layers.shows(Layer::Doors) {
        for door in &overlay.doors {
            let at = draw_arrow(&painter, door.position, &to_screen, colors::DOOR);
            painter.rect_stroke(
                egui::Rect::from_center_size(at, egui::vec2(10.0, 10.0)),
                1.0,
                Stroke::new(2.0, colors::DOOR),
                egui::StrokeKind::Middle,
            );
            label(
                &painter,
                at,
                &format!("P{} m{}", door.slot, door.model_id),
                colors::DOOR,
            );
        }
    }
    if layers.shows(Layer::Npcs) || layers.shows(Layer::Waypoints) {
        for npc in &overlay.npcs {
            let Some(position) = npc.position else {
                continue;
            };
            if layers.shows(Layer::Waypoints) {
                let mut previous = to_screen(position.from);
                for (index, &point) in npc.waypoints.iter().enumerate() {
                    let at = to_screen(point);
                    painter.line_segment([previous, at], Stroke::new(1.0, colors::WAYPOINT));
                    painter.add(diamond(at, 4.0, colors::WAYPOINT));
                    painter.text(
                        at + egui::vec2(5.0, 0.0),
                        egui::Align2::LEFT_CENTER,
                        format!("{}.{index}", npc.slot),
                        egui::FontId::monospace(10.0),
                        colors::WAYPOINT,
                    );
                    previous = at;
                }
                // The path starts again from its first point.
                if npc.waypoints.len() > 1 {
                    let first = to_screen(npc.waypoints[0]);
                    painter.line_segment([previous, first], Stroke::new(1.0, colors::WAYPOINT));
                }
            }
            if layers.shows(Layer::Npcs) {
                let at = draw_arrow(&painter, position, &to_screen, colors::NPC);
                painter.circle_filled(at, 5.0, colors::NPC);
                label(
                    &painter,
                    at,
                    &format!("N{} t{} s{}", npc.slot, npc.type_id, npc.script_id),
                    colors::NPC,
                );
            }
        }
    }
    if layers.shows(Layer::Exits) {
        for exit in overlay.exits.iter().filter(|exit| exit.is_used()) {
            if let Some(arrival) = exit.arrival {
                let at = draw_arrow(&painter, arrival, &to_screen, colors::ARRIVAL);
                painter.circle_stroke(at, 6.0, Stroke::new(2.0, colors::ARRIVAL));
                painter.text(
                    at + egui::vec2(0.0, 8.0),
                    egui::Align2::CENTER_TOP,
                    format!("↓{}", exit.exit),
                    egui::FontId::monospace(11.0),
                    colors::ARRIVAL,
                );
            }
            if let Some(label_at) = exit.label {
                label(
                    &painter,
                    to_screen(label_at),
                    &exit_label(exit.exit, &exit.target),
                    colors::EXIT_TEXT,
                );
            }
        }
    }
    let hits = if layers.shows(Layer::Events) {
        crate::events::draw(&painter, mapper, &to_screen)
    } else {
        Vec::new()
    };
    if let Some(at) = selected_point(mapper) {
        painter.circle_stroke(to_screen(at), 10.0, Stroke::new(2.5, colors::SELECTED));
    }
    if layers.shows(Layer::Live) {
        draw_live(&painter, &app.live, mapper, &to_screen);
    }
    crate::timeline::draw(&painter, app, &to_screen);
    crate::events::click(app, &response, &hits);
    if let Some(action) = context_menu(app, &response) {
        app.apply(action);
    }
}

/// Keeps the event under the cursor for the hover panel, which the cursor reaches after leaving
/// the canvas.
fn remember_event(app: &mut MapperApp) {
    let Some(hover) = app.mapper.hover else {
        return;
    };
    if let Some(event) = crate::events::event_at(&app.mapper, hover.cell) {
        app.last_event = Some((event.script, event.section, event.kind));
    }
}

/// Where the canvas's menu was opened: the ground point, the canvas point, the exit whose
/// trigger cell it is and the event that runs the cell (script, section).
#[derive(Clone, Copy, Debug)]
pub struct ContextPoint {
    pub world: glam::Vec3,
    pub canvas: Vec2,
    pub exit: Option<i32>,
    pub event: Option<(u16, u16)>,
}

fn context_point(mapper: &Mapper) -> Option<ContextPoint> {
    let hover = mapper.hover?;
    let exit = mapper
        .map
        .as_ref()
        .and_then(|map| map.overlay.exit_at(hover.value, &mapper.bands))
        .map(|mark| mark.exit);
    let event =
        crate::events::event_at(mapper, hover.cell).map(|event| (event.script, event.section));
    Some(ContextPoint {
        world: hover.world,
        canvas: mapper.view.to_canvas(hover.background),
        exit,
        event,
    })
}

/// The canvas's menu: add an NPC on the point, open the section of the event under it, and while
/// a game shows the map, put the player on the point or test the exit under it.
fn context_menu(app: &MapperApp, response: &egui::Response) -> Option<Action> {
    let point = app.context_point?;
    app.mapper.map.as_ref()?;
    let mut action = None;
    response.context_menu(|ui| {
        if ui.button(text::ADD_NPC_HERE).clicked() {
            action = Some(Action::AddNpc(Some(point.canvas)));
            ui.close();
        }
        if let Some((script, section)) = point.event
            && ui
                .button(format!("{} {section:#04x}", text::OPEN_SECTION))
                .clicked()
        {
            action = Some(Action::OpenSection { script, section });
            ui.close();
        }
        if app.mapper.timeline.shown && app.mapper.section.is_some() {
            ui.separator();
            for kind in [NewCue::Walk, NewCue::Turn, NewCue::CameraTo] {
                if ui.button(kind.label()).clicked() {
                    action = Some(Action::AddCue {
                        kind,
                        at: Some(point.canvas),
                    });
                    ui.close();
                }
            }
        }
        live_items(app, ui, point);
    });
    action
}

/// The menu's items while a game shows the map.
fn live_items(app: &MapperApp, ui: &mut egui::Ui, point: ContextPoint) {
    let Some(view) = app.live.shows(&app.mapper) else {
        return;
    };
    let rotation = view
        .player
        .map_or(0, |player| mapper_core::live::rotation_of_yaw(player.yaw));
    ui.separator();
    if ui.button(text::LIVE_PLACE_HERE).clicked() {
        let at = mapper_core::live::place_player(point.world, rotation);
        app.live.place(at, false);
        ui.close();
    }
    if let Some(exit) = point.exit {
        // The centre of the cell clicked, which has the exit's trigger.
        let cell = world::cell_center(world::cell_at(point.world));
        if ui
            .button(format!("{} {exit}", text::LIVE_TEST_EXIT))
            .on_hover_text(text::LIVE_TEST_EXIT_HINT)
            .clicked()
        {
            app.live
                .place(mapper_core::live::place_player(cell, rotation), true);
            ui.close();
        }
        if let Some(at) = app.mapper.place_at_arrival(exit)
            && ui
                .button(format!("{} {exit}", text::LIVE_TO_ARRIVAL))
                .clicked()
        {
            app.live.place(at, false);
            ui.close();
        }
    }
}

/// The game's characters where they are now, and the part of the background its window shows.
fn draw_live(
    painter: &egui::Painter,
    live: &crate::live::LivePanel,
    mapper: &Mapper,
    to_screen: &impl Fn(Vec2) -> Pos2,
) {
    let (Some(view), Some(map)) = (live.shows(mapper), &mapper.map) else {
        return;
    };
    if let Some(rect) = view.view {
        let corners = egui::Rect::from_min_max(
            to_screen(Vec2::new(rect.x, rect.y)),
            to_screen(Vec2::new(rect.x + rect.w, rect.y + rect.h)),
        );
        painter.rect_stroke(
            corners,
            0.0,
            Stroke::new(1.5, colors::LIVE_VIEW),
            egui::StrokeKind::Middle,
        );
    }
    let mut labels = LabelStack::default();
    let mut actor = |actor: &mapper_live::Actor, color: Color32, name: &str| {
        let from = glam::Vec3::new(actor.x, actor.y, actor.z);
        let [dx, dz] = actor.facing();
        let to = from + glam::Vec3::new(dx, 0.0, dz) * LIVE_ARROW;
        let project = |world| map.camera.project_to_background(world).map(|at| at.pixel);
        let (Some(from), Some(to)) = (project(from), project(to)) else {
            return;
        };
        let (from, to) = (to_screen(from), to_screen(to));
        painter.arrow(from, to - from, Stroke::new(2.5, color));
        painter.circle_filled(from, 6.0, color);
        painter.circle_stroke(from, 6.0, Stroke::new(1.5, Color32::BLACK));
        labels.place(painter, from, name, color);
    };
    for npc in &view.npcs {
        let name = format!("{} {}", text::LIVE_NPC, npc.id.unwrap_or(-1));
        actor(npc, colors::LIVE_NPC, &name);
    }
    if let Some(partner) = &view.partner {
        actor(partner, colors::LIVE_PARTNER, text::LIVE_PARTNER);
    }
    if let Some(player) = &view.player {
        actor(player, colors::LIVE_PLAYER, text::LIVE_PLAYER);
    }
}

/// The primary button's gesture, sent to the model: pressed, moved while held, released.
fn pointer_gesture(app: &mut MapperApp, ui: &egui::Ui, response: &egui::Response) {
    let origin = response.rect.min;
    let (pressed, released, moving, latest, keys) = ui.input(|input| {
        (
            input.pointer.primary_pressed(),
            input.pointer.primary_released(),
            // Moved this frame (`is_moving` reads the velocity, which a jump of the pointer
            // between two frames does not give).
            input.pointer.delta() != egui::Vec2::ZERO,
            input.pointer.latest_pos(),
            input.modifiers,
        )
    });
    let Some(latest) = latest else { return };
    let at = Vec2::new(latest.x - origin.x, latest.y - origin.y);
    if pressed && response.hovered() {
        app.pointer_held = true;
        let modifiers = Modifiers {
            shift: keys.shift,
            alt: keys.alt,
            ctrl: keys.command,
        };
        app.apply(Action::PointerDown { at, modifiers });
    } else if app.pointer_held && moving {
        app.apply(Action::PointerMove(at));
    }
    if released && app.pointer_held {
        app.pointer_held = false;
        app.apply(Action::PointerUp);
    }
}

/// Where the selection is drawn: an exit's arrival, an NPC, a point of its path, a door or a
/// toilet point.
fn selected_point(mapper: &Mapper) -> Option<Vec2> {
    let overlay = &mapper.map.as_ref()?.overlay;
    match mapper.selection? {
        Selection::Exit(exit) => overlay
            .exits
            .iter()
            .find(|mark| mark.exit == exit)?
            .arrival
            .map(|arrow| arrow.from),
        Selection::Npc(slot) => overlay
            .npcs
            .iter()
            .find(|npc| npc.slot == slot)?
            .position
            .map(|arrow| arrow.from),
        Selection::Waypoint { slot, part } => {
            let npc = overlay.npcs.iter().find(|npc| npc.slot == slot)?;
            let index = npc.parts.iter().position(|&known| known == part)?;
            npc.waypoints.get(index).copied()
        }
        Selection::Door(slot) => overlay
            .doors
            .iter()
            .find(|door| door.slot == slot)
            .map(|door| door.position.from),
        Selection::Toilet(index) => overlay.toilet.map(|points| points[index.min(1)]),
    }
}

/// What the tool will paint: the brush under the cursor, or the rectangle being dragged.
fn draw_tool_preview(painter: &egui::Painter, mapper: &Mapper, to_screen: &impl Fn(Vec2) -> Pos2) {
    let Some(map) = &mapper.map else { return };
    let region = match (mapper.drag, mapper.tool, mapper.hover) {
        (Some(Drag::Rectangle { from, to }), _, _) => Some((from, to)),
        (None, Tool::Brush, Some(hover)) => {
            let half = mapper.brush_size / 2;
            let cell = hover.cell;
            Some((
                Cell::new(cell.column - half, cell.row - half),
                Cell::new(cell.column + half, cell.row + half),
            ))
        }
        _ => None,
    };
    let Some((from, to)) = region else { return };
    let min = Cell::new(from.column.min(to.column), from.row.min(to.row));
    let max = Cell::new(from.column.max(to.column), from.row.max(to.row));
    let low = world::cell_min(min);
    let high = world::cell_min(max) + glam::Vec3::new(world::CELL_SIZE, 0.0, world::CELL_SIZE);
    let corners = [
        low,
        glam::Vec3::new(high.x, 0.0, low.z),
        high,
        glam::Vec3::new(low.x, 0.0, high.z),
    ];
    let points: Option<Vec<Pos2>> = corners
        .iter()
        .map(|&corner| {
            map.camera
                .project_to_background(corner)
                .map(|point| to_screen(point.pixel))
        })
        .collect();
    if let Some(points) = points {
        painter.add(Shape::convex_polygon(
            points,
            colors::PREVIEW,
            Stroke::new(1.5, colors::SELECTED),
        ));
    }
}

/// The text of an exit: its number and where it leads.
pub fn exit_label(exit: i32, target: &ExitTarget) -> String {
    match target {
        ExitTarget::Map { id, exit: to } => format!("{exit} → {id}:{to}"),
        ExitTarget::Fallback { id, exit: to } => format!("{exit} → {id}:{to} (?)"),
        ExitTarget::Conflict => format!("{exit} → {}", text::CONFLICT),
        ExitTarget::Nowhere => format!("{exit} → {}", text::NOWHERE),
    }
}

/// The fill of a cell, or None when its layer is hidden.
fn cell_color(mapper: &Mapper, kind: CellKind, band: Option<TriggerBand>) -> Option<Color32> {
    let layers = mapper.layers;
    match kind {
        CellKind::Free => None,
        CellKind::Blocked => layers.shows(Layer::Walls).then_some(colors::WALL),
        CellKind::Trigger(_) => {
            let color = match band? {
                TriggerBand::Step => colors::STEP,
                TriggerBand::Confirm => colors::CONFIRM,
                TriggerBand::Exit(_) => colors::EXIT,
                TriggerBand::Toilet => colors::TOILET,
                TriggerBand::Other => colors::OTHER,
            };
            let shown = layers.shows(Layer::Triggers)
                || (matches!(band, Some(TriggerBand::Exit(_))) && layers.shows(Layer::Exits));
            shown.then_some(color)
        }
    }
}

/// Every non-free cell as one mesh.
fn draw_cells(painter: &egui::Painter, mapper: &Mapper, to_screen: &impl Fn(Vec2) -> Pos2) {
    let Some(map) = &mapper.map else { return };
    let mut mesh = egui::Mesh::default();
    for shape in &map.overlay.cells {
        let Some(color) = cell_color(mapper, shape.kind, shape.band) else {
            continue;
        };
        let first = mesh.vertices.len() as u32;
        for corner in shape.quad {
            mesh.colored_vertex(to_screen(corner), color);
        }
        mesh.add_triangle(first, first + 1, first + 2);
        mesh.add_triangle(first, first + 2, first + 3);
    }
    painter.add(Shape::mesh(mesh));
}

/// The outline of the cell under the cursor.
fn draw_hover(painter: &egui::Painter, mapper: &Mapper, to_screen: &impl Fn(Vec2) -> Pos2) {
    let (Some(map), Some(hover)) = (&mapper.map, mapper.hover) else {
        return;
    };
    if !hover.cell.is_inside() {
        return;
    }
    let corners = cell_corners(hover.cell);
    let points: Option<Vec<Pos2>> = corners
        .iter()
        .map(|&corner| {
            map.camera
                .project_to_background(corner)
                .map(|point| to_screen(point.pixel))
        })
        .collect();
    if let Some(points) = points {
        painter.add(Shape::closed_line(points, Stroke::new(1.5, colors::HOVER)));
    }
}

/// The corners of a cell on the ground: -z edge first, then +x, +z and -x.
pub fn cell_corners(cell: Cell) -> [glam::Vec3; 4] {
    let min = world::cell_min(cell);
    let size = world::CELL_SIZE;
    [
        min,
        min + glam::Vec3::new(size, 0.0, 0.0),
        min + glam::Vec3::new(size, 0.0, size),
        min + glam::Vec3::new(0.0, 0.0, size),
    ]
}

/// A facing arrow; returns where it starts on the screen.
fn draw_arrow(
    painter: &egui::Painter,
    arrow: Arrow,
    to_screen: &impl Fn(Vec2) -> Pos2,
    color: Color32,
) -> Pos2 {
    let from = to_screen(arrow.from);
    let to = to_screen(arrow.to);
    painter.arrow(from, to - from, Stroke::new(2.0, color));
    from
}

pub fn diamond(center: Pos2, radius: f32, color: Color32) -> Shape {
    Shape::convex_polygon(
        vec![
            center + egui::vec2(0.0, -radius),
            center + egui::vec2(radius, 0.0),
            center + egui::vec2(0.0, radius),
            center + egui::vec2(-radius, 0.0),
        ],
        color,
        Stroke::NONE,
    )
}

/// Text on a dark box, above a point.
fn label(painter: &egui::Painter, at: Pos2, text: &str, color: Color32) {
    label_at(painter, at, text, color);
}

/// The box of a label above a point.
pub fn label_rect(painter: &egui::Painter, at: Pos2, text: &str, color: Color32) -> egui::Rect {
    let galley = painter.layout_no_wrap(text.to_owned(), egui::FontId::monospace(11.0), color);
    egui::Rect::from_center_size(
        at - egui::vec2(0.0, 14.0),
        galley.size() + egui::vec2(6.0, 2.0),
    )
}

/// Text on a dark box in a rectangle.
pub fn label_in(painter: &egui::Painter, rect: egui::Rect, text: &str, color: Color32) {
    let galley = painter.layout_no_wrap(text.to_owned(), egui::FontId::monospace(11.0), color);
    painter.rect_filled(rect, 2.0, colors::LABEL_BACK);
    painter.galley(rect.min + egui::vec2(3.0, 1.0), galley, color);
}

/// Text on a dark box, above a point; returns its box.
pub fn label_at(painter: &egui::Painter, at: Pos2, text: &str, color: Color32) -> egui::Rect {
    let rect = label_rect(painter, at, text, color);
    label_in(painter, rect, text, color);
    rect
}

/// Text on a dark box that starts at a point (its left middle); returns its box.
pub fn label_left(painter: &egui::Painter, at: Pos2, text: &str, color: Color32) -> egui::Rect {
    let size = label_rect(painter, at, text, color).size();
    let rect = egui::Rect::from_min_size(at - egui::vec2(0.0, size.y * 0.5), size);
    label_in(painter, rect, text, color);
    rect
}

/// Labels that move up out of each other's way (the game's characters often stand together).
#[derive(Default)]
struct LabelStack {
    placed: Vec<egui::Rect>,
}

impl LabelStack {
    /// The most a label moves up before it is drawn where it overlaps.
    const MOST_STEPS: usize = 6;

    fn place(&mut self, painter: &egui::Painter, at: Pos2, text: &str, color: Color32) {
        let mut rect = label_rect(painter, at, text, color);
        for _ in 0..Self::MOST_STEPS {
            if !self.placed.iter().any(|known| known.intersects(rect)) {
                break;
            }
            rect = rect.translate(egui::vec2(0.0, -(rect.height() + 1.0)));
        }
        label_in(painter, rect, text, color);
        self.placed.push(rect);
    }
}
