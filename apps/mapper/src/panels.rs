//! The panels around the canvas: the menu and layer bar, the map browser, the details of the map
//! shown, the live link and the status bar.

use dwe_map::CellKind;
use dwe_pack::GameFolders;
use eframe::egui;
use mapper_core::{Action, GroupBy, Layer, Message, text};
use mapper_live::{Endpoint, LiveClient};

use crate::app::MapperApp;

pub fn top_bar(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::top("top").show(ui, |ui| {
        ui.horizontal_wrapped(|ui| {
            ui.menu_button(text::MENU_FILE, |ui| {
                if ui.button(text::OPEN_CONTENT).clicked() {
                    ui.close();
                    if let Some(folder) = rfd::FileDialog::new().pick_folder() {
                        let team_packs = crate::options::find_team_packs(&folder);
                        let mods = app
                            .mapper
                            .content
                            .as_ref()
                            .map(|content| content.folders.mods.clone())
                            .unwrap_or_default();
                        app.apply(Action::OpenContent(GameFolders {
                            content: folder,
                            team_packs,
                            mods,
                        }));
                    }
                }
                if ui.button(text::QUIT).clicked() {
                    ui.ctx().send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });
            ui.separator();
            let back = !app.mapper.history.is_empty();
            if ui
                .add_enabled(back, egui::Button::new(text::BACK))
                .clicked()
            {
                app.apply(Action::Back);
            }
            if ui.button(text::FIT_VIEW).clicked() {
                app.apply(Action::FitView);
            }
            ui.separator();
            ui.label(text::LAYERS);
            for layer in Layer::ALL {
                let mut shown = app.mapper.layers.shows(layer);
                if ui.checkbox(&mut shown, layer.label()).changed() {
                    app.apply(Action::SetLayer(layer, shown));
                }
            }
            ui.separator();
            ui.label(text::TIME_OF_DAY);
            let variants: Vec<String> = app
                .mapper
                .map
                .as_ref()
                .map(|map| map.backgrounds.iter().map(|b| b.variant.clone()).collect())
                .unwrap_or_default();
            for variant in variants {
                let chosen = app.mapper.variant == variant;
                if ui
                    .selectable_label(chosen, text::variant_name(&variant))
                    .clicked()
                {
                    app.apply(Action::SetVariant(variant));
                }
            }
        });
    });
}

pub fn status_bar(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::bottom("status").show(ui, |ui| {
        ui.horizontal(|ui| match app.mapper.messages.last() {
            Some(Message::Error(message)) => {
                ui.colored_label(egui::Color32::LIGHT_RED, message);
            }
            Some(Message::Info(message)) => {
                ui.label(message);
            }
            None => {
                if let Some(content) = &app.mapper.content {
                    let packs: Vec<_> = content
                        .vfs()
                        .packs()
                        .iter()
                        .map(|pack| pack.id().to_owned())
                        .collect();
                    ui.label(format!(
                        "{} · {}",
                        content.folders.content.display(),
                        packs.join(" < ")
                    ));
                }
            }
        });
    });
}

pub fn browser(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::left("browser")
        .resizable(true)
        .default_size(230.0)
        .show(ui, |ui| {
            ui.heading(text::MAPS);
            let mut filter = app.mapper.filter.clone();
            if ui
                .add(egui::TextEdit::singleline(&mut filter).hint_text(text::FILTER_HINT))
                .changed()
            {
                app.apply(Action::SetFilter(filter));
            }
            ui.horizontal(|ui| {
                ui.label(text::GROUP_BY);
                for (group_by, label) in [
                    (GroupBy::LoadingName, text::GROUP_BY_NAME),
                    (GroupBy::Prefix, text::GROUP_BY_PREFIX),
                ] {
                    if ui
                        .selectable_label(app.mapper.group_by == group_by, label)
                        .clicked()
                    {
                        app.apply(Action::SetGroupBy(group_by));
                    }
                }
            });
            ui.separator();
            let shown = app.mapper.map.as_ref().map(|map| map.document.id.clone());
            let filtering = !app.mapper.filter.trim().is_empty();
            // Once per map shown: open its group and scroll to it.
            let reveal = shown.is_some() && shown != app.revealed_map;
            let mut revealed = false;
            let mut open = None;
            egui::ScrollArea::vertical().show(ui, |ui| {
                for group in app.mapper.groups() {
                    let has_shown = group.maps.iter().any(|map| Some(&map.id) == shown.as_ref());
                    let force_open = filtering || (reveal && has_shown);
                    egui::CollapsingHeader::new(format!("{} ({})", group.title, group.maps.len()))
                        .id_salt(&group.title)
                        .open(force_open.then_some(true))
                        .show(ui, |ui| {
                            for map in &group.maps {
                                let selected = Some(&map.id) == shown.as_ref();
                                let response = ui.selectable_label(selected, &map.id);
                                if selected && reveal {
                                    response.scroll_to_me(Some(egui::Align::Center));
                                    revealed = true;
                                }
                                if response.clicked() {
                                    open = Some(map.id.clone());
                                }
                            }
                        });
                }
            });
            // Until its label is drawn: the group may take a frame to open.
            if revealed || !reveal || filtering {
                app.revealed_map = shown;
            }
            if let Some(id) = open {
                app.apply(Action::OpenMap(id));
            }
        });
}

pub fn details(app: &mut MapperApp, ui: &mut egui::Ui) {
    egui::Panel::right("details")
        .resizable(true)
        .default_size(290.0)
        .show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                map_details(app, ui);
                ui.separator();
                hover_details(app, ui);
                ui.separator();
                live_details(app, ui);
            });
        });
}

