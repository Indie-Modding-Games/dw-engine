// No console window next to the launcher on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    dwe_launcher::run();
}
