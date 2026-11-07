//! The Mundo view: every map as a node, coloured by its area, with the exits (lines) and script
//! warps (dashed) between them, the checks as badges and in a side list, and the game's map and
//! trail when a game is linked. Maps are dragged to new places, double-clicked to open, and
//! connected by dragging from one to another with Shift (or the right button).

use std::collections::{BTreeMap, BTreeSet};

use eframe::egui::{self, Color32, Pos2, Rect, Shape, Stroke, Vec2};
use mapper_core::{Action, Connect, EdgeKind, Selection, Severity, WorldEdge, text};

use crate::app::MapperApp;

const NODE_RADIUS: f32 = 7.0;
/// The zoom from which every map shows its name.
const LABEL_ZOOM: f32 = 0.9;
const WHEEL_ZOOM: f32 = 0.0015;

/// What the view keeps between frames.
#[derive(Debug)]
pub struct WorldUi {
    /// Whether the central area shows the world instead of the map.
    pub shown: bool,
    zoom: f32,
    pan: Vec2,
    /// Fits the graph into the view on the next frame.
    fit: bool,
    pub search: String,
    hidden_areas: BTreeSet<i32>,
    pub show_exits: bool,
    pub show_scripts: bool,
    show_errors: bool,
    show_warnings: bool,
    show_notes: bool,
    /// A map being dragged to a new place, and where it is now.
    moving: Option<(String, glam::Vec2)>,
    /// A drag from a map toward another one, to connect them.
    linking: Option<String>,
    /// The connection being set up: the two maps, their exits and both ways or not.
    pub connect: Option<ConnectForm>,
    /// The map picked last (a click or the checks list).
    selected: Option<String>,
    /// A map to centre and pick once the layout is known (`--world --map <id>`).
    focus: Option<String>,
}

impl Default for WorldUi {
    fn default() -> Self {
        Self {
            shown: false,
            zoom: 1.0,
            pan: Vec2::ZERO,
            fit: true,
            search: String::new(),
            hidden_areas: BTreeSet::new(),
            show_exits: true,
            show_scripts: true,
            show_errors: true,
            show_warnings: true,
            show_notes: false,
            moving: None,
            linking: None,
            connect: None,
            selected: None,
            focus: None,
        }
    }
}

impl WorldUi {
    pub fn showing(shown: bool) -> Self {
        Self {
            shown,
            ..Self::default()
        }
    }

    /// Centres the view on a map, close up, and picks it.
    pub fn focus_on(&mut self, map: String) {
        self.focus = Some(map);
    }
}

/// The "connect" window.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConnectForm {
    pub from: String,
    pub exit: i32,
    pub to: String,
    pub to_exit: i32,
    pub back: bool,
}

/// The zoom of a map the view was asked to centre on.
const FOCUS_ZOOM: f32 = 1.6;

/// An area's colour: hues a golden angle apart.
pub fn area_color(area: i32) -> Color32 {
    let hue = (area.max(0) as f32 * 0.618_034).fract();
    let color = egui::ecolor::Hsva::new(hue, 0.55, 0.85, 1.0);
    Color32::from(color)
}

fn severity_color(severity: Severity) -> Color32 {
    match severity {
        Severity::Error => Color32::from_rgb(235, 70, 60),
        Severity::Warning => Color32::from_rgb(240, 170, 40),
        Severity::Info => Color32::from_rgb(120, 160, 230),
    }
}

/// The screen place of a graph point and back.
#[derive(Clone, Copy)]
struct Frame {
    centre: Pos2,
    pan: Vec2,
    zoom: f32,
}

impl Frame {
    fn to_screen(self, point: glam::Vec2) -> Pos2 {
        self.centre + self.pan + Vec2::new(point.x, point.y) * self.zoom
    }

    fn to_graph(self, pos: Pos2) -> glam::Vec2 {
        let local = (pos - self.centre - self.pan) / self.zoom;
        glam::Vec2::new(local.x, local.y)
    }
}

