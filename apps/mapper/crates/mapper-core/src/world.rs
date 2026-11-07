//! The world as a graph (docs/modulos/apps/mapper.md, "Mundo"): the maps of the stack, the exits
//! and script warps between them, what is wrong with them, and where to draw each map.
//!
//! The graph is built from the maps as the game loads them (with their patches) and the story as
//! the packs leave it, so an edit shows once it is saved; the map open in the editor takes part as
//! edited. The layout is computed, and the same every time for the same graph: a force layout
//! that starts each area's maps together, with the areas on a grid in the order the start map
//! reaches them. Maps the user moved keep their place (settings, per content folder).

use std::collections::{BTreeMap, BTreeSet, HashMap, VecDeque};

use dwe_map::world;
use dwe_map::{CellKind, MapDocument, MapTable, TriggerBands, WarpTarget, warp_target};
use dwe_pack::{AssetId, Vfs};
use dwe_story::{LoadedScript, MapEvents};
use glam::Vec2;
use serde::Deserialize;

use crate::text;

/// Where the field starts: `field/warps`' `start`.
const FIELD_WARPS: &str = "field/warps";
/// How far apart the areas start, in graph units.
const AREA_SPACING: f32 = 520.0;
/// The distance two linked maps settle at.
const LINK_LENGTH: f32 = 90.0;
/// The steps of the force layout.
const LAYOUT_STEPS: usize = 240;

/// How an edge leaves its map.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum EdgeKind {
    /// An exit of the map's table: `exit` of `from`.
    Exit { exit: i32 },
    /// A `warp_to` of a script section that runs on the map, with the conditions on its way.
    Script {
        script: u16,
        section: u16,
        guards: String,
    },
}

/// A way from one map to another: an exit or a script warp, to an exit of the target.
#[derive(Clone, Debug, PartialEq)]
pub struct WorldEdge {
    pub from: String,
    pub to: String,
    pub to_exit: i32,
    pub kind: EdgeKind,
    /// The trigger cells of the exit (0: the player cannot walk into it). Script warps: 0.
    pub cells: usize,
    /// Whether the target has a way back to `from` (an exit or a script warp).
    pub two_way: bool,
}

impl WorldEdge {
    pub fn is_exit(&self) -> bool {
        matches!(self.kind, EdgeKind::Exit { .. })
    }

    /// The exit it leaves through, for an exit.
    pub fn exit(&self) -> Option<i32> {
        match self.kind {
            EdgeKind::Exit { exit } => Some(exit),
            EdgeKind::Script { .. } => None,
        }
    }

    /// `map:exit → map:exit`, or `map script:section → map:exit`.
    pub fn label(&self) -> String {
        match &self.kind {
            EdgeKind::Exit { exit } => {
                format!("{}:{exit} → {}:{}", self.from, self.to, self.to_exit)
            }
            EdgeKind::Script {
                script, section, ..
            } => format!(
                "{} {script}:{section} → {}:{}",
                self.from, self.to, self.to_exit
            ),
        }
    }
}

/// A map of the graph.
#[derive(Clone, Debug, PartialEq)]
pub struct WorldNode {
    pub id: String,
    /// The place name the map is filed under (`loading_name`), its area.
    pub area: String,
    pub area_index: i32,
    /// A pack's own name for a map it added (`text/es/<pack>`), if it has one.
    pub title: Option<String>,
    /// A map a pack added (`pack:name`).
    pub added: bool,
    /// The exits of the document (used or not).
    pub exits: usize,
}

