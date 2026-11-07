//! The story's events of the map shown: the Eventos layer on the canvas (the cells that run a
//! section, the NPCs' talk sections and the map changes the sections make) and the events panel.
//! A click on an event opens its section in the section editor; a click on a map change opens
//! the map it leads to.

use std::collections::HashSet;

use dwe_map::world::{self, Cell};
use dwe_story::describe::Names;
use dwe_story::events::Jump;
use dwe_story::{Event, EventKind};
use eframe::egui::{self, Color32, Pos2, Rect, Stroke};
use glam::Vec2;
use mapper_core::{Action, Layer, Mapper, OpenMap, Selection, Tool, text};

use crate::app::MapperApp;

/// Below this zoom the layer draws the cells but not their labels.
const LABEL_ZOOM: f32 = 0.45;
/// The longest a map change's arrow gets, in screen pixels.
const JUMP_ARROW: f32 = 90.0;
/// The gap between stacked labels.
const STACK_GAP: f32 = 18.0;
/// How far inside the canvas's edges a map change's label stays.
const JUMP_MARGIN: f32 = 40.0;
/// Map change arrows that end this near each other (screen pixels) share one label.
const PILE_RADIUS: f32 = 6.0;
/// The most map changes a shared label's tooltip lists.
const MOST_PILE_LINES: usize = 12;

pub mod colors {
    use eframe::egui::Color32;

    pub const STEP: Color32 = Color32::from_rgb(255, 215, 70);
    pub const CONFIRM: Color32 = Color32::from_rgb(90, 200, 255);
    pub const MISSING: Color32 = Color32::from_rgb(255, 110, 70);
    pub const TALK: Color32 = Color32::from_rgb(255, 190, 120);
    pub const JUMP: Color32 = Color32::from_rgb(205, 165, 255);
    pub const ENTRY: Color32 = Color32::from_rgb(200, 230, 200);
}

/// What the story's descriptions look names up in, while content is open.
pub fn names(mapper: &Mapper) -> Option<Names<'_>> {
    let content = mapper.content.as_ref()?;
    Some(Names {
        story: &mapper.story_names,
        texts: &mapper.story.texts,
        maps: &content.table,
    })
}

/// The colour of an event: by kind, or the warning colour when its section is missing.
pub fn event_color(event: &Event) -> Color32 {
    if !event.exists {
        return colors::MISSING;
    }
    match event.kind {
        EventKind::Step => colors::STEP,
        EventKind::Confirm => colors::CONFIRM,
        EventKind::Talk { .. } => colors::TALK,
        EventKind::MapEntry | EventKind::MapLoaded => colors::ENTRY,
    }
}

/// An event's section as its label shows it: `0x33`, with `*` when a patch gives it.
fn section_tag(event: &Event) -> String {
    let patched = if event.patched_by.is_some() { "*" } else { "" };
    format!("{:#04x}{patched}", event.section)
}

/// A map change label on the canvas, to click.
pub struct JumpHit {
    pub rect: Rect,
    pub map: String,
    pub exit: i32,
    pub tooltip: String,
}