pub fn show(app: &mut MapperApp, ui: &mut egui::Ui) {
    if app.mapper.content.is_none() {
        ui.centered_and_justified(|ui| ui.label(text::NO_CONTENT));
        return;
    }
    if app.mapper.refresh_world() {
        app.world_ui.fit |= app.world_ui.moving.is_none() && app.world_ui.zoom == 1.0;
    }
    toolbar(app, ui);
    checks_panel(app, ui);
    canvas(app, ui);
}

/// Search, filters and layout buttons over the graph.
fn toolbar(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::top("world_tools").show(ui, |ui| {
        ui.horizontal_wrapped(|ui| {
            let world = &mut app.world_ui;
            let response = ui.add(
                egui::TextEdit::singleline(&mut world.search)
                    .hint_text(text::WORLD_SEARCH_HINT)
                    .desired_width(180.0),
            );
            if response.lost_focus() && ui.input(|input| input.key_pressed(egui::Key::Enter)) {
                let query = world.search.trim().to_lowercase();
                if let Some(node) = app
                    .mapper
                    .world
                    .graph
                    .nodes
                    .iter()
                    .find(|node| matches(node, &query))
                {
                    world.selected = Some(node.id.clone());
                    if let Some(place) = app.mapper.world.layout.get(&node.id) {
                        world.pan = -Vec2::new(place.x, place.y) * world.zoom;
                    }
                }
            }
            ui.checkbox(&mut world.show_exits, text::WORLD_EXITS);
            ui.checkbox(&mut world.show_scripts, text::WORLD_SCRIPT_WARPS);
            let areas = app.mapper.world.graph.areas();
            ui.menu_button(text::WORLD_AREAS, |ui| {
                ui.horizontal(|ui| {
                    if ui.button(text::WORLD_ALL).clicked() {
                        world.hidden_areas.clear();
                    }
                    if ui.button(text::WORLD_NONE).clicked() {
                        world.hidden_areas = areas.keys().copied().collect();
                    }
                });
                egui::ScrollArea::vertical()
                    .max_height(420.0)
                    .show(ui, |ui| {
                        for (area, (name, maps)) in &areas {
                            let mut shown = !world.hidden_areas.contains(area);
                            let label = egui::RichText::new(format!("{name} ({})", maps.len()))
                                .color(area_color(*area));
                            if ui.checkbox(&mut shown, label).changed() {
                                if shown {
                                    world.hidden_areas.remove(area);
                                } else {
                                    world.hidden_areas.insert(*area);
                                }
                            }
                        }
                    });
            });
            if ui.button(text::FIT_VIEW).clicked() {
                world.fit = true;
            }
            let mut reset = false;
            if ui
                .button(text::WORLD_RELAYOUT)
                .on_hover_text(text::WORLD_RELAYOUT_HINT)
                .clicked()
            {
                reset = true;
                world.fit = true;
            }
            let graph = &app.mapper.world.graph;
            ui.separator();
            ui.weak(text::world_stats(
                graph.nodes.len(),
                graph.edges.iter().filter(|edge| edge.is_exit()).count(),
                graph.edges.iter().filter(|edge| !edge.is_exit()).count(),
            ));
            if reset {
                app.apply(Action::ResetWorldLayout);
            }
        });
    });
}

fn matches(node: &mapper_core::WorldNode, query: &str) -> bool {
    !query.is_empty()
        && (node.id.contains(query)
            || node.area.to_lowercase().contains(query)
            || node
                .title
                .as_ref()
                .is_some_and(|title| title.to_lowercase().contains(query)))
}

