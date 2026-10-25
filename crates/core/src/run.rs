//! The long-running part: host and guest loops with automatic reconnect.
//! Settings can change at runtime: `Core::apply` stops the old loop and starts
//! the new one. The OS input grab thread starts once and lives for the process.
use crate::config::{self, Config, Role};
use anyhow::{bail, Context, Result};
use mono_input::{
    guest::GuestEngine,
    host::HostEngine,
    layout::{Link, PeerId, Size},
    os::{self, SharedEngine},
};
use mono_net::{connect, file, Connection, Host};
use mono_proto::Msg;
use std::{
    collections::VecDeque,
    net::SocketAddr,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::Duration,
};
use tokio::{
    sync::{mpsc, Mutex as AsyncMutex},
    task::JoinHandle,
};

const PEER: PeerId = 1;

#[derive(Default)]
pub struct Status {
    pub state: String,
    pub log: VecDeque<String>,
    pub code: Option<String>,
    pub conn: Option<Connection>,
    /// OS permissions still missing (macOS).
    pub needs: Vec<String>,
}

#[derive(Clone, Default)]
pub struct Shared(Arc<Mutex<Status>>);

impl Shared {
    fn with<R>(&self, f: impl FnOnce(&mut Status) -> R) -> R {
        f(&mut self.0.lock().unwrap_or_else(|p| p.into_inner()))
    }
    pub fn note(&self, msg: impl Into<String>) {
        let m = msg.into();
        eprintln!("{m}");
        self.with(|s| {
            s.log.push_back(m);
            while s.log.len() > 50 {
                s.log.pop_front();
            }
        });
    }
    fn state(&self, st: impl Into<String>) {
        self.with(|s| s.state = st.into());
    }
    fn set_conn(&self, c: Option<Connection>) {
        self.with(|s| s.conn = c);
    }
    /// (state, log lines, pairing code, connected?)
    pub fn snapshot(&self) -> (String, Vec<String>, Option<String>, bool) {
        self.with(|s| {
            (
                s.state.clone(),
                s.log.iter().cloned().collect(),
                s.code.clone(),
                s.conn.is_some(),
            )
        })
    }
    pub async fn send_file(&self, path: &std::path::Path) -> Result<u64> {
        let conn = self
            .with(|s| s.conn.clone())
            .context("not connected to another computer")?;
        file::send(&conn, path).await
    }
}

struct Clip {
    out: mpsc::UnboundedReceiver<String>,
    sink: std::sync::mpsc::Sender<String>,
}

impl Clip {
    fn inert() -> Clip {
        let (_tx, out) = mpsc::unbounded_channel();
        let (sink, _rx) = std::sync::mpsc::channel();
        Clip { out, sink }
    }

    fn start() -> Clip {
        let (rx, sink) = mono_clip::spawn();
        let (tx, out) = mpsc::unbounded_channel();
        std::thread::spawn(move || {
            while let Ok(t) = rx.recv() {
                if tx.send(t).is_err() {
                    break;
                }
            }
        });
        Clip { out, sink }
    }
}

/// Everything a user interface needs to draw itself.
#[derive(serde::Serialize)]
pub struct View {
    pub version: &'static str,
    pub state: String,
    pub log: Vec<String>,
    pub code: Option<String>,
    pub connected: bool,
    /// Permissions the user still has to switch on; empty when all is well.
    pub needs: Vec<String>,
    pub config: Config,
    pub download_dir: String,
    /// This computer's LAN address with the host port, if one could be found.
    pub address: Option<String>,
}

/// Local LAN address, found by asking the OS which interface it would use.
/// No packet is sent.
fn local_ip() -> Option<std::net::IpAddr> {
    let s = std::net::UdpSocket::bind("0.0.0.0:0").ok()?;
    s.connect("192.0.2.1:9").ok()?;
    Some(s.local_addr().ok()?.ip())
}

/// Check settings the way the UI needs before applying them.
pub fn validate(cfg: &Config) -> Result<()> {
    match cfg.role {
        Role::Guest => {
            cfg.host_addr
                .trim()
                .parse::<SocketAddr>()
                .map_err(|_| anyhow::anyhow!("Host address must look like 192.168.1.20:24800"))?;
            let ok = cfg
                .pairing_code
                .trim()
                .split_once(':')
                .is_some_and(|(f, t)| f.len() == 64 && !t.is_empty());
            if !ok {
                bail!("Pairing code looks incomplete; copy it again from the host");
            }
        }
        Role::Host => {
            cfg.bind
                .parse::<SocketAddr>()
                .map_err(|_| anyhow::anyhow!("Listen address is invalid"))?;
        }
        Role::None => {}
    }
    Ok(())
}

