//! The server's tools: what each one takes (a JSON schema) and does. Reads look at the content as
//! the editor shows it; edits go through the editor's actions on the map they name and stay
//! unsaved until `save`, or are undone at once with `dry_run`; live tools talk to a linked game.

use std::collections::BTreeMap;

use dwe_map::new_map::{self, Connection, NewMap, Template};
use dwe_map::save::SaveMode;
use dwe_map::world::Cell;
use dwe_map::{CellKind, TriggerBand};
use dwe_pack::AssetId;
use dwe_story::describe::Names;
use dwe_story::edit::StmtPath;
use dwe_story::{Block, EventKind, StmtKind};
use mapper_core::{Action, Connect, EdgeKind, Severity, StoryEdit, WorldEdge};
use serde_json::{Value, json};

use crate::{Server, errors};

pub const INSTRUCTIONS: &str = "Map editor of dwe. Read maps, the world graph and story sections; \
edit exits, collision, NPCs, story sections and lines (every edit takes dry_run: true to see the \
patch without keeping it); `save` writes the open map's edits into the target pack; `new_map` \
adds a map to the target pack. Edits are kept on one open map at a time: save or discard before \
editing another. Live tools need a running game (live_connect).";

/// The most results `find` gives.
const MOST_FOUND: usize = 200;

fn object(properties: Value, required: &[&str]) -> Value {
    json!({
        "type": "object",
        "properties": properties,
        "required": required,
        "additionalProperties": false,
    })
}

fn dry_run() -> Value {
    json!({"type": "boolean", "description": "Answer with the patch the edit gives, without keeping it.", "default": false})
}

