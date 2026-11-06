//! What the layers draw, projected once per map on the background with the map's own camera
//! (positions in background pixels). Collision cells lie on the ground plane y = 0; characters,
//! spawns and doors at their own height.

use dwe_map::world::{self, Cell, GRID_SIZE};
use dwe_map::{
    CellKind, MapCamera, MapDocument, MapEntry, MapTable, PathPart, TriggerBand, TriggerBands,
    WarpTarget, warp_target,
};
use glam::{IVec3, Vec2, Vec3};

/// How long a facing arrow is, in world units.
const ARROW_LENGTH: f32 = 120.0;

/// A segment in background pixels, from a position along a facing.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Arrow {
    pub from: Vec2,
    pub to: Vec2,
}

/// A collision cell that is not free.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CellShape {
    pub cell: Cell,
    pub value: u8,
    pub kind: CellKind,
    /// The band of a trigger cell.
    pub band: Option<TriggerBand>,
    /// The cell's corners on the background.
    pub quad: [Vec2; 4],
}

/// Where an exit leads, as the overlay shows it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ExitTarget {
    Map {
        id: String,
        exit: i32,
    },
    /// The id names no map; the original's index leads to this one.
    Fallback {
        id: String,
        exit: i32,
    },
    Conflict,
    Nowhere,
}

impl ExitTarget {
    fn of(target: WarpTarget<'_>, exit: i32) -> Self {
        match target {
            WarpTarget::Map(entry) => Self::Map {
                id: entry.id.clone(),
                exit,
            },
            WarpTarget::UnknownIdFallback(entry) => Self::Fallback {
                id: entry.id.clone(),
                exit,
            },
            WarpTarget::Conflict => Self::Conflict,
            WarpTarget::Nowhere => Self::Nowhere,
        }
    }

    /// The map and exit it leads to.
    pub fn destination(&self) -> Option<(&str, i32)> {
        match self {
            Self::Map { id, exit } | Self::Fallback { id, exit } => Some((id, *exit)),
            Self::Conflict | Self::Nowhere => None,
        }
    }
}

/// An exit of the map: its trigger cells, where it leads and where the player arrives through it.
#[derive(Clone, Debug, PartialEq)]
pub struct ExitMark {
    pub exit: i32,
    /// The centre of its trigger cells on the background; None when no cell has its trigger.
    pub label: Option<Vec2>,
    pub cell_count: usize,
    pub target: ExitTarget,
    /// Where the player appears entering this map through the exit, facing the way it faces.
    pub arrival: Option<Arrow>,
}

impl ExitMark {
    /// Whether the exit is used: it has trigger cells or leads somewhere.
    pub fn is_used(&self) -> bool {
        self.cell_count > 0 || self.target.destination().is_some()
    }
}

/// An NPC slot.
#[derive(Clone, Debug, PartialEq)]
pub struct NpcMark {
    pub slot: usize,
    pub type_id: i32,
    pub script_id: i32,
    pub position: Option<Arrow>,
    /// The points its path walks or runs to, in order; the path then starts again from the first.
    /// Waits are left out.
    pub waypoints: Vec<Vec2>,
    /// The part of the path (its place in `ai_sections`) of each point.
    pub parts: Vec<usize>,
}

/// A door model placed on the map.
#[derive(Clone, Debug, PartialEq)]
pub struct DoorMark {
    pub slot: usize,
    pub model_id: i32,
    pub position: Arrow,
}

/// Everything the layers draw for one map.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct MapOverlay {
    pub cells: Vec<CellShape>,
    pub exits: Vec<ExitMark>,
    pub npcs: Vec<NpcMark>,
    pub doors: Vec<DoorMark>,
    pub toilet: Option<[Vec2; 2]>,
    /// The grid's lines, each a polyline of projected corners (broken where a corner is behind
    /// the camera).
    pub grid: Vec<Vec<Vec2>>,
}