pub struct Core {
    pub sh: Shared,
    /// Fallback when the OS cannot report a display (headless CI).
    screen: Size,
    engine: SharedEngine,
    input_tx: mpsc::UnboundedSender<(PeerId, Msg)>,
    input_rx: AsyncMutex<mpsc::UnboundedReceiver<(PeerId, Msg)>>,
    clip: AsyncMutex<Clip>,
    grab_running: AtomicBool,
    task: Mutex<Option<JoinHandle<()>>>,
}

impl Core {
    pub fn new() -> Result<Arc<Core>> {
        Self::build(true, true)
    }

    /// No OS input grab and no clipboard watcher: for tests on a machine you are using.
    pub fn headless() -> Result<Arc<Core>> {
        Self::build(false, false)
    }

    fn build(grab: bool, clipboard: bool) -> Result<Arc<Core>> {
        let (input_tx, input_rx) = mpsc::unbounded_channel();
        let screen = os::screen_size().unwrap_or(Size {
            w: 1920.0,
            h: 1080.0,
        });
        Ok(Arc::new(Core {
            sh: Shared::default(),
            screen,
            engine: Arc::new(Mutex::new(HostEngine::new(screen, vec![]))),
            input_tx,
            input_rx: AsyncMutex::new(input_rx),
            clip: AsyncMutex::new(if clipboard {
                Clip::start()
            } else {
                Clip::inert()
            }),
            grab_running: AtomicBool::new(!grab),
            task: Mutex::new(None),
        }))
    }

    pub fn view(&self) -> View {
        let (state, log, code, connected) = self.sh.snapshot();
        let config = Config::load().unwrap_or_default();
        View {
            version: env!("CARGO_PKG_VERSION"),
            state,
            log,
            code,
            connected,
            needs: self.sh.with(|s| s.needs.clone()),
            download_dir: config.download_dir().display().to_string(),
            config,
            address: local_ip().map(|i| format!("{i}:24800")),
        }
    }

    /// Validate, save and start according to `cfg`.
    pub fn save_and_apply(self: &Arc<Self>, cfg: Config) -> Result<()> {
        validate(&cfg)?;
        cfg.save()?;
        self.apply(cfg);
        Ok(())
    }

    /// Stop whatever is running and start according to `cfg`.
    pub fn apply(self: &Arc<Self>, cfg: Config) {
        if let Some(t) = self.task.lock().unwrap_or_else(|p| p.into_inner()).take() {
            t.abort();
        }
        os::release_host(&self.engine);
        self.sh.set_conn(None);
        self.sh.with(|s| s.code = None);
        let core = self.clone();
        let h = match cfg.role {
            Role::None => {
                self.sh.state("Not set up yet");
                return;
            }
            Role::Host => tokio::spawn(async move {
                if let Err(e) = host_loop(&core, &cfg).await {
                    core.sh.note(format!("Host stopped: {e:#}"));
                    core.sh.state("Error (see log)");
                }
            }),
            Role::Guest => tokio::spawn(async move {
                if let Err(e) = guest_loop(&core, &cfg).await {
                    core.sh.note(format!("Guest stopped: {e:#}"));
                    core.sh.state("Error (see log)");
                }
            }),
        };
        *self.task.lock().unwrap_or_else(|p| p.into_inner()) = Some(h);
    }

    fn ensure_grab(self: &Arc<Self>) {
        if self.grab_running.swap(true, Ordering::SeqCst) {
            return;
        }
        let core = self.clone();
        std::thread::spawn(move || {
            if !wait_for_permissions(&core.sh, true) {
                core.grab_running.store(false, Ordering::SeqCst);
                return;
            }
            let r = os::run_host(core.engine.clone(), core.input_tx.clone());
            core.grab_running.store(false, Ordering::SeqCst);
            if let Err(e) = r {
                core.sh.note(format!("{e:#}"));
                core.sh.state("Input capture failed (see log)");
            }
        });
    }