/// Every tool: its name, what it does and the schema of its arguments.
pub fn list() -> Vec<Value> {
    let map = json!({"type": "string", "description": "A map id: twna01, or pack:name for a pack's map."});
    let int = |description: &str| json!({"type": "integer", "description": description});
    let tools = [
        (
            "list_maps",
            "The maps of the content with data: id, area and whether a pack added it.",
            object(
                json!({"filter": {"type": "string", "description": "Part of an id or area name."}}),
                &[],
            ),
        ),
        (
            "map_info",
            "A map: its area, exits (where they lead, trigger cells, arrival), NPCs, story events and a collision summary.",
            object(json!({"map": map}), &["map"]),
        ),
        (
            "world_graph",
            "The world as a graph: counts, areas and checks; with `map`, that map's ways in and out and its checks; `edges` lists the edges too.",
            object(
                json!({
                    "map": map,
                    "edges": {"type": "string", "enum": ["none", "exits", "all"], "default": "none"},
                }),
                &[],
            ),
        ),
        (
            "path",
            "The shortest way between two maps through exits and script warps.",
            object(json!({"from": map, "to": map}), &["from", "to"]),
        ),
        (
            "section",
            "A story section as the editor reads it: one described line per statement, its Lua and a timeline summary. Opens `map` first (the section runs there).",
            object(
                json!({"map": map, "script": int("Script number."), "section": int("Section number.")}),
                &["map", "script", "section"],
            ),
        ),
        (
            "find",
            "Finds text in the story (code and lines) and in map ids: a command (give_item), a trigger (trigger(12)), a variable (var(3)), a word of a line.",
            object(
                json!({"query": {"type": "string", "minLength": 1}}),
                &["query"],
            ),
        ),
        (
            "validate",
            "What is wrong in the world (exits to missing maps or exits, arrivals in walls, maps out of reach) and in the open map's unsaved edits.",
            object(json!({}), &[]),
        ),
        (
            "set_exit",
            "Makes an exit of a map lead to an exit of another (target_map null: nowhere). With `back`, that exit leads back.",
            object(
                json!({
                    "map": map,
                    "exit": int("The exit, 0 to 9."),
                    "target_map": {"type": ["string", "null"]},
                    "target_exit": int("The exit of the target the player arrives through."),
                    "back": {"type": "boolean", "default": false},
                    "dry_run": dry_run(),
                }),
                &["map", "exit", "target_map", "target_exit"],
            ),
        ),
        (
            "set_collision_rect",
            "Sets a rectangle of collision cells (corners included) to a value: 0 free, 128 wall, or a trigger value.",
            object(
                json!({
                    "map": map,
                    "column0": int("First corner's column (0-99)."),
                    "row0": int("First corner's row (0-99)."),
                    "column1": int("Second corner's column."),
                    "row1": int("Second corner's row."),
                    "value": {"type": "integer", "minimum": 0, "maximum": 255},
                    "dry_run": dry_run(),
                }),
                &["map", "column0", "row0", "column1", "row1", "value"],
            ),
        ),
        (
            "add_npc",
            "Adds an NPC slot (a copy of the last one) at a point in the original's units (x, z on the ground).",
            object(
                json!({"map": map, "x": int("x"), "z": int("z"), "dry_run": dry_run()}),
                &["map", "x", "z"],
            ),
        ),
        (
            "move_npc",
            "Moves an NPC slot to a point in the original's units.",
            object(
                json!({"map": map, "slot": int("The slot."), "x": int("x"), "z": int("z"), "dry_run": dry_run()}),
                &["map", "slot", "x", "z"],
            ),
        ),
        (
            "edit_section",
            "Replaces a story section's code (Lua, as `section` shows it) on a map; kept as a draft of the open map until `save`.",
            object(
                json!({"map": map, "script": int("Script."), "section": int("Section."), "code": {"type": "string"}, "dry_run": dry_run()}),
                &["map", "script", "section", "code"],
            ),
        ),
        (
            "set_text",
            "Sets a dialogue line by its key (story_001.text.0002) while a map is open.",
            object(
                json!({"map": map, "key": {"type": "string"}, "text": {"type": "string"}, "dry_run": dry_run()}),
                &["map", "key", "text"],
            ),
        ),
        (
            "new_map",
            "Adds a map to the target pack: a copy of a map (its images referenced unless copy_images) or a blank one, filed under an area (loading name index), maybe connected both ways to an exit of another map.",
            object(
                json!({
                    "name": {"type": "string", "pattern": "^[a-z0-9_]{1,64}$"},
                    "title": {"type": "string", "minLength": 1},
                    "area": int("Index of data/maps' map_names."),
                    "copy_of": map,
                    "copy_images": {"type": "boolean", "default": false},
                    "keep_exits": {"type": "boolean", "default": false},
                    "width": int("Blank map: tiles across (128 px)."),
                    "height": int("Blank map: tiles down."),
                    "image": {"type": "string", "description": "Blank map: a PNG file whose sides are whole tiles."},
                    "connect_map": map,
                    "connect_exit": int("The exit of connect_map that leads in."),
                    "own_exit": int("The new map's exit that leads back."),
                    "dry_run": dry_run(),
                }),
                &["name", "title", "area"],
            ),
        ),
        (
            "save",
            "Writes the open map's edits (and story drafts) into the target pack: as a patch, or as the whole data with whole: true.",
            object(json!({"whole": {"type": "boolean", "default": false}}), &[]),
        ),
        (
            "discard",
            "Drops the open map's unsaved edits.",
            object(json!({}), &[]),
        ),
        (
            "live_connect",
            "Links to a running game: tcp:<port> or unix:<socket>.",
            object(json!({"endpoint": {"type": "string"}}), &["endpoint"]),
        ),
        (
            "live_status",
            "The linked game's mode, map, tick and running story sections.",
            object(json!({}), &[]),
        ),
        (
            "live_reload",
            "Has the game read its packs and its map again (after a save), entering by an exit when given.",
            object(json!({"exit": int("0 to 9.")}), &[]),
        ),
        (
            "probar_evento",
            "Tests a story event of a map in the game: writes the triggers and variables it needs, puts the player where it runs and starts the section.",
            object(
                json!({"map": map, "script": int("Script."), "section": int("Section.")}),
                &["map", "script", "section"],
            ),
        ),
        (
            "play_from",
            "Starts a section in the game from one of its top-level statements (0 = the first), the way the timeline's play button does.",
            object(
                json!({"map": map, "script": int("Script."), "section": int("Section."), "statement": int("Top-level statement index.")}),
                &["map", "script", "section", "statement"],
            ),
        ),
    ];
    tools
        .into_iter()
        .map(|(name, description, schema)| {
            json!({"name": name, "description": description, "inputSchema": schema})
        })
        .collect()
}

pub fn exists(name: &str) -> bool {
    list()
        .iter()
        .any(|tool| tool.get("name").and_then(Value::as_str) == Some(name))
}

fn string<'a>(arguments: &'a Value, key: &str) -> Result<&'a str, String> {
    arguments
        .get(key)
        .and_then(Value::as_str)
        .ok_or_else(|| format!("'{key}' is needed (a string)"))
}

fn integer(arguments: &Value, key: &str) -> Result<i64, String> {
    arguments
        .get(key)
        .and_then(Value::as_i64)
        .ok_or_else(|| format!("'{key}' is needed (an integer)"))
}

fn small<T: TryFrom<i64>>(arguments: &Value, key: &str) -> Result<T, String> {
    let value = integer(arguments, key)?;
    T::try_from(value).map_err(|_| format!("'{key}' is out of range: {value}"))
}