fn map_details(app: &mut MapperApp, ui: &mut egui::Ui) {
    let mut action = None;
    {
        let mapper = &app.mapper;
        let Some(map) = &mapper.map else {
            ui.label(crate::app::status_text(mapper));
            return;
        };
        ui.heading(format!("{} {}", text::MAP_INFO, map.document.id));
        if let (Some(content), Some(entry)) = (&mapper.content, &map.entry) {
            let name = content.table.display_name(entry);
            ui.label(format!("#{} · {name}", entry.index));
        }
        let size = map.background_size();
        ui.label(format!("{} × {} px", size.x, size.y));

        ui.add_space(6.0);
        ui.strong(text::EXITS);
        egui::Grid::new("exits").striped(true).show(ui, |ui| {
            for exit in map.overlay.exits.iter().filter(|exit| exit.is_used()) {
                ui.monospace(exit.exit.to_string());
                ui.monospace(crate::canvas::exit_label(exit.exit, &exit.target));
                if exit.cell_count == 0 {
                    ui.weak(text::NO_TRIGGER_CELLS);
                } else if exit.target.destination().is_some() && ui.button(text::GO).clicked() {
                    action = Some(Action::FollowExit(exit.exit));
                } else {
                    ui.label("");
                }
                ui.end_row();
            }
        });

        ui.add_space(6.0);
        ui.strong(text::NPCS);
        egui::Grid::new("npcs").striped(true).show(ui, |ui| {
            for (npc, record) in map.overlay.npcs.iter().zip(&map.document.npcs) {
                ui.monospace(format!("N{}", npc.slot));
                ui.label(format!(
                    "t{} s{} ({}, {}, {})",
                    npc.type_id,
                    npc.script_id,
                    record.position.x,
                    record.position.y,
                    record.position.z
                ));
                ui.end_row();
            }
        });
    }
    if let Some(action) = action {
        app.apply(action);
    }
}

fn hover_details(app: &MapperApp, ui: &mut egui::Ui) {
    ui.strong(text::HOVER);
    let Some(hover) = app.mapper.hover else {
        ui.weak("—");
        return;
    };
    let kind = match hover.kind {
        CellKind::Free => text::KIND_FREE.to_owned(),
        CellKind::Blocked => text::KIND_BLOCKED.to_owned(),
        CellKind::Trigger(_) => hover.band.map(text::band_name).unwrap_or_default(),
    };
    egui::Grid::new("hover").show(ui, |ui| {
        ui.label(text::CELL);
        ui.monospace(format!("{}, {}", hover.cell.column, hover.cell.row));
        ui.end_row();
        ui.label(text::VALUE);
        ui.monospace(format!("{} ({kind})", hover.value));
        ui.end_row();
        ui.label(text::WORLD);
        ui.monospace(format!("{:.0}, {:.0}", hover.world.x, hover.world.z));
        ui.end_row();
        let original = hover.original();
        ui.label(text::ORIGINAL);
        ui.monospace(format!("{:.0}, {:.0}", original.x, original.z));
        ui.end_row();
    });
}

fn live_details(app: &mut MapperApp, ui: &mut egui::Ui) {
    ui.strong(text::LIVE);
    let live = &mut app.live;
    ui.add(egui::TextEdit::singleline(&mut live.endpoint).hint_text(text::LIVE_ENDPOINT_HINT));
    let mut open = None;
    ui.horizontal(|ui| {
        if live.client.is_none() {
            if ui.button(text::LIVE_CONNECT).clicked() {
                live.connect();
            }
        } else {
            if ui.button(text::LIVE_DISCONNECT).clicked() {
                live.client = None;
                live.state = text::LIVE_OFF.to_owned();
            }
            if ui.button(text::LIVE_FOLLOW).clicked() {
                open = live.game_map();
            }
        }
    });
    if live.client.is_some() && ui.button(text::LIVE_RELOAD).clicked() {
        live.reload();
    }
    ui.weak(&live.state);
    if let Some(map) = open {
        app.apply(Action::OpenMap(map));
    }
}

/// The link to a running game.
pub struct LivePanel {
    pub endpoint: String,
    pub client: Option<LiveClient>,
    pub state: String,
}

impl Default for LivePanel {
    fn default() -> Self {
        Self {
            endpoint: String::new(),
            client: None,
            state: text::LIVE_OFF.to_owned(),
        }
    }
}

impl LivePanel {
    pub fn connect(&mut self) {
        let connected =
            Endpoint::parse(&self.endpoint).and_then(|endpoint| LiveClient::connect(&endpoint));
        match connected {
            Ok(client) => {
                self.client = Some(client);
                self.state = text::LIVE_ON.to_owned();
            }
            Err(error) => {
                self.client = None;
                self.state = error.to_string();
            }
        }
    }

    /// The map the game shows, if it is in the field.
    pub fn game_map(&mut self) -> Option<String> {
        let client = self.client.as_mut()?;
        match client.status() {
            Ok(status) => {
                self.state = format!("{} · {} · tick {}", text::LIVE_ON, status.mode, status.tick);
                status.map
            }
            Err(error) => {
                self.fail(error);
                None
            }
        }
    }

    pub fn reload(&mut self) {
        let Some(client) = self.client.as_mut() else {
            return;
        };
        match client.reload_map() {
            Ok(()) => self.state = text::LIVE_ON.to_owned(),
            Err(error) => self.fail(error),
        }
    }

    fn fail(&mut self, error: mapper_live::Error) {
        if matches!(
            error,
            mapper_live::Error::Closed | mapper_live::Error::Io(_)
        ) {
            self.client = None;
        }
        self.state = error.to_string();
    }
}
