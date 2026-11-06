//! A field map's document, `map/<id>/data` (content/schemas/map.schema.json,
//! docs/formats/mapas.md): the parts the map tools show. Numbers stay as the extractor wrote them,
//! in the original's axes; [`crate::world`] converts them. Sections the tools do not use yet
//! (regions, images, the ambient color) are skipped when reading; the lights and the objects are
//! read with the engine's keys, so that a patch that breaks them is refused as the game refuses
//! it ([`crate::patch`]).

use glam::{IVec2, IVec3};
use serde::{Deserialize, Serialize};
use serde_yaml_ng::{Mapping, Value};

use crate::camera::MapCameraDesc;
use crate::collision::{Collision, CollisionSection};
use crate::{Error, Result};

/// A vector of the original's data.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct RawVec3 {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

impl From<RawVec3> for IVec3 {
    fn from(value: RawVec3) -> Self {
        IVec3::new(value.x, value.y, value.z)
    }
}

/// The map's 3D camera (map.c: loadMapSetup); the original doubles each coordinate.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct CameraSetup {
    pub viewpoint: RawVec3,
    pub reference: RawVec3,
    pub projection: i32,
}

/// A color of the original's lights.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Rgb {
    pub r: i32,
    pub g: i32,
    pub b: i32,
}

/// One of the map's directional lights.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Light {
    pub direction: RawVec3,
    pub color: Rgb,
}

/// A sprite of the map's animated objects (the original's map object sprites).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ObjectSprite {
    pub tex_x: i32,
    pub tex_y: i32,
    pub width: i32,
    pub height: i32,
    pub position: RawVec3,
    pub clut: i32,
    pub transparency: i32,
}

/// One frame of an object instance: the sprite shown and for how long.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ObjectFrame {
    pub sprite: i32,
    pub time: i32,
}

/// An animated object placed on the background.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ObjectInstance {
    pub frames: Vec<ObjectFrame>,
    pub x: i32,
    pub y: i32,
    pub flag: i32,
}

/// The map's animated objects.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct MapObjects {
    pub sprites: Vec<ObjectSprite>,
    pub instances: Vec<ObjectInstance>,
}

/// The background's grid of 128 x 128 pixel tiles.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Tiles {
    pub width: i32,
    pub height: i32,
    pub rows: Vec<String>,
}

/// An exit of the map (the original's MapWarps): where the player appears entering through it, and
/// where its warp trigger `110 + exit` leads.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Warp {
    pub exit: i32,
    pub spawn: RawVec3,
    /// 4096 per turn.
    pub rotation: i32,
    /// The original's raw map index; meaningless for unused exits.
    pub target_map: i32,
    /// The target's id in `data/maps`, or None.
    pub target_map_id: Option<String>,
    pub target_exit: i32,
}

/// An NPC slot of the map (overworld.c): the fields the tools show, and the others as the
/// document has them.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct Npc {
    pub type_id: i32,
    pub follow_mode: i32,
    pub position: RawVec3,
    pub rotation: RawVec3,
    pub tracking_range: i32,
    pub script_id: i32,
    /// What each part of the NPC's path does: 0 wait, 1 walk, 2 run, -1 end.
    #[serde(default)]
    pub ai_sections: Vec<i32>,
    /// Per part of the path: the point to walk or run to, or, for a wait, its frames in `x`.
    #[serde(default)]
    pub waypoints: Vec<RawVec3>,
    /// The slot's other fields (stats, moves, the species...), in the document's order; the tools
    /// keep them as they are and may change their numbers.
    #[serde(skip)]
    pub other: Mapping,
}

/// The keys of [`Npc`]'s own fields.
const NPC_FIELDS: [&str; 8] = [
    "type_id",
    "follow_mode",
    "position",
    "rotation",
    "tracking_range",
    "script_id",
    "ai_sections",
    "waypoints",
];

