//! Two real cores on loopback: pairing, file transfer both ways, reconnect.
//! Headless: no OS input is grabbed and the real clipboard is untouched.
use mono_core::config::{Config, Role};
use mono_core::Core;
use std::{path::Path, time::Duration};

async fn until(what: &str, mut f: impl FnMut() -> bool) {
    for _ in 0..100 {
        if f() {
            return;
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    panic!("timed out waiting for: {what}");
}

fn only_file(dir: &Path) -> Option<std::path::PathBuf> {
    std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .next()
}

#[tokio::test]
async fn pair_transfer_and_reconnect() {
    let root = std::env::temp_dir().join(format!("mono-core-it-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    std::fs::create_dir_all(&root).unwrap();
    std::env::set_var("MONO_HOME", root.join("home"));
    let (in_host, in_guest) = (root.join("in-host"), root.join("in-guest"));

    let host = Core::headless().unwrap();
    host.save_and_apply(Config {
        role: Role::Host,
        bind: "127.0.0.1:24816".into(),
        download_dir: in_host.display().to_string(),
        ..Config::default()
    })
    .unwrap();
    until("host pairing code", || host.view().code.is_some()).await;
    let code = host.view().code.unwrap();

    let guest_cfg = Config {
        role: Role::Guest,
        host_addr: "127.0.0.1:24816".into(),
        pairing_code: code.clone(),
        download_dir: in_guest.display().to_string(),
        ..Config::default()
    };
    let guest = Core::headless().unwrap();
    guest.save_and_apply(guest_cfg.clone()).unwrap();
    until("both connected", || {
        host.view().connected && guest.view().connected
    })
    .await;

    let data: Vec<u8> = (0..2_000_001usize).map(|i| (i * 7 % 253) as u8).collect();
    let src = root.join("src.bin");
    std::fs::write(&src, &data).unwrap();
    assert_eq!(host.sh.send_file(&src).await.unwrap(), data.len() as u64);
    until("guest got file", || only_file(&in_guest).is_some()).await;
    assert_eq!(std::fs::read(only_file(&in_guest).unwrap()).unwrap(), data);

    guest.sh.send_file(&src).await.unwrap();
    until("host got file", || only_file(&in_host).is_some()).await;

    // guest restarts (settings re-applied): must come back without waiting for idle timeout
    guest.save_and_apply(guest_cfg).unwrap();
    tokio::time::sleep(Duration::from_millis(300)).await;
    until("reconnected", || {
        host.view().connected && guest.view().connected
    })
    .await;

    // host settings saved again (e.g. direction changed): same port must rebind and the guest return
    let mut hc = Config::load().unwrap();
    hc.role = Role::Host;
    hc.bind = "127.0.0.1:24816".into();
    hc.download_dir = in_host.display().to_string();
    host.save_and_apply(hc).unwrap();
    until("reconnected after host re-save", || {
        host.view().connected && guest.view().connected
    })
    .await;
    assert!(
        !host.view().state.contains("Error"),
        "{}",
        host.view().state
    );

    // bad pairing code is rejected up front
    let mut bad = Config::load().unwrap();
    bad.role = Role::Guest;
    bad.host_addr = "127.0.0.1:24816".into();
    bad.pairing_code = "abc".into();
    assert!(guest.save_and_apply(bad).is_err());
    std::fs::remove_dir_all(&root).unwrap();
}
