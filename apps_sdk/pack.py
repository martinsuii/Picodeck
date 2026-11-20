#!/usr/bin/env python3
"""Pack a PicoPad app folder into a single .pap file.

The PicoPad's FAT layer only understands 8.3 names, so the package extension
is .pap (3 chars) and every file inside must fit 8.3. The container magic is
"PAPP".

Usage:
    python3 pack.py examples/hello
    python3 pack.py examples/hello out/hello.pap
"""

import os
import struct
import sys

MAGIC = b"PAPP"
VERSION = 1
TOC_SIZE = 40

SKIP_NAMES = {"config.ini", "data"}
SKIP_EXTS = {".c", ".cpp", ".h", ".hpp", ".md", ".py"}


def short_name(name):
    if name in SKIP_NAMES:
        return None
    base, ext = os.path.splitext(name)
    if ext.lower() in SKIP_EXTS:
        return None
    ext = ext[1:] if ext.startswith(".") else ext
    if not base or len(base) > 8 or len(ext) > 3:
        raise SystemExit(
            f"error: '{name}' is not an 8.3 name (base <= 8, ext <= 3)"
        )
    out = base.upper()
    if ext:
        out += "." + ext.upper()
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    appdir = os.path.abspath(sys.argv[1])
    if not os.path.isdir(appdir):
        raise SystemExit(f"error: not a folder: {appdir}")

    appname = os.path.basename(appdir.rstrip("/\\"))
    if len(appname) > 8:
        raise SystemExit(
            f"error: folder name '{appname}' is longer than 8 chars; "
            "the PicoPad needs an 8.3 id"
        )

    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(appdir, appname + ".pap")

    files = []
    for name in sorted(os.listdir(appdir)):
        path = os.path.join(appdir, name)
        if not os.path.isfile(path):
            continue
        if name.lower().endswith(".pap"):
            continue
        sname = short_name(name)
        if not sname:
            continue
        files.append((sname, open(path, "rb").read()))

    have = {n.upper() for n, _ in files}
    if "APP.INI" not in have:
        raise SystemExit("error: app.ini is missing")
    if "APP.BIN" not in have:
        raise SystemExit("error: app.bin is missing (run build.sh first)")

    header = MAGIC + struct.pack("<HHHH", VERSION, len(files), 0, 0)
    data_off = len(header) + TOC_SIZE * len(files)

    toc = bytearray()
    body = bytearray()
    for name, data in files:
        if len(name.encode("ascii", "strict")) > 31:
            raise SystemExit(f"error: name too long: {name}")
        toc += struct.pack(
            "<32sII", name.encode("ascii"), data_off + len(body), len(data)
        )
        body += data

    with open(out, "wb") as f:
        f.write(header)
        f.write(toc)
        f.write(body)

    print(f"packed {len(files)} files -> {out} ({os.path.getsize(out)} bytes)")


if __name__ == "__main__":
    main()