/// One part of an NPC's path, as the game plays it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathPart {
    /// Stand for this many frames.
    Wait(i32),
    /// Walk to this point (original axes).
    Walk(RawVec3),
    /// Run to this point (original axes).
    Run(RawVec3),
}

/// `ai_sections` values.
const PATH_WAIT: i32 = 0;
const PATH_WALK: i32 = 1;
const PATH_RUN: i32 = 2;
const PATH_END: i32 = -1;

impl Npc {
    /// The parts of the path up to its end; the game then starts again from the first one. A part
    /// without its waypoint reads zeros, like the game (a wait of 0 frames, or the map's origin).
    /// An unknown part stops the path there.
    pub fn path(&self) -> Vec<PathPart> {
        let waypoint = |part: usize| self.waypoints.get(part).copied().unwrap_or_default();
        self.ai_sections
            .iter()
            .enumerate()
            .take_while(|&(_, &section)| section != PATH_END)
            .map_while(|(part, &section)| match section {
                PATH_WAIT => Some(PathPart::Wait(waypoint(part).x)),
                PATH_WALK => Some(PathPart::Walk(waypoint(part))),
                PATH_RUN => Some(PathPart::Run(waypoint(part))),
                _ => None,
            })
            .collect()
    }
}

#[derive(Deserialize)]
struct RawDocument {
    camera: CameraSetup,
    #[serde(default)]
    lights: Vec<Light>,
    tiles: Tiles,
    #[serde(default)]
    objects: Option<MapObjects>,
    warps: Vec<Warp>,
    #[serde(default)]
    npcs: Vec<Npc>,
    collision: CollisionSection,
}

/// An exit that a patch changed, and the pack whose patch changed it last (MapExitPatch).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ExitPatch {
    pub exit: i32,
    pub pack: String,
    /// Whether that pack has the base's rights.
    pub base_rights: bool,
}

/// A map's document.
#[derive(Clone, Debug, PartialEq)]
pub struct MapDocument {
    /// The map id (`area_a`, or `pack:name` for a pack's map).
    pub id: String,
    pub camera: CameraSetup,
    pub lights: Vec<Light>,
    pub tiles: Tiles,
    pub objects: Option<MapObjects>,
    pub warps: Vec<Warp>,
    pub npcs: Vec<Npc>,
    pub collision: Collision,
    /// The packs whose `map/<id>/patch` changed the data, bottom to top.
    pub patches: Vec<String>,
    /// The exits those patches changed, by exit.
    pub patched_exits: Vec<ExitPatch>,
    /// The whole document as read (with the patches' fields put in): the sections and keys the
    /// fields above leave out, in their order, for [`MapDocument::to_tree`].
    pub source: Mapping,
}

impl MapDocument {
    /// Parses the text of `map/<id>/data`.
    pub fn parse(id: &str, yaml: &str) -> Result<Self> {
        let invalid = |error: String| Error::Invalid(format!("map '{id}': {error}"));
        // Typed first, for the line of a problem; then the tree, for what it leaves out.
        let raw: RawDocument =
            serde_yaml_ng::from_str(yaml).map_err(|error| invalid(error.to_string()))?;
        let tree: Value =
            serde_yaml_ng::from_str(yaml).map_err(|error| invalid(error.to_string()))?;
        Self::from_raw(id, raw, &tree).map_err(invalid)
    }

    /// Reads a document's tree, as a patch left it; the problem comes without the map's id.
    pub(crate) fn from_tree(id: &str, tree: &Value) -> std::result::Result<Self, String> {
        let raw: RawDocument =
            serde_yaml_ng::from_value(tree.clone()).map_err(|error| error.to_string())?;
        Self::from_raw(id, raw, tree)
    }