impl WorldNode {
    /// The id and the name shown under it.
    pub fn label(&self) -> String {
        format!(
            "{} · {}",
            self.id,
            self.title.as_deref().unwrap_or(&self.area)
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Severity {
    Error,
    Warning,
    Info,
}

/// What a check found.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum CheckKind {
    /// An exit or a warp leads to a map the stack does not have (or whose id and index disagree).
    MissingMap { target: String },
    /// An exit or a warp leads to an exit the target does not have.
    MissingExit { target: String, exit: i32 },
    /// The exit's id names no map, but its original index does: the game goes there anyway.
    Fallback { target: String },
    /// The target has no way back (many are meant: a fall, a one-way door).
    OneWay { target: String },
    /// No way from the start map leads here.
    Unreachable,
    /// The player arrives through this exit inside a wall.
    ArrivalInWall { exit: i32 },
}

/// A problem of the graph, on a map and maybe an edge.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct WorldCheck {
    pub severity: Severity,
    pub map: String,
    pub kind: CheckKind,
    /// The edge it is about: an exit, or a script section.
    pub via: Option<EdgeKind>,
}

impl WorldCheck {
    /// The text the errors panel shows.
    pub fn message(&self) -> String {
        let via = match &self.via {
            Some(EdgeKind::Exit { exit }) => format!("{}:{exit}", self.map),
            Some(EdgeKind::Script {
                script, section, ..
            }) => format!("{} ({} {script}:{section})", self.map, text::SCRIPT_WARP),
            None => self.map.clone(),
        };
        match &self.kind {
            CheckKind::MissingMap { target } => {
                format!("{via}: {} '{target}'", text::CHECK_MISSING_MAP)
            }
            CheckKind::MissingExit { target, exit } => {
                format!("{via}: {} {target}:{exit}", text::CHECK_MISSING_EXIT)
            }
            CheckKind::Fallback { target } => {
                format!("{via}: {} '{target}'", text::CHECK_FALLBACK)
            }
            CheckKind::OneWay { target } => format!("{via}: {} {target}", text::CHECK_ONE_WAY),
            CheckKind::Unreachable => format!("{via}: {}", text::CHECK_UNREACHABLE),
            CheckKind::ArrivalInWall { exit } => {
                format!("{}:{exit}: {}", self.map, text::CHECK_ARRIVAL_IN_WALL)
            }
        }
    }
}

/// The whole world: maps, the ways between them, the checks and where the field starts.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WorldGraph {
    pub nodes: Vec<WorldNode>,
    pub edges: Vec<WorldEdge>,
    pub checks: Vec<WorldCheck>,
    /// `field/warps`' start: the map and the exit.
    pub start: Option<(String, i32)>,
    /// Maps of the table whose data does not load, with why.
    pub broken: Vec<(String, String)>,
}

/// The maps a graph is built from: the stack's documents (the editor's as edited) and a way to
/// load a script for the map's events.
pub struct WorldSource<'a> {
    pub vfs: &'a Vfs,
    pub table: &'a MapTable,
    pub bands: &'a TriggerBands,
    /// The map open in the editor, as edited: it replaces the stack's.
    pub open: Option<&'a MapDocument>,
}

#[derive(Deserialize)]
struct FieldWarps {
    start: FieldStart,
}

#[derive(Deserialize)]
struct FieldStart {
    map: String,
    exit: i32,
}

/// `field/warps`' start, if the stack has one.
pub fn field_start(vfs: &Vfs) -> Option<(String, i32)> {
    let text = vfs.read_text(&AssetId::parse(FIELD_WARPS).ok()?).ok()?;
    let warps: FieldWarps = serde_yaml_ng::from_str(&text).ok()?;
    Some((warps.start.map, warps.start.exit))
}

/// A pack's own name for a map it added: `map_names.<name>` of `text/es/<pack>`.
pub fn added_map_title(vfs: &Vfs, id: &str) -> Option<String> {
    let (pack, name) = id.split_once(':')?;
    let asset = AssetId::parse(&format!("text/es/{pack}")).ok()?;
    let texts: BTreeMap<String, String> =
        serde_yaml_ng::from_str(&vfs.read_text(&asset).ok()?).ok()?;
    texts.get(&dwe_map::new_map::name_key(name)).cloned()
}

