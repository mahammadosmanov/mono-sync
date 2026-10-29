#!/usr/bin/env python3
"""Build a per-user NSIS setup from Deskflow's verified portable package."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath


def checked_extract(archive: Path, destination: Path) -> Path:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        entries = package.infolist()
        files = [entry for entry in entries if not entry.is_dir()]
        roots = set()
        seen_paths = set()
        for entry in entries:
            name = PurePosixPath(entry.filename.rstrip("/"))
            if (
                "\\" in entry.filename
                or name.is_absolute()
                or ".." in name.parts
                or not name.parts
                or ":" in name.parts[0]
            ):
                raise ValueError(f"unsafe path in portable archive: {entry.filename!r}")
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f"symbolic link in portable archive: {entry.filename!r}")
            normalized = str(name).casefold()
            if normalized in seen_paths:
                raise ValueError(f"duplicate path in portable archive: {entry.filename!r}")
            seen_paths.add(normalized)
            if not entry.is_dir():
                roots.add(name.parts[0])
        if len(roots) != 1:
            raise ValueError(f"expected one top-level folder, found {sorted(roots)}")
        expected_root = "deskflow-1.27.0-monosync-win-x64-portable"
        if roots != {expected_root}:
            raise ValueError(f"expected pinned package folder {expected_root!r}, got {sorted(roots)}")
        package.extractall(destination)
    payload = destination / next(iter(roots))
    for required in (
        "deskflow.exe",
        "deskflow-core.exe",
        "LICENSE",
        "LICENSE_EXCEPTION",
        "vcruntime140.dll",
        "msvcp140.dll",
        "Qt6Core.dll",
        "libcrypto-3-x64.dll",
        "libssl-3-x64.dll",
    ):
        if not (payload / required).is_file():
            raise ValueError(f"portable package is missing {required}")
    if (payload / "deskflow-daemon.exe").exists():
        raise ValueError("portable package unexpectedly contains deskflow-daemon.exe")

    marker = payload / "settings" / "Deskflow.conf"
    if not marker.is_file():
        raise ValueError("portable settings marker is missing")
    settings_files = list((payload / "settings").rglob("*"))
    if settings_files != [marker]:
        raise ValueError("portable settings folder contains data beyond its marker")
    marker.unlink()
    (payload / "settings").rmdir()
    return payload


def nsis_uninstall_include(payload: Path, path: Path) -> None:
    files = sorted(p for p in payload.rglob("*") if p.is_file())
    directories = sorted(
        (p for p in payload.rglob("*") if p.is_dir()),
        key=lambda p: len(p.relative_to(payload).parts),
        reverse=True,
    )
    lines = []
    for item in files:
        relative = item.relative_to(payload).as_posix()
        if not re.fullmatch(r"[A-Za-z0-9._ /+-]+", relative):
            raise ValueError(f"path cannot be represented safely in NSIS: {relative!r}")
        lines.append(f'Delete "$INSTDIR\\{relative.replace("/", chr(92))}"')
    for item in directories:
        relative = item.relative_to(payload).as_posix()
        if not re.fullmatch(r"[A-Za-z0-9._ /+-]+", relative):
            raise ValueError(f"path cannot be represented safely in NSIS: {relative!r}")
        lines.append(f'RMDir "$INSTDIR\\{relative.replace("/", chr(92))}"')
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--portable-zip", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--makensis", default="makensis")
    args = parser.parse_args()

    archive = args.portable_zip.resolve(strict=True)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.makensis)
    if compiler is None:
        raise SystemExit(f"NSIS compiler not found: {args.makensis}")

    script = Path(__file__).with_name("standard-user-setup.nsi").resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="mono-sync-nsis-") as temporary:
        temporary_path = Path(temporary)
        payload = checked_extract(archive, temporary_path / "expanded")
        uninstall_include = temporary_path / "uninstall-files.nsh"
        nsis_uninstall_include(payload, uninstall_include)

        command = [
            compiler,
            "-V2",
            f"-DPAYLOAD_DIR={payload}",
            f"-DOUTPUT_FILE={output}",
            f"-DUNINSTALL_FILE_LIST={uninstall_include}",
            str(script),
        ]
        result = subprocess.run(command, check=False)
        if result.returncode:
            return result.returncode

        verifier = Path(__file__).with_name("verify-windows-as-invoker.py").resolve(
            strict=True
        )
        subprocess.run(
            [
                sys.executable,
                str(verifier),
                str(output),
                str(payload / "deskflow.exe"),
                str(payload / "deskflow-core.exe"),
            ],
            check=True,
        )

    if not output.is_file() or output.stat().st_size < 1_000_000:
        raise SystemExit("NSIS completed without producing a plausible setup executable")
    print(f"Created {output} ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"setup packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
