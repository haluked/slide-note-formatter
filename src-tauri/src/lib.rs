use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use tauri::{AppHandle, Emitter};
use serde_json::Value;
use walkdir::WalkDir;
use regex::Regex;

fn get_config_path() -> Option<PathBuf> {
    dirs::home_dir().map(|h| h.join(".slide_note_formatter").join("config.json"))
}

fn find_python() -> Option<PathBuf> {
    // 0. Explicit MinGW GCC Python (verified working 3.12)
    let mingw_py = Path::new("C:\\msys64\\mingw64\\bin\\python.exe");
    if mingw_py.exists() {
        return Some(mingw_py.to_path_buf());
    }

    // 1. Check WindowsApps PythonSoftwareFoundation real directory
    if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
        let windows_apps = Path::new(&local_app_data).join("Microsoft").join("WindowsApps");
        if let Ok(entries) = std::fs::read_dir(&windows_apps) {
            for entry in entries.flatten() {
                let name = entry.file_name();
                let name_str = name.to_string_lossy();
                if name_str.starts_with("PythonSoftwareFoundation.Python.3") {
                    let py_exe = entry.path().join("python.exe");
                    if py_exe.exists() {
                        return Some(py_exe);
                    }
                }
            }
        }
    }

    // 2. py -3 launcher
    if let Ok(output) = Command::new("py").args(["-3", "--version"]).output() {
        if output.status.success() {
            return Some(PathBuf::from("py"));
        }
    }

    // 3. Standard python in PATH
    if let Ok(output) = Command::new("python").arg("--version").output() {
        if output.status.success() {
            return Some(PathBuf::from("python"));
        }
    }

    None
}

#[tauri::command]
fn pick_files() -> Vec<String> {
    if let Some(files) = rfd::FileDialog::new()
        .add_filter("PDF Dosyaları", &["pdf"])
        .set_title("Dönüştürülecek PDF Dosyalarını Seçin")
        .pick_files()
    {
        return files.into_iter().map(|p| p.to_string_lossy().to_string()).collect();
    }
    Vec::new()
}

#[tauri::command]
fn pick_folder() -> Option<String> {
    rfd::FileDialog::new()
        .set_title("PDF İçeren Klasörü Seçin")
        .pick_folder()
        .map(|p| p.to_string_lossy().to_string())
}

#[tauri::command]
fn get_user_config() -> Result<Value, String> {
    if let Some(path) = get_config_path() {
        if path.exists() {
            let content = fs::read_to_string(&path).map_err(|e| e.to_string())?;
            let val: Value = serde_json::from_str(&content).map_err(|e| e.to_string())?;
            return Ok(val);
        }
    }
    Ok(serde_json::json!({}))
}

#[tauri::command]
fn save_user_config(config: Value) -> Result<(), String> {
    if let Some(path) = get_config_path() {
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        let pretty = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())?;
        fs::write(&path, pretty).map_err(|e| e.to_string())?;
        return Ok(());
    }
    Err("Cannot resolve config directory".into())
}

#[tauri::command]
fn scan_folder_for_pdfs(folder: String) -> Vec<String> {
    let mut results = Vec::new();
    for entry in WalkDir::new(&folder).into_iter().filter_map(|e| e.ok()) {
        let p = entry.path();
        if p.is_file() {
            if let Some(ext) = p.extension() {
                if ext.eq_ignore_ascii_case("pdf") {
                    let path_str = p.to_string_lossy().to_string();
                    if !path_str.contains("_Not_Formatli") && !path_str.contains("(Not Alanli)") {
                        results.push(path_str);
                    }
                }
            }
        }
    }
    results
}

#[tauri::command]
fn auto_detect_course(paths: Vec<String>) -> String {
    let re = Regex::new(r"(?i)\b([a-z]{2,5}\s*\d{2,4}[a-z]?)\b").unwrap();
    for p in &paths {
        if let Some(caps) = re.captures(p) {
            if let Some(m) = caps.get(1) {
                return m.as_str().to_uppercase();
            }
        }
    }
    "".to_string()
}

#[tauri::command]
fn open_folder(path: String) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        Command::new("explorer")
            .arg(&path)
            .spawn()
            .map_err(|e| e.to_string())?;
    }
    #[cfg(not(target_os = "windows"))]
    {
        Command::new("open")
            .arg(&path)
            .spawn()
            .map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[tauri::command]
async fn process_batch(
    files: Vec<String>,
    course_name: String,
    config: Value,
    app: AppHandle,
) -> Result<Value, String> {
    let py_bin = find_python().ok_or_else(|| "Python 3 environment not found on system".to_string())?;

    let current_dir = std::env::current_dir().map_err(|e| e.to_string())?;
    let worker_path = current_dir.join("engine").join("worker.py");

    if !worker_path.exists() {
        return Err(format!("Engine worker not found at: {}", worker_path.display()));
    }

    let payload = serde_json::json!({
        "files": files,
        "course_name": course_name,
        "config": config,
    });

    let payload_str = payload.to_string();

    let mut cmd = if py_bin == PathBuf::from("py") {
        let mut c = Command::new("py");
        c.arg("-3");
        c
    } else {
        Command::new(py_bin)
    };

    cmd.arg(&worker_path)
        .arg(&payload_str)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn worker: {}", e))?;

    let stdout = child.stdout.take().ok_or("Failed to capture stdout")?;
    let reader = BufReader::new(stdout);

    let mut final_result = serde_json::json!({
        "type": "done",
        "success_count": 0,
        "total_files": 0,
        "output_dir": "",
        "errors": []
    });

    for line in reader.lines() {
        if let Ok(l) = line {
            if let Ok(parsed) = serde_json::from_str::<Value>(&l) {
                let _ = app.emit("batch-progress", &parsed);
                if parsed.get("type").and_then(|t| t.as_str()) == Some("done") {
                    final_result = parsed;
                }
            }
        }
    }

    let status = child.wait().map_err(|e| e.to_string())?;
    if !status.success() {
        return Err("Engine worker terminated with error".into());
    }

    Ok(final_result)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    std::panic::set_hook(Box::new(|info| {
        let payload = if let Some(s) = info.payload().downcast_ref::<&str>() {
            (*s).to_string()
        } else if let Some(s) = info.payload().downcast_ref::<String>() {
            s.clone()
        } else {
            format!("{:?}", info)
        };
        let _ = std::fs::write("tauri_panic.log", payload);
    }));

    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            pick_files,
            pick_folder,
            get_user_config,
            save_user_config,
            scan_folder_for_pdfs,
            auto_detect_course,
            open_folder,
            process_batch
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