impl WorldGraph {
    /// Builds the graph of every map of the table that has data. `load` gives a script as it runs
    /// with the packs' patches (the events of a map name the sections that warp).
    pub fn build(source: &WorldSource<'_>, mut load: impl FnMut(u16) -> LoadedScript) -> Self {
        let mut graph = Self {
            start: field_start(source.vfs),
            ..Self::default()
        };
        let mut documents: BTreeMap<String, MapDocument> = BTreeMap::new();
        for entry in &source.table.maps {
            let document = match source.open {
                Some(open) if open.id == entry.id => Ok(open.clone()),
                _ => match dwe_map::load::map_data_id(&entry.id) {
                    Ok(data) if !source.vfs.contains(&data) => continue,
                    _ => dwe_map::load_map(source.vfs, &entry.id),
                },
            };
            match document {
                Ok(document) => {
                    documents.insert(entry.id.clone(), document);
                }
                Err(error) => graph.broken.push((entry.id.clone(), error.to_string())),
            }
        }

        for (id, document) in &documents {
            let entry = source.table.find(id);
            graph.nodes.push(WorldNode {
                id: id.clone(),
                area: entry
                    .map(|entry| source.table.display_name(entry).to_owned())
                    .filter(|name| !name.is_empty())
                    .unwrap_or_else(|| text::UNNAMED_GROUP.to_owned()),
                area_index: entry.map_or(-1, |entry| entry.loading_name),
                title: added_map_title(source.vfs, id),
                added: id.contains(':'),
                exits: document.warps.len(),
            });
        }

        let mut checks = BTreeSet::new();
        for (id, document) in &documents {
            graph.add_exits(source, id, document, &documents, &mut checks);
            let events = MapEvents::build_with(
                &mut load,
                source.table,
                source.bands,
                document,
                source.table.find(id),
            );
            graph.add_script_warps(id, &events, &documents, &mut checks);
        }
        graph.edges.sort_by(|a, b| {
            (&a.from, &a.kind, &a.to, a.to_exit).cmp(&(&b.from, &b.kind, &b.to, b.to_exit))
        });
        graph.edges.dedup();

        let links: BTreeSet<(&str, &str)> = graph
            .edges
            .iter()
            .map(|edge| (edge.from.as_str(), edge.to.as_str()))
            .collect();
        let two_way: Vec<bool> = graph
            .edges
            .iter()
            .map(|edge| links.contains(&(edge.to.as_str(), edge.from.as_str())))
            .collect();
        for (edge, back) in graph.edges.iter_mut().zip(two_way) {
            edge.two_way = back;
            if !back && edge.is_exit() && edge.from != edge.to {
                checks.insert(WorldCheck {
                    severity: Severity::Info,
                    map: edge.from.clone(),
                    kind: CheckKind::OneWay {
                        target: edge.to.clone(),
                    },
                    via: Some(edge.kind.clone()),
                });
            }
        }

        if let Some((start, _)) = &graph.start {
            let reached = graph.reachable_from(start);
            for node in &graph.nodes {
                if !reached.contains(node.id.as_str()) {
                    checks.insert(WorldCheck {
                        severity: Severity::Warning,
                        map: node.id.clone(),
                        kind: CheckKind::Unreachable,
                        via: None,
                    });
                }
            }
        }
        graph.checks = checks.into_iter().collect();
        graph
    }

    fn add_exits(
        &mut self,
        source: &WorldSource<'_>,
        id: &str,
        document: &MapDocument,
        documents: &BTreeMap<String, MapDocument>,
        checks: &mut BTreeSet<WorldCheck>,
    ) {
        for warp in &document.warps {
            let kind = EdgeKind::Exit { exit: warp.exit };
            let target = match warp_target(source.table, warp) {
                WarpTarget::Nowhere => {
                    // An id the table does not have, and no original index to fall back on.
                    if let Some(target) = &warp.target_map_id {
                        checks.insert(WorldCheck {
                            severity: Severity::Error,
                            map: id.to_owned(),
                            kind: CheckKind::MissingMap {
                                target: target.clone(),
                            },
                            via: Some(kind),
                        });
                    }
                    continue;
                }
                WarpTarget::Conflict => {
                    checks.insert(WorldCheck {
                        severity: Severity::Error,
                        map: id.to_owned(),
                        kind: CheckKind::MissingMap {
                            target: warp
                                .target_map_id
                                .clone()
                                .unwrap_or_else(|| warp.target_map.to_string()),
                        },
                        via: Some(kind),
                    });
                    continue;
                }
                WarpTarget::UnknownIdFallback(entry) => {
                    checks.insert(WorldCheck {
                        severity: Severity::Warning,
                        map: id.to_owned(),
                        kind: CheckKind::Fallback {
                            target: entry.id.clone(),
                        },
                        via: Some(kind.clone()),
                    });
                    entry.id.clone()
                }
                WarpTarget::Map(entry) => entry.id.clone(),
            };
            let cells = u8::try_from(warp.exit)
                .ok()
                .and_then(|exit| source.bands.exit_trigger(exit))
                .map_or(0, |trigger| {
                    document
                        .collision
                        .cells()
                        .filter(|&(_, value)| value == trigger)
                        .count()
                });
            // A leftover of the original: a map but no exit to arrive at, and no cell to walk in.
            if warp.target_exit < 0 && cells == 0 {
                continue;
            }
            self.add_edge(
                WorldEdge {
                    from: id.to_owned(),
                    to: target,
                    to_exit: warp.target_exit,
                    kind,
                    cells,
                    two_way: false,
                },
                documents,
                checks,
            );
        }
    }

