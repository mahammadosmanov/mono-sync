//! Local control panel (ADR-0012): a tiny HTTP server on 127.0.0.1 serving one
//! page. Protection against other web pages poking it (CSRF / DNS rebinding):
//! a random per-run key is required on every request and the Host header must
//! be loopback.
use crate::{
    config::{self, Config, Role},
    run::Core,
};
use anyhow::{anyhow, Result};
use serde_json::json;
use std::{io::Read, sync::Arc};
use tiny_http::{Header, Method, Request, Response, Server};

const PAGE: &str = include_str!("page.html");

pub struct Web {
    pub url: String,
}

pub fn start(core: Arc<Core>) -> Result<Web> {
    let server = Server::http("127.0.0.1:24801")
        .or_else(|_| Server::http("127.0.0.1:0"))
        .map_err(|e| anyhow!("cannot start control panel: {e}"))?;
    let port = server.server_addr().to_ip().map(|a| a.port()).unwrap_or(0);
    let key = config::random_hex(16)?;
    let url = format!("http://127.0.0.1:{port}/?k={key}");
    let handle = tokio::runtime::Handle::current();
    std::thread::spawn(move || {
        for req in server.incoming_requests() {
            let (core, key, handle) = (core.clone(), key.clone(), handle.clone());
            // One thread per request: a long upload must not block status polls.
            std::thread::spawn(move || handle_request(req, &core, &key, port, &handle));
        }
    });
    Ok(Web { url })
}

fn header<'a>(req: &'a Request, name: &str) -> Option<&'a str> {
    req.headers()
        .iter()
        .find(|h| h.field.as_str().as_str().eq_ignore_ascii_case(name))
        .map(|h| h.value.as_str())
}

fn reply(req: Request, code: u16, ctype: &str, body: impl Into<Vec<u8>>) {
    let mut r = Response::from_data(body.into()).with_status_code(code);
    for (k, v) in [
        ("Content-Type", ctype),
        ("Cache-Control", "no-store"),
        ("X-Content-Type-Options", "nosniff"),
        (
            "Content-Security-Policy",
            "default-src 'self' 'unsafe-inline'; frame-ancestors 'none'",
        ),
    ] {
        r.add_header(Header::from_bytes(k, v).expect("static header"));
    }
    let _ = req.respond(r);
}

fn json_reply(req: Request, code: u16, v: serde_json::Value) {
    reply(req, code, "application/json", v.to_string());
}

fn handle_request(
    mut req: Request,
    core: &Arc<Core>,
    key: &str,
    port: u16,
    rt: &tokio::runtime::Handle,
) {
    // Handlers spawn tokio tasks (Core::apply), so they need the runtime context.
    let _guard = rt.enter();
    let host_ok = matches!(header(&req, "Host"), Some(h) if h == format!("127.0.0.1:{port}") || h == format!("localhost:{port}"));
    if !host_ok {
        return reply(req, 403, "text/plain", "bad host");
    }
    let url = req.url().to_string();
    let (path, query) = url.split_once('?').unwrap_or((&url, ""));

    if req.method() == &Method::Get && path == "/" {
        if !query.split('&').any(|p| p == format!("k={key}")) {
            return reply(req, 403, "text/plain", "open the link printed by mono");
        }
        return reply(req, 200, "text/html; charset=utf-8", PAGE);
    }
    if header(&req, "X-Mono-Key") != Some(key) {
        return reply(req, 403, "text/plain", "forbidden");
    }
    let method = req.method().clone();
    match (&method, path) {
        (&Method::Get, "/api/state") => {
            let (state, log, code, connected) = core.sh.snapshot();
            let cfg = Config::load().unwrap_or_default();
            json_reply(
                req,
                200,
                json!({
                    "version": env!("CARGO_PKG_VERSION"),
                    "state": state, "log": log, "code": code, "connected": connected,
                    "config": cfg, "download_dir": cfg.download_dir().display().to_string(),
                    "address": local_ip().map(|i| format!("{i}:24800")),
                }),
            )
        }
        (&Method::Post, "/api/config") => {
            let mut body = String::new();
            let _ = req.as_reader().take(64 * 1024).read_to_string(&mut body);
            match save_config(core, &body) {
                Ok(()) => json_reply(req, 200, json!({"ok": true})),
                Err(e) => json_reply(req, 400, json!({"error": format!("{e:#}")})),
            }
        }
        (&Method::Post, "/api/upload") => {
            let name = percent_decode(header(&req, "X-Filename").unwrap_or("file"));
            match upload(core, &mut req, &name, rt) {
                Ok(n) => json_reply(req, 200, json!({"ok": true, "bytes": n})),
                Err(e) => json_reply(req, 500, json!({"error": format!("{e:#}")})),
            }
        }
        (&Method::Post, "/api/open-folder") => {
            let d = Config::load().unwrap_or_default().download_dir();
            let _ = std::fs::create_dir_all(&d);
            open_path(&d.display().to_string());
            json_reply(req, 200, json!({"ok": true}))
        }
        (&Method::Post, "/api/quit") => {
            json_reply(req, 200, json!({"ok": true}));
            std::thread::sleep(std::time::Duration::from_millis(200));
            std::process::exit(0);
        }
        _ => reply(req, 404, "text/plain", "not found"),
    }
}

