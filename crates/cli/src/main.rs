//! Headless `mono`: the same engine as the desktop app, driven from a terminal.
use anyhow::Result;
use clap::{Parser, Subcommand};
use mono_core::config::{Config, EdgeCfg, Role};
use mono_core::Core;

#[derive(Parser)]
#[command(
    version,
    about = "One keyboard and mouse across computers on the same LAN"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Option<Cmd>,
}

#[derive(Subcommand)]
enum Cmd {
    /// Share this machine's keyboard and mouse (saves the choice as settings).
    Host {
        #[arg(long, default_value = "0.0.0.0:24800")]
        bind: String,
        /// Side of this screen where the other computer sits.
        #[arg(long, value_enum, default_value = "right")]
        edge: EdgeArg,
    },
    /// Receive another machine's keyboard and mouse (saves the choice as settings).
    Guest {
        /// Host address, e.g. 192.168.1.20:24800
        addr: String,
        /// Pairing code shown by the host (fingerprint:token).
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

impl From<EdgeArg> for EdgeCfg {
    fn from(e: EdgeArg) -> EdgeCfg {
        match e {
            EdgeArg::Left => EdgeCfg::Left,
            EdgeArg::Right => EdgeCfg::Right,
            EdgeArg::Top => EdgeCfg::Top,
            EdgeArg::Bottom => EdgeCfg::Bottom,
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut cfg = Config::load()?;
    match Cli::parse().cmd {
        Some(Cmd::Host { bind, edge }) => {
            cfg.role = Role::Host;
            cfg.bind = bind;
            cfg.edge = edge.into();
        }
        Some(Cmd::Guest { addr, code }) => {
            cfg.role = Role::Guest;
            cfg.host_addr = addr;
            cfg.pairing_code = code;
        }
        None => {}
    }
    let core = Core::new()?;
    core.save_and_apply(cfg)?;
    for _ in 0..20 {
        if let Some(code) = core.view().code {
            println!("Pairing code (treat like a password):\n  {code}");
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    }
    tokio::signal::ctrl_c().await?;
    Ok(())
}
