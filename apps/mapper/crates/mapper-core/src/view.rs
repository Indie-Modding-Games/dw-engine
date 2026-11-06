//! The canvas's window on the background: which background pixel is at the canvas centre and how
//! many canvas pixels each background pixel takes.

use glam::Vec2;

const MIN_ZOOM: f32 = 0.1;
const MAX_ZOOM: f32 = 16.0;
/// The margin a fitted map keeps around it, as a share of the canvas.
const FIT_MARGIN: f32 = 0.95;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct View {
    /// The canvas size in pixels.
    pub canvas: Vec2,
    /// The background pixel at the canvas centre.
    pub center: Vec2,
    /// Canvas pixels per background pixel.
    pub zoom: f32,
}

impl Default for View {
    fn default() -> Self {
        Self {
            canvas: Vec2::new(800.0, 600.0),
            center: Vec2::ZERO,
            zoom: 1.0,
        }
    }
}

impl View {
    /// A background pixel in canvas pixels, from the canvas's top-left corner.
    pub fn to_canvas(&self, background: Vec2) -> Vec2 {
        (background - self.center) * self.zoom + self.canvas * 0.5
    }

    /// A canvas pixel on the background.
    pub fn to_background(&self, canvas: Vec2) -> Vec2 {
        (canvas - self.canvas * 0.5) / self.zoom + self.center
    }

    /// Shows the whole background, centred.
    pub fn fit(&mut self, background: Vec2) {
        let ratio = self.canvas / background.max(Vec2::ONE);
        self.zoom = (ratio.x.min(ratio.y) * FIT_MARGIN).clamp(MIN_ZOOM, MAX_ZOOM);
        self.center = background * 0.5;
    }

    /// Moves the view by a drag of `delta` canvas pixels.
    pub fn pan(&mut self, delta: Vec2) {
        self.center -= delta / self.zoom;
    }

    /// Zooms by `factor`, keeping the background pixel under `anchor` (canvas pixels) in place.
    pub fn zoom_at(&mut self, factor: f32, anchor: Vec2) {
        let fixed = self.to_background(anchor);
        self.zoom = (self.zoom * factor).clamp(MIN_ZOOM, MAX_ZOOM);
        self.center = fixed - (anchor - self.canvas * 0.5) / self.zoom;
    }

    pub fn set_canvas(&mut self, size: Vec2) {
        self.canvas = size.max(Vec2::ONE);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn zooming_keeps_the_anchor_and_panning_follows_the_drag() {
        let mut view = View::default();
        view.fit(Vec2::new(768.0, 640.0));
        assert_eq!(
            view.to_canvas(Vec2::new(384.0, 320.0)),
            Vec2::new(400.0, 300.0)
        );
        let anchor = Vec2::new(100.0, 450.0);
        let under = view.to_background(anchor);
        view.zoom_at(2.5, anchor);
        assert!((view.to_background(anchor) - under).length() < 1e-3);
        let before = view.to_canvas(Vec2::new(10.0, 10.0));
        view.pan(Vec2::new(30.0, -12.0));
        assert!(
            (view.to_canvas(Vec2::new(10.0, 10.0)) - before - Vec2::new(30.0, -12.0)).length()
                < 1e-3
        );
        view.zoom_at(1e6, anchor);
        assert_eq!(view.zoom, MAX_ZOOM);
    }
}
