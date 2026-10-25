//! QUIC transport. Host (owns keyboard/mouse) listens; guest connects.
//!
//! Trust model: guest pins the host's certificate fingerprint (stops an
//! impostor host injecting input); host requires the pairing token (stops
//! strangers receiving keystrokes). Both travel together in a pairing code.
use anyhow::{bail, Context, Result};
use mono_proto::{Msg, VERSION};
use quinn::Endpoint;
use rustls::pki_types::{CertificateDer, PrivateKeyDer, PrivatePkcs8KeyDer, ServerName, UnixTime};
use sha2::{Digest, Sha256};
use std::{net::SocketAddr, sync::Arc, time::Duration};
use tokio::sync::mpsc;

pub mod file;
pub use quinn::Connection;

const MAX_FRAME: usize = 64 * 1024;

pub struct Identity {
    cert: CertificateDer<'static>,
    key: PrivatePkcs8KeyDer<'static>,
}

impl Identity {
    pub fn generate() -> Result<Self> {
        let ck = rcgen::generate_simple_self_signed(vec!["mono".into()])?;
        Ok(Self {
            cert: ck.cert.der().clone(),
            key: PrivatePkcs8KeyDer::from(ck.signing_key.serialize_der()),
        })
    }
    pub fn fingerprint(&self) -> String {
        fingerprint(&self.cert)
    }

    /// Text form for the config directory: two hex lines (certificate, key).
    /// The key is a secret: callers must write it with owner-only permissions.
    pub fn to_text(&self) -> String {
        format!(
            "{}\n{}\n",
            hex(self.cert.as_ref()),
            hex(self.key.secret_pkcs8_der())
        )
    }

    pub fn from_text(s: &str) -> Result<Self> {
        let mut l = s.lines();
        let (c, k) = (
            l.next().context("missing cert")?,
            l.next().context("missing key")?,
        );
        Ok(Self {
            cert: CertificateDer::from(unhex(c)?),
            key: PrivatePkcs8KeyDer::from(unhex(k)?),
        })
    }
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn unhex(s: &str) -> Result<Vec<u8>> {
    let s = s.trim();
    if !s.len().is_multiple_of(2) || !s.is_ascii() {
        bail!("bad hex");
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).context("bad hex"))
        .collect()
}

fn fingerprint(c: &CertificateDer<'_>) -> String {
    Sha256::digest(c.as_ref())
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect()
}

fn transport() -> Arc<quinn::TransportConfig> {
    let mut t = quinn::TransportConfig::default();
    t.keep_alive_interval(Some(Duration::from_secs(2)));
    t.max_idle_timeout(Some(Duration::from_secs(10).try_into().unwrap()));
    t.datagram_receive_buffer_size(Some(64 * 1024));
    Arc::new(t)
}

/// An established, authenticated link. `recv` yields messages from both the
/// reliable stream and datagrams.
pub struct Session {
    conn: Connection,
    tx: quinn::SendStream,
    rx: mpsc::Receiver<Msg>,
}

impl Session {
    async fn start(conn: Connection, tx: quinn::SendStream, mut rs: quinn::RecvStream) -> Self {
        let (out, rx) = mpsc::channel(1024);
        let o = out.clone();
        tokio::spawn(async move {
            while let Ok(m) = read_frame(&mut rs).await {
                if o.send(m).await.is_err() {
                    break;
                }
            }
        });
        let c = conn.clone();
        tokio::spawn(async move {
            while let Ok(d) = c.read_datagram().await {
                if let Ok(m) = Msg::decode(&d) {
                    if out.try_send(m).is_err() {
                        // queue full: drop lossy motion rather than lag
                    }
                }
            }
        });
        Self { conn, tx, rx }
    }

    /// Handle for file transfer and other extra streams.
    pub fn connection(&self) -> Connection {
        self.conn.clone()
    }

    pub async fn send(&mut self, m: &Msg) -> Result<()> {
        if m.is_lossy() {
            self.conn.send_datagram(m.encode().into())?;
        } else {
            write_frame(&mut self.tx, m).await?;
        }
        Ok(())
    }

    /// `None` when the peer is gone.
    pub async fn recv(&mut self) -> Option<Msg> {
        self.rx.recv().await
    }
}

async fn write_frame(s: &mut quinn::SendStream, m: &Msg) -> Result<()> {
    let b = m.encode();
    s.write_all(&(b.len() as u32).to_be_bytes()).await?;
    s.write_all(&b).await?;
    Ok(())
}

async fn read_frame(s: &mut quinn::RecvStream) -> Result<Msg> {
    let mut len = [0u8; 4];
    s.read_exact(&mut len).await?;
    let n = u32::from_be_bytes(len) as usize;
    if n > MAX_FRAME {
        bail!("frame too large: {n}");
    }
    let mut buf = vec![0u8; n];
    s.read_exact(&mut buf).await?;
    Ok(Msg::decode(&buf)?)
}