    /// Wire up file receiving for a fresh connection.
    fn attach(&self, conn: Connection, dir: std::path::PathBuf) {
        self.sh.set_conn(Some(conn.clone()));
        let (tx, mut rx) = mpsc::unbounded_channel();
        tokio::spawn(file::receive_loop(conn, dir, tx));
        let sh = self.sh.clone();
        tokio::spawn(async move {
            while let Some(ev) = rx.recv().await {
                match ev {
                    file::FileEvent::Received { path, size } => {
                        sh.note(format!("Received {} ({} bytes)", path.display(), size))
                    }
                    file::FileEvent::Failed { name, error } => {
                        sh.note(format!("Receiving {name} failed: {error}"))
                    }
                }
            }
        });
    }
}

/// macOS: ask for each missing permission, open its Settings page once, and wait
/// (polling) until the user has switched it on. `block` = report in the status
/// line too. Returns false if it gave up.
fn wait_for_permissions(sh: &Shared, block: bool) -> bool {
    use std::collections::HashSet;
    let mut opened: HashSet<&'static str> = HashSet::new();
    let mut announced = false;
    loop {
        let missing = os::missing_permissions();
        sh.with(|s| s.needs = missing.iter().map(|m| m.to_string()).collect());
        if missing.is_empty() {
            if announced {
                sh.note("Permissions granted");
                if block {
                    sh.state(if sh.snapshot().3 {
                        "Connected"
                    } else {
                        "Waiting for the other computer to connect"
                    });
                }
            }
            return true;
        }
        if !announced {
            announced = true;
            os::request_permissions();
            sh.note(format!(
                "macOS needs your OK: turn on mono-sync under {} (Settings opened for you)",
                missing.join(" and ")
            ));
        }
        // One page at a time: opening two panes at once leaves the user on the last one.
        if let Some(m) = missing.first() {
            if opened.insert(m) {
                os::open_permission_settings(m);
            }
        }
        if block {
            sh.state(format!(
                "Waiting for macOS permission: {}",
                missing.join(" + ")
            ));
        }
        std::thread::sleep(Duration::from_secs(2));
    }
}

async fn host_loop(core: &Arc<Core>, cfg: &Config) -> Result<()> {
    let (id, token) = config::load_identity()?;
    let bind: SocketAddr = cfg.bind.parse().context("bad listen address")?;
    // The previous listener may need a moment to release the port after Save.
    let mut tries = 0;
    let srv = loop {
        match Host::bind(bind, &id, token.clone()) {
            Ok(s) => break s,
            Err(e) if tries < 10 => {
                tries += 1;
                let _ = e;
                tokio::time::sleep(Duration::from_millis(300)).await;
            }
            Err(e) => return Err(e),
        }
    };
    core.sh
        .with(|s| s.code = Some(format!("{}:{}", id.fingerprint(), token)));
    core.ensure_grab();
    let host_screen = os::screen_size().unwrap_or(core.screen);
    let mut rx = core.input_rx.lock().await;
    let mut clip = core.clip.lock().await;
    let mut next: Option<mono_net::Session> = None;
    loop {
        let mut session = match next.take() {
            Some(s) => s,
            None => {
                core.sh.state("Waiting for the other computer to connect");
                srv.accept().await?
            }
        };
        let guest = match tokio::time::timeout(Duration::from_secs(5), session.recv()).await {
            Ok(Some(Msg::Screen { w, h })) => Size {
                w: f64::from(w),
                h: f64::from(h),
            },
            _ => {
                core.sh
                    .note("A guest connected but sent no screen size; ignored");
                continue;
            }
        };
        os::set_engine(
            &core.engine,
            HostEngine::new(
                host_screen,
                vec![Link {
                    edge: cfg.edge.into(),
                    peer: PEER,
                    size: guest,
                }],
            ),
        );
        while rx.try_recv().is_ok() {}
        core.attach(session.connection(), cfg.download_dir());
        core.sh
            .note("Guest connected. Ctrl+Alt(Option)+Esc takes control back");
        core.sh.state("Connected");
        loop {
            tokio::select! {
                Some((_, m)) = rx.recv() => {
                    match m {
                        Msg::Enter { .. } => core.sh.note("Cursor moved to the other computer"),
                        Msg::Leave => core.sh.note("Cursor came back to this computer"),
                        _ => {}
                    }
                    if session.send(&m).await.is_err() { break }
                }
                Some(text) = clip.out.recv() => {
                    if session.send(&Msg::Clip { text }).await.is_err() { break }
                }
                m = session.recv() => match m {
                    Some(Msg::Clip { text }) => { let _ = clip.sink.send(text); }
                    Some(_) => {}
                    None => break,
                },
                // A paired guest reconnecting replaces a stale connection at once.
                n = srv.accept() => { next = Some(n?); break }
            }
        }
        os::release_host(&core.engine);
        core.sh.set_conn(None);
        core.sh.note("Guest disconnected");
    }
}