    fn add_script_warps(
        &mut self,
        id: &str,
        events: &MapEvents,
        documents: &BTreeMap<String, MapDocument>,
        checks: &mut BTreeSet<WorldCheck>,
    ) {
        for event in &events.events {
            for jump in event.jumps.iter().filter(|jump| !jump.file_jump) {
                let kind = EdgeKind::Script {
                    script: event.script,
                    section: event.section,
                    guards: dwe_story::events::guards_text(&jump.guards),
                };
                let Some(target) = &jump.to_map else {
                    checks.insert(WorldCheck {
                        severity: Severity::Error,
                        map: id.to_owned(),
                        kind: CheckKind::MissingMap {
                            target: jump.map_index.to_string(),
                        },
                        via: Some(kind),
                    });
                    continue;
                };
                self.add_edge(
                    WorldEdge {
                        from: id.to_owned(),
                        to: target.clone(),
                        to_exit: i32::try_from(jump.exit).unwrap_or(-1),
                        kind,
                        cells: 0,
                        two_way: false,
                    },
                    documents,
                    checks,
                );
            }
        }
    }

    /// Adds an edge whose target exists, checking the exit it arrives through.
    fn add_edge(
        &mut self,
        edge: WorldEdge,
        documents: &BTreeMap<String, MapDocument>,
        checks: &mut BTreeSet<WorldCheck>,
    ) {
        let Some(target) = documents.get(&edge.to) else {
            checks.insert(WorldCheck {
                severity: Severity::Error,
                map: edge.from.clone(),
                kind: CheckKind::MissingMap {
                    target: edge.to.clone(),
                },
                via: Some(edge.kind),
            });
            return;
        };
        let Some(arrival) = target.warps.iter().find(|warp| warp.exit == edge.to_exit) else {
            checks.insert(WorldCheck {
                severity: Severity::Error,
                map: edge.from.clone(),
                kind: CheckKind::MissingExit {
                    target: edge.to.clone(),
                    exit: edge.to_exit,
                },
                via: Some(edge.kind),
            });
            return;
        };
        let cell = world::cell_at(world::to_world(arrival.spawn.into()));
        if target.collision.kind(cell) == CellKind::Blocked {
            checks.insert(WorldCheck {
                severity: Severity::Warning,
                map: edge.to.clone(),
                kind: CheckKind::ArrivalInWall { exit: edge.to_exit },
                via: None,
            });
        }
        self.edges.push(edge);
    }

    pub fn node(&self, id: &str) -> Option<&WorldNode> {
        self.nodes.iter().find(|node| node.id == id)
    }

    /// The maps a walk from `start` reaches through exits with trigger cells and script warps.
    pub fn reachable_from<'a>(&'a self, start: &'a str) -> BTreeSet<&'a str> {
        let mut reached = BTreeSet::new();
        let mut queue = VecDeque::from([start]);
        while let Some(map) = queue.pop_front() {
            if !reached.insert(map) {
                continue;
            }
            for edge in self.edges.iter().filter(|edge| edge.from == map) {
                if edge.cells > 0 || !edge.is_exit() {
                    queue.push_back(edge.to.as_str());
                }
            }
        }
        reached
    }

