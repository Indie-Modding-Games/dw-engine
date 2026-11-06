//! The window: panels around the map canvas. Every change goes to the model as an Action.

use std::path::PathBuf;

use eframe::egui;
use mapper_core::{Action, Mapper, text};

use crate::options::Options;
use crate::panels::LivePanel;
use crate::shot::Shot;

/// The background image on the GPU, and the model revision it was loaded for.
struct BackgroundTexture {
    revision: u64,
    file: Option<PathBuf>,
    texture: Option<egui::TextureHandle>,
}

pub struct MapperApp {
    pub mapper: Mapper,
    background: BackgroundTexture,
    pub live: LivePanel,
    shot: Option<Shot>,
    /// The map the browser last opened its group for and scrolled to.
    pub revealed_map: Option<String>,
    /// The map of the command line, opened on the first frame, once the canvas has its size.
    pending_map: Option<(String, Option<i32>)>,
}

impl MapperApp {
    pub fn new(_creation: &eframe::CreationContext<'_>, options: Options) -> Self {
        let mut mapper = Mapper::new();
        if let Some(layers) = &options.layers {
            mapper.layers = mapper_core::LayerSet::none();
            for &layer in layers {
                mapper.apply(Action::SetLayer(layer, true));
            }
        }
        if let Some(variant) = &options.variant {
            mapper.apply(Action::SetVariant(variant.clone()));
        }
        if let Some(group_by) = options.group_by {
            mapper.apply(Action::SetGroupBy(group_by));
        }
        if let Some(filter) = &options.filter {
            mapper.apply(Action::SetFilter(filter.clone()));
        }
        if let Some(folders) = options.folders() {
            mapper.apply(Action::OpenContent(folders));
        }
        let mut live = LivePanel::default();
        if let Some(endpoint) = &options.live {
            live.endpoint = endpoint.clone();
            live.connect();
        }
        Self {
            mapper,
            background: BackgroundTexture {
                revision: u64::MAX,
                file: None,
                texture: None,
            },
            live,
            shot: options.shot.clone().map(Shot::new),
            revealed_map: None,
            pending_map: options.map.map(|map| (map, options.exit)),
        }
    }

    pub fn apply(&mut self, action: Action) {
        self.mapper.apply(action);
    }

    /// Loads the background again when the map or its variant changed.
    fn refresh_background(&mut self, ctx: &egui::Context) {
        if self.background.revision == self.mapper.revision {
            return;
        }
        self.background.revision = self.mapper.revision;
        let file = self.mapper.background_file();
        if file == self.background.file {
            return;
        }
        self.background.texture = file.as_ref().and_then(|file| match load_image(file) {
            Ok(image) => Some(ctx.load_texture("background", image, egui::TextureOptions::LINEAR)),
            Err(error) => {
                self.mapper
                    .messages
                    .push(mapper_core::Message::Error(format!(
                        "{}: {error}",
                        file.display()
                    )));
                None
            }
        });
        self.background.file = file;
    }

    pub fn background_texture(&self) -> Option<&egui::TextureHandle> {
        self.background.texture.as_ref()
    }
}

impl eframe::App for MapperApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        crate::panels::top_bar(self, ui);
        crate::panels::status_bar(self, ui);
        crate::panels::browser(self, ui);
        crate::panels::details(self, ui);
        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(egui::Color32::from_gray(24)))
            .show(ui, |ui| {
                let size = ui.available_size();
                self.apply(Action::SetCanvasSize(glam::Vec2::new(size.x, size.y)));
                if let Some((map, exit)) = self.pending_map.take() {
                    match exit {
                        Some(exit) => self.apply(Action::OpenMapAtExit { map, exit }),
                        None => self.apply(Action::OpenMap(map)),
                    }
                }
                self.refresh_background(&ctx);
                crate::canvas::show(self, ui);
            });
        if let Some(shot) = &mut self.shot {
            shot.tick(&ctx);
        }
    }
}

/// Decodes a background image (PNG) for the GPU.
fn load_image(file: &std::path::Path) -> Result<egui::ColorImage, String> {
    let image = image::open(file)
        .map_err(|error| error.to_string())?
        .to_rgba8();
    let size = [image.width() as usize, image.height() as usize];
    Ok(egui::ColorImage::from_rgba_unmultiplied(
        size,
        image.as_raw(),
    ))
}

pub fn status_text(mapper: &Mapper) -> String {
    match (&mapper.content, &mapper.map) {
        (None, _) => text::NO_CONTENT.to_owned(),
        (Some(_), None) => text::NO_MAP.to_owned(),
        (Some(_), Some(map)) => map.document.id.clone(),
    }
}
