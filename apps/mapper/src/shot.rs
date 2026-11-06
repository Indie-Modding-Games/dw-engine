//! `--shot`: a picture of the window once it has drawn a few frames, then quit. For the docs and
//! for checking the viewer on a machine without a screen (a virtual X display).

use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicU8, Ordering};

use eframe::egui;

/// Frames drawn before the picture, so the background and the panels have settled.
const FRAMES_BEFORE: u32 = 8;

const WAITING: u8 = 0;
const SAVED: u8 = 1;
const FAILED: u8 = 2;
const CLOSING: u8 = 3;

pub struct Shot {
    path: PathBuf,
    frames: u32,
    requested: bool,
    state: Arc<AtomicU8>,
}

impl Shot {
    pub fn new(path: PathBuf) -> Self {
        Self {
            path,
            frames: 0,
            requested: false,
            state: Arc::new(AtomicU8::new(WAITING)),
        }
    }

    /// Called every frame: asks for the picture after a few frames and quits once it is saved.
    pub fn tick(&mut self, ctx: &egui::Context) {
        match self.state.load(Ordering::Acquire) {
            SAVED => {
                eprintln!("dwe-mapper: saved {}", self.path.display());
                self.close(ctx);
                return;
            }
            FAILED => {
                self.close(ctx);
                return;
            }
            CLOSING => return,
            _ => {}
        }
        self.frames += 1;
        if !self.requested && self.frames >= FRAMES_BEFORE {
            self.requested = true;
            let path = self.path.clone();
            let state = Arc::clone(&self.state);
            let repaint = ctx.clone();
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(
                egui::ScreenshotCallback::new(move |image| {
                    let saved = save(&path, &image);
                    if let Err(error) = &saved {
                        eprintln!("dwe-mapper: {}: {error}", path.display());
                    }
                    state.store(
                        if saved.is_ok() { SAVED } else { FAILED },
                        Ordering::Release,
                    );
                    repaint.request_repaint();
                }),
            ));
        }
        ctx.request_repaint();
    }

    fn close(&self, ctx: &egui::Context) {
        self.state.store(CLOSING, Ordering::Release);
        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
    }
}

fn save(path: &std::path::Path, image: &egui::ColorImage) -> Result<(), String> {
    let [width, height] = image.size;
    let pixels: Vec<u8> = image
        .pixels
        .iter()
        .flat_map(|color| color.to_array())
        .collect();
    let buffer = image::RgbaImage::from_raw(width as u32, height as u32, pixels)
        .ok_or("the picture has the wrong size")?;
    buffer.save(path).map_err(|error| error.to_string())
}
