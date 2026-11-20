"""
Merge the launcher binary and the recovery binary into one install image.

The launcher bin starts at 0x10000000 (boot2 + game gap + launcher). The
recovery bin is a standalone image linked for its own partition. Merge them
into a single sparse image so one UF2 can lay down the whole boot chain.

Usage:
  python merge_install_bin.py <launcher.bin> <recovery.bin> <out.bin> \
      [--base 0x10000000] [--recovery-base 0x100FB000]

The output spans 0x10000000 .. recovery_base + recovery_size. bin_to_uf2.py
then skips all 0xFF pages, so the gap costs nothing in the final UF2.
"""
import argparse
import sys
from pathlib import Path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("launcher", type=Path)
    ap.add_argument("recovery", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--base", default="0x10000000")
    ap.add_argument("--recovery-base", default="0x100FB000")
    args = ap.parse_args()

    base = int(args.base, 0)
    rec_base = int(args.recovery_base, 0)

    launcher = args.launcher.read_bytes()
    recovery = args.recovery.read_bytes()

    launcher_end = base + len(launcher)
    recovery_end = rec_base + len(recovery)

    if launcher_end > rec_base:
        sys.exit(
            f"ERROR: launcher ends at {launcher_end:#x}, "
            f"overlaps recovery base {rec_base:#x}"
        )

    img = bytearray(launcher)
    img.extend(b"\xff" * (rec_base - launcher_end))
    img.extend(recovery)

    args.output.write_bytes(bytes(img))
    print(
        f"install image: {len(img)} B "
        f"({base:#x}..{base + len(img):#x}) "
        f"launcher_end={launcher_end:#x} "
        f"recovery={len(recovery)} B at {rec_base:#x}..{recovery_end:#x}"
    )


if __name__ == "__main__":
    main()
