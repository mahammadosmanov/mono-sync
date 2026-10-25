//! File transfer. Each file travels on its own QUIC unidirectional stream, so
//! a large transfer never delays keystrokes or mouse motion (separate stream).
//!
//! Wire format: u8 version, u16 name length, name (UTF-8), u64 size,
//! `size` bytes of data, 32-byte SHA-256 of the data.
use anyhow::{bail, ensure, Context, Result};
use quinn::Connection;
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use tokio::{
    fs::File,
    io::{AsyncReadExt, AsyncWriteExt},
    sync::mpsc::UnboundedSender,
};

const FILE_VERSION: u8 = 1;
const MAX_NAME: usize = 512;
const CHUNK: usize = 256 * 1024;

#[derive(Debug, Clone, PartialEq)]
pub enum FileEvent {
    Received { path: PathBuf, size: u64 },
    Failed { name: String, error: String },
}

/// Strip directories and characters Windows/macOS reject. Never returns an
/// empty name or one that could escape the download directory (threat T7).
pub fn sanitize_name(raw: &str) -> String {
    let last = raw.rsplit(['/', '\\']).next().unwrap_or("");
    let cleaned: String = last
        .chars()
        .filter(|c| !c.is_control() && !matches!(c, '<' | '>' | ':' | '"' | '|' | '?' | '*'))
        .collect();
    let cleaned = cleaned.trim().trim_matches('.').trim().to_string();
    if cleaned.is_empty() {
        "file".to_string()
    } else {
        cleaned
    }
}

/// `dir/name`, or `dir/name (1).ext` etc. if taken. Never overwrites.
pub fn unique_path(dir: &Path, name: &str) -> PathBuf {
    let first = dir.join(name);
    if !first.exists() {
        return first;
    }
    let p = Path::new(name);
    let stem = p.file_stem().and_then(|s| s.to_str()).unwrap_or(name);
    let ext = p.extension().and_then(|s| s.to_str());
    for i in 1.. {
        let n = match ext {
            Some(e) => format!("{stem} ({i}).{e}"),
            None => format!("{stem} ({i})"),
        };
        let c = dir.join(n);
        if !c.exists() {
            return c;
        }
    }
    unreachable!()
}

/// Send one file. Returns bytes sent once the peer has the whole stream.
pub async fn send(conn: &Connection, path: &Path) -> Result<u64> {
    let mut f = File::open(path)
        .await
        .with_context(|| format!("open {}", path.display()))?;
    let meta = f.metadata().await?;
    ensure!(meta.is_file(), "not a regular file: {}", path.display());
    let name = sanitize_name(path.file_name().and_then(|n| n.to_str()).unwrap_or("file"));
    ensure!(name.len() <= MAX_NAME, "file name too long");

    let mut tx = conn.open_uni().await?;
    let mut head = vec![FILE_VERSION];
    head.extend((name.len() as u16).to_be_bytes());
    head.extend(name.as_bytes());
    head.extend(meta.len().to_be_bytes());
    tx.write_all(&head).await?;

    let mut hash = Sha256::new();
    let mut buf = vec![0u8; CHUNK];
    let mut sent = 0u64;
    while sent < meta.len() {
        let n = f.read(&mut buf).await?;
        if n == 0 {
            bail!("file shrank while sending");
        }
        hash.update(&buf[..n]);
        tx.write_all(&buf[..n]).await?;
        sent += n as u64;
    }
    tx.write_all(&hash.finalize()).await?;
    tx.finish()?;
    tx.stopped().await?;
    Ok(sent)
}

/// Accept incoming files until the connection closes.
pub async fn receive_loop(conn: Connection, dir: PathBuf, ev: UnboundedSender<FileEvent>) {
    while let Ok(rx) = conn.accept_uni().await {
        let (dir, ev) = (dir.clone(), ev.clone());
        tokio::spawn(async move {
            let mut name = String::from("?");
            let r = receive_one(rx, &dir, &mut name).await;
            let _ = ev.send(match r {
                Ok((path, size)) => FileEvent::Received { path, size },
                Err(e) => FileEvent::Failed {
                    name,
                    error: format!("{e:#}"),
                },
            });
        });
    }
}

async fn receive_one(
    mut rx: quinn::RecvStream,
    dir: &Path,
    name_out: &mut String,
) -> Result<(PathBuf, u64)> {
    let mut v = [0u8; 1];
    rx.read_exact(&mut v).await?;
    ensure!(v[0] == FILE_VERSION, "unsupported file protocol {}", v[0]);
    let mut l = [0u8; 2];
    rx.read_exact(&mut l).await?;
    let n = u16::from_be_bytes(l) as usize;
    ensure!(n <= MAX_NAME, "file name too long");
    let mut nb = vec![0u8; n];
    rx.read_exact(&mut nb).await?;
    let name = sanitize_name(&String::from_utf8(nb).context("file name not UTF-8")?);
    *name_out = name.clone();
    let mut sz = [0u8; 8];
    rx.read_exact(&mut sz).await?;
    let size = u64::from_be_bytes(sz);

    tokio::fs::create_dir_all(dir).await?;
    let dest = unique_path(dir, &name);
    let part = dest.with_extension(format!(
        "{}part",
        dest.extension()
            .and_then(|e| e.to_str())
            .map(|e| format!("{e}."))
            .unwrap_or_default()
    ));
    let res = write_body(&mut rx, &part, size).await;
    match res {
        Ok(()) => {
            tokio::fs::rename(&part, &dest).await?;
            Ok((dest, size))
        }
        Err(e) => {
            let _ = tokio::fs::remove_file(&part).await;
            Err(e)
        }
    }
}

async fn write_body(rx: &mut quinn::RecvStream, part: &Path, size: u64) -> Result<()> {
    let mut out = File::create(part).await?;
    let mut hash = Sha256::new();
    let mut buf = vec![0u8; CHUNK];
    let mut left = size;
    while left > 0 {
        let want = left.min(CHUNK as u64) as usize;
        rx.read_exact(&mut buf[..want]).await?;
        hash.update(&buf[..want]);
        out.write_all(&buf[..want]).await?;
        left -= want as u64;
    }
    let mut theirs = [0u8; 32];
    rx.read_exact(&mut theirs).await?;
    out.flush().await?;
    ensure!(
        hash.finalize().as_slice() == theirs,
        "checksum mismatch, file discarded"
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitize_blocks_traversal_and_bad_chars() {
        assert_eq!(sanitize_name("../../etc/passwd"), "passwd");
        assert_eq!(sanitize_name("..\\..\\evil.exe"), "evil.exe");
        assert_eq!(sanitize_name("a:b*c?.txt"), "abc.txt");
        assert_eq!(sanitize_name(".."), "file");
        assert_eq!(sanitize_name(""), "file");
        assert_eq!(sanitize_name("rapor 2026.pdf"), "rapor 2026.pdf");
        assert_eq!(sanitize_name("çay.txt"), "çay.txt");
    }

    #[test]
    fn unique_path_never_overwrites() {
        let d = std::env::temp_dir().join(format!("mono-uniq-{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        std::fs::write(d.join("a.txt"), b"x").unwrap();
        assert_eq!(unique_path(&d, "a.txt"), d.join("a (1).txt"));
        std::fs::write(d.join("a (1).txt"), b"x").unwrap();
        assert_eq!(unique_path(&d, "a.txt"), d.join("a (2).txt"));
        assert_eq!(unique_path(&d, "b"), d.join("b"));
        std::fs::remove_dir_all(&d).unwrap();
    }
}