/// The checks of the graph, by severity; a click centres the map.
fn checks_panel(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::right("world_checks")
        .default_size(320.0)
        .show(ui, |ui| {
            let graph = &app.mapper.world.graph;
            let world = &mut app.world_ui;
            ui.heading(text::WORLD_CHECKS);
            let count = |severity| {
                graph
                    .checks
                    .iter()
                    .filter(|check| check.severity == severity)
                    .count()
            };
            ui.horizontal_wrapped(|ui| {
                for (shown, severity, label) in [
                    (&mut world.show_errors, Severity::Error, text::WORLD_ERRORS),
                    (
                        &mut world.show_warnings,
                        Severity::Warning,
                        text::WORLD_WARNINGS,
                    ),
                    (&mut world.show_notes, Severity::Info, text::WORLD_NOTES),
                ] {
                    let label = egui::RichText::new(format!("{label} ({})", count(severity)))
                        .color(severity_color(severity));
                    ui.checkbox(shown, label);
                }
            });
            if graph.checks.is_empty() {
                ui.weak(text::WORLD_NO_CHECKS);
            }
            ui.separator();
            let mut centre = None;
            egui::ScrollArea::vertical().show(ui, |ui| {
                for check in &graph.checks {
                    let shown = match check.severity {
                        Severity::Error => world.show_errors,
                        Severity::Warning => world.show_warnings,
                        Severity::Info => world.show_notes,
                    };
                    if !shown {
                        continue;
                    }
                    let label =
                        egui::RichText::new(check.message()).color(severity_color(check.severity));
                    if ui
                        .add(egui::Label::new(label).sense(egui::Sense::click()).wrap())
                        .on_hover_text(text::WORLD_CHECK_HINT)
                        .clicked()
                    {
                        centre = Some(check.map.clone());
                    }
                }
            });
            if let Some(map) = centre {
                if let Some(place) = app.mapper.world.layout.get(&map) {
                    world.pan = -Vec2::new(place.x, place.y) * world.zoom;
                }
                world.selected = Some(map);
            }
        });
}

/// Whether an edge is drawn with the filters set.
fn edge_shown(world: &WorldUi, graph_area: impl Fn(&str) -> Option<i32>, edge: &WorldEdge) -> bool {
    let kind = if edge.is_exit() {
        world.show_exits
    } else {
        world.show_scripts
    };
    let area_shown =
        |map: &str| graph_area(map).is_some_and(|area| !world.hidden_areas.contains(&area));
    kind && area_shown(&edge.from) && area_shown(&edge.to)
}

/// The lines between two maps: one per pair and kind, with arrows where they lead.
struct Link<'a> {
    a: &'a str,
    b: &'a str,
    script: bool,
    a_to_b: Vec<&'a WorldEdge>,
    b_to_a: Vec<&'a WorldEdge>,
}

fn links<'a>(edges: impl Iterator<Item = &'a WorldEdge>) -> Vec<Link<'a>> {
    let mut links: BTreeMap<(&str, &str, bool), Link<'a>> = BTreeMap::new();
    for edge in edges {
        if edge.from == edge.to {
            continue;
        }
        let (a, b) = if edge.from < edge.to {
            (edge.from.as_str(), edge.to.as_str())
        } else {
            (edge.to.as_str(), edge.from.as_str())
        };
        let script = !edge.is_exit();
        let link = links.entry((a, b, script)).or_insert_with(|| Link {
            a,
            b,
            script,
            a_to_b: Vec::new(),
            b_to_a: Vec::new(),
        });
        if edge.from == a {
            link.a_to_b.push(edge);
        } else {
            link.b_to_a.push(edge);
        }
    }
    links.into_values().collect()
}

/// The points of a link's line: straight for exits, bent to one side for script warps.
fn link_points(from: Pos2, to: Pos2, script: bool) -> Vec<Pos2> {
    if !script {
        return vec![from, to];
    }
    let middle = from + (to - from) * 0.5;
    let side = (to - from).rot90().normalized() * ((to - from).length() * 0.15).min(40.0);
    let control = middle + side;
    (0..=16)
        .map(|step| {
            let t = step as f32 / 16.0;
            let a = from + (control - from) * t;
            let b = control + (to - control) * t;
            a + (b - a) * t
        })
        .collect()
}