/// Draws the layer; returns the map change labels, for clicks and tooltips.
pub fn draw(
    painter: &egui::Painter,
    mapper: &Mapper,
    to_screen: &impl Fn(Vec2) -> Pos2,
) -> Vec<JumpHit> {
    let Some(map) = &mapper.map else {
        return Vec::new();
    };
    let names = names(mapper);
    let labels = mapper.view.zoom >= LABEL_ZOOM;
    let mut hits = Vec::new();
    let mut stacked = 0;
    let mut piles: Vec<Pile> = Vec::new();
    for event in &map.events.events {
        let color = event_color(event);
        if !event.cells.is_empty() {
            draw_group(painter, map, &event.cells, color, to_screen);
        }
        if !labels {
            continue;
        }
        let anchor = match event.kind {
            EventKind::Step | EventKind::Confirm => event
                .centre_cell()
                .and_then(|cell| project(map, world::cell_center(cell)))
                .map(to_screen),
            EventKind::Talk { slot } => talk_badge(painter, map, slot, event, to_screen),
            EventKind::MapEntry | EventKind::MapLoaded => None,
        };
        let on_map = anchor.is_some();
        let anchor = anchor.unwrap_or_else(|| {
            stacked += 1;
            to_screen(Vec2::ZERO) + egui::vec2(8.0, stacked as f32 * STACK_GAP)
        });
        if on_map {
            if !matches!(event.kind, EventKind::Talk { .. }) {
                crate::canvas::label_at(painter, anchor, &section_tag(event), color);
            }
        } else {
            let tag = format!("{} {}", text::event_kind(event.kind), section_tag(event));
            crate::canvas::label_left(painter, anchor, &tag, color);
        }
        // The labels stay inside the canvas, one under the other.
        let area = painter.clip_rect().shrink(JUMP_MARGIN);
        for jump in &event.jumps {
            let Some(target) = &jump.to_map else { continue };
            let Ok(exit) = i32::try_from(jump.exit) else {
                continue;
            };
            let tooltip = jump_tooltip(names.as_ref(), jump);
            if on_map {
                let toward = jump_direction(map, mapper, anchor, target, to_screen);
                let tip = area.clamp(anchor + toward * JUMP_ARROW);
                painter.arrow(anchor, tip - anchor, Stroke::new(1.5, colors::JUMP));
                let label = text::jump_label(jump);
                match piles
                    .iter_mut()
                    .find(|pile| pile.tip.distance(tip) <= PILE_RADIUS)
                {
                    Some(pile) => pile.jumps.push((label, target.clone(), exit, tooltip)),
                    None => piles.push(Pile {
                        tip,
                        jumps: vec![(label, target.clone(), exit, tooltip)],
                    }),
                }
            } else {
                stacked += 1;
                let at = to_screen(Vec2::ZERO) + egui::vec2(24.0, stacked as f32 * STACK_GAP);
                let rect =
                    crate::canvas::label_left(painter, at, &text::jump_label(jump), colors::JUMP);
                hits.push(JumpHit {
                    rect,
                    map: target.clone(),
                    exit,
                    tooltip,
                });
            }
        }
    }
    // The map changes whose arrows end at one point show as one label with their count.
    for pile in piles {
        let labels: Vec<&str> = pile
            .jumps
            .iter()
            .map(|(label, ..)| label.as_str())
            .collect();
        let rect =
            crate::canvas::label_at(painter, pile.tip, &text::jump_pile(&labels), colors::JUMP);
        let (_, map, exit, _) = &pile.jumps[0];
        let tooltip = if pile.jumps.len() == 1 {
            pile.jumps[0].3.clone()
        } else {
            pile.jumps
                .iter()
                .take(MOST_PILE_LINES)
                .map(|(label, _, _, tooltip)| format!("{label}\n{tooltip}"))
                .chain((pile.jumps.len() > MOST_PILE_LINES).then(|| "…".to_owned()))
                .collect::<Vec<_>>()
                .join("\n\n")
        };
        hits.push(JumpHit {
            rect,
            map: map.clone(),
            exit: *exit,
            tooltip,
        });
    }
    hits
}

/// Map change labels whose arrows end at one point: each one's label, map, exit and tooltip.
struct Pile {
    tip: Pos2,
    jumps: Vec<(String, String, i32, String)>,
}

/// The badge of an NPC's talk section, beside its marker; returns where the marker is.
fn talk_badge(
    painter: &egui::Painter,
    map: &OpenMap,
    slot: usize,
    event: &Event,
    to_screen: &impl Fn(Vec2) -> Pos2,
) -> Option<Pos2> {
    let npc = map.overlay.npcs.iter().find(|npc| npc.slot == slot)?;
    let at = to_screen(npc.position?.from);
    let badge = format!("§{}", section_tag(event));
    let galley = painter.layout_no_wrap(badge, egui::FontId::monospace(11.0), event_color(event));
    let rect = Rect::from_min_size(
        at + egui::vec2(8.0, 2.0),
        galley.size() + egui::vec2(6.0, 2.0),
    );
    painter.rect_filled(rect, 2.0, crate::canvas::colors::LABEL_BACK);
    painter.galley(rect.min + egui::vec2(3.0, 1.0), galley, event_color(event));
    Some(at)
}

/// The conditions on a map change's way, for its tooltip.
fn jump_tooltip(names: Option<&Names<'_>>, jump: &Jump) -> String {
    let when = if jump.guards.is_empty() {
        text::JUMP_ALWAYS.to_owned()
    } else {
        let guards: Vec<String> = jump
            .guards
            .iter()
            .map(|guard| {
                names.map_or_else(
                    || dwe_story::write::expr_text(guard),
                    |n| n.condition(guard),
                )
            })
            .collect();
        format!("{}: {}", text::JUMP_WHEN, guards.join(" y "))
    };
    format!(
        "{when}\n{}\n{}",
        dwe_story::events::guards_text(&jump.guards),
        text::JUMP_HINT
    )
}