    /// The shortest way between two maps through any edge: the edges taken, in order. Empty when
    /// `from` is `to`; None when there is no way.
    pub fn path<'a>(&'a self, from: &'a str, to: &'a str) -> Option<Vec<&'a WorldEdge>> {
        let mut came: HashMap<&str, usize> = HashMap::new();
        let mut queue = VecDeque::from([from]);
        let mut seen = BTreeSet::from([from]);
        while let Some(map) = queue.pop_front() {
            if map == to {
                let mut path = Vec::new();
                let mut at = to;
                while let Some(&index) = came.get(at) {
                    path.push(&self.edges[index]);
                    at = self.edges[index].from.as_str();
                }
                path.reverse();
                return Some(path);
            }
            for (index, edge) in self.edges.iter().enumerate() {
                if edge.from == map && seen.insert(edge.to.as_str()) {
                    came.insert(edge.to.as_str(), index);
                    queue.push_back(edge.to.as_str());
                }
            }
        }
        None
    }

    /// The edges of other maps that lead to a map.
    pub fn references_to(&self, id: &str) -> Vec<&WorldEdge> {
        self.edges
            .iter()
            .filter(|edge| edge.to == id && edge.from != id)
            .collect()
    }

    /// The checks about a map, worst first.
    pub fn checks_of(&self, id: &str) -> impl Iterator<Item = &WorldCheck> {
        self.checks.iter().filter(move |check| check.map == id)
    }

    /// The worst severity of a map's checks.
    pub fn worst(&self, id: &str) -> Option<Severity> {
        self.checks_of(id).map(|check| check.severity).min()
    }

    /// The areas of the graph, by their index, with their maps.
    pub fn areas(&self) -> BTreeMap<i32, (String, Vec<&str>)> {
        let mut areas: BTreeMap<i32, (String, Vec<&str>)> = BTreeMap::new();
        for node in &self.nodes {
            areas
                .entry(node.area_index)
                .or_insert_with(|| (node.area.clone(), Vec::new()))
                .1
                .push(&node.id);
        }
        areas
    }

    /// Where to draw each map: a force layout from a start where each area's maps sit together,
    /// with the areas on a grid in the order a walk from the start map meets them. The same graph
    /// always gives the same places. `pinned` maps stay where they are and the others settle
    /// around them.
    pub fn layout(&self, pinned: &BTreeMap<String, [i32; 2]>) -> BTreeMap<String, Vec2> {
        let index: HashMap<&str, usize> = self
            .nodes
            .iter()
            .enumerate()
            .map(|(i, node)| (node.id.as_str(), i))
            .collect();
        let count = self.nodes.len();
        if count == 0 {
            return BTreeMap::new();
        }

        // Areas in the order a walk from the start meets them, then the rest by index.
        let mut area_order: Vec<i32> = Vec::new();
        let start = self
            .start
            .as_ref()
            .map(|(map, _)| map.as_str())
            .filter(|map| index.contains_key(map))
            .unwrap_or(&self.nodes[0].id);
        let mut queue = VecDeque::from([start]);
        let mut seen = BTreeSet::from([start]);
        while let Some(map) = queue.pop_front() {
            let area = self.nodes[index[map]].area_index;
            if !area_order.contains(&area) {
                area_order.push(area);
            }
            for edge in self.edges.iter().filter(|edge| edge.from == map) {
                if index.contains_key(edge.to.as_str()) && seen.insert(edge.to.as_str()) {
                    queue.push_back(edge.to.as_str());
                }
            }
        }
        for area in self.areas().into_keys() {
            if !area_order.contains(&area) {
                area_order.push(area);
            }
        }
        let columns = (area_order.len() as f32).sqrt().ceil().max(1.0) as usize;
        let area_centre = |area: i32| {
            let slot = area_order.iter().position(|&a| a == area).unwrap_or(0);
            Vec2::new(
                (slot % columns) as f32 * AREA_SPACING,
                (slot / columns) as f32 * AREA_SPACING,
            )
        };

        // Each area's maps on a small circle around its centre, by id.
        let areas = self.areas();
        let mut positions = vec![Vec2::ZERO; count];
        for (area, (_, maps)) in &areas {
            let centre = area_centre(*area);
            let radius = 30.0 * (maps.len() as f32).sqrt();
            for (slot, map) in maps.iter().enumerate() {
                let angle = slot as f32 / maps.len() as f32 * std::f32::consts::TAU;
                positions[index[map]] = centre + radius * Vec2::new(angle.cos(), angle.sin());
            }
        }
        let fixed: Vec<bool> = self
            .nodes
            .iter()
            .map(|node| pinned.contains_key(&node.id))
            .collect();
        for (i, node) in self.nodes.iter().enumerate() {
            if let Some([x, y]) = pinned.get(&node.id) {
                positions[i] = Vec2::new(*x as f32, *y as f32);
            }
        }

        let links: BTreeSet<(usize, usize)> = self
            .edges
            .iter()
            .filter_map(|edge| {
                let (a, b) = (
                    *index.get(edge.from.as_str())?,
                    *index.get(edge.to.as_str())?,
                );
                (a != b).then(|| (a.min(b), a.max(b)))
            })
            .collect();
        let area_of: Vec<i32> = self.nodes.iter().map(|node| node.area_index).collect();
        let mut temperature = LINK_LENGTH;
        for _ in 0..LAYOUT_STEPS {
            let mut forces = vec![Vec2::ZERO; count];
            for a in 0..count {
                for b in a + 1..count {
                    let delta = positions[a] - positions[b];
                    let distance = delta.length().max(1.0);
                    let reach = if area_of[a] == area_of[b] {
                        LINK_LENGTH
                    } else {
                        LINK_LENGTH * 2.0
                    };
                    if distance > reach * 4.0 {
                        continue;
                    }
                    let push = delta / distance * (reach * reach / distance);
                    forces[a] += push;
                    forces[b] -= push;
                }
            }
            for &(a, b) in &links {
                let delta = positions[b] - positions[a];
                let distance = delta.length().max(1.0);
                let pull = delta / distance * (distance * distance / LINK_LENGTH) * 0.5;
                forces[a] += pull;
                forces[b] -= pull;
            }
            for (i, force) in forces.iter_mut().enumerate() {
                *force += (area_centre(area_of[i]) - positions[i]) * 0.05;
            }
            for i in 0..count {
                if fixed[i] {
                    continue;
                }
                let force = forces[i];
                let length = force.length();
                if length > 0.0 {
                    positions[i] += force / length * length.min(temperature);
                }
            }
            temperature = (temperature * 0.985).max(1.0);
        }
        self.nodes
            .iter()
            .zip(positions)
            .map(|(node, position)| (node.id.clone(), position))
            .collect()
    }
}

/// The maps the game was on this session, in order (no repeats in a row).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Trail {
    pub maps: Vec<String>,
}

impl Trail {
    pub fn visit(&mut self, map: &str) {
        if self.maps.last().map(String::as_str) != Some(map) {
            self.maps.push(map.to_owned());
        }
    }

    pub fn current(&self) -> Option<&str> {
        self.maps.last().map(String::as_str)
    }

    /// The pairs of maps the game went between.
    pub fn steps(&self) -> impl Iterator<Item = (&str, &str)> {
        self.maps
            .windows(2)
            .map(|pair| (pair[0].as_str(), pair[1].as_str()))
    }
}

/// What the Mundo view shows: the graph, where each map is drawn and the game's trail. The graph
/// is built again when the content, a save or the open map changed since.
#[derive(Clone, Debug, Default)]
pub struct WorldState {
    pub graph: WorldGraph,
    pub layout: BTreeMap<String, Vec2>,
    pub trail: Trail,
    /// The open map the graph was built with, or None when it must be built again.
    pub(crate) built_with: Option<Option<MapDocument>>,
}

impl WorldState {
    /// Builds the graph again on the next look.
    pub fn invalidate(&mut self) {
        self.built_with = None;
    }
}
