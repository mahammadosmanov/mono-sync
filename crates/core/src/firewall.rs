//! Windows Firewall rule for the host (ADR-0016).
//!
//! The host listens on UDP 24800. A rule for this exact program lets inbound
//! packets through on private networks. Adding a rule needs administrator
//! rights, so the app asks for one UAC approval when the user clicks.
//! Elsewhere the check always passes and nothing is changed.
#![cfg_attr(not(windows), allow(dead_code))]

/// Name of the firewall rule.
pub const RULE_NAME: &str = "mono-sync";
/// UDP port the host listens on.
pub const PORT: u16 = 24800;

/// Pure: a `netsh advfirewall firewall show rule` run found the rule.
pub fn parse_show_rule_output(stdout: &str, code: i32) -> bool {
    code == 0 && !stdout.contains("No rules match")
}

/// Pure: does the rule name `exe` as its program? If the output has no English
/// `Program:` line, or the path is not ASCII (netsh prints in the console code
/// page, which can garble it), only the rule name is checked.
pub fn program_matches(stdout: &str, exe: &str) -> bool {
    let printed = stdout
        .lines()
        .any(|l| l.trim_start().starts_with("Program"));
    if !exe.is_ascii() || !printed {
        return true;
    }
    stdout
        .to_ascii_lowercase()
        .contains(&exe.to_ascii_lowercase())
}

/// Pure: a PowerShell single-quoted literal. `it's` becomes `'it''s'`.
pub fn ps_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', "''"))
}

/// Pure: the netsh arguments that add the inbound rule for `exe`.
fn add_rule_args(exe: &str) -> String {
    format!(
        "advfirewall firewall add rule name=\"{RULE_NAME}\" dir=in action=allow protocol=UDP localport={PORT} profile=private program=\"{exe}\""
    )
}

/// Pure: the PowerShell script that runs netsh elevated (one UAC prompt) and waits.
/// Exit code 0 or netsh's own code when it ran; 1223 when the prompt was not
/// accepted (ERROR_CANCELLED).
fn elevated_script(exe: &str) -> String {
    format!(
        "try {{ $p = Start-Process netsh -ArgumentList {} -Verb RunAs -Wait -PassThru -ErrorAction Stop; exit $p.ExitCode }} catch {{ exit 1223 }}",
        ps_quote(&add_rule_args(exe))
    )
}

/// Pure: a PowerShell `-EncodedCommand` payload, i.e. base64 of the script as
/// UTF-16LE. This avoids passing the script through command-line quoting.
pub fn encode_ps(script: &str) -> String {
    let bytes: Vec<u8> = script.encode_utf16().flat_map(u16::to_le_bytes).collect();
    base64(&bytes)
}

fn base64(bytes: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let b0 = u32::from(chunk[0]);
        let b1 = u32::from(*chunk.get(1).unwrap_or(&0));
        let b2 = u32::from(*chunk.get(2).unwrap_or(&0));
        let n = (b0 << 16) | (b1 << 8) | b2;
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(char::from(ALPHABET[((n >> (18 - 6 * i)) & 63) as usize]));
            } else {
                out.push('=');
            }
        }
    }
    out
}

#[cfg(windows)]
mod win {
    use super::*;
    use std::os::windows::process::CommandExt;
    use std::process::Command;

    /// Child process gets no console window.
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;

    fn current_exe() -> Result<String, String> {
        std::env::current_exe()
            .map(|p| p.display().to_string())
            .map_err(|e| format!("cannot find the mono-sync program path: {e}"))
    }

    pub fn rule_present() -> bool {
        let Ok(exe) = current_exe() else {
            return false;
        };
        let out = Command::new("netsh")
            .args([
                "advfirewall",
                "firewall",
                "show",
                "rule",
                &format!("name={RULE_NAME}"),
                "verbose",
            ])
            .creation_flags(CREATE_NO_WINDOW)
            .output();
        match out {
            Ok(o) => {
                let stdout = String::from_utf8_lossy(&o.stdout);
                parse_show_rule_output(&stdout, o.status.code().unwrap_or(-1))
                    && program_matches(&stdout, &exe)
            }
            Err(_) => false,
        }
    }

    pub fn request_rule() -> Result<(), String> {
        let exe = current_exe()?;
        let status = Command::new("powershell")
            .args([
                "-NoProfile",
                "-NonInteractive",
                "-EncodedCommand",
                &encode_ps(&elevated_script(&exe)),
            ])
            .creation_flags(CREATE_NO_WINDOW)
            .status()
            .map_err(|e| format!("cannot run PowerShell: {e}"))?;
        match status.code() {
            Some(0) => {}
            Some(1223) => return Err("Windows admin prompt was cancelled".into()),
            Some(code) => return Err(format!("Windows did not add the rule (exit code {code})")),
            None => return Err("Windows did not add the rule".into()),
        }
        if rule_present() {
            Ok(())
        } else {
            Err("The rule is still missing after the change; check Windows Firewall".into())
        }
    }
}