/// Which way a map change's arrow points: to the map's exit that leads to the same map, else
/// to the nearest edge of the background.
fn jump_direction(
    map: &OpenMap,
    mapper: &Mapper,
    from: Pos2,
    target: &str,
    to_screen: &impl Fn(Vec2) -> Pos2,
) -> egui::Vec2 {
    let exit_label = map
        .overlay
        .exits
        .iter()
        .filter(|exit| {
            exit.target
                .destination()
                .is_some_and(|(id, _)| id == target)
        })
        .find_map(|exit| exit.label);
    let point = match exit_label {
        Some(label) => to_screen(label),
        None => {
            let size = map.background_size();
            let at = mapper
                .view
                .to_background(Vec2::new(from.x, from.y) - screen_origin(mapper, to_screen));
            let edges = [
                (at.x, Vec2::new(0.0, at.y)),
                (size.x - at.x, Vec2::new(size.x, at.y)),
                (at.y, Vec2::new(at.x, 0.0)),
                (size.y - at.y, Vec2::new(at.x, size.y)),
            ];
            let nearest = edges
                .iter()
                .min_by(|a, b| a.0.total_cmp(&b.0))
                .map_or(Vec2::ZERO, |edge| edge.1);
            to_screen(nearest)
        }
    };
    let toward = point - from;
    if toward.length() < 1.0 {
        egui::vec2(1.0, 0.0)
    } else {
        toward.normalized()
    }
}

/// Where the canvas starts on the screen.
fn screen_origin(mapper: &Mapper, to_screen: &impl Fn(Vec2) -> Pos2) -> Vec2 {
    let canvas = mapper.view.to_canvas(Vec2::ZERO);
    let screen = to_screen(Vec2::ZERO);
    Vec2::new(screen.x - canvas.x, screen.y - canvas.y)
}

fn project(map: &OpenMap, world: glam::Vec3) -> Option<Vec2> {
    map.camera
        .project_to_background(world)
        .map(|point| point.pixel)
}

/// A group of cells: a light fill, and its outline where a cell has no neighbour of the group.
fn draw_group(
    painter: &egui::Painter,
    map: &OpenMap,
    cells: &[Cell],
    color: Color32,
    to_screen: &impl Fn(Vec2) -> Pos2,
) {
    let group: HashSet<Cell> = cells.iter().copied().collect();
    let fill = color.gamma_multiply(0.25);
    let stroke = Stroke::new(1.5, color);
    for &cell in cells {
        let corners = crate::canvas::cell_corners(cell);
        let Some(points) = corners
            .iter()
            .map(|&corner| project(map, corner).map(to_screen))
            .collect::<Option<Vec<Pos2>>>()
        else {
            continue;
        };
        painter.add(egui::Shape::convex_polygon(
            points.clone(),
            fill,
            Stroke::NONE,
        ));
        // The corners go -z edge, +x edge, +z edge, -x edge.
        let neighbours = [
            Cell::new(cell.column, cell.row - 1),
            Cell::new(cell.column + 1, cell.row),
            Cell::new(cell.column, cell.row + 1),
            Cell::new(cell.column - 1, cell.row),
        ];
        for (edge, neighbour) in neighbours.iter().enumerate() {
            if !group.contains(neighbour) {
                painter.line_segment([points[edge], points[(edge + 1) % 4]], stroke);
            }
        }
    }
}

/// A click on the canvas with Seleccionar and the layer shown: a map change label opens its
/// map; an event's cell (where nothing was picked) opens its section.
pub fn click(app: &mut MapperApp, response: &egui::Response, hits: &[JumpHit]) {
    if app.mapper.tool != Tool::Select || !app.mapper.layers.shows(Layer::Events) {
        return;
    }
    let pointer = response.hover_pos();
    if let Some(hit) = pointer.and_then(|at| hits.iter().find(|hit| hit.rect.contains(at))) {
        response.clone().on_hover_text_at_pointer(&hit.tooltip);
        if response.clicked() {
            app.apply(Action::OpenMapAtExit {
                map: hit.map.clone(),
                exit: hit.exit,
            });
        }
        return;
    }
    if !response.clicked() || app.mapper.selection.is_some() {
        return;
    }
    let event = app.mapper.hover.and_then(|hover| {
        let map = app.mapper.map.as_ref()?;
        map.events.at_cell(hover.cell)
    });
    if let Some(event) = event {
        let (script, section) = (event.script, event.section);
        app.apply(Action::OpenSection { script, section });
    }
}

/// The event that runs a cell of the map shown.
pub fn event_at(mapper: &Mapper, cell: Cell) -> Option<&Event> {
    mapper.map.as_ref()?.events.at_cell(cell)
}

/// Centres the view on a point of the background.
pub fn center_on(app: &mut MapperApp, background: Vec2) {
    let view = app.mapper.view;
    let delta = view.canvas * 0.5 - view.to_canvas(background);
    app.apply(Action::Pan(delta));
}