pub struct Host {
    ep: Endpoint,
    token: String,
}

impl Host {
    pub fn bind(addr: SocketAddr, id: &Identity, token: String) -> Result<Self> {
        let provider = rustls::crypto::ring::default_provider();
        let tls = rustls::ServerConfig::builder_with_provider(provider.into())
            .with_safe_default_protocol_versions()?
            .with_no_client_auth()
            .with_single_cert(
                vec![id.cert.clone()],
                PrivateKeyDer::Pkcs8(id.key.clone_key()),
            )?;
        let mut cfg = quinn::ServerConfig::with_crypto(Arc::new(
            quinn::crypto::rustls::QuicServerConfig::try_from(tls)?,
        ));
        cfg.transport_config(transport());
        Ok(Self {
            ep: Endpoint::server(cfg, addr)?,
            token,
        })
    }

    pub fn local_addr(&self) -> Result<SocketAddr> {
        Ok(self.ep.local_addr()?)
    }

    /// Wait for one guest that presents the right token. Bad guests are
    /// dropped and the wait continues.
    pub async fn accept(&self) -> Result<Session> {
        loop {
            let Some(inc) = self.ep.accept().await else {
                bail!("endpoint closed")
            };
            match self.handshake(inc).await {
                Ok(s) => return Ok(s),
                Err(e) => eprintln!("rejected guest: {e:#}"),
            }
        }
    }

    async fn handshake(&self, inc: quinn::Incoming) -> Result<Session> {
        let conn = inc.await?;
        let (tx, mut rs) = tokio::time::timeout(Duration::from_secs(5), conn.accept_bi())
            .await
            .context("no stream")??;
        let hello = tokio::time::timeout(Duration::from_secs(5), read_frame(&mut rs))
            .await
            .context("no hello")??;
        match hello {
            Msg::Hello { version, token } if version == VERSION && ct_eq(&token, &self.token) => {}
            Msg::Hello { version, .. } if version != VERSION => bail!("version {version}"),
            _ => bail!("bad token"),
        }
        let mut s = Session::start(conn, tx, rs).await;
        s.send(&Msg::HelloOk).await?;
        Ok(s)
    }
}

fn ct_eq(a: &str, b: &str) -> bool {
    a.len() == b.len() && a.bytes().zip(b.bytes()).fold(0u8, |d, (x, y)| d | (x ^ y)) == 0
}

#[derive(Debug)]
struct Pin(String);

impl rustls::client::danger::ServerCertVerifier for Pin {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _: &[CertificateDer<'_>],
        _: &ServerName<'_>,
        _: &[u8],
        _: UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        if ct_eq(&fingerprint(end_entity), &self.0) {
            Ok(rustls::client::danger::ServerCertVerified::assertion())
        } else {
            Err(rustls::Error::General("host fingerprint mismatch".into()))
        }
    }
    fn verify_tls12_signature(
        &self,
        m: &[u8],
        c: &CertificateDer<'_>,
        d: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            m,
            c,
            d,
            &rustls::crypto::ring::default_provider().signature_verification_algorithms,
        )
    }
    fn verify_tls13_signature(
        &self,
        m: &[u8],
        c: &CertificateDer<'_>,
        d: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            m,
            c,
            d,
            &rustls::crypto::ring::default_provider().signature_verification_algorithms,
        )
    }
    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        rustls::crypto::ring::default_provider()
            .signature_verification_algorithms
            .supported_schemes()
    }
}

