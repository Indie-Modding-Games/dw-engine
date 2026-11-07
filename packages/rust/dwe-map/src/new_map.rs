//! Maps a pack adds (docs/modding/mapas-nuevos.md): what to write so that the game finds a new
//! map, and the same to rename or delete one the pack added.
//!
//! A pack's map is `<pack>:<name>`. The game needs three things (docs/formats/mapas.md, "En el
//! motor"): its data, `map/<pack>/<name>/data`; its entry in the pack's `data/maps`, which the
//! engine numbers after the original's maps; and a background, in its own folder or in the folder
//! of the map its data names in `assets_from`. Its name is the text `<pack>.map_names.<name>` of
//! `text/<language>/<pack>`.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::{Path, PathBuf};

use dwe_pack::{AssetId, ContentId, MAP_COMPOSITION, MountedPack, Vfs};
use glam::IVec2;
use serde_yaml_ng::{Mapping, Value};

use crate::camera::{MapCamera, TILE_SIZE};
use crate::collision::{BLOCKED_BIT, Collision, FREE};
use crate::document::{ASSETS_FROM, CameraSetup, MapDocument, RawVec3, Tiles, Warp};
use crate::load::{MAP_TABLE_ID, load_map, map_data_id, map_folder};
use crate::save::DATA_FILE;
use crate::table::MapTable;
use crate::triggers::{TriggerBand, TriggerBands};
use crate::world::{self, Cell, GRID_SIZE};
use crate::{Error, Result};

/// The most tiles of 128 x 128 pixels a background may have (the original reserves 35).
pub const MOST_TILES: i32 = 35;
/// The languages a new map's name is written in. The editor is in Spanish; English gets the same
/// text until someone translates it, so the game never shows the raw key.
pub const NAME_LANGUAGES: [&str; 2] = ["es", "en"];
/// The block of a pack's text table that names its maps (localization::addedMapNameKey).
pub const MAP_NAMES_BLOCK: &str = "map_names";
/// The exits a map has, used or not.
const EXIT_COUNT: i32 = 10;
/// A blank map's camera: high above the ground's centre, looking down and north, as the example
/// mod's map (tests/mods/example_mod).
const BLANK_CAMERA: CameraSetup = CameraSetup {
    viewpoint: RawVec3 {
        x: 0,
        y: -4301,
        z: -5973,
    },
    reference: RawVec3 {
        x: 0,
        y: 0,
        z: -256,
    },
    projection: 1813,
};
/// The flags of a blank map's entry: a fixed time of day (one background), no sound bank bits.
const BLANK_FLAGS: i64 = 0x40;
/// The two greys of a generated background: squares of this many pixels.
const CHECKER: u32 = 32;
const CHECKER_COLORS: [[u8; 3]; 2] = [[96, 104, 96], [120, 128, 120]];

/// What a new map starts from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Template {
    /// A copy of a map of the stack as the game loads it (with its patches): camera, lights,
    /// collision, exits and NPCs. Its images are referenced (`assets_from`) unless `copy_images`.
    /// Without `keep_exits` the exits lead nowhere and their trigger cells are freed.
    Copy {
        from: String,
        copy_images: bool,
        keep_exits: bool,
    },
    /// An empty map: a background of `width` x `height` tiles from a PNG of the user's (its size
    /// gives the tiles), or a generated grey one; every cell the camera does not show is a wall.
    Blank {
        width: i32,
        height: i32,
        image: Option<PathBuf>,
    },
}

/// A new map: the name after the pack's namespace, its shown name, the place name of the original
/// it is filed under (its area: `loading_name`, an index of `map_names`) and what it starts from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NewMap {
    pub name: String,
    pub title: String,
    pub loading_name: i32,
    pub template: Template,
    /// An exit of the new map that leads to an exit of another map (whoever writes the map makes
    /// that exit lead back).
    pub connect: Option<Connection>,
}

/// The new map's `own_exit` leads to `exit` of `map`. When the template leaves that exit without
/// trigger cells, the plan puts two on the ground the camera shows nearest to it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Connection {
    pub map: String,
    pub exit: i32,
    pub own_exit: i32,
}

/// A file a plan writes, copies or removes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FileOp {
    Write { path: PathBuf, bytes: Vec<u8> },
    Copy { from: PathBuf, to: PathBuf },
    Remove(PathBuf),
}

/// What a new map, a rename or a delete writes.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct MapFilesPlan {
    /// The map's id after the plan (`pack:name`).
    pub id: String,
    pub ops: Vec<FileOp>,
    /// The new map's document, as written.
    pub document: Option<MapDocument>,
}

