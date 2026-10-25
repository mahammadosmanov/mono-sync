//! `mono host` shares this machine's keyboard and mouse; `mono guest` receives.
use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use mono_input::{
    guest::GuestEngine,
    host::HostEngine,
    layout::{Edge, Link},
    os,
};
use mono_net::{connect, Host, Identity};
use mono_proto::Msg;
use std::{
    net::SocketAddr,
    sync::{atomic::AtomicBool, atomic::Ordering, Arc},
};
use tokio::sync::mpsc;

#[derive(Parser)]
#[command(
    version,
    about = "One keyboard and mouse across computers on the same LAN"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Share this machine's keyboard and mouse with one guest.
    Host {
        #[arg(long, default_value = "0.0.0.0:24800")]
        bind: SocketAddr,
        /// Side of this screen where the guest sits.
        #[arg(long, value_enum, default_value = "right")]
        edge: EdgeArg,
    },
    /// Receive another machine's keyboard and mouse.
    Guest {
        /// Host address, e.g. 192.168.1.20:24800
        addr: SocketAddr,
        /// Pairing code printed by `mono host` (fingerprint:token).
        code: String,
    },
}

#[derive(Clone, clap::ValueEnum)]
enum EdgeArg {
    Left,
    Right,
    Top,
    Bottom,
}

impl From<EdgeArg> for Edge {
    fn from(e: EdgeArg) -> Edge {
        match e {
            EdgeArg::Left => Edge::Left,
            EdgeArg::Right => Edge::Right,
            EdgeArg::Top => Edge::Top,
            EdgeArg::Bottom => Edge::Bottom,
        }
    }
}

const PEER: u32 = 1;

/// Clipboard watcher thread -> async channel; plus the sink for incoming text.
fn clip_bridge() -> (
    mpsc::UnboundedReceiver<String>,
    std::sync::mpsc::Sender<String>,
) {
    let (rx, sink) = mono_clip::spawn();
    let (tx, arx) = mpsc::unbounded_channel();
    std::thread::spawn(move || {
        while let Ok(t) = rx.recv() {
            if tx.send(t).is_err() {
                break;
            }
        }
    });
    (arx, sink)
}

fn random_token() -> Result<String> {
    let mut b = [0u8; 16];
    getrandom::fill(&mut b).map_err(|e| anyhow::anyhow!("rng: {e}"))?;
    Ok(b.iter().map(|x| format!("{x:02x}")).collect())
}

#[tokio::main]
async fn main() -> Result<()> {
    match Cli::parse().cmd {
        Cmd::Host { bind, edge } => host(bind, edge.into()).await,
        Cmd::Guest { addr, code } => guest(addr, &code).await,
    }
}

async fn host(bind: SocketAddr, edge: Edge) -> Result<()> {
    let id = Identity::generate()?;
    let token = random_token()?;
    let srv = Host::bind(bind, &id, token.clone())?;
    println!("Listening on {}", srv.local_addr()?);
    println!(
        "Pairing code (treat like a password):\n  {}:{}",
        id.fingerprint(),
        token
    );
    println!("Waiting for a guest... (Ctrl+C to quit)");

    let mut session = srv.accept().await?;
    let guest_screen = match session.recv().await {
        Some(Msg::Screen { w, h }) => mono_input::layout::Size {
            w: f64::from(w),
            h: f64::from(h),
        },
        _ => bail!("guest did not send its screen size"),
    };
    println!("Guest connected. Hotkey to take control back: Ctrl+Alt(Option)+Esc");

    let engine = HostEngine::new(
        os::screen_size()?,
        vec![Link {
            edge,
            peer: PEER,
            size: guest_screen,
        }],
    );
    let (tx, mut rx) = mpsc::unbounded_channel();
    let alive = Arc::new(AtomicBool::new(true));
    let a = alive.clone();
    // The grab loop blocks its thread for the life of the process.
    let grab = std::thread::spawn(move || os::run_host(engine, tx, a));

    let (clip_out, clip_in) = clip_bridge();
    let mut clip_out = clip_out;
    let mut tick = tokio::time::interval(std::time::Duration::from_millis(500));
    loop {
        tokio::select! {
            _ = tick.tick() => if grab.is_finished() { break },
            Some(text) = clip_out.recv() => {
                if session.send(&Msg::Clip { text }).await.is_err() { break }
            }
            Some((_, m)) = rx.recv() => {
                if let Err(e) = session.send(&m).await {
                    eprintln!("send failed: {e:#}");
                    break;
                }
            }
            m = session.recv() => match m {
                Some(Msg::Clip { text }) => { let _ = clip_in.send(text); }
                Some(_) => {}
                None => { eprintln!("guest disconnected"); break }
            },
            _ = tokio::signal::ctrl_c() => break,
        }
    }
    alive.store(false, Ordering::Relaxed);
    if grab.is_finished() {
        grab.join()
            .map_err(|_| anyhow::anyhow!("grab thread panicked"))??;
    }
    Ok(())
}

async fn guest(addr: SocketAddr, code: &str) -> Result<()> {
    let (fp, token) = code
        .split_once(':')
        .context("pairing code must be fingerprint:token")?;
    let mut session = connect(addr, fp, token).await?;
    let screen = os::screen_size()?;
    session
        .send(&Msg::Screen {
            w: screen.w as u32,
            h: screen.h as u32,
        })
        .await?;
    println!("Connected to {addr}. Ctrl+C to quit.");

    let mut engine = GuestEngine::new(screen);
    let (mut clip_out, clip_in) = clip_bridge();
    loop {
        tokio::select! {
            Some(text) = clip_out.recv() => {
                if session.send(&Msg::Clip { text }).await.is_err() { break }
            }
            m = session.recv() => match m {
                Some(Msg::Clip { text }) => { let _ = clip_in.send(text); }
                Some(m) => for a in engine.on_msg(&m) {
                    if let Err(e) = os::inject(&a) { eprintln!("{e:#}"); }
                },
                None => { eprintln!("host disconnected"); break }
            },
            _ = tokio::signal::ctrl_c() => break,
        }
    }
    for a in engine.on_disconnect() {
        let _ = os::inject(&a);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    //! Whole chain without OS input: host engine -> QUIC -> guest engine.
    use super::*;
    use mono_input::{guest::Act, host::Ev, layout::Size};
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

        // link dies with A still held: guest must release it
        assert_eq!(
            ge.on_disconnect(),
            vec![Act::Key {
                key: Key::KeyA,
                down: false
            }]
        );
    }
}
