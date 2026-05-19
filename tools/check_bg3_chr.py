#!/usr/bin/env python3
"""Optional heuristic: BG3 tile 0 CHR at a fixed LoROM address (read-only)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from bg3_chr import DEFAULT_BG3_CHR_PC, check_bg3_chr_tile0, load_rom


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Check BG3 CHR tile 0 (does not modify the ROM)"
    )
    ap.add_argument("rom", type=Path, help="path to .sfc")
    ap.add_argument("--pc", default=f"{DEFAULT_BG3_CHR_PC:04X}", help="LoROM CHR address")
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument(
        "--min-nonzero",
        type=int,
        default=8,
        help="min non-zero bytes in tile 0 (default 8)",
    )
    args = ap.parse_args()

    if not args.rom.is_file():
        print(f"error: not found: {args.rom}", file=sys.stderr)
        return 2

    ok, msg = check_bg3_chr_tile0(
        load_rom(args.rom),
        pc=int(args.pc, 16),
        bank=args.bank,
        min_nonzero=args.min_nonzero,
    )
    print(msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