impl MapOverlay {
    pub fn build(
        map: &MapDocument,
        entry: Option<&MapEntry>,
        table: &MapTable,
        camera: &MapCamera,
        bands: &TriggerBands,
    ) -> Self {
        let project = |world: Vec3| camera.project_to_background(world).map(|point| point.pixel);
        let arrow = |position: IVec3, rotation: i32| -> Option<Arrow> {
            let from = world::to_world(position);
            let to =
                from + world::forward_of_yaw(world::yaw_from_original(rotation)) * ARROW_LENGTH;
            Some(Arrow {
                from: project(from)?,
                to: project(to)?,
            })
        };

        let mut cells = Vec::new();
        for (cell, value) in map.collision.cells() {
            let kind = dwe_map::collision::kind_of(value);
            if kind == CellKind::Free {
                continue;
            }
            let Some(quad) = cell_quad(cell, &project) else {
                continue;
            };
            let band = match kind {
                CellKind::Trigger(value) => Some(bands.band(value)),
                _ => None,
            };
            cells.push(CellShape {
                cell,
                value,
                kind,
                band,
                quad,
            });
        }

        let exits = map
            .warps
            .iter()
            .map(|warp| {
                let trigger = u8::try_from(warp.exit)
                    .ok()
                    .and_then(|exit| bands.exit_trigger(exit));
                let centres: Vec<Vec2> = cells
                    .iter()
                    .filter(|shape| Some(shape.value) == trigger)
                    .map(|shape| shape.quad.iter().copied().sum::<Vec2>() / 4.0)
                    .collect();
                let label = (!centres.is_empty())
                    .then(|| centres.iter().copied().sum::<Vec2>() / centres.len() as f32);
                ExitMark {
                    exit: warp.exit,
                    label,
                    cell_count: centres.len(),
                    target: ExitTarget::of(warp_target(table, warp), warp.target_exit),
                    arrival: arrow(warp.spawn.into(), warp.rotation),
                }
            })
            .collect();

        let npcs = map
            .npcs
            .iter()
            .enumerate()
            .map(|(slot, npc)| {
                let (parts, waypoints) = npc
                    .path()
                    .into_iter()
                    .enumerate()
                    .filter_map(|(part, step)| match step {
                        PathPart::Walk(point) | PathPart::Run(point) => {
                            Some((part, project(world::to_world(point.into()))?))
                        }
                        PathPart::Wait(_) => None,
                    })
                    .unzip();
                NpcMark {
                    slot,
                    type_id: npc.type_id,
                    script_id: npc.script_id,
                    position: arrow(npc.position.into(), npc.rotation.y),
                    waypoints,
                    parts,
                }
            })
            .collect();

        let doors = entry
            .and_then(|entry| table.doors_of(entry))
            .map(|set| {
                set.doors
                    .iter()
                    .enumerate()
                    .filter_map(|(slot, door)| {
                        Some(DoorMark {
                            slot,
                            model_id: door.model_id?,
                            position: arrow(door.position.into(), door.rotation)?,
                        })
                    })
                    .collect()
            })
            .unwrap_or_default();

        let toilet = entry
            .and_then(|entry| table.toilet_of(entry))
            .and_then(|toilet| {
                let [first, second] = toilet.points();
                Some([project(first)?, project(second)?])
            });

        Self {
            cells,
            exits,
            npcs,
            doors,
            toilet,
            grid: grid_lines(&project),
        }
    }

    /// The exit whose trigger a cell has.
    pub fn exit_at(&self, value: u8, bands: &TriggerBands) -> Option<&ExitMark> {
        match bands.band(value) {
            TriggerBand::Exit(exit) => self.exits.iter().find(|mark| mark.exit == i32::from(exit)),
            _ => None,
        }
    }
}

fn cell_quad(cell: Cell, project: &impl Fn(Vec3) -> Option<Vec2>) -> Option<[Vec2; 4]> {
    let min = world::cell_min(cell);
    let size = world::CELL_SIZE;
    Some([
        project(min)?,
        project(min + Vec3::new(size, 0.0, 0.0))?,
        project(min + Vec3::new(size, 0.0, size))?,
        project(min + Vec3::new(0.0, 0.0, size))?,
    ])
}

fn grid_lines(project: &impl Fn(Vec3) -> Option<Vec2>) -> Vec<Vec<Vec2>> {
    let mut lines = Vec::new();
    let corner = |column: i32, row: i32| world::cell_min(Cell::new(column, row));
    for along in 0..=GRID_SIZE {
        for points in [
            (0..=GRID_SIZE)
                .map(|i| corner(along, i))
                .collect::<Vec<_>>(),
            (0..=GRID_SIZE)
                .map(|i| corner(i, along))
                .collect::<Vec<_>>(),
        ] {
            let mut line = Vec::new();
            for point in points {
                match project(point) {
                    Some(pixel) => line.push(pixel),
                    None if line.len() > 1 => lines.push(std::mem::take(&mut line)),
                    None => line.clear(),
                }
            }
            if line.len() > 1 {
                lines.push(line);
            }
        }
    }
    lines
}
