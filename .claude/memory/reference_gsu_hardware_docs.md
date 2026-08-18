---
name: gsu-hardware-reference-docs
description: External web sources and hardware facts about the Super FX/GSU chip (registers, timing, opcodes) gathered via web research for the snesfox emulator project.
metadata:
  type: reference
---

`sneslab.net/wiki/Super_FX` (+ per-opcode subpages like `/wiki/PLOT`, `/wiki/CACHE`,
`/wiki/COLOR`, `/wiki/CMODE`, `/wiki/WITH`) is the best public GSU hardware doc found
via web search. `problemkaputt.de/fullsnes.htm` has essentially no GSU coverage (just
notes the `$3000-$32FF` I/O range) — don't re-check it for GSU details.

Key facts extracted (2026-08-18):
- Clock: GSU base 10.74MHz (~4x the S-CPU), pipelining can reach ~21.48MHz effective —
  matches snesfox's CLSR-driven 4x/8x clock scaling in `gsu.cpp`/`bus.cpp`.
- Cycle-cost pattern across the opcode set: 1 cycle when running from cache, 3 from
  ROM, similar-but-sometimes-higher from RAM (e.g. `PLOT`: 1-48 cache / 3-48 ROM / 3-51
  RAM; `COLOR`: 1 cache / 3 ROM+RAM; `CMODE`: 2 cache / 6 ROM+RAM).
- `CACHE` instruction semantics (verbatim from sneslab): "If the cache base register is
  equal to R15 & 0FFF0h, nothing happens. Otherwise, set the cache base register to
  R15 & 0FFF0h and reset all cache flags." — invalidation is keyed to PC-aligned-to-16,
  not an explicit flush call. Cache is 512 bytes / 32×16-byte blocks.
- `R14`: writing it triggers a ROM buffering fetch. `R15`: PC, directly readable/
  writable as a GPR (excluding bank).
- MMIO map ($3000+): R0-R13 general regs, R14 ROM ptr, R15 PC, SFR ($3030 low/$3031
  high — bit7 of $3031=IRQ, bit5 of $3030=GO), BRAMR, PBR, ROMBR, CLSR, SCBR, CFGR,
  SCMR (HT1-0 screen height incl. `11`=OBJ mode, ROM/RAM access-enable bits, MD1-0
  color depth), VCR, RAMBR, CBR — matches snesfox's existing `gsu.hpp`/`gsu.cpp` layout
  1:1, no discrepancy found.
- Pixel pipeline: `COLOR` loads the pending-pixel color register from a source
  register's low byte. `CMODE` sets 5 plot/color flag bits (transparent, dither,
  color-src-high, freeze-high, OBJ mode) from a source register's low 5 bits. `PLOT`
  uses R1=X (auto-incremented), R2=Y, and the current color register; the actual
  pixel-cache flush happens in "the Game Pak RAM controller" per the docs — matches
  snesfox's own pixel-cache-flush-to-GSU-RAM path in `gsu.cpp`.

**Why the docs are thin**: the public wiki pages mostly just cite page numbers of the
official (not publicly scanned/posted) Nintendo "SNES Dev Manual Book II" rather than
reproducing it. Only a loosely-related US patent PDF turned up as an external link
(`https://patentimages.storage.googleapis.com/de/a2/7a/f07754f66f39d9/US5724497.pdf`).

**How to apply**: fine for a quick opcode-level sanity check, but `ares-ref/component/
processor/gsu/` (vendored locally in the snesfox repo — see the `emulate-gsu-starfrog`
and `emulate-gsu-starfox` skills) is more complete and directly actionable for actually
fixing a GSU bug — prefer diffing against `ares-ref` over further web research there.
Web research is better spent filling conceptual gaps than trying to get exhaustive
opcode/cycle tables off the public web, since none of the sites found have them in full.