fn flag(arguments: &Value, key: &str) -> bool {
    arguments.get(key).and_then(Value::as_bool).unwrap_or(false)
}

pub fn call(server: &mut Server, name: &str, arguments: &Value) -> Result<Value, String> {
    match name {
        "list_maps" => Ok(list_maps(server, arguments)),
        "map_info" => map_info(server, string(arguments, "map")?),
        "world_graph" => Ok(world_graph(server, arguments)),
        "path" => path(server, string(arguments, "from")?, string(arguments, "to")?),
        "section" => section(server, arguments),
        "find" => find(server, string(arguments, "query")?),
        "validate" => Ok(validate(server)),
        "set_exit" => set_exit(server, arguments),
        "set_collision_rect" => {
            let cell = |column, row| -> Result<Cell, String> {
                Ok(Cell::new(small(arguments, column)?, small(arguments, row)?))
            };
            let from = cell("column0", "row0")?;
            let to = cell("column1", "row1")?;
            let value = small(arguments, "value")?;
            edit_map(
                server,
                arguments,
                vec![Action::PaintRect { from, to, value }],
            )
        }
        "add_npc" => add_npc(server, arguments),
        "move_npc" => {
            open(server, string(arguments, "map")?)?;
            let slot: usize = small(arguments, "slot")?;
            let npc = moved_npc(server, slot, arguments)?;
            edit_map(server, arguments, vec![Action::SetNpc { slot, npc }])
        }
        "edit_section" => edit_section(server, arguments),
        "set_text" => {
            let key = string(arguments, "key")?.to_owned();
            let text = string(arguments, "text")?.to_owned();
            edit_map(server, arguments, vec![Action::SetText { key, text }])
        }
        "new_map" => new_map(server, arguments),
        "save" => save(server, flag(arguments, "whole")),
        "discard" => {
            server.mapper.apply(Action::Revert);
            Ok(json!({"discarded": true}))
        }
        "live_connect" => {
            server.connect(string(arguments, "endpoint")?)?;
            live_status(server)
        }
        "live_status" => live_status(server),
        "live_reload" => {
            let exit = arguments
                .get("exit")
                .and_then(Value::as_i64)
                .and_then(|exit| i32::try_from(exit).ok());
            let reloaded = live(server)?
                .reload_map(exit)
                .map_err(|error| error.to_string())?;
            Ok(json!({"map": reloaded.map, "exit": reloaded.exit}))
        }
        "probar_evento" => probar_evento(server, arguments),
        "play_from" => play_from(server, arguments),
        other => Err(format!("unknown tool '{other}'")),
    }
}

/// Opens a map, unless another one has unsaved edits.
fn open(server: &mut Server, map: &str) -> Result<(), String> {
    let mapper = &mut server.mapper;
    if let Some(shown) = &mapper.map {
        if shown.document.id == map {
            return Ok(());
        }
        if mapper.is_dirty() {
            return Err(format!(
                "the map '{}' has unsaved edits: save or discard them first",
                shown.document.id
            ));
        }
    }
    mapper.apply(Action::OpenMap(map.to_owned()));
    if mapper
        .map
        .as_ref()
        .is_none_or(|shown| shown.document.id != map)
    {
        let problems = errors(mapper);
        return Err(format!(
            "the map '{map}' does not open: {}",
            problems.join("; ")
        ));
    }
    Ok(())
}

fn list_maps(server: &mut Server, arguments: &Value) -> Value {
    let filter = arguments
        .get("filter")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_lowercase();
    server.mapper.refresh_world();
    let maps: Vec<Value> = server
        .mapper
        .world
        .graph
        .nodes
        .iter()
        .filter(|node| {
            filter.is_empty()
                || node.id.contains(&filter)
                || node.area.to_lowercase().contains(&filter)
        })
        .map(|node| {
            json!({"id": node.id, "area": node.area, "area_index": node.area_index, "added": node.added, "title": node.title})
        })
        .collect();
    json!({"count": maps.len(), "maps": maps})
}

