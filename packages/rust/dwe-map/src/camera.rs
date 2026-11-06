//! The fixed camera of a field map and its projection on the prerendered background, exactly as
//! `packages/engine/render/src/map_camera.cpp` computes them (docs/formats/mapas.md, "Cómo se
//! dibuja"): a background pixel is `H · view / depth + background centre`, with the eye and the
//! reference doubled from the document. The tests compare it with numbers the C++ printed.

use glam::{IVec2, IVec3, Mat3, Mat4, Vec2, Vec3};

use crate::world::from_original_axes;

/// The screen the original frames a map for, in pixels.
pub const ORIGINAL_SCREEN_SIZE: Vec2 = Vec2::new(320.0, 240.0);

/// The side of a background tile, in pixels.
pub const TILE_SIZE: i32 = 128;

/// The camera of a map in world space.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MapCameraDesc {
    pub eye: Vec3,
    pub target: Vec3,
    /// The projection distance H: background pixels per unit of view x / depth.
    pub focal_length: f32,
    /// The size of the whole background, in pixels.
    pub background_size: Vec2,
}

impl MapCameraDesc {
    /// A document's raw camera (the original's axes, before doubling) and its background size in
    /// tiles (mapCameraDesc): the original doubles the viewpoint and the reference.
    pub fn from_original(
        viewpoint: IVec3,
        reference: IVec3,
        projection: i32,
        tiles: IVec2,
    ) -> Self {
        let doubled = |value: IVec3| from_original_axes((value * 2).as_vec3());
        Self {
            eye: doubled(viewpoint),
            target: doubled(reference),
            focal_length: projection as f32,
            background_size: (tiles * TILE_SIZE).as_vec2(),
        }
    }
}

/// A world point projected on the background, and its distance along the camera's axis.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BackgroundPoint {
    pub pixel: Vec2,
    pub view_depth: f32,
}

/// A map camera for a viewport (MapCamera): the view is fixed; the viewport shows a window of the
/// background that follows a focus point and stops at the background's edges.
#[derive(Clone, Debug)]
pub struct MapCamera {
    desc: MapCameraDesc,
    view: Mat4,
    inverse_view: Mat4,
    viewport: Vec2,
    scale: f32,
    focus: Vec2,
    scroll: Vec2,
}

impl MapCamera {
    pub fn new(desc: MapCameraDesc) -> Self {
        let view = look_at_without_roll(desc.eye, desc.target);
        let mut camera = Self {
            desc,
            view,
            inverse_view: view.inverse(),
            viewport: ORIGINAL_SCREEN_SIZE,
            scale: 1.0,
            // map.c: setupMap starts centred.
            focus: desc.background_size * 0.5,
            scroll: Vec2::ZERO,
        };
        camera.set_viewport(ORIGINAL_SCREEN_SIZE);
        camera
    }

    pub fn desc(&self) -> &MapCameraDesc {
        &self.desc
    }

    pub fn view(&self) -> &Mat4 {
        &self.view
    }

    /// Sets the viewport in pixels and keeps the focus; sizes below one pixel count as one.
    pub fn set_viewport(&mut self, size: Vec2) {
        self.viewport = size.max(Vec2::ONE);
        self.scale = (self.viewport.x / ORIGINAL_SCREEN_SIZE.x)
            .min(self.viewport.y / ORIGINAL_SCREEN_SIZE.y);
        self.follow_background_pixel(self.focus);
    }

    /// Viewport pixels per background pixel.
    pub fn scale(&self) -> f32 {
        self.scale
    }

    /// The background pixel at the viewport's top-left corner.
    pub fn scroll(&self) -> Vec2 {
        self.scroll
    }

    pub fn visible_size(&self) -> Vec2 {
        self.viewport / self.scale
    }

    /// Centres the window on a world point, clamped to the background.
    pub fn follow(&mut self, world: Vec3) {
        if let Some(point) = self.project_to_background(world) {
            self.follow_background_pixel(point.pixel);
        }
    }

    /// Centres the window on a background pixel, clamped to the background.
    pub fn follow_background_pixel(&mut self, pixel: Vec2) {
        self.focus = pixel;
        let visible = self.visible_size();
        let size = self.desc.background_size;
        self.scroll = Vec2::new(
            scroll_axis(pixel.x, visible.x, size.x, self.scale),
            scroll_axis(pixel.y, visible.y, size.y, self.scale),
        );
    }

    /// The distance from the camera along its axis of a world point (the original's SZ).
    pub fn view_depth(&self, world: Vec3) -> f32 {
        -self.view.transform_point3(world).z
    }

