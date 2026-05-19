#!/usr/bin/env python3
"""Optional BG3 CHR check: first 16 bytes at a fixed LoROM PC (heuristic).

Read-only — does not modify ROM files. ROMs valid on hardware often fail here
if BG3_tiles is linked elsewhere or tile index 0 is unused; use --pc from your .map.
"""

from __future__ import annotations

from pathlib import Path

# Example LoROM PC for one older Mode1BG3HighPriority linkage; yours may differ.
DEFAULT_BG3_CHR_PC = 0xA9E4
TILE0_BYTES = 16  # one 8×8 tile, 2 bpp SNES


def lorom_offset(bank: int, addr: int) -> int:
    return ((bank & 0x7F) << 15) + ((addr & 0xFFFF) - 0x8000)


def tile0_nonzero_count(rom: bytes, offset: int) -> int:
    chunk = rom[offset : offset + TILE0_BYTES]
    if len(chunk) < TILE0_BYTES:
        return 0
    return sum(1 for b in chunk if b != 0)


def check_bg3_chr_tile0(
    rom: bytes,
    pc: int = DEFAULT_BG3_CHR_PC,
    bank: int = 0,
    min_nonzero: int = 8,
) -> tuple[bool, str]:
    """Return (ok, message). Fails if tile 0 (first 16 bytes) is effectively empty."""
    off = lorom_offset(bank, pc)
    if off + TILE0_BYTES > len(rom):
        return False, (
            f"ROM too small for BG3 CHR @ ${bank:02X}:{pc:04X} (file offset {off:#x})"
        )

    nz = tile0_nonzero_count(rom, off)
    if nz >= min_nonzero:
        return True, (
            f"OK: BG3 CHR tile 0 has {nz}/{TILE0_BYTES} non-zero bytes "
            f"@ ${bank:02X}:{pc:04X}"
        )

    return False, (
        f"Heuristic fail: tile 0 CHR empty ({nz}/{TILE0_BYTES} non-zero) "
        f"@ ${bank:02X}:{pc:04X} (file {off:#x}).\n"
        "  If the game is correct on SNES, this address may not be BG3_tiles — "
        "re-run with --pc from your linker map.\n"
        "  Otherwise check PVSNES output: BG_4COLORS (2 bpp) for mode 1 BG3, "
        "and that BG3_tiles is linked where you expect.\n"
        "  Re-run: python3 tools/check_bg3_chr.py YourRom.sfc --pc <LoROM_PC>"
    )


def load_rom(path: Path) -> bytes:
    return path.read_bytes()