fn map_info(server: &mut Server, map: &str) -> Result<Value, String> {
    open(server, map)?;
    let mapper = &server.mapper;
    let shown = mapper.map.as_ref().ok_or("no map is open")?;
    let document = &shown.document;
    let area = mapper
        .content
        .as_ref()
        .zip(shown.entry.as_ref())
        .map(|(content, entry)| content.table.display_name(entry).to_owned());
    let exits: Vec<Value> = shown
        .overlay
        .exits
        .iter()
        .zip(&document.warps)
        .filter(|(mark, _)| mark.is_used())
        .map(|(mark, warp)| {
            json!({
                "exit": mark.exit,
                "to": mark.target.destination().map(|(map, exit)| format!("{map}:{exit}")),
                "cells": mark.cell_count,
                "arrival": {"x": warp.spawn.x, "y": warp.spawn.y, "z": warp.spawn.z, "rotation": warp.rotation},
            })
        })
        .collect();
    let npcs: Vec<Value> = document
        .npcs
        .iter()
        .enumerate()
        .map(|(slot, npc)| {
            json!({"slot": slot, "type_id": npc.type_id, "script_id": npc.script_id,
                   "x": npc.position.x, "y": npc.position.y, "z": npc.position.z})
        })
        .collect();
    let events: Vec<Value> = mapper
        .events()
        .iter()
        .map(|event| {
            let kind = match event.kind {
                EventKind::MapEntry => "map_entry".to_owned(),
                EventKind::MapLoaded => "map_loaded".to_owned(),
                EventKind::Step => "step".to_owned(),
                EventKind::Confirm => "confirm".to_owned(),
                EventKind::Talk { slot } => format!("talk {slot}"),
            };
            let warps: Vec<String> = event
                .jumps
                .iter()
                .filter_map(|jump| Some(format!("{}:{}", jump.to_map.as_ref()?, jump.exit)))
                .collect();
            json!({"kind": kind, "script": event.script, "section": event.section,
                   "cells": event.cells.len(), "exists": event.exists, "warps": warps})
        })
        .collect();
    let mut collision: BTreeMap<&str, usize> = BTreeMap::new();
    for (_, value) in document.collision.cells() {
        let kind = match dwe_map::collision::kind_of(value) {
            CellKind::Free => "free",
            CellKind::Blocked => "wall",
            CellKind::Trigger(value) => match mapper.bands.band(value) {
                TriggerBand::Step => "step",
                TriggerBand::Confirm => "confirm",
                TriggerBand::Exit(_) => "exit",
                TriggerBand::Toilet => "toilet",
                TriggerBand::Other => "other",
            },
        };
        *collision.entry(kind).or_default() += 1;
    }
    Ok(json!({
        "id": document.id,
        "area": area,
        "tiles": [document.tiles.width, document.tiles.height],
        "images_from": document.assets_map(),
        "exits": exits,
        "npcs": npcs,
        "events": events,
        "collision": collision,
        "unsaved": mapper.is_dirty(),
    }))
}

fn edge_json(edge: &WorldEdge) -> Value {
    let mut value = json!({
        "from": edge.from, "to": edge.to, "to_exit": edge.to_exit,
        "two_way": edge.two_way, "label": edge.label(),
    });
    match &edge.kind {
        EdgeKind::Exit { exit } => {
            value["exit"] = json!(exit);
            value["cells"] = json!(edge.cells);
        }
        EdgeKind::Script {
            script,
            section,
            guards,
        } => {
            value["script"] = json!(script);
            value["section"] = json!(section);
            value["guards"] = json!(guards);
        }
    }
    value
}

fn severity(severity: Severity) -> &'static str {
    match severity {
        Severity::Error => "error",
        Severity::Warning => "warning",
        Severity::Info => "note",
    }
}

fn world_graph(server: &mut Server, arguments: &Value) -> Value {
    server.mapper.refresh_world();
    let graph = &server.mapper.world.graph;
    let which = arguments
        .get("edges")
        .and_then(Value::as_str)
        .unwrap_or("none");
    let map = arguments.get("map").and_then(Value::as_str);
    let checks: Vec<Value> = graph
        .checks
        .iter()
        .filter(|check| map.is_none_or(|map| check.map == map))
        .filter(|check| map.is_some() || check.severity != Severity::Info)
        .map(|check| json!({"severity": severity(check.severity), "map": check.map, "message": check.message()}))
        .collect();
    let edges: Vec<Value> = graph
        .edges
        .iter()
        .filter(|edge| match map {
            Some(map) => edge.from == map || edge.to == map,
            None => which != "none",
        })
        .filter(|edge| which != "exits" || edge.is_exit())
        .map(edge_json)
        .collect();
    let areas: Vec<Value> = graph
        .areas()
        .into_iter()
        .map(|(index, (name, maps))| json!({"index": index, "name": name, "maps": maps.len()}))
        .collect();
    let count = |severity| {
        graph
            .checks
            .iter()
            .filter(|check| check.severity == severity)
            .count()
    };
    json!({
        "maps": graph.nodes.len(),
        "exits": graph.edges.iter().filter(|edge| edge.is_exit()).count(),
        "script_warps": graph.edges.iter().filter(|edge| !edge.is_exit()).count(),
        "start": graph.start.as_ref().map(|(map, exit)| format!("{map}:{exit}")),
        "errors": count(Severity::Error),
        "warnings": count(Severity::Warning),
        "notes": count(Severity::Info),
        "areas": if map.is_none() { Value::Array(areas) } else { Value::Null },
        "node": map.and_then(|map| graph.node(map)).map(|node| json!({"id": node.id, "area": node.area, "added": node.added, "title": node.title})),
        "checks": checks,
        "edges": edges,
    })
}

