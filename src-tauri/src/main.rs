#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::io::Write;

fn main() {
    let _ = std::fs::write("tauri_run.log", "CALLING RUN\n");
    app_lib::run();
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open("tauri_run.log") {
        let _ = f.write_all(b"RUN FINISHED\n");
    }
}