/// Whether a name may follow a pack's namespace: 1 to 64 of `a-z`, `0-9` and `_`.
pub fn is_map_name(name: &str) -> bool {
    (1..=64).contains(&name.len())
        && name
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

/// The id a pack gives a map it adds.
pub fn added_map_id(pack: &str, name: &str) -> String {
    format!("{pack}:{name}")
}

/// The text key of a pack's map's name, without the table (`map_names.<name>`).
pub fn name_key(name: &str) -> String {
    format!("{MAP_NAMES_BLOCK}.{name}")
}

/// Plans a new map in a pack: its data, its entry and its name, and its images when it has its
/// own. Fails on a name that is taken or not valid, an area the table does not have, or a
/// template the stack cannot load.
pub fn plan_new_map(
    vfs: &Vfs,
    table: &MapTable,
    bands: &TriggerBands,
    target: &MountedPack,
    new: &NewMap,
) -> Result<MapFilesPlan> {
    let invalid = |message: String| Err(Error::Invalid(message));
    if !is_map_name(&new.name) {
        return invalid(format!(
            "'{}' is not a map name: 1 to 64 of a-z, 0-9 and _",
            new.name
        ));
    }
    let id = added_map_id(target.id(), &new.name);
    if table.find(&id).is_some() || vfs.contains(&map_data_id(&id)?) {
        return invalid(format!("the map '{id}' already exists"));
    }
    if new.title.trim().is_empty() {
        return invalid("a new map needs a name".to_owned());
    }
    if usize::try_from(new.loading_name).map_or(true, |area| area >= table.map_names.len()) {
        return invalid(format!("there is no place name {}", new.loading_name));
    }
    let folder = target.root.join(map_folder(&id));
    let mut ops = Vec::new();
    let (mut document, mut entry) = match &new.template {
        Template::Copy {
            from,
            copy_images,
            keep_exits,
        } => {
            let mut document = load_map(vfs, from)?;
            let images = document.assets_map().to_owned();
            if *copy_images {
                document.source.remove(ASSETS_FROM);
                ops.extend(copy_images_of(vfs, &images, &folder));
            } else {
                document
                    .source
                    .insert(Value::from(ASSETS_FROM), Value::from(images));
            }
            if !keep_exits {
                let kept = new.connect.as_ref().map(|connection| connection.own_exit);
                clear_exits(&mut document, bands, kept);
            }
            (document, composed_entry(vfs, from)?)
        }
        Template::Blank {
            width,
            height,
            image,
        } => {
            let (tiles, png) = match image {
                Some(file) => user_background(file)?,
                None => {
                    let tiles = IVec2::new(*width, *height);
                    check_tiles(tiles)?;
                    (tiles, checker_background(tiles))
                }
            };
            ops.push(FileOp::Write {
                path: folder.join("background_day.png"),
                bytes: png,
            });
            (blank_document(vfs, table, tiles)?, blank_entry())
        }
    };
    document.id = id.clone();
    if let Some(connection) = &new.connect {
        connect_exit(vfs, table, bands, &mut document, connection)?;
    }
    entry.insert("id".into(), Value::from(id.clone()));
    entry.insert("loading_name".into(), Value::from(new.loading_name));
    entry.insert("has_npcs".into(), Value::from(!document.npcs.is_empty()));
    let text = document.to_yaml();
    // What is written must read back as the game reads it.
    let written = MapDocument::parse(&id, &text)?;
    ops.push(FileOp::Write {
        path: folder.join(DATA_FILE),
        bytes: text.into_bytes(),
    });
    ops.push(table_op(target, |maps| {
        maps.push(Value::Mapping(entry));
        Ok(())
    })?);
    for language in NAME_LANGUAGES {
        ops.push(texts_op(target, language, |texts| {
            texts.insert(name_key(&new.name), new.title.trim().to_owned());
        })?);
    }
    Ok(MapFilesPlan {
        id,
        ops,
        document: Some(written),
    })
}

/// The maps a pack added itself: the namespaced entries of its own `data/maps`.
pub fn own_maps(target: &MountedPack) -> Vec<String> {
    let Some(file) = target.file(&table_asset()) else {
        return Vec::new();
    };
    let Ok(value) = read_yaml(file) else {
        return Vec::new();
    };
    let prefix = format!("{}:", target.id());
    value
        .get("maps")
        .and_then(Value::as_sequence)
        .into_iter()
        .flatten()
        .filter_map(|entry| entry.get("id").and_then(Value::as_str))
        .filter(|id| id.starts_with(&prefix))
        .map(str::to_owned)
        .collect()
}

/// Plans removing a map the pack added: its folder's files, its entry and its names. Whoever
/// calls it checks first that nothing leads to the map.
pub fn plan_delete_map(target: &MountedPack, id: &str) -> Result<MapFilesPlan> {
    let name = own_name(target, id)?;
    let mut ops: Vec<FileOp> = own_files(target, id)
        .into_iter()
        .map(|(_, file)| FileOp::Remove(file))
        .collect();
    ops.push(table_op(target, |maps| {
        maps.retain(|entry| entry.get("id").and_then(Value::as_str) != Some(id));
        Ok(())
    })?);
    for language in NAME_LANGUAGES {
        if let Some(op) = texts_op_if_any(target, language, |texts| {
            texts.remove(&name_key(&name));
        })? {
            ops.push(op);
        }
    }
    Ok(MapFilesPlan {
        id: id.to_owned(),
        ops,
        document: None,
    })
}

/// Plans renaming a map the pack added: its files move to the new folder, its entry and its names
/// take the new id, and the exits of the pack's own maps that lead to it follow. Whoever calls it
/// checks first that no other pack leads to the map.
pub fn plan_rename_map(
    vfs: &Vfs,
    table: &MapTable,
    target: &MountedPack,
    id: &str,
    new_name: &str,
) -> Result<MapFilesPlan> {
    let name = own_name(target, id)?;
    if !is_map_name(new_name) {
        return Err(Error::Invalid(format!("'{new_name}' is not a map name")));
    }
    let new_id = added_map_id(target.id(), new_name);
    if table.find(&new_id).is_some() || vfs.contains(&map_data_id(&new_id)?) {
        return Err(Error::Invalid(format!("the map '{new_id}' already exists")));
    }
    let old_folder = map_folder(id);
    let new_folder = target.root.join(map_folder(&new_id));
    let mut ops = Vec::new();
    for (asset, file) in own_files(target, id) {
        let rest = &asset.as_str()[old_folder.len()..];
        let extension = file
            .extension()
            .map(|extension| format!(".{}", extension.to_string_lossy()))
            .unwrap_or_default();
        ops.push(FileOp::Copy {
            from: file.clone(),
            to: new_folder.join(format!("{rest}{extension}")),
        });
        ops.push(FileOp::Remove(file));
    }
    ops.push(table_op(target, |maps| {
        for entry in maps.iter_mut() {
            if entry.get("id").and_then(Value::as_str) == Some(id) {
                entry["id"] = Value::from(new_id.clone());
            }
        }
        Ok(())
    })?);
    for language in NAME_LANGUAGES {
        if let Some(op) = texts_op_if_any(target, language, |texts| {
            if let Some(text) = texts.remove(&name_key(&name)) {
                texts.insert(name_key(new_name), text);
            }
        })? {
            ops.push(op);
        }
    }
    ops.extend(retarget_own_exits(target, id, &new_id)?);
    Ok(MapFilesPlan {
        id: new_id,
        ops,
        document: None,
    })
}

/// Carries out a plan: writes each file atomically, copies, then removes.
pub fn apply_ops(ops: &[FileOp]) -> std::io::Result<()> {
    for op in ops {
        match op {
            FileOp::Write { path, bytes } => write_atomically(path, bytes)?,
            FileOp::Copy { from, to } => {
                if let Some(folder) = to.parent() {
                    std::fs::create_dir_all(folder)?;
                }
                std::fs::copy(from, to)?;
            }
            FileOp::Remove(_) => {}
        }
    }
    for op in ops {
        if let FileOp::Remove(path) = op {
            match std::fs::remove_file(path) {
                Err(error) if error.kind() != std::io::ErrorKind::NotFound => return Err(error),
                _ => {}
            }
            remove_empty_folders(path.parent());
        }
    }
    Ok(())
}

fn write_atomically(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    if let Some(folder) = path.parent() {
        std::fs::create_dir_all(folder)?;
    }
    let mut temporary = path.as_os_str().to_owned();
    temporary.push(".tmp");
    let temporary = PathBuf::from(temporary);
    std::fs::write(&temporary, bytes)?;
    std::fs::rename(&temporary, path)
}

/// Removes a map's folders as they empty, up to `map/`.
fn remove_empty_folders(mut folder: Option<&Path>) {
    while let Some(current) = folder {
        if current.file_name().is_some_and(|name| name == "map") {
            break;
        }
        if std::fs::remove_dir(current).is_err() {
            break;
        }
        folder = current.parent();
    }
}

/// The name of a map the pack added, or an error if the pack did not add it.
fn own_name(target: &MountedPack, id: &str) -> Result<String> {
    let content = ContentId::parse(id)?;
    if !content.is_namespaced()
        || content.pack_namespace() != target.id()
        || !own_maps(target).iter().any(|own| own == id)
    {
        return Err(Error::Invalid(format!(
            "the pack '{}' did not add the map '{id}': only maps a pack added can be renamed or deleted there",
            target.id()
        )));
    }
    Ok(content.name().to_owned())
}

/// The target's files of a map's folder, by asset id.
fn own_files(target: &MountedPack, id: &str) -> Vec<(AssetId, PathBuf)> {
    let folder = map_folder(id);
    target
        .ids()
        .filter(|asset| asset.as_str().starts_with(&folder))
        .filter_map(|asset| Some((asset.clone(), target.file(asset)?.to_path_buf())))
        .collect()
}

/// Copies a map's backgrounds and textures (with their metadata) into a folder.
fn copy_images_of(vfs: &Vfs, map: &str, folder: &Path) -> Vec<FileOp> {
    let source = map_folder(map);
    vfs.ids_with_prefix(&source)
        .filter(|asset| {
            let rest = &asset.as_str()[source.len()..];
            (rest.starts_with("background_") && !rest.contains('/'))
                || rest.starts_with("texture8/")
                || rest.starts_with("texture4/")
        })
        .filter_map(|asset| {
            let (_, file) = vfs.resolve(asset)?;
            let rest = &asset.as_str()[source.len()..];
            let extension = file
                .extension()
                .map(|extension| format!(".{}", extension.to_string_lossy()))
                .unwrap_or_default();
            Some(FileOp::Copy {
                from: file.to_path_buf(),
                to: folder.join(format!("{rest}{extension}")),
            })
        })
        .collect()
}

/// Makes every exit lead nowhere and frees the cells of their triggers (a trigger without a target
/// is a dead warp for a pack's map), except the cells of `kept`, which the caller connects.
fn clear_exits(document: &mut MapDocument, bands: &TriggerBands, kept: Option<i32>) {
    for warp in &mut document.warps {
        warp.target_map = -1;
        warp.target_map_id = None;
        warp.target_exit = -1;
    }
    let cells: Vec<Cell> = document
        .collision
        .cells()
        .filter(|&(_, value)| match bands.band(value) {
            TriggerBand::Exit(exit) => Some(i32::from(exit)) != kept,
            _ => false,
        })
        .map(|(cell, _)| cell)
        .collect();
    for cell in cells {
        document.collision.set(cell, FREE);
    }
}

/// Makes the new map's exit lead to the connection's map and exit, with trigger cells to walk
/// into: two free cells next to each other nearest the bottom of the background when it has none,
/// and the arrival on the free cell nearest the middle of what the camera shows.
fn connect_exit(
    vfs: &Vfs,
    table: &MapTable,
    bands: &TriggerBands,
    document: &mut MapDocument,
    connection: &Connection,
) -> Result<()> {
    let invalid = |message: String| Err(Error::Invalid(message));
    let Some(entry) = table.find(&connection.map) else {
        return invalid(format!("there is no map '{}'", connection.map));
    };
    let other = load_map(vfs, &connection.map)?;
    if !other.warps.iter().any(|warp| warp.exit == connection.exit) {
        return invalid(format!(
            "the map '{}' has no exit {}",
            connection.map, connection.exit
        ));
    }
    let Some(warp) = document
        .warps
        .iter_mut()
        .find(|warp| warp.exit == connection.own_exit)
    else {
        return invalid(format!("the new map has no exit {}", connection.own_exit));
    };
    warp.target_map = if entry.original_index {
        entry.index
    } else {
        -1
    };
    warp.target_map_id = Some(connection.map.clone());
    warp.target_exit = connection.exit;
    place_door(document, bands, connection.own_exit)?;
    Ok(())
}

/// Gives an exit that has no trigger cells a door: two cells of free ground the camera shows,
/// low and central on the background, and an arrival a few cells inside from them. Answers whether
/// the map changed; an exit that already has cells is left alone.
pub fn place_door(document: &mut MapDocument, bands: &TriggerBands, exit: i32) -> Result<bool> {
    let invalid = |message: String| Err(Error::Invalid(message));
    let Some(trigger) = u8::try_from(exit)
        .ok()
        .and_then(|exit| bands.exit_trigger(exit))
    else {
        return invalid(format!("there is no exit {exit}"));
    };
    if !document.warps.iter().any(|warp| warp.exit == exit) {
        return invalid(format!("the map '{}' has no exit {exit}", document.id));
    }
    if document
        .collision
        .cells()
        .any(|(_, value)| value == trigger)
    {
        return Ok(false);
    }
    let camera = MapCamera::new(document.camera_desc());
    let size = document.camera_desc().background_size;
    let shown: Vec<(Cell, glam::Vec2)> = document
        .collision
        .cells()
        .filter(|&(_, value)| value == FREE)
        .filter_map(|(cell, _)| {
            let point = camera.project_to_background(world::cell_center(cell))?;
            (point.pixel.cmpge(glam::Vec2::ZERO).all() && point.pixel.cmplt(size).all())
                .then_some((cell, point.pixel))
        })
        .collect();
    let is_free = |cell: Cell| shown.iter().any(|&(other, _)| other == cell);
    let middle = size.x / 2.0;
    let door = shown
        .iter()
        .filter(|&&(cell, _)| is_free(Cell::new(cell.column + 1, cell.row)))
        .max_by(|(_, a), (_, b)| {
            (a.y - (a.x - middle).abs()).total_cmp(&(b.y - (b.x - middle).abs()))
        })
        .map(|&(cell, _)| cell);
    let Some(door) = door else {
        return invalid("the camera shows no free ground for an exit".to_owned());
    };
    let beside = Cell::new(door.column + 1, door.row);
    document.collision.set(door, trigger);
    document.collision.set(beside, trigger);
    // The arrival: free ground ARRIVAL_STEPS cells from the door (or the nearest there is),
    // the one nearest the middle of what the camera shows.
    let centre = shown.iter().map(|&(_, pixel)| pixel).sum::<glam::Vec2>() / shown.len() as f32;
    let steps = |cell: Cell| {
        (cell.column - door.column)
            .abs()
            .max((cell.row - door.row).abs())
    };
    let arrival = shown
        .iter()
        .filter(|&&(cell, _)| cell != door && cell != beside)
        .min_by(|&&(a, pa), &&(b, pb)| {
            (steps(a) - ARRIVAL_STEPS)
                .abs()
                .cmp(&(steps(b) - ARRIVAL_STEPS).abs())
                .then(pa.distance(centre).total_cmp(&pb.distance(centre)))
        })
        .map(|&(cell, _)| cell)
        .unwrap_or(door);
    let point = world::from_original_axes(world::cell_center(arrival));
    if let Some(warp) = document.warps.iter_mut().find(|warp| warp.exit == exit) {
        warp.spawn = RawVec3 {
            x: point.x.round() as i32,
            y: 0,
            z: point.z.round() as i32,
        };
    }
    Ok(true)
}

/// How many cells from its door a placed exit's arrival is.
const ARRIVAL_STEPS: i32 = 3;

/// A map's entry in the composed table, without the number the engine gave it.
fn composed_entry(vfs: &Vfs, map: &str) -> Result<Mapping> {
    let composed = vfs
        .read_composed_table(&table_asset(), &MAP_COMPOSITION)?
        .value;
    let mut entry = composed
        .get("maps")
        .and_then(Value::as_sequence)
        .into_iter()
        .flatten()
        .find(|entry| entry.get("id").and_then(Value::as_str) == Some(map))
        .and_then(Value::as_mapping)
        .cloned()
        .ok_or_else(|| Error::Invalid(format!("data/maps has no map '{map}'")))?;
    entry.remove("index");
    Ok(entry)
}

/// A blank map's entry, its keys in the table's order.
fn blank_entry() -> Mapping {
    let mut entry = Mapping::new();
    entry.insert("id".into(), Value::Null);
    entry.insert("filename".into(), Value::from("NEWMAP"));
    entry.insert("images_8bpp".into(), Value::from(0));
    entry.insert("images_4bpp".into(), Value::from(0));
    entry.insert("flags".into(), Value::from(BLANK_FLAGS));
    entry.insert("sound_id".into(), Value::from(0));
    entry.insert("fixed_time_of_day".into(), Value::from(true));
    entry.insert("has_npcs".into(), Value::from(false));
    entry.insert("doors".into(), Value::Null);
    entry.insert("toilet".into(), Value::Null);
    entry.insert("loading_name".into(), Value::from(0));
    entry
}

fn check_tiles(tiles: IVec2) -> Result<()> {
    if tiles.x < 1 || tiles.y < 1 || tiles.x * tiles.y > MOST_TILES {
        return Err(Error::Invalid(format!(
            "a background has 1 to {MOST_TILES} tiles of {TILE_SIZE} pixels, not {} x {}",
            tiles.x, tiles.y
        )));
    }
    Ok(())
}

/// An empty map: the shape of the stack's first map, with a blank map's camera, no images,
/// objects, exits or NPCs, and walls wherever the camera does not show the ground.
fn blank_document(vfs: &Vfs, table: &MapTable, tiles: IVec2) -> Result<MapDocument> {
    let mut document = table
        .maps
        .iter()
        .find_map(|entry| load_map(vfs, &entry.id).ok())
        .ok_or_else(|| Error::Invalid("the content has no map to take the shape of".to_owned()))?;
    document.camera = BLANK_CAMERA;
    document.tiles = Tiles {
        width: tiles.x,
        height: tiles.y,
        rows: (0..tiles.y)
            .map(|row| {
                (0..tiles.x)
                    .map(|column| (row * tiles.x + column).to_string())
                    .collect::<Vec<_>>()
                    .join(" ")
            })
            .collect(),
    };
    document.objects = None;
    document.npcs.clear();
    document.warps = (0..EXIT_COUNT)
        .map(|exit| Warp {
            exit,
            spawn: RawVec3::default(),
            rotation: 0,
            target_map: -1,
            target_map_id: None,
            target_exit: -1,
        })
        .collect();
    let camera = MapCamera::new(document.camera_desc());
    let size = document.camera_desc().background_size;
    let mut values = Vec::with_capacity((GRID_SIZE * GRID_SIZE) as usize);
    for row in 0..GRID_SIZE {
        for column in 0..GRID_SIZE {
            let shown = camera
                .project_to_background(world::cell_center(Cell::new(column, row)))
                .is_some_and(|point| {
                    point.pixel.cmpge(glam::Vec2::ZERO).all() && point.pixel.cmplt(size).all()
                });
            values.push(if shown { FREE } else { BLOCKED_BIT });
        }
    }
    document.collision = Collision::from_values(values)?;
    let source = &mut document.source;
    source.remove(ASSETS_FROM);
    source.insert("images".into(), Value::Sequence(Vec::new()));
    let unset = Value::Sequence(vec![Value::from(-1); 4]);
    let mut regions = Mapping::new();
    regions.insert("a".into(), unset.clone());
    regions.insert("b".into(), unset);
    source.insert("regions".into(), Value::Mapping(regions));
    // The count of NPC records the original stored next to them: none.
    for (key, value) in source.iter_mut() {
        if key.as_str().is_some_and(|key| key.ends_with("_count")) {
            *value = Value::from(0);
        }
    }
    // The NPC records of the shape's map go; the typed fields write the rest.
    source.insert("npcs".into(), Value::Sequence(Vec::new()));
    Ok(document)
}

/// A generated background: grey squares, one palette.
fn checker_background(tiles: IVec2) -> Vec<u8> {
    let size = (tiles * TILE_SIZE).as_uvec2();
    let indices: Vec<u8> = (0..size.y)
        .flat_map(|y| (0..size.x).map(move |x| u8::from((x / CHECKER + y / CHECKER) % 2 == 1)))
        .collect();
    encode_indexed(size.x, size.y, &indices, &CHECKER_COLORS).expect("a generated PNG encodes")
}

/// The user's background: a PNG whose sides are whole tiles. The game reads indexed PNGs, so one
/// with more colours than a palette holds is refused, and one with fewer is turned into one.
fn user_background(file: &Path) -> Result<(IVec2, Vec<u8>)> {
    let bytes = std::fs::read(file)
        .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?;
    let invalid = |message: String| Error::Invalid(format!("{}: {message}", file.display()));
    let mut decoder = png::Decoder::new(Cursor::new(&bytes));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut reader = decoder
        .read_info()
        .map_err(|error| invalid(error.to_string()))?;
    let indexed = reader.info().color_type == png::ColorType::Indexed;
    let size = reader
        .output_buffer_size()
        .ok_or_else(|| invalid("the image is too big".to_owned()))?;
    let mut buffer = vec![0; size];
    let frame = reader
        .next_frame(&mut buffer)
        .map_err(|error| invalid(error.to_string()))?;
    let tile = TILE_SIZE as u32;
    if frame.width % tile != 0 || frame.height % tile != 0 {
        return Err(invalid(format!(
            "a background's sides are whole tiles of {tile} pixels, not {} x {}",
            frame.width, frame.height
        )));
    }
    let tiles = IVec2::new((frame.width / tile) as i32, (frame.height / tile) as i32);
    check_tiles(tiles).map_err(|error| invalid(error.to_string()))?;
    if indexed {
        return Ok((tiles, bytes));
    }
    let channels = frame.color_type.samples();
    let mut palette: Vec<[u8; 3]> = Vec::new();
    let mut lookup: BTreeMap<[u8; 3], u8> = BTreeMap::new();
    let mut indices = Vec::with_capacity((frame.width * frame.height) as usize);
    for pixel in buffer[..frame.buffer_size()].chunks_exact(channels) {
        let color = match channels {
            1 | 2 => [pixel[0]; 3],
            _ => [pixel[0], pixel[1], pixel[2]],
        };
        let index = match lookup.get(&color) {
            Some(&index) => index,
            None => {
                let Ok(index) = u8::try_from(palette.len()) else {
                    return Err(invalid(
                        "the image has more than 256 colours: save it as an indexed PNG (8 bits)"
                            .to_owned(),
                    ));
                };
                palette.push(color);
                lookup.insert(color, index);
                index
            }
        };
        indices.push(index);
    }
    let png = encode_indexed(frame.width, frame.height, &indices, &palette)
        .map_err(|error| invalid(error.to_string()))?;
    Ok((tiles, png))
}

fn encode_indexed(
    width: u32,
    height: u32,
    indices: &[u8],
    palette: &[[u8; 3]],
) -> std::result::Result<Vec<u8>, png::EncodingError> {
    let mut out = Vec::new();
    {
        let mut encoder = png::Encoder::new(&mut out, width, height);
        encoder.set_color(png::ColorType::Indexed);
        encoder.set_depth(png::BitDepth::Eight);
        encoder.set_palette(palette.concat());
        let mut writer = encoder.write_header()?;
        writer.write_image_data(indices)?;
    }
    Ok(out)
}

fn table_asset() -> AssetId {
    AssetId::parse(MAP_TABLE_ID).expect("data/maps is an asset id")
}

fn read_yaml(file: &Path) -> Result<Value> {
    let text = std::fs::read_to_string(file)
        .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?;
    serde_yaml_ng::from_str(&text)
        .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))
}