fn path(server: &mut Server, from: &str, to: &str) -> Result<Value, String> {
    server.mapper.refresh_world();
    let graph = &server.mapper.world.graph;
    for map in [from, to] {
        if graph.node(map).is_none() {
            return Err(format!("there is no map '{map}'"));
        }
    }
    let steps = graph
        .path(from, to)
        .ok_or_else(|| format!("no way leads from {from} to {to}"))?;
    Ok(json!({"steps": steps.into_iter().map(edge_json).collect::<Vec<_>>()}))
}

/// The statements of a block as described lines, indented by depth, with their paths.
fn describe(
    names: &Names<'_>,
    block: &Block,
    blocks: &[(usize, usize)],
    depth: usize,
    out: &mut Vec<Value>,
) {
    for (index, stmt) in block.iter().enumerate() {
        if matches!(stmt.kind, StmtKind::Comment(_)) {
            continue;
        }
        let mut line = format!("{}{}", "  ".repeat(depth), names.stmt(stmt));
        if depth == 0 {
            line = format!("[{index}] {line}");
        }
        out.push(Value::String(line));
        let inner: Vec<&Block> = match &stmt.kind {
            StmtKind::If { arms, otherwise } => arms
                .iter()
                .map(|(_, block)| block)
                .chain(otherwise.as_ref())
                .collect(),
            StmtKind::While { body, .. } | StmtKind::Do(body) => vec![body],
            _ => Vec::new(),
        };
        for (arm, inner) in inner.into_iter().enumerate() {
            let mut path = blocks.to_vec();
            path.push((index, arm));
            describe(names, inner, &path, depth + 1, out);
        }
    }
}

fn open_section(server: &mut Server, arguments: &Value) -> Result<(u16, u16), String> {
    open(server, string(arguments, "map")?)?;
    let script: u16 = small(arguments, "script")?;
    let section: u16 = small(arguments, "section")?;
    server.mapper.apply(Action::OpenSection { script, section });
    if server.mapper.open_code().is_none() {
        return Err(format!("script {script} has no section {section}"));
    }
    Ok((script, section))
}

fn section(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    let (script, section) = open_section(server, arguments)?;
    let mapper = &server.mapper;
    let content = mapper.content.as_ref().ok_or("no content")?;
    let code = mapper.open_code().ok_or("no section is open")?;
    let names = Names {
        story: &mapper.story_names,
        texts: &mapper.story.texts,
        maps: &content.table,
    };
    let mut lines = Vec::new();
    describe(&names, code, &[], 0, &mut lines);
    let timeline = mapper.sequence().map(|sequence| {
        json!({
            "cues": sequence.cues.len(),
            "frames": sequence.length,
            "seconds": f64::from(sequence.length) / f64::from(dwe_story::sequence::FRAMES_PER_SECOND),
            "ends_with_map_change": sequence.ends,
            "commands": sequence.cues.iter().map(|cue| format!("{} @{} +{}", cue.command, cue.start, cue.frames)).collect::<Vec<_>>(),
        })
    });
    Ok(json!({
        "script": script,
        "section": section,
        "from": mapper.open_origin(),
        "lines": lines,
        "lua": dwe_story::write::block_text(code),
        "timeline": timeline,
    }))
}

fn find(server: &mut Server, query: &str) -> Result<Value, String> {
    let needle = query.to_lowercase();
    let content = server.mapper.content.as_ref().ok_or("no content")?;
    let vfs = content.vfs();
    let mut found = Vec::new();
    for entry in &content.table.maps {
        if entry.id.contains(&needle) {
            found.push(json!({"kind": "map", "map": entry.id}));
        }
    }
    let ids: Vec<AssetId> = vfs
        .ids()
        .filter(|id| {
            let id = id.as_str();
            id.starts_with("story/") || id.starts_with("text/en/story_")
        })
        .cloned()
        .collect();
    'files: for id in ids {
        let Ok(text) = vfs.read_text(&id) else {
            continue;
        };
        let mut section = None;
        for (number, line) in text.lines().enumerate() {
            if let Some(hex) = line.trim().strip_prefix("-- section 0x") {
                section = u32::from_str_radix(hex.trim(), 16).ok();
            }
            if line.to_lowercase().contains(&needle) && !line.trim_start().starts_with("--") {
                found.push(json!({
                    "kind": if id.as_str().starts_with("text/") { "line" } else { "code" },
                    "file": id.as_str(),
                    "section": section,
                    "line": number + 1,
                    "text": line.trim(),
                }));
                if found.len() >= MOST_FOUND {
                    break 'files;
                }
            }
        }
    }
    Ok(json!({"count": found.len(), "complete": found.len() < MOST_FOUND, "found": found}))
}

