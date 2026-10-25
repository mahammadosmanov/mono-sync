//! Everything except the user interface: settings, identity, host/guest loops.
//! The CLI and the desktop app are thin shells over this crate.
pub mod config;
pub mod run;

pub use run::{Core, View};

pub use mono_input::os::open_permission_settings;

/// Names of the macOS permissions, for UIs.
pub mod os_names {
    pub use mono_input::os::{ACCESSIBILITY, INPUT_MONITORING};
}