fn arrow_head(painter: &egui::Painter, tip: Pos2, from: Pos2, color: Color32) {
    let direction = (tip - from).normalized();
    let tip = tip - direction * (NODE_RADIUS + 1.0);
    let back = tip - direction * 8.0;
    let side = direction.rot90() * 4.0;
    painter.add(Shape::convex_polygon(
        vec![tip, back + side, back - side],
        color,
        Stroke::NONE,
    ));
}

fn distance_to_polyline(point: Pos2, points: &[Pos2]) -> f32 {
    points
        .windows(2)
        .map(|pair| {
            let (a, b) = (pair[0], pair[1]);
            let ab = b - a;
            let t = ((point - a).dot(ab) / ab.length_sq().max(1e-6)).clamp(0.0, 1.0);
            (a + ab * t).distance(point)
        })
        .fold(f32::INFINITY, f32::min)
}

fn edge_tooltip(edges: &[&WorldEdge]) -> String {
    edges
        .iter()
        .map(|edge| match &edge.kind {
            EdgeKind::Script { guards, .. } if !guards.is_empty() => {
                format!("{}\n    {} {guards}", edge.label(), text::WORLD_IF)
            }
            _ => edge.label(),
        })
        .collect::<Vec<_>>()
        .join("\n")
}

fn canvas(app: &mut MapperApp, ui: &mut egui::Ui) {
    let (response, painter) =
        ui.allocate_painter(ui.available_size(), egui::Sense::click_and_drag());
    let rect = response.rect;
    let painter = painter.with_clip_rect(rect);
    painter.rect_filled(rect, 0.0, Color32::from_gray(22));

    let layout = app.mapper.world.layout.clone();
    let world = &mut app.world_ui;
    if world.fit && !layout.is_empty() {
        let (min, max) = layout.values().fold(
            (
                glam::Vec2::splat(f32::INFINITY),
                glam::Vec2::splat(f32::NEG_INFINITY),
            ),
            |(min, max), place| (min.min(*place), max.max(*place)),
        );
        let size = (max - min).max(glam::Vec2::splat(1.0));
        world.zoom = ((rect.width() - 80.0) / size.x)
            .min((rect.height() - 80.0) / size.y)
            .clamp(0.05, 4.0);
        let middle = (min + max) / 2.0;
        world.pan = -Vec2::new(middle.x, middle.y) * world.zoom;
        world.fit = false;
    }
    if let Some(map) = world.focus.take() {
        if let Some(place) = layout.get(&map) {
            world.zoom = FOCUS_ZOOM;
            world.pan = -Vec2::new(place.x, place.y) * world.zoom;
            world.fit = false;
            world.selected = Some(map);
        } else if layout.is_empty() {
            world.focus = Some(map);
        }
    }
    let frame = Frame {
        centre: rect.center(),
        pan: world.pan,
        zoom: world.zoom,
    };

    // Zoom around the cursor; pan with an empty-space drag.
    if let Some(pointer) = response.hover_pos() {
        let (scroll, pinch) = ui.input(|input| (input.smooth_scroll_delta.y, input.zoom_delta()));
        let factor = pinch * (scroll * WHEEL_ZOOM).exp();
        if (factor - 1.0).abs() > 1e-4 {
            let before = frame.to_graph(pointer);
            world.zoom = (world.zoom * factor).clamp(0.05, 8.0);
            let after = Frame {
                zoom: world.zoom,
                ..frame
            }
            .to_screen(before);
            world.pan += pointer - after;
        }
    }
    let frame = Frame {
        centre: rect.center(),
        pan: world.pan,
        zoom: world.zoom,
    };

    let graph = &app.mapper.world.graph;
    let moving = world.moving.clone();
    let hidden = world.hidden_areas.clone();
    let place = |id: &str| -> Option<Pos2> {
        if let Some((moving, to)) = &moving
            && moving == id
        {
            return Some(frame.to_screen(*to));
        }
        layout.get(id).map(|point| frame.to_screen(*point))
    };
    let area_of = |id: &str| graph.node(id).map(|node| node.area_index);
    let pointer = response.hover_pos();
    let node_at = |pos: Pos2| {
        graph
            .nodes
            .iter()
            .filter(|node| !hidden.contains(&node.area_index))
            .filter_map(|node| Some((node, place(&node.id)?.distance(pos))))
            .filter(|(_, distance)| *distance <= NODE_RADIUS + 3.0)
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(node, _)| node.id.clone())
    };
    let hovered_node = pointer.and_then(node_at);

    // Area boxes and names.
    for (area, (name, maps)) in graph.areas() {
        if world.hidden_areas.contains(&area) {
            continue;
        }
        let points: Vec<Pos2> = maps.iter().filter_map(|map| place(map)).collect();
        let Some(first) = points.first() else {
            continue;
        };
        let bounds = points
            .iter()
            .fold(Rect::from_min_max(*first, *first), |bounds, point| {
                bounds.union(Rect::from_min_max(*point, *point))
            })
            .expand(16.0);
        let color = area_color(area);
        painter.rect_filled(bounds, 8.0, color.gamma_multiply(0.07));
        painter.rect_stroke(
            bounds,
            8.0,
            Stroke::new(1.0, color.gamma_multiply(0.35)),
            egui::StrokeKind::Outside,
        );
        if world.zoom > 0.25 {
            painter.text(
                bounds.left_top() + Vec2::new(6.0, 2.0),
                egui::Align2::LEFT_TOP,
                name,
                egui::FontId::proportional(12.0),
                color.gamma_multiply(0.8),
            );
        }
    }

    // The game's trail and map.
    let current = app.live.game_map().map(str::to_owned);
    let trail: BTreeSet<(String, String)> = app
        .mapper
        .world
        .trail
        .steps()
        .map(|(a, b)| (a.to_owned(), b.to_owned()))
        .collect();

    // Links.
    let mut hovered_link: Option<(Vec<&WorldEdge>, f32)> = None;
    let shown = graph
        .edges
        .iter()
        .filter(|edge| edge_shown(world, area_of, edge));
    for link in links(shown) {
        let (Some(a), Some(b)) = (place(link.a), place(link.b)) else {
            continue;
        };
        let points = link_points(a, b, link.script);
        let walked = trail.contains(&(link.a.to_owned(), link.b.to_owned()))
            || trail.contains(&(link.b.to_owned(), link.a.to_owned()));
        let base = if link.script {
            Color32::from_rgb(190, 140, 230)
        } else {
            Color32::from_gray(170)
        };
        let color = if walked {
            Color32::from_rgb(250, 220, 60)
        } else {
            base.gamma_multiply(0.75)
        };
        let width = if walked { 3.0 } else { 1.2 };
        if link.script {
            painter.extend(Shape::dashed_line(
                &points,
                Stroke::new(width, color),
                6.0,
                4.0,
            ));
        } else {
            painter.add(Shape::line(points.clone(), Stroke::new(width, color)));
        }
        let n = points.len();
        if !link.a_to_b.is_empty() {
            arrow_head(&painter, points[n - 1], points[n - 2], color);
        }
        if !link.b_to_a.is_empty() {
            arrow_head(&painter, points[0], points[1], color);
        }
        if let Some(pointer) = pointer
            && hovered_node.is_none()
        {
            let distance = distance_to_polyline(pointer, &points);
            if distance < 5.0
                && hovered_link
                    .as_ref()
                    .is_none_or(|(_, best)| distance < *best)
            {
                let mut edges = link.a_to_b.clone();
                edges.extend(&link.b_to_a);
                hovered_link = Some((edges, distance));
            }
        }
    }

    // Nodes.
    let query = world.search.trim().to_lowercase();
    for node in &graph.nodes {
        if world.hidden_areas.contains(&node.area_index) {
            continue;
        }
        let Some(centre) = place(&node.id) else {
            continue;
        };
        if !rect.expand(40.0).contains(centre) {
            continue;
        }
        let color = area_color(node.area_index);
        let found = matches(node, &query);
        let picked = world.selected.as_deref() == Some(node.id.as_str());
        painter.circle_filled(centre, NODE_RADIUS, color);
        if node.added {
            painter.circle_stroke(centre, NODE_RADIUS + 2.0, Stroke::new(1.5, Color32::WHITE));
        }
        if let Some(severity) = graph.worst(&node.id)
            && (severity != Severity::Info || world.show_notes)
        {
            let badge = centre + Vec2::new(NODE_RADIUS, -NODE_RADIUS);
            painter.circle_filled(badge, 4.5, severity_color(severity));
            let count = graph
                .checks_of(&node.id)
                .filter(|check| check.severity == severity)
                .count();
            if world.zoom > 0.5 {
                painter.text(
                    badge + Vec2::new(5.0, 0.0),
                    egui::Align2::LEFT_CENTER,
                    count.to_string(),
                    egui::FontId::proportional(10.0),
                    severity_color(severity),
                );
            }
        }
        if current.as_deref() == Some(node.id.as_str()) {
            painter.circle_stroke(
                centre,
                NODE_RADIUS + 6.0,
                Stroke::new(3.0, Color32::from_rgb(250, 220, 60)),
            );
        }
        if found || picked {
            painter.circle_stroke(
                centre,
                NODE_RADIUS + 4.0,
                Stroke::new(2.0, Color32::from_rgb(90, 220, 250)),
            );
        }
        let hovered = hovered_node.as_deref() == Some(node.id.as_str());
        if world.zoom >= LABEL_ZOOM || found || picked || hovered {
            painter.text(
                centre + Vec2::new(0.0, NODE_RADIUS + 2.0),
                egui::Align2::CENTER_TOP,
                node.label(),
                egui::FontId::proportional(11.0),
                Color32::from_gray(225),
            );
        }
    }

    // A link being dragged.
    if let (Some(from), Some(pointer)) = (world.linking.as_deref().and_then(place), pointer) {
        painter.line_segment(
            [from, pointer],
            Stroke::new(2.0, Color32::from_rgb(90, 220, 250)),
        );
    }

    // Tooltips.
    if let Some(id) = &hovered_node
        && let Some(node) = graph.node(id)
        && world.moving.is_none()
    {
        let mut lines = vec![node.label(), format!("{}: {}", text::WORLD_AREA, node.area)];
        lines.extend(graph.checks_of(id).map(|check| check.message()));
        lines.push(text::WORLD_NODE_HINT.to_owned());
        response.clone().on_hover_text(lines.join("\n"));
    } else if let Some((edges, _)) = &hovered_link {
        response.clone().on_hover_text(format!(
            "{}\n{}",
            edge_tooltip(edges),
            text::WORLD_EDGE_HINT
        ));
    }

    // Gestures.
    let mut actions = Vec::new();
    let linking_button = ui.input(|input| {
        input.pointer.secondary_down() || (input.pointer.primary_down() && input.modifiers.shift)
    });
    let pressed_node = response.interact_pointer_pos().and_then(node_at);
    let origin_node = ui
        .input(|input| input.pointer.press_origin())
        .and_then(node_at);
    if response.drag_started() {
        let start = origin_node;
        match start {
            Some(id) if linking_button => world.linking = Some(id),
            Some(id) => {
                let at = layout.get(&id).copied().unwrap_or_default();
                world.moving = Some((id, at));
            }
            None => {}
        }
    }
    if response.dragged() {
        let delta = response.drag_delta();
        if let Some((_, at)) = &mut world.moving {
            *at += glam::Vec2::new(delta.x, delta.y) / world.zoom;
        } else if world.linking.is_none() {
            world.pan += delta;
        }
    }
    if response.drag_stopped() {
        if let Some((id, to)) = world.moving.take() {
            actions.push(Action::MoveWorldNode { id, to });
        }
        if let Some(from) = world.linking.take()
            && let Some(to) = pressed_node.clone()
        {
            world.connect = Some(ConnectForm {
                exit: free_exit(graph, &from),
                to_exit: free_exit(graph, &to),
                from,
                to,
                back: true,
            });
        }
    }
    let mut open_map = None;
    if response.double_clicked()
        && let Some(id) = &hovered_node
    {
        open_map = Some(id.clone());
    } else if response.clicked() {
        world.selected = hovered_node.clone();
        if hovered_node.is_none()
            && let Some((edges, _)) = &hovered_link
            && let Some(edge) = edges.first()
        {
            let edge = (*edge).clone();
            world.shown = false;
            actions.push(Action::OpenMap(edge.from.clone()));
            match edge.kind {
                EdgeKind::Exit { exit } => {
                    actions.push(Action::Select(Some(Selection::Exit(exit))));
                    actions.push(Action::CenterSelection);
                }
                EdgeKind::Script {
                    script, section, ..
                } => actions.push(Action::OpenSection { script, section }),
            }
        }
    }
    if let Some(id) = open_map {
        world.shown = false;
        actions.push(Action::OpenMap(id));
    }
    for action in actions {
        app.apply(action);
    }
}