async fn guest_loop(core: &Arc<Core>, cfg: &Config) -> Result<()> {
    let addr: SocketAddr = cfg
        .host_addr
        .trim()
        .parse()
        .context("host address must look like 192.168.1.20:24800")?;
    let (fp, token) = cfg
        .pairing_code
        .trim()
        .split_once(':')
        .context("pairing code must look like fingerprint:token")?;
    if fp.len() != 64 || token.is_empty() {
        bail!("pairing code looks incomplete; copy it again from the host");
    }
    let screen = os::screen_size().unwrap_or(core.screen);
    if !os::permissions_granted() {
        // Injecting input needs Accessibility on macOS; guide the user, keep connecting.
        let sh = core.sh.clone();
        std::thread::spawn(move || {
            wait_for_permissions(&sh, false);
        });
    }
    let mut clip = core.clip.lock().await;
    let mut last_err = String::new();
    loop {
        core.sh.state(format!("Connecting to {addr}"));
        let mut session = match connect(addr, fp, token).await {
            Ok(s) => s,
            Err(e) => {
                let m = format!("Cannot connect: {e:#}");
                if m != last_err {
                    core.sh.note(m.clone());
                    last_err = m;
                }
                tokio::time::sleep(Duration::from_secs(2)).await;
                continue;
            }
        };
        last_err.clear();
        session
            .send(&Msg::Screen {
                w: screen.w as u32,
                h: screen.h as u32,
            })
            .await?;
        core.attach(session.connection(), cfg.download_dir());
        core.sh.note(format!("Connected to {addr}"));
        core.sh.state("Connected");
        let mut engine = GuestEngine::new(screen);
        loop {
            tokio::select! {
                Some(text) = clip.out.recv() => {
                    if session.send(&Msg::Clip { text }).await.is_err() { break }
                }
                m = session.recv() => match m {
                    Some(Msg::Clip { text }) => { let _ = clip.sink.send(text); }
                    Some(m) => for a in { if matches!(m, Msg::Enter { .. }) { core.sh.note("Cursor arrived from the other computer"); } engine.on_msg(&m) } {
                        if let Err(e) = os::inject(&a) { core.sh.note(format!("{e:#}")); }
                    },
                    None => break,
                },
            }
        }
        for a in engine.on_disconnect() {
            let _ = os::inject(&a);
        }
        core.sh.set_conn(None);
        core.sh.note("Disconnected; retrying");
    }
}

#[cfg(test)]
mod tests {
    //! Whole chain without OS input: host engine -> QUIC -> guest engine.
    use super::*;
    use mono_input::{guest::Act, host::Ev, layout::Edge};
    use mono_net::Identity;
    use mono_proto::Key;

    #[tokio::test]
    async fn host_events_reach_guest_actions_and_disconnect_releases() {
        let id = Identity::generate().unwrap();
        let srv = Host::bind("127.0.0.1:0".parse().unwrap(), &id, "tok".into()).unwrap();
        let addr = srv.local_addr().unwrap();
        let fp = id.fingerprint();
        let g = tokio::spawn(async move { connect(addr, &fp, "tok").await.unwrap() });
        let mut hs = srv.accept().await.unwrap();
        let mut gs = g.await.unwrap();

        let guest_size = Size {
            w: 1000.0,
            h: 500.0,
        };
        let mut he = HostEngine::new(
            Size {
                w: 1920.0,
                h: 1080.0,
            },
            vec![Link {
                edge: Edge::Right,
                peer: PEER,
                size: guest_size,
            }],
        );
        let mut ge = GuestEngine::new(guest_size);

        let mut steps = vec![he.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        })];
        steps.push(he.on_event(Ev::Move { x: 965.0, y: 543.0 }));
        steps.push(he.on_event(Ev::Key {
            key: Key::KeyA,
            down: true,
        }));
        for (_, m) in steps.into_iter().flat_map(|s| s.send) {
            hs.send(&m).await.unwrap();
        }

        let mut acts = vec![];
        for _ in 0..3 {
            acts.extend(ge.on_msg(&gs.recv().await.unwrap()));
        }
        assert!(acts.contains(&Act::Key {
            key: Key::KeyA,
            down: true
        }));
        assert!(matches!(acts[0], Act::MoveTo(..)));
        assert_eq!(
            ge.on_disconnect(),
            vec![Act::Key {
                key: Key::KeyA,
                down: false
            }]
        );
    }
}
