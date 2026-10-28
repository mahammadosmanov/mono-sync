//! Input-side logic. `layout` and `guard` are pure (no OS calls) so they are
//! tested everywhere; OS backends will live in separate modules (ADR-0005).
pub mod guard;
pub mod guest;
pub mod host;
pub mod layout;
pub mod os;
pub mod queue;
