//! Settings and persistent identity, stored per user (no admin rights needed).
use anyhow::{Context, Result};
use mono_input::layout::Edge;
use mono_net::Identity;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Serialize, Deserialize, Clone, Copy, Default, PartialEq, Eq, Debug)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    #[default]
    None,
    Host,
    Guest,
}

#[derive(Serialize, Deserialize, Clone, Copy, Default, PartialEq, Eq, Debug)]
#[serde(rename_all = "lowercase")]
pub enum EdgeCfg {
    Left,
    #[default]
    Right,
    Top,
    Bottom,
}

impl From<EdgeCfg> for Edge {
    fn from(e: EdgeCfg) -> Edge {
        match e {
            EdgeCfg::Left => Edge::Left,
            EdgeCfg::Right => Edge::Right,
            EdgeCfg::Top => Edge::Top,
            EdgeCfg::Bottom => Edge::Bottom,
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(default)]
pub struct Config {
    pub role: Role,
    /// Host: side of this screen where the guest sits.
    pub edge: EdgeCfg,
    /// Host: address to listen on.
    pub bind: String,
    /// Guest: host address, e.g. 192.168.1.20:24800.
    pub host_addr: String,
    /// Guest: pairing code shown by the host (fingerprint:token).
    pub pairing_code: String,
    /// Where received files go. Empty = Downloads/mono.
    pub download_dir: String,
    /// Guest: draw the cursor ourselves even if Windows reports a mouse.
    /// (Without any mouse Windows shows no pointer; that is detected automatically.)
    pub draw_cursor: bool,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            role: Role::None,
            edge: EdgeCfg::Right,
            bind: "0.0.0.0:24800".into(),
            host_addr: String::new(),
            pairing_code: String::new(),
            download_dir: String::new(),
            draw_cursor: false,
        }
    }
}

pub fn dir() -> Result<PathBuf> {
    let d = match std::env::var_os("MONO_HOME") {
        Some(h) => PathBuf::from(h),
        None => dirs::config_dir()
            .context("no config directory")?
            .join("mono-sync"),
    };
    std::fs::create_dir_all(&d)?;
    Ok(d)
}

impl Config {
    pub fn load() -> Result<Config> {
        match std::fs::read_to_string(dir()?.join("config.json")) {
            Ok(t) => Ok(serde_json::from_str(&t).context("config.json is damaged")?),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(Config::default()),
            Err(e) => Err(e.into()),
        }
    }

    pub fn save(&self) -> Result<()> {
        write_private(
            &dir()?.join("config.json"),
            &serde_json::to_string_pretty(self)?,
        )
    }

    pub fn download_dir(&self) -> PathBuf {
        if self.download_dir.trim().is_empty() {
            dirs::download_dir()
                .or_else(dirs::home_dir)
                .unwrap_or_default()
                .join("mono")
        } else {
            PathBuf::from(self.download_dir.trim())
        }
    }
}

/// Write via a temp file so a crash never leaves a half-written file; owner-only on Unix.
fn write_private(path: &std::path::Path, text: &str) -> Result<()> {
    let tmp = path.with_extension("tmp");
    std::fs::write(&tmp, text)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&tmp, std::fs::Permissions::from_mode(0o600))?;
    }
    std::fs::rename(&tmp, path)?;
    Ok(())
}

pub fn random_hex(n: usize) -> Result<String> {
    let mut b = vec![0u8; n];
    getrandom::fill(&mut b).map_err(|e| anyhow::anyhow!("rng: {e}"))?;
    Ok(b.iter().map(|x| format!("{x:02x}")).collect())
}

/// Host identity (certificate) and pairing token, created once and reused so
/// the pairing code stays valid across restarts.
pub fn load_identity() -> Result<(Identity, String)> {
    let d = dir()?;
    let (idf, tokf) = (d.join("identity.txt"), d.join("token.txt"));
    let id = match std::fs::read_to_string(&idf) {
        Ok(t) => Identity::from_text(&t)
            .context("identity.txt is damaged; delete it to reset pairing")?,
        Err(_) => {
            let id = Identity::generate()?;
            write_private(&idf, &id.to_text())?;
            id
        }
    };
    let token = match std::fs::read_to_string(&tokf) {
        Ok(t) if t.trim().len() == 32 => t.trim().to_string(),
        _ => {
            let t = random_hex(16)?;
            write_private(&tokf, &t)?;
            t
        }
    };
    Ok((id, token))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_roundtrip_defaults_and_identity_persist() {
        let d = std::env::temp_dir().join(format!("mono-cfg-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::env::set_var("MONO_HOME", &d);

        assert_eq!(Config::load().unwrap(), Config::default());
        let c = Config {
            role: Role::Host,
            edge: EdgeCfg::Left,
            ..Config::default()
        };
        c.save().unwrap();
        assert_eq!(Config::load().unwrap(), c);

        // unknown/missing fields fall back to defaults instead of failing
        std::fs::write(d.join("config.json"), r#"{"role":"guest"}"#).unwrap();
        assert_eq!(Config::load().unwrap().role, Role::Guest);
        std::fs::write(d.join("config.json"), "not json").unwrap();
        assert!(Config::load().is_err());

        let (a, ta) = load_identity().unwrap();
        let (b, tb) = load_identity().unwrap();
        assert_eq!(a.fingerprint(), b.fingerprint());
        assert_eq!(ta, tb);
        assert_eq!(ta.len(), 32);
        std::fs::remove_dir_all(&d).unwrap();
    }
}