/// Changes the `maps` list of the target's own `data/maps` (made when it has none).
fn table_op(
    target: &MountedPack,
    change: impl FnOnce(&mut Vec<Value>) -> Result<()>,
) -> Result<FileOp> {
    let file = target.file(&table_asset()).map(Path::to_path_buf);
    let mut own = match &file {
        Some(file) => read_yaml(file)?,
        None => Value::Mapping(Mapping::new()),
    };
    let maps = own
        .as_mapping_mut()
        .ok_or_else(|| Error::Invalid("data/maps is not a mapping".to_owned()))?
        .entry("maps".into())
        .or_insert_with(|| Value::Sequence(Vec::new()));
    let Some(maps) = maps.as_sequence_mut() else {
        return Err(Error::Invalid("data/maps: maps is not a list".to_owned()));
    };
    change(maps)?;
    let text = serde_yaml_ng::to_string(&own).map_err(|error| Error::Invalid(error.to_string()))?;
    Ok(FileOp::Write {
        path: file.unwrap_or_else(|| target.root.join(format!("{MAP_TABLE_ID}.yaml"))),
        bytes: text.into_bytes(),
    })
}

/// The target's own text table of its added entries in a language: `text/<language>/<pack>`.
fn texts_asset(target: &MountedPack, language: &str) -> Result<AssetId> {
    Ok(AssetId::parse(&format!("text/{language}/{}", target.id()))?)
}