fn validate(server: &mut Server) -> Value {
    server.mapper.refresh_world();
    let graph = &server.mapper.world.graph;
    let checks: Vec<Value> = graph
        .checks
        .iter()
        .filter(|check| check.severity != Severity::Info)
        .map(|check| json!({"severity": severity(check.severity), "map": check.map, "message": check.message()}))
        .collect();
    let broken: Vec<Value> = graph
        .broken
        .iter()
        .map(|(map, why)| json!({"map": map, "error": why}))
        .collect();
    let open = server
        .mapper
        .map
        .as_ref()
        .map(|map| json!({"map": map.document.id, "unsaved": map.is_dirty()}));
    json!({
        "ok": checks.iter().all(|check| check["severity"] != "error") && broken.is_empty(),
        "checks": checks,
        "broken_maps": broken,
        "open_map": open,
    })
}

/// Applies edits to a map as one undo step: with `dry_run`, answers the patch and undoes them.
fn edit_map(server: &mut Server, arguments: &Value, actions: Vec<Action>) -> Result<Value, String> {
    let map = string(arguments, "map")?.to_owned();
    open(server, &map)?;
    let before = server.mapper.map.as_ref().map(|shown| {
        (
            shown.document.clone(),
            shown.drafts.scripts.len(),
            shown.drafts.texts.len(),
        )
    });
    server.mapper.apply(Action::BeginEdit);
    for action in actions {
        server.mapper.apply(action);
    }
    server.mapper.apply(Action::EndEdit);
    let problems = errors(&mut server.mapper);
    let after = server.mapper.map.as_ref().map(|shown| {
        (
            shown.document.clone(),
            shown.drafts.scripts.len(),
            shown.drafts.texts.len(),
        )
    });
    let changed = before != after;
    if !problems.is_empty() || !changed {
        if changed {
            server.mapper.apply(Action::Undo);
        }
        let why = if problems.is_empty() {
            "nothing changed".to_owned()
        } else {
            problems.join("; ")
        };
        return Err(format!("{map}: {why}"));
    }
    let patch = server.mapper.preview_patch().map(|(_, text)| text);
    let texts = server
        .mapper
        .map
        .as_ref()
        .map(|shown| shown.drafts.texts.clone())
        .unwrap_or_default();
    let dry = flag(arguments, "dry_run");
    if dry {
        server.mapper.apply(Action::Undo);
    }
    Ok(json!({
        "map": map,
        "dry_run": dry,
        "kept": !dry,
        "patch": patch,
        "texts": texts,
    }))
}

fn set_exit(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    let map = string(arguments, "map")?;
    let exit: i32 = small(arguments, "exit")?;
    let target_exit: i32 = small(arguments, "target_exit")?;
    let target = arguments.get("target_map").and_then(Value::as_str);
    open(server, map)?;
    let shown = server.mapper.map.as_ref().ok_or("no map is open")?;
    let mut warp = shown
        .document
        .warp(exit)
        .cloned()
        .ok_or_else(|| format!("{map} has no exit {exit}"))?;
    match target {
        Some(target) => {
            let content = server.mapper.content.as_ref().ok_or("no content")?;
            let entry = content
                .table
                .find(target)
                .ok_or_else(|| format!("there is no map '{target}'"))?;
            warp.target_map_id = Some(target.to_owned());
            warp.target_map = if entry.original_index {
                entry.index
            } else {
                -1
            };
            warp.target_exit = target_exit;
        }
        None => {
            warp.target_map_id = None;
            warp.target_map = -1;
            warp.target_exit = -1;
        }
    }
    let mut answer = edit_map(server, arguments, vec![Action::SetWarp(warp)])?;
    if flag(arguments, "back")
        && let Some(target) = target
    {
        if flag(arguments, "dry_run") {
            answer["back"] = json!(format!(
                "{target}:{target_exit} → {map}:{exit} (written on save as a patch of {target})"
            ));
        } else {
            server.mapper.apply(Action::Connect(Connect {
                from: target.to_owned(),
                exit: target_exit,
                to: map.to_owned(),
                to_exit: exit,
                back: false,
            }));
            let problems = errors(&mut server.mapper);
            if !problems.is_empty() {
                return Err(problems.join("; "));
            }
            answer["back"] = json!(format!(
                "{target}:{target_exit} → {map}:{exit} (patch written)"
            ));
        }
    }
    Ok(answer)
}