    fn from_raw(id: &str, raw: RawDocument, tree: &Value) -> std::result::Result<Self, String> {
        let collision =
            Collision::from_section(&raw.collision).map_err(|error| error.to_string())?;
        let source = tree.as_mapping().cloned().unwrap_or_default();
        let mut npcs = raw.npcs;
        if let Some(Value::Sequence(records)) = source.get(NPCS) {
            for (npc, record) in npcs.iter_mut().zip(records) {
                if let Value::Mapping(record) = record {
                    npc.other = record
                        .iter()
                        .filter(|(key, _)| {
                            !key.as_str().is_some_and(|key| NPC_FIELDS.contains(&key))
                        })
                        .map(|(key, value)| (key.clone(), value.clone()))
                        .collect();
                }
            }
        }
        Ok(Self {
            id: id.to_owned(),
            camera: raw.camera,
            lights: raw.lights,
            tiles: raw.tiles,
            objects: raw.objects,
            warps: raw.warps,
            npcs,
            collision,
            patches: Vec::new(),
            patched_exits: Vec::new(),
            source,
        })
    }

    /// The document as a tree: what was read, with the fields above put over it. Keys keep their
    /// place, unknown keys and the sections the fields leave out stay as they were, and a
    /// collision grid with the values it was read with keeps its text.
    pub fn to_tree(&self) -> Value {
        let mut tree = Value::Mapping(self.source.clone());
        let Value::Mapping(root) = &mut tree else {
            unreachable!()
        };
        put(root, "camera", to_value(&self.camera), false);
        put(root, "lights", to_value(&self.lights), true);
        put(root, "tiles", to_value(&self.tiles), false);
        put(root, "objects", to_value(&self.objects), true);
        put(root, "warps", to_value(&self.warps), false);
        let records = match root.get(NPCS) {
            Some(Value::Sequence(records)) => records.clone(),
            _ => Vec::new(),
        };
        let npcs = self
            .npcs
            .iter()
            .enumerate()
            .map(|(slot, npc)| {
                let Value::Mapping(mut fields) = to_value(npc) else {
                    unreachable!()
                };
                // An empty path list the record does not have stays out; a new slot writes both,
                // since the game's loader needs them.
                let record = records.get(slot).and_then(Value::as_mapping);
                fields.retain(|key, value| {
                    !matches!(value, Value::Sequence(items) if items.is_empty())
                        || record.is_none_or(|record| record.contains_key(key))
                });
                for (key, value) in &npc.other {
                    fields.insert(key.clone(), value.clone());
                }
                Value::Mapping(fields)
            })
            .collect();
        put(root, NPCS, Value::Sequence(npcs), true);
        let kept = root
            .get(COLLISION)
            .and_then(|section| serde_yaml_ng::from_value::<CollisionSection>(section.clone()).ok())
            .and_then(|section| Collision::from_section(&section).ok())
            .is_some_and(|read| read == self.collision);
        if !kept {
            root.insert(
                Value::from(COLLISION),
                to_value(&self.collision.to_section()),
            );
        }
        tree
    }

    /// The document's text, written as `dwx` writes it ([`crate::emit`]).
    pub fn to_yaml(&self) -> String {
        crate::emit::to_yaml(&self.to_tree())
    }

    /// The camera in world space (render::mapCameraDesc).
    pub fn camera_desc(&self) -> MapCameraDesc {
        MapCameraDesc::from_original(
            self.camera.viewpoint.into(),
            self.camera.reference.into(),
            self.camera.projection,
            IVec2::new(self.tiles.width, self.tiles.height),
        )
    }

    /// The exit with this number.
    pub fn warp(&self, exit: i32) -> Option<&Warp> {
        self.warps.iter().find(|warp| warp.exit == exit)
    }

    /// The patch that changed an exit last, or None.
    pub fn exit_patch(&self, exit: i32) -> Option<&ExitPatch> {
        self.patched_exits.iter().find(|patch| patch.exit == exit)
    }
}

const NPCS: &str = "npcs";
const COLLISION: &str = "collision";

fn to_value<T: Serialize>(value: &T) -> Value {
    serde_yaml_ng::to_value(value).unwrap_or(Value::Null)
}

