//! dwe-mapper: the map editor of dwe (docs/modulos/apps/mapper.md). Today a read-only viewer of
//! the field maps of a content folder: background, collision, exits, characters, doors and toilet.

mod app;
mod canvas;
mod options;
mod panels;
mod shot;

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