fn save_config(core: &Arc<Core>, body: &str) -> Result<()> {
    let cfg: Config = serde_json::from_str(body)?;
    match cfg.role {
        Role::Guest => {
            cfg.host_addr
                .trim()
                .parse::<std::net::SocketAddr>()
                .map_err(|_| anyhow!("Host address must look like 192.168.1.20:24800"))?;
            let ok = cfg
                .pairing_code
                .trim()
                .split_once(':')
                .is_some_and(|(f, t)| f.len() == 64 && !t.is_empty());
            if !ok {
                return Err(anyhow!(
                    "Pairing code looks incomplete; copy it again from the host"
                ));
            }
        }
        Role::Host => {
            cfg.bind
                .parse::<std::net::SocketAddr>()
                .map_err(|_| anyhow!("Listen address is invalid"))?;
        }
        Role::None => {}
    }
    cfg.save()?;
    core.apply(cfg);
    Ok(())
}

fn upload(
    core: &Arc<Core>,
    req: &mut Request,
    name: &str,
    rt: &tokio::runtime::Handle,
) -> Result<u64> {
    let dir = std::env::temp_dir().join(format!("mono-up-{}", config::random_hex(6)?));
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(mono_net::file::sanitize_name(name));
    let res = (|| -> Result<u64> {
        let mut f = std::fs::File::create(&path)?;
        std::io::copy(req.as_reader(), &mut f)?;
        drop(f);
        rt.block_on(core.sh.send_file(&path))
    })();
    let _ = std::fs::remove_dir_all(&dir);
    if let Ok(n) = &res {
        core.sh.note(format!(
            "Sent {} ({n} bytes)",
            mono_net::file::sanitize_name(name)
        ));
    }
    res
}

fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() {
            if let Some(v) = std::str::from_utf8(&b[i + 1..i + 3])
                .ok()
                .and_then(|h| u8::from_str_radix(h, 16).ok())
            {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(b[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// Local LAN address, found by asking the OS which interface it would use.
/// No packet is sent.
fn local_ip() -> Option<std::net::IpAddr> {
    let s = std::net::UdpSocket::bind("0.0.0.0:0").ok()?;
    s.connect("192.0.2.1:9").ok()?;
    Some(s.local_addr().ok()?.ip())
}

pub fn open_path(target: &str) {
    #[cfg(target_os = "windows")]
    let r = std::process::Command::new("rundll32")
        .args(["url.dll,FileProtocolHandler", target])
        .spawn();
    #[cfg(target_os = "macos")]
    let r = std::process::Command::new("open").arg(target).spawn();
    #[cfg(all(unix, not(target_os = "macos")))]
    let r = std::process::Command::new("xdg-open").arg(target).spawn();
    let _ = r;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn percent_decoding() {
        assert_eq!(percent_decode("a%20b.txt"), "a b.txt");
        assert_eq!(percent_decode("%C3%A7ay.txt"), "çay.txt");
        assert_eq!(percent_decode("100%"), "100%");
        assert_eq!(percent_decode("x%zz"), "x%zz");
        assert_eq!(percent_decode("plain"), "plain");
        assert_eq!(percent_decode("%aé"), "%aé");
    }
}