fn moved_npc(server: &Server, slot: usize, arguments: &Value) -> Result<dwe_map::Npc, String> {
    let shown = server.mapper.map.as_ref().ok_or("no map is open")?;
    let mut npc = shown
        .document
        .npcs
        .get(slot)
        .cloned()
        .ok_or_else(|| format!("there is no NPC slot {slot}"))?;
    npc.position.x = small(arguments, "x")?;
    npc.position.z = small(arguments, "z")?;
    Ok(npc)
}

fn add_npc(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    let map = string(arguments, "map")?;
    open(server, map)?;
    let count = server
        .mapper
        .map
        .as_ref()
        .map_or(0, |shown| shown.document.npcs.len());
    let dry = flag(arguments, "dry_run");
    // Adding grows the list: a patch cannot say it, so the map is saved whole.
    let added = edit_map(server, &json!({"map": map}), vec![Action::AddNpc(None)])?;
    let npc = moved_npc(server, count, arguments)?;
    let mut answer = edit_map(
        server,
        &json!({"map": map}),
        vec![Action::SetNpc { slot: count, npc }],
    )
    .unwrap_or(added);
    answer["slot"] = json!(count);
    answer["note"] = json!("a map whose NPC slots grew is saved whole");
    if dry {
        server.mapper.apply(Action::Undo);
        server.mapper.apply(Action::Undo);
        answer["dry_run"] = json!(true);
        answer["kept"] = json!(false);
    }
    Ok(answer)
}

fn edit_section(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    open_section(server, arguments)?;
    let code = string(arguments, "code")?.to_owned();
    let mut answer = edit_map(
        server,
        arguments,
        vec![Action::EditStory(StoryEdit::SetCode(code))],
    )?;
    if !flag(arguments, "dry_run")
        && let Some(code) = server.mapper.open_code()
    {
        answer["lua"] = json!(dwe_story::write::block_text(code));
    }
    Ok(answer)
}

fn new_map(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    let template = match arguments.get("copy_of").and_then(Value::as_str) {
        Some(from) => Template::Copy {
            from: from.to_owned(),
            copy_images: flag(arguments, "copy_images"),
            keep_exits: flag(arguments, "keep_exits"),
        },
        None => Template::Blank {
            width: small(arguments, "width").unwrap_or(5),
            height: small(arguments, "height").unwrap_or(4),
            image: arguments
                .get("image")
                .and_then(Value::as_str)
                .map(Into::into),
        },
    };
    let connect = arguments
        .get("connect_map")
        .and_then(Value::as_str)
        .map(|map| Connection {
            map: map.to_owned(),
            exit: small(arguments, "connect_exit").unwrap_or(0),
            own_exit: small(arguments, "own_exit").unwrap_or(0),
        });
    let new = NewMap {
        name: string(arguments, "name")?.to_owned(),
        title: string(arguments, "title")?.to_owned(),
        loading_name: small(arguments, "area")?,
        template,
        connect,
    };
    if flag(arguments, "dry_run") {
        let mapper = &server.mapper;
        let target = mapper
            .current_target()
            .ok_or("no target pack: start with --target")?;
        let content = mapper.content.as_ref().ok_or("no content")?;
        let pack = content
            .vfs()
            .packs()
            .iter()
            .find(|pack| pack.root == target.root)
            .ok_or("the target is not mounted")?;
        let plan = new_map::plan_new_map(content.vfs(), &content.table, &mapper.bands, pack, &new)
            .map_err(|error| error.to_string())?;
        let files: Vec<Value> = plan
            .ops
            .iter()
            .map(|op| match op {
                new_map::FileOp::Write { path, bytes } => {
                    json!({"write": path, "bytes": bytes.len()})
                }
                new_map::FileOp::Copy { from, to } => json!({"copy": from, "to": to}),
                new_map::FileOp::Remove(path) => json!({"remove": path}),
            })
            .collect();
        return Ok(json!({"id": plan.id, "dry_run": true, "files": files,
                         "back": new.connect.as_ref().map(|c| format!("{}:{} → {}:{}", c.map, c.exit, plan.id, c.own_exit))}));
    }
    if server.mapper.is_dirty() {
        return Err("the open map has unsaved edits: save or discard them first".to_owned());
    }
    server.mapper.apply(Action::NewMap(new));
    let problems = errors(&mut server.mapper);
    if !problems.is_empty() {
        return Err(problems.join("; "));
    }
    let id = server
        .mapper
        .map
        .as_ref()
        .map(|shown| shown.document.id.clone())
        .ok_or("the new map did not open")?;
    Ok(json!({"id": id, "dry_run": false}))
}