/// Opens an event's section and shows where it runs.
pub fn open_event(app: &mut MapperApp, event: &Event) {
    app.apply(Action::OpenSection {
        script: event.script,
        section: event.section,
    });
    match event.kind {
        EventKind::Talk { slot } => {
            app.apply(Action::Select(Some(Selection::Npc(slot))));
            app.apply(Action::CenterSelection);
        }
        EventKind::Step | EventKind::Confirm => {
            let point = app.mapper.map.as_ref().and_then(|map| {
                let cell = event.centre_cell()?;
                project(map, world::cell_center(cell))
            });
            if let Some(point) = point {
                center_on(app, point);
            }
        }
        EventKind::MapEntry | EventKind::MapLoaded => {}
    }
}

/// Whether an event's row matches the panel's filter (kind, script or section).
fn matches_filter(filter: &str, event: &Event) -> bool {
    let filter = filter.trim().to_lowercase();
    if filter.is_empty() {
        return true;
    }
    let words = format!(
        "{} {} {:#04x} {}",
        text::event_kind(event.kind).to_lowercase(),
        event.script,
        event.section,
        event.section
    );
    words.contains(&filter)
}

/// The events panel: each event of the map shown with its section, marks and map changes.
pub fn panel(app: &mut MapperApp, ui: &mut egui::Ui) {
    let count = app.mapper.events().len();
    let mut open = None;
    let mut test = None;
    egui::CollapsingHeader::new(format!("{} ({count})", text::EVENTS))
        .id_salt("events")
        .default_open(true)
        .show(ui, |ui| {
            ui.add(
                egui::TextEdit::singleline(&mut app.events_filter)
                    .hint_text(text::EVENTS_FILTER_HINT),
            );
            if count == 0 {
                ui.weak(text::EVENTS_NONE);
                return;
            }
            let connected = app.live.is_connected();
            let shown = app.mapper.section.as_ref();
            egui::Grid::new("events")
                .striped(true)
                .num_columns(3)
                .show(ui, |ui| {
                    for (index, event) in app.mapper.events().iter().enumerate() {
                        if !matches_filter(&app.events_filter, event) {
                            continue;
                        }
                        let chosen = shown.is_some_and(|open| {
                            open.script == event.script && open.section == event.section
                        });
                        let mut line = format!(
                            "{} · {}:{}",
                            text::event_kind(event.kind),
                            event.script,
                            section_tag(event)
                        );
                        if !event.exists {
                            line.push_str(" · ");
                            line.push_str(text::SECTION_MISSING);
                        }
                        let mut label = egui::RichText::new(line);
                        if !event.exists {
                            label = label.color(colors::MISSING);
                        }
                        let mut hint = text::EVENT_OPEN_HINT.to_owned();
                        if let Some(patch) = &event.patched_by {
                            hint = format!("{} {patch}\n{hint}", text::EVENT_PATCHED);
                        }
                        if !event.exists {
                            hint = format!("{}\n{hint}", text::EVENT_MISSING);
                        }
                        if ui
                            .selectable_label(chosen, label)
                            .on_hover_text(hint)
                            .clicked()
                        {
                            open = Some(index);
                        }
                        if event.jumps.is_empty() {
                            ui.label("");
                        } else {
                            let jumps: Vec<String> =
                                event.jumps.iter().map(text::jump_label).collect();
                            ui.weak(text::jump_count(event.jumps.len()))
                                .on_hover_text(jumps.join("\n"));
                        }
                        if ui
                            .add_enabled(connected, egui::Button::new(text::TEST_EVENT).small())
                            .on_hover_text(text::TEST_EVENT_HINT)
                            .on_disabled_hover_text(text::TEST_EVENT_OFFLINE)
                            .clicked()
                        {
                            test = Some(index);
                        }
                        ui.end_row();
                    }
                });
        });
    if let Some(event) = open.and_then(|index| app.mapper.events().get(index).cloned()) {
        open_event(app, &event);
    }
    if let Some(event) = test.and_then(|index| app.mapper.events().get(index).cloned()) {
        crate::probe::ask(app, &event);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn event(kind: EventKind, section: u16) -> Event {
        Event {
            kind,
            script: 12,
            section,
            cells: Vec::new(),
            exists: true,
            patched_by: None,
            jumps: Vec::new(),
        }
    }

    #[test]
    fn the_filter_reads_kind_script_and_section() {
        let step = event(EventKind::Step, 0x33);
        assert!(matches_filter("", &step));
        assert!(matches_filter("pisar", &step));
        assert!(matches_filter("0x33", &step));
        assert!(matches_filter("51", &step));
        assert!(!matches_filter("botón", &step));
        assert_eq!(section_tag(&step), "0x33");
    }
}
