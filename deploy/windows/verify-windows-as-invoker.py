#!/usr/bin/env python3
"""Require a standard-user execution manifest in each supplied Windows PE file."""

from __future__ import annotations

import argparse
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def manifest(data: bytes) -> bytes:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")

    coff = pe + 4
    section_count = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x20B:
        directory_offset = optional + 112
    elif magic == 0x10B:
        directory_offset = optional + 96
    else:
        raise ValueError(f"unknown optional-header magic {magic:#x}")

    resource_rva, resource_size = struct.unpack_from("<II", data, directory_offset + 16)
    if not resource_rva or not resource_size:
        raise ValueError("resource directory is missing")

    sections = []
    section_table = optional + optional_size
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        sections.append((virtual_address, max(virtual_size, raw_size), raw_size, raw_offset))

    def rva_offset(rva: int, size: int = 1) -> int:
        for address, span, raw_size, raw_offset in sections:
            delta = rva - address
            if 0 <= delta < span and delta + size <= raw_size:
                return raw_offset + delta
        raise ValueError(f"RVA {rva:#x} is not backed by PE data")

    base = rva_offset(resource_rva, resource_size)

    def entries(relative: int) -> list[tuple[int | None, int, bool]]:
        named, numeric = struct.unpack_from("<HH", data, base + relative + 12)
        result = []
        for index in range(named + numeric):
            name, target = struct.unpack_from("<II", data, base + relative + 16 + index * 8)
            identifier = None if name & 0x80000000 else name & 0xFFFF
            result.append((identifier, target & 0x7FFFFFFF, bool(target & 0x80000000)))
        return result

    type_entry = next((e for e in entries(0) if e[0] == 24 and e[2]), None)
    if type_entry is None:
        raise ValueError("RT_MANIFEST resource is missing")
    name_entry = next((e for e in entries(type_entry[1]) if e[2]), None)
    if name_entry is None:
        raise ValueError("manifest name entry is missing")
    language_entry = next((e for e in entries(name_entry[1]) if not e[2]), None)
    if language_entry is None:
        raise ValueError("manifest language entry is missing")
    content_rva, content_size = struct.unpack_from("<II", data, base + language_entry[1])
    content_offset = rva_offset(content_rva, content_size)
    return data[content_offset : content_offset + content_size]


def check(path: Path) -> None:
    root = ET.fromstring(manifest(path.read_bytes()))
    levels = [element for element in root.iter() if element.tag.rsplit("}", 1)[-1] == "requestedExecutionLevel"]
    if len(levels) != 1:
        raise ValueError("expected exactly one requestedExecutionLevel")
    level = levels[0]
    if level.attrib.get("level") != "asInvoker" or level.attrib.get("uiAccess", "false").lower() != "false":
        raise ValueError(f"unsafe execution manifest: {level.attrib}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.files:
        try:
            check(path)
        except (OSError, ValueError, struct.error, ET.ParseError) as error:
            print(f"FAIL {path}: {error}", file=sys.stderr)
            return 1
        print(f"PASS {path}: asInvoker, uiAccess=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
