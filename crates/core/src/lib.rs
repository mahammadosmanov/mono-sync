//! Everything except the user interface: settings, identity, host/guest loops.
//! The CLI and the desktop app are thin shells over this crate.
pub mod config;
pub mod run;

pub use run::{Core, View};