fn save(server: &mut Server, whole: bool) -> Result<Value, String> {
    let mapper = &mut server.mapper;
    let map = mapper
        .map
        .as_ref()
        .map(|shown| shown.document.id.clone())
        .ok_or("no map is open")?;
    if mapper.current_target().is_none() {
        return Err("no target pack: start with --target".to_owned());
    }
    let saves = mapper.saves;
    let mode = if whole {
        SaveMode::WholeData
    } else {
        SaveMode::Patch
    };
    mapper.apply(Action::Save(mode));
    let problems: Vec<String> = mapper
        .save_problems
        .iter()
        .filter(|problem| problem.is_error())
        .map(mapper_core::text::problem)
        .chain(
            mapper
                .story_problems
                .iter()
                .filter(|p| p.is_error())
                .map(|p| format!("{p:?}")),
        )
        .collect();
    let mut found = errors(mapper);
    found.extend(problems);
    if mapper.saves == saves || !found.is_empty() {
        return Err(format!("{map} was not saved: {}", found.join("; ")));
    }
    let target = mapper.current_target().map(|target| target.id);
    Ok(json!({"map": map, "saved_in": target}))
}

fn live(server: &mut Server) -> Result<&mut mapper_live::LiveClient, String> {
    server
        .live
        .as_mut()
        .ok_or_else(|| "no game is linked: live_connect first".to_owned())
}

fn live_status(server: &mut Server) -> Result<Value, String> {
    let client = live(server)?;
    let status = client.status().map_err(|error| error.to_string())?;
    let story = client.story_state().ok();
    Ok(json!({
        "mode": status.mode,
        "map": status.map,
        "tick": status.tick,
        "paused": status.paused,
        "story": story.map(|story| json!({
            "busy": story.busy,
            "map_script": story.map_script,
            "sections": story.sections.iter().map(|section| format!("{}:{}", section.script, section.section)).collect::<Vec<_>>(),
            "stage": story.stage,
        })),
    }))
}

fn probar_evento(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    open(server, string(arguments, "map")?)?;
    let script: u16 = small(arguments, "script")?;
    let section: u16 = small(arguments, "section")?;
    let event = server
        .mapper
        .events()
        .iter()
        .find(|event| event.script == script && event.section == section)
        .cloned()
        .ok_or_else(|| format!("the map runs no section {script}:{section}"))?;
    let plan = server.mapper.test_plan(&event);
    let client = live(server)?;
    let mut written = Vec::new();
    for need in &plan.writes {
        let write = match *need {
            dwe_story::needs::Need::Trigger { number, on } => mapper_live::StoryWrite::Trigger {
                number: u32::try_from(number).map_err(|_| "a trigger out of range")?,
                on,
            },
            dwe_story::needs::Need::Var { number, value } => mapper_live::StoryWrite::Variable {
                number: u32::try_from(number).map_err(|_| "a variable out of range")?,
                value: u8::try_from(value).map_err(|_| "a value out of range")?,
            },
        };
        client.story_set(write).map_err(|error| error.to_string())?;
        written.push(format!("{write:?}"));
    }
    if let Some(place) = &plan.place {
        client.place(place).map_err(|error| error.to_string())?;
    }
    let sent = client
        .story_run(&mapper_live::StoryRun {
            script,
            section,
            code: None,
        })
        .map_err(|error| error.to_string())?;
    Ok(
        json!({"map": plan.map, "written": written, "place": plan.place,
              "still_needed": plan.others, "started": sent}),
    )
}

fn play_from(server: &mut Server, arguments: &Value) -> Result<Value, String> {
    let (script, section) = open_section(server, arguments)?;
    let index: usize = small(arguments, "statement")?;
    let at = StmtPath {
        blocks: Vec::new(),
        index,
    };
    let sent = server.mapper.play_from_here(&at)?;
    let client = live(server)?;
    let taken = client
        .story_run(&mapper_live::StoryRun {
            script,
            section,
            code: Some(sent.code.clone()),
        })
        .map_err(|error| error.to_string())?;
    Ok(json!({"script": script, "section": section, "statement": index, "sent": taken}))
}