fn read_texts(file: Option<&Path>) -> Result<BTreeMap<String, String>> {
    let Some(file) = file else {
        return Ok(BTreeMap::new());
    };
    match read_yaml(file)? {
        Value::Null => Ok(BTreeMap::new()),
        value => serde_yaml_ng::from_value(value)
            .map_err(|error| Error::Invalid(format!("{}: {error}", file.display()))),
    }
}

fn texts_op(
    target: &MountedPack,
    language: &str,
    change: impl FnOnce(&mut BTreeMap<String, String>),
) -> Result<FileOp> {
    let id = texts_asset(target, language)?;
    let file = target.file(&id).map(Path::to_path_buf);
    let mut texts = read_texts(file.as_deref())?;
    change(&mut texts);
    let text =
        serde_yaml_ng::to_string(&texts).map_err(|error| Error::Invalid(error.to_string()))?;
    Ok(FileOp::Write {
        path: file.unwrap_or_else(|| target.root.join(format!("{}.yaml", id.as_str()))),
        bytes: text.into_bytes(),
    })
}

/// The same for a table the target already has; nothing when it has none.
fn texts_op_if_any(
    target: &MountedPack,
    language: &str,
    change: impl FnOnce(&mut BTreeMap<String, String>),
) -> Result<Option<FileOp>> {
    if target.file(&texts_asset(target, language)?).is_none() {
        return Ok(None);
    }
    texts_op(target, language, change).map(Some)
}

