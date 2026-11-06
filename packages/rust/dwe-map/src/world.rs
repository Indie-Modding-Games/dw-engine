//! World space (docs/architecture/world-space.md): right-handed, +Y up, in the original's units;
//! 100 units are one collision cell. The original's `(x, y, z)` is `(x, -y, -z)` here. Mirrors
//! the world-space helpers of `packages/engine/assets/include/dwe/assets/map_document.hpp`.

use glam::{IVec3, Vec3};

/// The side of a collision cell, in world units.
pub const CELL_SIZE: f32 = 100.0;

/// Cells per side of the collision grid.
pub const GRID_SIZE: i32 = 100;

/// The cell of the world origin along each axis.
const GRID_ORIGIN: i32 = GRID_SIZE / 2;

/// One turn in the original's angle units.
pub const ORIGINAL_ANGLE_PER_TURN: i32 = 4096;

/// A cell of the collision grid: column along world +X, row along world +Z. Row `r` is the
/// `r`-th string of the document's `collision.rows`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Cell {
    pub column: i32,
    pub row: i32,
}

impl Cell {
    pub fn new(column: i32, row: i32) -> Self {
        Self { column, row }
    }

    pub fn is_inside(self) -> bool {
        (0..GRID_SIZE).contains(&self.column) && (0..GRID_SIZE).contains(&self.row)
    }
}

/// A point of the original's data (x, y down, z) in world space.
pub fn to_world(original: IVec3) -> Vec3 {
    from_original_axes(original.as_vec3())
}

/// A point or direction of the original's axes in world space; the same turn back.
pub fn from_original_axes(original: Vec3) -> Vec3 {
    Vec3::new(original.x, -original.y, -original.z)
}

/// The cell under a world position: `floor(x / 100) + 50`, `floor(z / 100) + 50`
/// (collisionCellAt). The original truncates and corrects negatives, which differs only on the
/// cell edges.
pub fn cell_at(world: Vec3) -> Cell {
    Cell {
        column: (world.x / CELL_SIZE).floor() as i32 + GRID_ORIGIN,
        row: (world.z / CELL_SIZE).floor() as i32 + GRID_ORIGIN,
    }
}

/// The world corner of a cell with the lowest x and z, on the ground.
pub fn cell_min(cell: Cell) -> Vec3 {
    Vec3::new(
        (cell.column - GRID_ORIGIN) as f32 * CELL_SIZE,
        0.0,
        (cell.row - GRID_ORIGIN) as f32 * CELL_SIZE,
    )
}

/// The world centre of a cell, on the ground.
pub fn cell_center(cell: Cell) -> Vec3 {
    cell_min(cell) + Vec3::new(CELL_SIZE / 2.0, 0.0, CELL_SIZE / 2.0)
}

/// The yaw in radians of an original Y rotation: `-rotation * 2π / 4096`. At yaw 0 a character
/// faces world +Z.
pub fn yaw_from_original(rotation: i32) -> f32 {
    -(rotation as f32) * std::f32::consts::TAU / ORIGINAL_ANGLE_PER_TURN as f32
}

/// The ground direction a character with this yaw faces: `(sin yaw, 0, cos yaw)`.
pub fn forward_of_yaw(yaw: f32) -> Vec3 {
    Vec3::new(yaw.sin(), 0.0, yaw.cos())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cells_follow_the_engine() {
        assert_eq!(cell_at(Vec3::ZERO), Cell::new(50, 50));
        assert_eq!(cell_at(Vec3::new(-1.0, 0.0, -1.0)), Cell::new(49, 49));
        assert_eq!(cell_at(Vec3::new(4999.0, 0.0, -5000.0)), Cell::new(99, 0));
        assert_eq!(cell_min(Cell::new(49, 51)), Vec3::new(-100.0, 0.0, 100.0));
        assert_eq!(cell_center(Cell::new(50, 50)), Vec3::new(50.0, 0.0, 50.0));
        assert!(Cell::new(99, 0).is_inside());
        assert!(!Cell::new(100, 0).is_inside());
    }

    #[test]
    fn original_axes_and_rotations() {
        assert_eq!(to_world(IVec3::new(1, 2, 3)), Vec3::new(1.0, -2.0, -3.0));
        // Rotation 0 faces the original's -Z, which is world +Z; 1024 faces the original's -X.
        assert!((forward_of_yaw(yaw_from_original(0)) - Vec3::Z).length() < 1e-6);
        assert!((forward_of_yaw(yaw_from_original(1024)) - Vec3::NEG_X).length() < 1e-6);
        assert!((forward_of_yaw(yaw_from_original(2048)) - Vec3::NEG_Z).length() < 1e-6);
    }
}
