//! The window: menus, tools and panels around the map canvas. Every change goes to the model as
//! an Action; the window only adds what needs the platform (dialogs, closing, the live link).

use std::path::PathBuf;

use eframe::egui;
use mapper_core::{Action, Mapper, Settings, text};

use crate::dialogs::NewPack;
use crate::live::LivePanel;
use crate::options::Options;
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
    /// The section of the command line, opened with the map.
    pending_section: Option<(u16, u16)>,
    /// Shows the timeline of the command line's section.
    pending_timeline: bool,
    /// Whether the primary button went down on the canvas and is still held.
    pub pointer_held: bool,
    /// The saves the live link has seen (to reload the game after each new one).
    seen_saves: u64,
    /// The "new mod pack" form, when open.
    pub new_pack: Option<NewPack>,
    pub show_shortcuts: bool,
    /// The text that narrows the list of target maps in the exit inspector.
    pub target_filter: String,
    /// Where the canvas's menu was opened.
    pub context_point: Option<crate::canvas::ContextPoint>,
    /// Ctrl+F asked for the map browser's filter to take the keyboard.
    pub focus_filter: bool,
    /// The text that narrows the events panel.
    pub events_filter: String,
    /// The last event the cursor was over on the canvas (script, section, kind), for the hover
    /// panel's button.
    pub last_event: Option<(u16, u16, dwe_story::EventKind)>,
    /// What the section editor keeps between frames.
    pub section_ui: crate::section::SectionUi,
    /// "Probar evento": the question, the test running and what it changed.
    pub probe: crate::probe::Probe,
    /// Archivo → Revertir asks first when the map has unsaved changes.
    pub confirm_revert: bool,
    pub show_settings: bool,
    /// The dwx path as typed in the settings.
    pub dwx_text: String,
    /// The text field with the keyboard, for undo while it has nothing typed.
    pub field_focus: crate::keys::FieldFocus,
    /// The timeline panel's zoom, drags and the section played in the game.
    pub timeline_ui: crate::timeline::TimelineUi,
    /// Where the canvas was drawn last frame (the section editor stays above the timeline).
    pub canvas_rect: Option<egui::Rect>,
}

impl MapperApp {
    pub fn new(creation: &eframe::CreationContext<'_>, options: Options) -> Self {
        // The proportional font lacks arrows (→) the texts use: the monospace one fills them in.
        let mut fonts = egui::FontDefinitions::default();
        if let Some(mono) = fonts.families.get(&egui::FontFamily::Monospace).cloned()
            && let Some(proportional) = fonts.families.get_mut(&egui::FontFamily::Proportional)
        {
            proportional.extend(mono);
        }
        creation.egui_ctx.set_fonts(fonts);
        let mut mapper = Settings::default_file().map_or_else(Mapper::new, Mapper::with_settings);
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
            pending_section: options.section,
            pending_timeline: options.timeline,
            pointer_held: false,
            seen_saves: 0,
            new_pack: None,
            show_shortcuts: false,
            target_filter: String::new(),
            context_point: None,
            focus_filter: false,
            events_filter: String::new(),
            last_event: None,
            section_ui: crate::section::SectionUi::default(),
            probe: crate::probe::Probe::default(),
            confirm_revert: false,
            show_settings: false,
            dwx_text: String::new(),
            field_focus: crate::keys::FieldFocus::default(),
            timeline_ui: crate::timeline::TimelineUi::default(),
            canvas_rect: None,
        }
    }

    pub fn apply(&mut self, action: Action) {
        self.mapper.apply(action);
    }

    /// Opens the settings with the fields as the settings have them.
    pub fn open_settings(&mut self) {
        self.dwx_text = self
            .mapper
            .settings
            .dwx
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_default();
        self.show_settings = true;
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

    /// After a new save, has a connected game read the map again (when asked to).
    fn reload_after_save(&mut self) {
        if self.mapper.saves == self.seen_saves {
            return;
        }
        self.seen_saves = self.mapper.saves;
        if self.mapper.reload_on_save() && self.live.is_connected() {
            self.live.reload();
        }
    }

    /// Closing the window with unsaved changes asks first; the model says when to close.
    fn handle_close(&mut self, ctx: &egui::Context) {
        let requested = ctx.input(|input| input.viewport().close_requested());
        if requested && !self.mapper.quit && self.mapper.is_dirty() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.apply(Action::Quit);
        }
        if self.mapper.quit {
            // The link drops the game's preview as it goes.
            self.live.disconnect();
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        }
    }
}

impl eframe::App for MapperApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.live.update(&mut self.mapper, self.pointer_held);
        if self.live.is_linked() {
            ctx.request_repaint_after(crate::live::REDRAW_EVERY);
        }
        crate::keys::handle(self, &ctx);
        crate::panels::top_bar(self, ui);
        crate::toolbar::show(self, ui);
        crate::panels::status_bar(self, ui);
        crate::timeline::panel(self, ui);
        crate::dialogs::problems(self, ui);
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
                    if let Some((script, section)) = self.pending_section.take() {
                        self.apply(Action::OpenSection { script, section });
                        if self.pending_timeline {
                            self.apply(Action::ShowTimeline(true));
                        }
                    }
                }
                self.refresh_background(&ctx);
                crate::canvas::show(self, ui);
            });
        crate::dialogs::show(self, &ctx);
        crate::section::show(self, &ctx);
        crate::probe::show(self, &ctx);
        crate::probe::tick(self);
        crate::timeline::tick(self);
        self.reload_after_save();
        self.handle_close(&ctx);
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
