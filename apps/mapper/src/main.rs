//! dwe-mapper: the map editor of dwe (docs/modulos/apps/mapper.md). It shows the field maps of a
//! content folder (background, collision, exits, characters, doors and toilet), edits their
//! collision, exits, NPCs, doors, toilet and story sections, and saves the changes as patches
//! into a mod pack.

mod app;
mod canvas;
mod dialogs;
mod events;
mod inspector;
mod keys;
mod live;
mod options;
mod panels;
mod places;
mod probe;
mod section;
mod shot;
mod toolbar;

use eframe::egui;

fn main() -> std::process::ExitCode {
    let options = match options::Options::parse(std::env::args().skip(1)) {
        Ok(options) => options,
        Err(message) => {
            eprintln!("dwe-mapper: {message}\n\n{}", options::USAGE);
            return std::process::ExitCode::from(2);
        }
    };
    if options.help {
        println!("{}", options::USAGE);
        return std::process::ExitCode::SUCCESS;
    }
    let native = eframe::NativeOptions {
        renderer: eframe::Renderer::Wgpu,
        viewport: egui::ViewportBuilder::default()
            .with_title(mapper_core::text::APP_TITLE)
            .with_inner_size(options.size),
        ..Default::default()
    };
    let result = eframe::run_native(
        mapper_core::text::APP_TITLE,
        native,
        Box::new(|creation| Ok(Box::new(app::MapperApp::new(creation, options)))),
    );
    match result {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("dwe-mapper: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