/// The first exit of a map that leads nowhere, or 0.
fn free_exit(graph: &mapper_core::WorldGraph, map: &str) -> i32 {
    let exits = graph.node(map).map_or(0, |node| node.exits) as i32;
    (0..exits)
        .find(|&exit| {
            !graph
                .edges
                .iter()
                .any(|edge| edge.from == map && edge.exit() == Some(exit))
        })
        .unwrap_or(0)
}

/// Where an exit leads now, for the connect window.
fn exit_label(graph: &mapper_core::WorldGraph, map: &str, exit: i32) -> String {
    graph
        .edges
        .iter()
        .find(|edge| edge.from == map && edge.exit() == Some(exit))
        .map_or_else(
            || format!("{exit} ({})", text::TARGET_MAP_NONE),
            |edge| format!("{exit} → {}:{}", edge.to, edge.to_exit),
        )
}

/// The window that connects two maps' exits.
pub fn connect_window(app: &mut MapperApp, ctx: &egui::Context) {
    let Some(form) = &mut app.world_ui.connect else {
        return;
    };
    let graph = &app.mapper.world.graph;
    let mut open = true;
    let mut done = None;
    egui::Window::new(text::WORLD_CONNECT_TITLE)
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .show(ctx, |ui| {
            egui::Grid::new("connect").num_columns(2).show(ui, |ui| {
                for (label, map, exit) in [
                    (text::WORLD_CONNECT_FROM, &form.from, &mut form.exit),
                    (text::WORLD_CONNECT_TO, &form.to, &mut form.to_exit),
                ] {
                    ui.label(format!("{label} {map}"));
                    let exits = graph.node(map).map_or(0, |node| node.exits) as i32;
                    egui::ComboBox::from_id_salt(label)
                        .selected_text(exit_label(graph, map, *exit))
                        .show_ui(ui, |ui| {
                            for choice in 0..exits {
                                ui.selectable_value(exit, choice, exit_label(graph, map, choice));
                            }
                        });
                    ui.end_row();
                }
            });
            ui.checkbox(&mut form.back, text::WORLD_CONNECT_BACK);
            ui.add(egui::Label::new(egui::RichText::new(text::WORLD_CONNECT_HELP).weak()).wrap());
            ui.horizontal(|ui| {
                if ui.button(text::WORLD_CONNECT).clicked() {
                    done = Some(true);
                }
                if ui.button(text::UNSAVED_CANCEL).clicked() {
                    done = Some(false);
                }
            });
        });
    if !open {
        done = Some(false);
    }
    if let Some(connect) = done {
        let form = app.world_ui.connect.take().expect("the form is open");
        if connect {
            app.apply(Action::Connect(Connect {
                from: form.from,
                exit: form.exit,
                to: form.to,
                to_exit: form.to_exit,
                back: form.back,
            }));
        }
    }
}