/// Puts a value at a key: over the value there, key by key (and item by item for lists of the same
/// length), so that every key keeps its place and the keys the value does not know stay. A key
/// the mapping does not have is added at the end, unless `optional` and the value is empty.
fn put(mapping: &mut Mapping, key: &str, value: Value, optional: bool) {
    match mapping.get_mut(key) {
        Some(old) => merge(old, value),
        None => {
            let empty = match &value {
                Value::Null => true,
                Value::Sequence(items) => items.is_empty(),
                _ => false,
            };
            if !(optional && empty) {
                mapping.insert(Value::from(key), value);
            }
        }
    }
}

fn merge(old: &mut Value, value: Value) {
    match (old, value) {
        (Value::Mapping(old), Value::Mapping(fields)) => {
            for (key, value) in fields {
                match old.get_mut(&key) {
                    Some(slot) => merge(slot, value),
                    None => {
                        old.insert(key, value);
                    }
                }
            }
        }
        (Value::Sequence(old), Value::Sequence(items)) if old.len() == items.len() => {
            for (slot, item) in old.iter_mut().zip(items) {
                merge(slot, item);
            }
        }
        (old, value) => *old = value,
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::world::Cell;

    /// A small synthetic map document: neutral numbers, the layout `dwx` writes.
    pub(crate) fn sample_yaml() -> String {
        let mut rows = vec!["#".repeat(100); 100];
        rows[50] = format!(
            "{}{}{}",
            "#".repeat(40),
            ".".repeat(10) + "DD",
            "#".repeat(48)
        );
        rows[51] = format!(
            "{}{}{}",
            "#".repeat(40),
            "AAB".to_owned() + &".".repeat(9),
            "#".repeat(48)
        );
        let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
        format!(
            "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
             lights: []\nambient: {{r: 0, g: 0, b: 0}}\n\
             tiles:\n  width: 3\n  height: 2\n  rows:\n  - \"0 1 2\"\n  - \"3 4 -1\"\n\
             objects: null\n\
             warps:\n\
             - exit: 0\n  spawn: {{x: 100, y: 0, z: -200}}\n  rotation: 1024\n  target_map: 5\n  target_map_id: \"area_b\"\n  target_exit: 1\n\
             - exit: 1\n  spawn: {{x: 0, y: 0, z: 0}}\n  rotation: 0\n  target_map: -1\n  target_map_id: null\n  target_exit: -1\n\
             npcs:\n\
             - type_id: 7\n  follow_mode: 1\n  position: {{x: -300, y: 0, z: 400}}\n  rotation: {{x: 0, y: 2048, z: 0}}\n  tracking_range: 500\n  script_id: 3\n  hp: 1\n  waypoints:\n  - {{x: 10, y: 0, z: 20}}\n\
             collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
             \x20 - cell: \".\"\n    value: 0\n  - cell: \"A\"\n    value: 51\n  - cell: \"B\"\n    value: 80\n\
             \x20 - cell: \"D\"\n    value: 110\n  - cell: \"#\"\n    value: 128\n  rows:\n{rows}"
        )
    }

    #[test]
    fn reads_the_parts_the_tools_show() {
        let map = MapDocument::parse("area_a", &sample_yaml()).unwrap();
        assert_eq!(map.tiles.width, 3);
        assert_eq!(map.warps.len(), 2);
        assert_eq!(
            map.warp(0).unwrap().target_map_id.as_deref(),
            Some("area_b")
        );
        assert_eq!(map.npcs[0].waypoints.len(), 1);
        assert_eq!(map.collision.value(Cell::new(50, 50)), 110);
        assert_eq!(map.collision.value(Cell::new(40, 51)), 51);
        assert_eq!(map.camera_desc().focal_length, 1000.0);
        assert!(MapDocument::parse("area_a", "camera: 1\n").is_err());
    }

    #[test]
    fn path_pairs_parts_with_waypoints_until_the_end() {
        let point = |x| RawVec3 { x, y: 0, z: 0 };
        let npc = |ai_sections: Vec<i32>, waypoints: Vec<RawVec3>| Npc {
            type_id: 0,
            follow_mode: 0,
            position: RawVec3::default(),
            rotation: RawVec3::default(),
            tracking_range: 0,
            script_id: 0,
            ai_sections,
            waypoints,
            other: Mapping::new(),
        };
        let walker = npc(
            vec![1, 0, 2, -1, 1],
            vec![point(5), point(30), point(-7), point(9)],
        );
        assert_eq!(
            walker.path(),
            [
                PathPart::Walk(point(5)),
                PathPart::Wait(30),
                PathPart::Run(point(-7))
            ]
        );
        // Fewer waypoints than parts: the missing ones read zeros.
        let short = npc(vec![0, 1], vec![point(12)]);
        assert_eq!(short.path(), [PathPart::Wait(12), PathPart::Walk(point(0))]);
        // An unknown part stops the path.
        assert_eq!(
            npc(vec![1, 9, 1], vec![point(1); 3]).path(),
            [PathPart::Walk(point(1))]
        );
        assert!(npc(vec![], vec![point(1)]).path().is_empty());
    }

    fn tree(text: &str) -> Value {
        serde_yaml_ng::from_str(text).unwrap()
    }

    #[test]
    fn writes_back_what_it_read_with_the_keys_it_skips() {
        let text = sample_yaml();
        let map = MapDocument::parse("area_a", &text).unwrap();
        assert_eq!(map.npcs[0].other.get("hp"), Some(&Value::from(1)));
        let written = map.to_yaml();
        assert_eq!(tree(&written), tree(&text));
        // Written as dwx writes: block style, so a second pass gives the same text.
        let again = MapDocument::parse("area_a", &written).unwrap();
        assert_eq!(again.to_yaml(), written);
        assert_eq!(
            again,
            MapDocument {
                source: again.source.clone(),
                ..map
            }
        );
    }

    #[test]
    fn edits_land_in_place_and_keep_the_rest() {
        let mut map = MapDocument::parse("area_a", &sample_yaml()).unwrap();
        map.warps[1].target_map_id = Some("area_c".to_owned());
        map.npcs[0].position.x = 55;
        map.npcs[0].waypoints.clear();
        map.npcs[0].ai_sections = vec![1, -1];
        map.npcs[0].other.insert(Value::from("hp"), Value::from(9));
        map.collision.set(Cell::new(1, 1), 51);
        let back = MapDocument::parse("area_a", &map.to_yaml()).unwrap();
        assert_eq!(back.warps[1].target_map_id.as_deref(), Some("area_c"));
        assert_eq!(back.npcs[0].position.x, 55);
        assert!(back.npcs[0].waypoints.is_empty());
        assert_eq!(back.npcs[0].ai_sections, [1, -1]);
        assert_eq!(back.npcs[0].other.get("hp"), Some(&Value::from(9)));
        assert_eq!(back.collision.value(Cell::new(1, 1)), 51);
        let written = tree(&map.to_yaml());
        let keys: Vec<_> = written["npcs"][0]
            .as_mapping()
            .unwrap()
            .keys()
            .filter_map(Value::as_str)
            .collect();
        assert_eq!(
            keys,
            [
                "type_id",
                "follow_mode",
                "position",
                "rotation",
                "tracking_range",
                "script_id",
                "hp",
                "waypoints",
                "ai_sections"
            ]
        );
        assert_eq!(written["ambient"], tree("{r: 0, g: 0, b: 0}"));
    }

    #[test]
    fn a_new_slot_writes_its_empty_path() {
        let mut map = MapDocument::parse("area_a", &sample_yaml()).unwrap();
        let mut npc = map.npcs[0].clone();
        npc.waypoints.clear();
        map.npcs.push(npc);
        let written = tree(&map.to_yaml());
        let added = written["npcs"][map.npcs.len() - 1].as_mapping().unwrap();
        assert_eq!(added.get("waypoints"), Some(&Value::Sequence(Vec::new())));
        assert!(added.contains_key("ai_sections"));
    }
}
