use std::io::Write;

fn main() {
    let _ = std::fs::write("tauri_lifecycle.log", "STARTING APP\n");
    app_lib::run();
    if let Ok(mut f) = std::fs::OpenOptions::new().append(true).open("tauri_lifecycle.log") {
        let _ = f.write_all(b"EXITED APP CLEANLY\n");
    }
}
