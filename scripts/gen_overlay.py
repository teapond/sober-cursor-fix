#!/usr/bin/env python3
"""Generate transparent Roblox cursor textures for Sober's asset_overlay.

Roblox draws its own mouse cursor from PNG textures inside base.apk. Replacing
those textures with fully transparent PNGs of the same size makes Roblox's
cursor invisible, so only the compositor's native cursor is shown.

The script reads the installed base.apk, finds every cursor texture, and writes
a same-size transparent PNG into the overlay directory. No third-party Python
packages are required.

Usage:
    gen_overlay.py <base.apk> <overlay-dir>
"""

from __future__ import annotations

import os
import struct
import sys
import zipfile
import zlib

APK_ASSET_PREFIX = "assets/"
CURSOR_DIR = "content/textures/Cursors/"
EXTRA_CURSOR = "content/textures/ArrowCursor.png"

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MANIFEST_NAME = ".sober-cursor-fix.manifest"


def png_size(data: bytes) -> tuple[int, int] | None:
    """Return (width, height) from a PNG's IHDR chunk, or None."""
    if len(data) < 24 or data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


def _chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def transparent_png(width: int, height: int) -> bytes:
    """Build an 8-bit RGBA PNG that is completely transparent."""
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    # One filter byte (0 = none) followed by width * 4 transparent pixels.
    raw_row = b"\x00" * (1 + width * 4)
    idat = zlib.compress(raw_row * height, 9)
    return (
        PNG_SIGNATURE
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", idat)
        + _chunk(b"IEND", b"")
    )


def is_cursor_asset(name: str) -> bool:
    if not name.startswith(APK_ASSET_PREFIX) or not name.lower().endswith(".png"):
        return False
    rel = name[len(APK_ASSET_PREFIX):]
    return rel == EXTRA_CURSOR or rel.startswith(CURSOR_DIR)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} <base.apk> <overlay-dir>", file=sys.stderr)
        return 2

    apk_path, overlay_dir = argv[1], argv[2]
    if not os.path.isfile(apk_path):
        print(f"error: APK not found: {apk_path}", file=sys.stderr)
        return 1

    written: list[str] = []
    with zipfile.ZipFile(apk_path) as apk:
        for info in apk.infolist():
            if not is_cursor_asset(info.filename):
                continue
            data = apk.read(info)
            size = png_size(data)
            if size is None:
                print(f"skip (not a PNG): {info.filename}", file=sys.stderr)
                continue

            rel = info.filename[len(APK_ASSET_PREFIX):]
            dst = os.path.join(overlay_dir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as fh:
                fh.write(transparent_png(*size))
            written.append(rel)

    if not written:
        print("error: no cursor textures found in the APK", file=sys.stderr)
        return 1

    manifest = os.path.join(overlay_dir, MANIFEST_NAME)
    os.makedirs(overlay_dir, exist_ok=True)
    with open(manifest, "w", encoding="utf-8") as fh:
        fh.write("\n".join(written) + "\n")

    print(f"wrote {len(written)} transparent cursor textures to {overlay_dir}")
    for rel in written:
        print(f"  {rel}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