/// True when inbound UDP 24800 is allowed for this program (Windows). Always true
/// elsewhere. Asks `netsh` (no admin needed); keep the calls rare.
#[cfg(windows)]
pub fn rule_present() -> bool {
    win::rule_present()
}

/// Add the rule for this program on Windows, after one UAC approval. Does nothing
/// elsewhere. Blocks while the prompt is open: call it off the UI thread.
#[cfg(windows)]
pub fn request_rule() -> Result<(), String> {
    win::request_rule()
}

#[cfg(not(windows))]
pub fn rule_present() -> bool {
    true
}

#[cfg(not(windows))]
pub fn request_rule() -> Result<(), String> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const EXE: &str = r"C:\Users\anna\AppData\Local\Programs\mono-sync\mono-sync.exe";

    #[test]
    fn no_rules_match_is_not_present() {
        let out = "No rules match the specified criteria.\r\n";
        assert!(!parse_show_rule_output(out, 1));
        // Even with a zero code, the "no rule" text means absent.
        assert!(!parse_show_rule_output(out, 0));
    }

    #[test]
    fn matching_rule_is_present() {
        let out =
            "Rule Name:      mono-sync\r\nEnabled:        Yes\r\nAction:         Allow\r\nOk.\r\n";
        assert!(parse_show_rule_output(out, 0));
    }

    #[test]
    fn nonzero_exit_is_not_present() {
        assert!(!parse_show_rule_output("Rule Name: mono-sync\r\n", 1));
        assert!(!parse_show_rule_output("", -1));
    }

    #[test]
    fn program_path_must_match_when_printed() {
        let right = format!("Rule Name: mono-sync\r\nProgram: {EXE}\r\n");
        assert!(program_matches(&right, EXE));
        assert!(program_matches(&right, &EXE.to_uppercase()));

        let old = "Rule Name: mono-sync\r\nProgram: C:\\Old\\mono-sync.exe\r\n";
        assert!(
            !program_matches(old, EXE),
            "a rule for another path is not ours"
        );
    }

    #[test]
    fn program_check_falls_back_to_name_only() {
        // No Program line (e.g. localised output): the name check alone decides.
        assert!(program_matches("Rule Name: mono-sync\r\n", EXE));
        // Non-ASCII path: console code page may garble it, so do not compare.
        let garbled = "Rule Name: mono-sync\r\nProgram: C:\\Users\\J?rgen\\x.exe\r\n";
        assert!(program_matches(garbled, "C:\\Users\\Jürgen\\mono-sync.exe"));
    }

    #[test]
    fn ps_quote_doubles_single_quotes() {
        assert_eq!(ps_quote("it's"), "'it''s'");
        assert_eq!(ps_quote("plain"), "'plain'");
        assert_eq!(
            ps_quote(r"C:\Users\O'Brien\mono-sync.exe"),
            r"'C:\Users\O''Brien\mono-sync.exe'"
        );
    }

    #[test]
    fn elevated_script_escapes_exe_path_quotes() {
        let exe = r"C:\Users\O'Brien\mono-sync.exe";
        let script = elevated_script(exe);
        // The path is inside a PowerShell single-quoted literal, so each ' is doubled.
        assert!(script.contains("O''Brien"), "{script}");
        assert!(!script.contains("O'Brien"), "unescaped quote: {script}");
        assert!(
            script.contains(r#"program="C:\Users\O''Brien\mono-sync.exe""#),
            "{script}"
        );
        assert!(script.contains("-Verb RunAs -Wait"));
        assert!(script.contains("exit 1223"));
    }

    #[test]
    fn add_rule_is_inbound_udp_private_for_this_program() {
        let args = add_rule_args(EXE);
        assert!(args.contains("name=\"mono-sync\""));
        assert!(args.contains("dir=in action=allow protocol=UDP localport=24800 profile=private"));
        assert!(args.ends_with(&format!("program=\"{EXE}\"")));
    }

    #[test]
    fn base64_matches_rfc4648_vectors() {
        assert_eq!(base64(b""), "");
        assert_eq!(base64(b"f"), "Zg==");
        assert_eq!(base64(b"fo"), "Zm8=");
        assert_eq!(base64(b"foo"), "Zm9v");
        assert_eq!(base64(b"foobar"), "Zm9vYmFy");
    }

    #[test]
    fn encode_ps_is_utf16le_base64() {
        // "a" is 61 00 in UTF-16LE.
        assert_eq!(encode_ps("a"), "YQA=");
        // Non-ASCII survives as UTF-16 code units.
        assert_eq!(encode_ps("é"), base64(&[0xE9, 0x00]));
    }

    #[cfg(not(windows))]
    #[test]
    fn non_windows_always_passes_and_asks_nothing() {
        assert!(rule_present());
        assert_eq!(request_rule(), Ok(()));
    }
}
