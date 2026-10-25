//! mono: one keyboard and mouse across computers on the same LAN.
mod config;
mod run;
mod web;

use anyhow::Result;
use clap::{Parser, Subcommand};
use config::{Config, EdgeCfg, Role};

#[derive(Parser)]
#[command(
    version,
    about = "One keyboard and mouse across computers on the same LAN"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Option<Cmd>,
    /// Do not open the control panel in the browser.
    #[arg(long, global = true)]
    no_browser: bool,
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
    let cli = Cli::parse();
    let mut cfg = Config::load()?;
    let open_browser = cli.cmd.is_none() && !cli.no_browser;
    match cli.cmd {
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
    cfg.save()?;
    let core = run::Core::new()?;
    let web = web::start(core.clone())?;
    println!("Control panel: {}", web.url);
    if open_browser {
        web::open_path(&web.url);
    }
    core.apply(cfg);
    for _ in 0..20 {
        if let (_, _, Some(code), _) = core.sh.snapshot() {
            println!("Pairing code (treat like a password):\n  {code}");
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    }
    tokio::signal::ctrl_c().await?;
    Ok(())
}