    /// The background pixel a world point projects to; None behind the camera.
    pub fn project_to_background(&self, world: Vec3) -> Option<BackgroundPoint> {
        let view = self.view * world.extend(1.0);
        let depth = -view.z;
        if depth <= 0.0 {
            return None;
        }
        let original_view = Vec2::new(view.x, -view.y);
        Some(BackgroundPoint {
            pixel: self.desc.focal_length * original_view / depth + self.desc.background_size * 0.5,
            view_depth: depth,
        })
    }

    pub fn background_to_window(&self, pixel: Vec2) -> Vec2 {
        (pixel - self.scroll) * self.scale
    }

    pub fn window_to_background(&self, window: Vec2) -> Vec2 {
        window / self.scale + self.scroll
    }

    /// Where a world point appears in the viewport; None behind the camera.
    pub fn world_to_window(&self, world: Vec3) -> Option<Vec2> {
        self.project_to_background(world)
            .map(|point| self.background_to_window(point.pixel))
    }

    /// The world point under a window pixel on the plane y = `ground_height`; None when the ray
    /// does not meet it in front of the camera (pickGround).
    pub fn pick_ground(&self, window: Vec2, ground_height: f32) -> Option<Vec3> {
        self.ground_at_background(self.window_to_background(window), ground_height)
    }

    /// The world point under a background pixel on the plane y = `ground_height`.
    pub fn ground_at_background(&self, pixel: Vec2, ground_height: f32) -> Option<Vec3> {
        let offset = (pixel - self.desc.background_size * 0.5) / self.desc.focal_length;
        let view_direction = Vec3::new(offset.x, -offset.y, -1.0);
        let direction = Mat3::from_mat4(self.inverse_view) * view_direction;
        if direction.y.abs() < 1e-6 {
            return None;
        }
        let distance = (ground_height - self.desc.eye.y) / direction.y;
        if distance <= 0.0 {
            return None;
        }
        Some(self.desc.eye + direction * distance)
    }
}

/// GsSetRefView2 has no roll: a look-at with world +Y up, or -Z for a camera looking straight up
/// or down.
fn look_at_without_roll(eye: Vec3, target: Vec3) -> Mat4 {
    let forward = target - eye;
    let horizontal = forward.x.hypot(forward.z);
    let up = if horizontal > 1e-4 * forward.length() {
        Vec3::Y
    } else {
        Vec3::NEG_Z
    };
    Mat4::look_at_rh(eye, target, up)
}

/// The top-left background pixel of a window of `visible` pixels centred on `focus` along one
/// axis: clamped to the background, centred when the background is smaller, snapped to whole
/// viewport pixels.
fn scroll_axis(focus: f32, visible: f32, background: f32, scale: f32) -> f32 {
    if background < visible {
        return (background - visible) * 0.5;
    }
    let snapped = ((focus - visible * 0.5) * scale).round() / scale;
    snapped.clamp(0.0, background - visible)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn town_like() -> MapCameraDesc {
        MapCameraDesc::from_original(
            IVec3::new(0, -4000, -6000),
            IVec3::new(0, 0, -300),
            1800,
            IVec2::new(6, 5),
        )
    }

    #[test]
    fn the_reference_falls_on_the_background_centre() {
        let desc = town_like();
        let camera = MapCamera::new(desc);
        let point = camera.project_to_background(desc.target).unwrap();
        assert!((point.pixel - desc.background_size * 0.5).length() < 1e-3);
        assert_eq!(desc.background_size, Vec2::new(768.0, 640.0));
        assert!(
            camera
                .project_to_background(desc.eye + (desc.eye - desc.target))
                .is_none()
        );
    }

    #[test]
    fn picking_the_ground_inverts_the_projection() {
        let mut camera = MapCamera::new(town_like());
        camera.set_viewport(Vec2::new(1280.0, 720.0));
        camera.follow(Vec3::new(300.0, 0.0, 200.0));
        let world = Vec3::new(-450.0, 0.0, 1210.0);
        let window = camera.world_to_window(world).unwrap();
        let back = camera.pick_ground(window, 0.0).unwrap();
        assert!((back - world).length() < 0.05, "{back}");
    }

    #[test]
    fn the_window_is_clamped_to_the_background() {
        let mut camera = MapCamera::new(town_like());
        camera.follow_background_pixel(Vec2::new(-500.0, 9000.0));
        assert_eq!(camera.scroll(), Vec2::new(0.0, 640.0 - 240.0));
        // A background narrower than the window is centred, with bars.
        let mut small = MapCamera::new(MapCameraDesc::from_original(
            IVec3::new(0, -1000, -1000),
            IVec3::ZERO,
            300,
            IVec2::new(2, 1),
        ));
        small.follow_background_pixel(Vec2::ZERO);
        assert_eq!(small.scroll(), Vec2::new(-32.0, -56.0));
    }
}