pub async fn connect(addr: SocketAddr, fingerprint: &str, token: &str) -> Result<Session> {
    let provider = Arc::new(rustls::crypto::ring::default_provider());
    let tls = rustls::ClientConfig::builder_with_provider(provider)
        .with_safe_default_protocol_versions()?
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(Pin(fingerprint.to_string())))
        .with_no_client_auth();
    let mut cfg = quinn::ClientConfig::new(Arc::new(
        quinn::crypto::rustls::QuicClientConfig::try_from(tls)?,
    ));
    cfg.transport_config(transport());
    let bind: SocketAddr = if addr.is_ipv6() {
        "[::]:0"
    } else {
        "0.0.0.0:0"
    }
    .parse()?;
    let mut ep = Endpoint::client(bind)?;
    ep.set_default_client_config(cfg);
    let conn = ep
        .connect(addr, "mono")?
        .await
        .context("connect (fingerprint wrong?)")?;
    let (mut tx, rs) = conn.open_bi().await?;
    write_frame(
        &mut tx,
        &Msg::Hello {
            version: VERSION,
            token: token.into(),
        },
    )
    .await?;
    let mut s = Session::start(conn, tx, rs).await;
    match tokio::time::timeout(Duration::from_secs(5), s.recv()).await {
        Ok(Some(Msg::HelloOk)) => Ok(s),
        _ => bail!("host refused (wrong token or version)"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use mono_proto::Key;

    fn lo() -> SocketAddr {
        "127.0.0.1:0".parse().unwrap()
    }

    #[tokio::test]
    async fn happy_path_reliable_and_datagram() {
        let id = Identity::generate().unwrap();
        let host = Host::bind(lo(), &id, "secret".into()).unwrap();
        let addr = host.local_addr().unwrap();
        let fp = id.fingerprint();
        let g = tokio::spawn(async move { connect(addr, &fp, "secret").await.unwrap() });
        let mut hs = host.accept().await.unwrap();
        let mut gs = g.await.unwrap();

        hs.send(&Msg::Key {
            key: Key::KeyA,
            down: true,
        })
        .await
        .unwrap();
        assert_eq!(
            gs.recv().await,
            Some(Msg::Key {
                key: Key::KeyA,
                down: true
            })
        );

        hs.send(&Msg::Move { dx: 5, dy: -5 }).await.unwrap();
        assert_eq!(gs.recv().await, Some(Msg::Move { dx: 5, dy: -5 }));
    }

    #[tokio::test]
    async fn file_roundtrip_with_hash_check() {
        let id = Identity::generate().unwrap();
        let host = Host::bind(lo(), &id, "secret".into()).unwrap();
        let addr = host.local_addr().unwrap();
        let fp = id.fingerprint();
        let g = tokio::spawn(async move { connect(addr, &fp, "secret").await.unwrap() });
        let hs = host.accept().await.unwrap();
        let gs = g.await.unwrap();

        let dir = std::env::temp_dir().join(format!("mono-ft-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let (etx, mut erx) = tokio::sync::mpsc::unbounded_channel();
        tokio::spawn(file::receive_loop(gs.connection(), dir.join("in"), etx));

        // 3 MiB + 1 byte of non-trivial data, crosses several chunks
        let data: Vec<u8> = (0..3 * 1024 * 1024 + 1)
            .map(|i: usize| (i * 31 % 251) as u8)
            .collect();
        let src = dir.join("big.bin");
        std::fs::write(&src, &data).unwrap();
        assert_eq!(
            file::send(&hs.connection(), &src).await.unwrap(),
            data.len() as u64
        );
        match tokio::time::timeout(Duration::from_secs(10), erx.recv())
            .await
            .unwrap()
            .unwrap()
        {
            file::FileEvent::Received { path, size } => {
                assert_eq!(size, data.len() as u64);
                assert_eq!(std::fs::read(path).unwrap(), data);
            }
            e => panic!("{e:?}"),
        }
        // second send of same name must not overwrite
        file::send(&hs.connection(), &src).await.unwrap();
        match erx.recv().await.unwrap() {
            file::FileEvent::Received { path, .. } => {
                assert!(path.file_name().unwrap().to_str().unwrap().contains("(1)"))
            }
            e => panic!("{e:?}"),
        }
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[tokio::test]
    async fn identity_survives_save_and_load() {
        let id = Identity::generate().unwrap();
        let back = Identity::from_text(&id.to_text()).unwrap();
        assert_eq!(id.fingerprint(), back.fingerprint());
        // the reloaded identity must still work for TLS
        let host = Host::bind(lo(), &back, "t".into()).unwrap();
        let addr = host.local_addr().unwrap();
        let fp = id.fingerprint();
        let g = tokio::spawn(async move { connect(addr, &fp, "t").await.is_ok() });
        host.accept().await.unwrap();
        assert!(g.await.unwrap());
        assert!(Identity::from_text("zz\nzz").is_err());
        assert!(Identity::from_text("").is_err());
    }

    #[tokio::test]
    async fn wrong_token_refused() {
        let id = Identity::generate().unwrap();
        let host = Host::bind(lo(), &id, "secret".into()).unwrap();
        let addr = host.local_addr().unwrap();
        let fp = id.fingerprint();
        tokio::spawn(async move {
            let _ = host.accept().await;
        });
        assert!(connect(addr, &fp, "nope").await.is_err());
    }

    #[tokio::test]
    async fn wrong_fingerprint_refused() {
        let id = Identity::generate().unwrap();
        let host = Host::bind(lo(), &id, "secret".into()).unwrap();
        let addr = host.local_addr().unwrap();
        tokio::spawn(async move {
            let _ = host.accept().await;
        });
        let bad = "00".repeat(32);
        assert!(connect(addr, &bad, "secret").await.is_err());
    }
}
