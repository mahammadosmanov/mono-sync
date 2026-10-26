//! mono-sync desktop app (ADR-0013): a Tauri window and tray icon over `mono-core`.
#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

use mono_core::{config::Config, Core, View};
use std::{path::Path, sync::Arc};
use tauri::{
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, Manager, State, WindowEvent,
};

type Shared = Arc<Core>;

#[tauri::command]
async fn view(core: State<'_, Shared>) -> Result<View, String> {
    Ok(core.view())
}

#[tauri::command]
async fn save(core: State<'_, Shared>, config: Config) -> Result<(), String> {
    core.save_and_apply(config).map_err(|e| format!("{e:#}"))
}

/// Sends each path; returns a one-line summary for the UI.
#[tauri::command]
async fn send_files(core: State<'_, Shared>, paths: Vec<String>) -> Result<String, String> {
    let mut sent = 0;
    for p in &paths {
        let path = Path::new(p);
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("file");
        match core.sh.send_file(path).await {
            Ok(n) => {
                core.sh.note(format!("Sent {name} ({n} bytes)"));
                sent += 1;
            }
            Err(e) => {
                let m = format!("{name}: {e:#}");
                core.sh.note(format!("Sending failed: {m}"));
                return Err(m);
            }
        }
    }
    Ok(format!("Sent {sent} file(s)"))
}

#[tauri::command]
fn open_folder() {
    let d = Config::load().unwrap_or_default().download_dir();
    let _ = std::fs::create_dir_all(&d);
    open_path(&d.display().to_string());
}

fn open_path(target: &str) {
    #[cfg(target_os = "windows")]
    let r = std::process::Command::new("explorer").arg(target).spawn();
    #[cfg(target_os = "macos")]
    let r = std::process::Command::new("open").arg(target).spawn();
    #[cfg(all(unix, not(target_os = "macos")))]
    let r = std::process::Command::new("xdg-open").arg(target).spawn();
    let _ = r;
}

#[tauri::command]
fn open_permission_settings(name: String) {
    mono_core::open_permission_settings(&name);
}

/// Windows host: add the firewall rule after one UAC approval (ADR-0016).
#[tauri::command]
async fn allow_firewall(core: State<'_, Shared>) -> Result<(), String> {
    let r = tauri::async_runtime::spawn_blocking(mono_core::firewall::request_rule)
        .await
        .map_err(|e| format!("{e}"))?;
    core.forget_firewall();
    r?;
    core.sh
        .note("Windows Firewall: mono-sync is allowed on private networks");
    Ok(())
}

#[tauri::command]
fn quit(app: AppHandle) {
    app.exit(0);
}

fn show_main(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.show();
        let _ = w.unminimize();
        let _ = w.set_focus();
    }
}

fn main() {
    let core = Core::new().expect("start core");
    let stop_core = core.clone();
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| {
            show_main(app)
        }))
        .plugin(tauri_plugin_dialog::init())
        .manage(core.clone())
        .invoke_handler(tauri::generate_handler![
            view,
            save,
            send_files,
            open_folder,
            open_permission_settings,
            allow_firewall,
            quit
        ])
        .setup(move |app| {
            // Resume the saved role (if any) as soon as the app starts.
            let c = core.clone();
            tauri::async_runtime::spawn(async move {
                match Config::load() {
                    Ok(cfg) => {
                        if let Err(e) = c.save_and_apply(cfg) {
                            c.sh.note(format!("Saved settings not applied: {e:#}"));
                        }
                    }
                    Err(e) => c.sh.note(format!("Settings unreadable: {e:#}")),
                }
            });

            let show = MenuItem::with_id(app, "show", "Open mono-sync", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show, &quit])?;
            TrayIconBuilder::new()
                .icon(app.default_window_icon().expect("window icon").clone())
                .tooltip("mono-sync")
                .menu(&menu)
                .show_menu_on_left_click(false)
                .on_menu_event(|app, e| match e.id.as_ref() {
                    "show" => show_main(app),
                    "quit" => app.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, e| {
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = e
                    {
                        show_main(tray.app_handle());
                    }
                })
                .build(app)?;
            Ok(())
        })
        .on_window_event(|w, e| {
            // Closing the window keeps sharing alive in the tray.
            if let WindowEvent::CloseRequested { api, .. } = e {
                api.prevent_close();
                let _ = w.hide();
            }
        })
        .build(tauri::generate_context!())
        .expect("build mono-sync")
        .run(move |_app, e| {
            // Put back MouseKeys (ADR-0015) before the process goes away.
            if let tauri::RunEvent::Exit = e {
                stop_core.stop();
            }
        });
}