/// Makes the exits of the target's own map data and patches that lead to `old` lead to `new`.
fn retarget_own_exits(target: &MountedPack, old: &str, new: &str) -> Result<Vec<FileOp>> {
    let mut ops = Vec::new();
    let renamed = map_folder(old);
    for asset in target.ids() {
        let text = asset.as_str();
        let is_map_file = text.starts_with("map/")
            && (text.ends_with("/data") || text.ends_with("/patch"))
            && !text.starts_with(&renamed);
        let Some(file) = is_map_file.then(|| target.file(asset)).flatten() else {
            continue;
        };
        let mut tree = read_yaml(file)?;
        let Some(warps) = tree.get_mut("warps").and_then(Value::as_sequence_mut) else {
            continue;
        };
        let mut changed = false;
        for warp in warps {
            if warp.get("target_map_id").and_then(Value::as_str) == Some(old) {
                warp["target_map_id"] = Value::from(new);
                changed = true;
            }
        }
        if changed {
            ops.push(FileOp::Write {
                path: file.to_path_buf(),
                bytes: crate::emit::to_yaml(&tree).into_bytes(),
            });
        }
    }
    Ok(ops)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::patch::tests::{Packs, fixture_map};

    const ENTRY: &str = "  filename: \"TESTA01\"\n  images_8bpp: 1\n  images_4bpp: 0\n  \
                         flags: 140\n  sound_id: 12\n  fixed_time_of_day: false\n  \
                         has_npcs: false\n  doors: null\n  toilet: 3\n  loading_name: 1\n";

    /// A base with one map (its day background) and a mod pack the edits go to.
    fn content() -> (Packs, Vfs) {
        let packs = Packs::new();
        packs.pack("base", "base", 0);
        packs.file("base/map/testa01/data.yaml", &fixture_map("null"));
        packs.file("base/map/testa01/background_day.png", "png");
        packs.file("base/map/testa01/texture8/000.png", "png");
        packs.file(
            "base/data/maps.yaml",
            &format!(
                "maps:\n- id: \"testa01\"\n  index: 0\n{ENTRY}map_names:\n- \"place a\"\n- \"place b\"\n"
            ),
        );
        packs.pack("my_mod", "mods", 0);
        let vfs = mount(&packs);
        (packs, vfs)
    }

    fn mount(packs: &Packs) -> Vfs {
        let location = |id: &str, origin| dwe_pack::PackLocation {
            root: packs.dir.path().join(id),
            origin,
        };
        Vfs::mount(&[
            location("base", dwe_pack::PackOrigin::Engine),
            location("my_mod", dwe_pack::PackOrigin::Mod),
        ])
        .unwrap()
    }

    fn table(vfs: &Vfs) -> MapTable {
        crate::load_map_table(vfs).unwrap()
    }

    fn target(vfs: &Vfs) -> &MountedPack {
        vfs.packs()
            .iter()
            .find(|pack| pack.id() == "my_mod")
            .unwrap()
    }

    fn new_copy(name: &str, copy_images: bool) -> NewMap {
        NewMap {
            name: name.to_owned(),
            title: "Prado".to_owned(),
            loading_name: 1,
            connect: None,
            template: Template::Copy {
                from: "testa01".to_owned(),
                copy_images,
                keep_exits: false,
            },
        }
    }

    fn create(packs: &Packs, new: &NewMap) -> Vfs {
        let vfs = mount(packs);
        let plan = plan_new_map(
            &vfs,
            &table(&vfs),
            &TriggerBands::default(),
            target(&vfs),
            new,
        )
        .unwrap();
        apply_ops(&plan.ops).unwrap();
        mount(packs)
    }

    #[test]
    fn a_copy_takes_the_template_its_images_by_reference_and_frees_its_exits() {
        let (packs, _) = content();
        let vfs = create(&packs, &new_copy("meadow", false));
        let table = table(&vfs);
        let entry = table.find("my_mod:meadow").unwrap();
        assert!(!entry.original_index);
        assert_eq!(entry.loading_name, 1);
        assert_eq!(entry.toilet, Some(3));
        let map = load_map(&vfs, "my_mod:meadow").unwrap();
        assert_eq!(map.assets_map(), "testa01");
        assert_eq!(map.warp(0).unwrap().target_map_id, None);
        assert_eq!(map.warp(0).unwrap().target_map, -1);
        // The exit's trigger is gone; the step trigger stays.
        assert_eq!(map.collision.value(Cell::new(10, 20)), FREE);
        assert_eq!(map.collision.value(Cell::new(11, 20)), 51);
        assert_eq!(crate::backgrounds(&vfs, map.assets_map()).len(), 1);
        let es = std::fs::read_to_string(packs.dir.path().join("my_mod/text/es/my_mod.yaml"));
        assert_eq!(es.unwrap(), "map_names.meadow: Prado\n");
        assert!(packs.dir.path().join("my_mod/text/en/my_mod.yaml").exists());
        // The table the pack writes has no index: the engine numbers a pack's maps.
        let own = std::fs::read_to_string(packs.dir.path().join("my_mod/data/maps.yaml")).unwrap();
        assert!(!own.contains("index"));
        assert_eq!(own_maps(target(&vfs)), ["my_mod:meadow"]);
    }

    #[test]
    fn a_copy_may_take_its_own_images() {
        let (packs, _) = content();
        let vfs = create(&packs, &new_copy("meadow", true));
        let map = load_map(&vfs, "my_mod:meadow").unwrap();
        assert_eq!(map.assets_map(), "my_mod:meadow");
        let folder = packs.dir.path().join("my_mod/map/my_mod/meadow");
        assert!(folder.join("background_day.png").exists());
        assert!(folder.join("texture8/000.png").exists());
    }

    #[test]
    fn names_ids_and_areas_are_checked() {
        let (packs, vfs) = content();
        let table = table(&vfs);
        let plan =
            |new: &NewMap| plan_new_map(&vfs, &table, &TriggerBands::default(), target(&vfs), new);
        assert!(plan(&new_copy("Bad-Name", false)).is_err());
        let mut far = new_copy("meadow", false);
        far.loading_name = 9;
        assert!(plan(&far).is_err());
        let mut untitled = new_copy("meadow", false);
        untitled.title = " ".to_owned();
        assert!(plan(&untitled).is_err());
        let vfs = create(&packs, &new_copy("meadow", false));
        let error = plan_new_map(
            &vfs,
            &self::table(&vfs),
            &TriggerBands::default(),
            target(&vfs),
            &new_copy("meadow", false),
        )
        .unwrap_err();
        assert!(error.to_string().contains("already exists"));
    }

    #[test]
    fn a_blank_map_has_a_generated_background_and_walls_out_of_sight() {
        let (packs, _) = content();
        let new = NewMap {
            name: "field".to_owned(),
            title: "Campo".to_owned(),
            loading_name: 0,
            template: Template::Blank {
                width: 2,
                height: 2,
                image: None,
            },
            connect: Some(Connection {
                map: "testa01".to_owned(),
                exit: 0,
                own_exit: 3,
            }),
        };
        let vfs = create(&packs, &new);
        let map = load_map(&vfs, "my_mod:field").unwrap();
        assert_eq!((map.tiles.width, map.tiles.height), (2, 2));
        assert!(map.npcs.is_empty());
        // Exit 3 leads to the base's map, through two trigger cells; the arrival is on free ground.
        let exit = &map.warps[3];
        assert_eq!(exit.target_map_id.as_deref(), Some("testa01"));
        assert_eq!((exit.target_map, exit.target_exit), (0, 0));
        assert!(
            map.warps
                .iter()
                .filter(|warp| warp.exit != 3)
                .all(|warp| warp.target_map == -1)
        );
        let doors = map
            .collision
            .cells()
            .filter(|&(_, value)| value == 113)
            .count();
        assert_eq!(doors, 2);
        let arrival = world::cell_at(world::to_world(exit.spawn.into()));
        assert_eq!(map.collision.value(arrival), FREE);
        assert_eq!(map.assets_map(), "my_mod:field");
        let walls = map
            .collision
            .cells()
            .filter(|&(_, value)| value == BLOCKED_BIT)
            .count();
        assert!(walls > 0 && walls < 10_000);
        let png = std::fs::read(
            packs
                .dir
                .path()
                .join("my_mod/map/my_mod/field/background_day.png"),
        );
        let decoder = png::Decoder::new(Cursor::new(png.unwrap()));
        let info = decoder.read_info().unwrap().info().clone();
        assert_eq!((info.width, info.height), (256, 256));
        assert_eq!(info.color_type, png::ColorType::Indexed);
        let entry = self::table(&vfs).find("my_mod:field").cloned().unwrap();
        assert!(entry.fixed_time_of_day);
    }

    #[test]
    fn a_users_background_must_be_whole_tiles_and_becomes_indexed() {
        let folder = tempfile::tempdir().unwrap();
        let file = folder.path().join("bg.png");
        let write = |width: u32, height: u32| {
            let mut out = Vec::new();
            let mut encoder = png::Encoder::new(&mut out, width, height);
            encoder.set_color(png::ColorType::Rgb);
            encoder.set_depth(png::BitDepth::Eight);
            let mut writer = encoder.write_header().unwrap();
            let pixels: Vec<u8> = (0..width * height)
                .flat_map(|i| [(i % 3) as u8 * 50, 10, 20])
                .collect();
            writer.write_image_data(&pixels).unwrap();
            drop(writer);
            std::fs::write(&file, out).unwrap();
        };
        write(256, 128);
        let (tiles, png) = user_background(&file).unwrap();
        assert_eq!(tiles, IVec2::new(2, 1));
        let info = png::Decoder::new(Cursor::new(png))
            .read_info()
            .unwrap()
            .info()
            .clone();
        assert_eq!(info.color_type, png::ColorType::Indexed);
        write(200, 128);
        assert!(
            user_background(&file)
                .unwrap_err()
                .to_string()
                .contains("whole tiles")
        );
    }

    #[test]
    fn a_pack_renames_and_deletes_only_its_own_maps() {
        let (packs, _) = content();
        let vfs = create(&packs, &new_copy("meadow", false));
        // An exit of the pack's own patch leads to the map.
        packs.file(
            "my_mod/map/testa01/patch.yaml",
            "warps:\n- exit: 1\n  target_map: -1\n  target_map_id: \"my_mod:meadow\"\n  target_exit: 0\n",
        );
        let vfs = {
            drop(vfs);
            mount(&packs)
        };
        assert!(plan_delete_map(target(&vfs), "testa01").is_err());
        let plan =
            plan_rename_map(&vfs, &table(&vfs), target(&vfs), "my_mod:meadow", "glade").unwrap();
        apply_ops(&plan.ops).unwrap();
        let vfs = mount(&packs);
        assert!(load_map(&vfs, "my_mod:glade").is_ok());
        assert!(table(&vfs).find("my_mod:meadow").is_none());
        assert_eq!(
            load_map(&vfs, "testa01")
                .unwrap()
                .warp(1)
                .unwrap()
                .target_map_id
                .as_deref(),
            Some("my_mod:glade")
        );
        let es = std::fs::read_to_string(packs.dir.path().join("my_mod/text/es/my_mod.yaml"));
        assert_eq!(es.unwrap(), "map_names.glade: Prado\n");
        assert!(!packs.dir.path().join("my_mod/map/my_mod/meadow").exists());

        let plan = plan_delete_map(target(&vfs), "my_mod:glade").unwrap();
        apply_ops(&plan.ops).unwrap();
        let vfs = mount(&packs);
        assert!(table(&vfs).find("my_mod:glade").is_none());
        assert!(!packs.dir.path().join("my_mod/map/my_mod").exists());
        assert!(own_maps(target(&vfs)).is_empty());
    }
}
