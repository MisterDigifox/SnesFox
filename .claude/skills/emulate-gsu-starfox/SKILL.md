---
name: emulate-gsu-starfox
description: How to use StarFox/ (the full, unmodified leaked Star Fox source) as a broad real-world regression target for snesfox's GSU (Super FX) core, and how to cross-reference emulator behavior against its actual assembly. Use whenever asked to work on GSU/Super FX emulation against the full game (as opposed to StarFrog's title-screen-only fixture), or to get Star Fox running further/more correctly in snesfox.
---

# Using StarFox/ as a GSU regression target

## What this is, and how it differs from StarFrog

`StarFox/` (sibling directory, untracked by the outer `snesfox` repo, own nested
git repo) is the leaked Argonaut Software 65816 source for the **complete,
unmodified** *Star Fox* — see `StarFox/CLAUDE.md`/`README.md` for the DOSBox-X
build details. Build with `cd StarFox && ./build.sh` (needs `brew install lhasa
--cask dosbox-x-app`); output is `starfox.sfc` (1,048,576 bytes), already
checked in at `StarFox/starfox.sfc` so you don't need to rebuild unless you're
editing the source. `./snesfox header StarFox/starfox.sfc` confirms LoROM +
`Special Chip: SuperFX` is detected correctly.

This is a **different fixture from `StarFrog/`**, covered by the separate
`emulate-gsu-starfrog` skill — don't conflate them:

- `StarFrog/` is a *renamed, hand-edited* fork, deliberately stripped down so
  the compiled ROM boots straight into `title.asm` and can never reach any
  other screen (levels, menus, AI-strategy banks are all unreachable at
  runtime in that build). It's narrow by design — a fast, deterministic
  smoke test for one boot path.
- `StarFox/` is the **real, full game**, unedited (`git log` there is just two
  commits: "build tools" and "Claude files" — no gameplay-stripping commits
  like StarFrog's). Playing further into it exercises level rendering, the
  AI/enemy "strategy" scripts (`gstrats.asm`, `dstrats.asm`, `pstrats.asm`,
  `ga2strat.asm`, etc.), and the full 3D shape-rendering pipeline — a much
  broader slice of the GSU ISA and of main-CPU/GSU interaction timing than
  StarFrog's single starfield-and-logo loop can ever reach. Use this fixture
  when a bug or gap looks like it needs real gameplay to reproduce, or when
  you've exhausted what StarFrog's narrow path can tell you.

`StarFox/SG_extracted/`+`StarFox/MAPS_extracted/` are git-tracked,
human-readable extractions of the *same* leaked source the ROM is built from
(355 + 130 files) — use them exactly like StarFrog's copies to cross-reference
a disassembled address against real labels/comments: `gstrats.asm`/
`dstrats.asm`/`pstrats.asm`/`ga2strat.asm`/`ga3strat.asm`/`expstrat.asm` for
per-enemy AI, `mmaths.mc`/`mwrot.mc`/`mdrawlis.mc`/`mgdots.mc`/`mobj.mc`/
`msprite.mc` for the shared GSU microcode, `MAPS_extracted/maps/*.asm` for
per-level layout/camera scripts (`1-1.asm`, `2-1.asm`, `3-1.asm`, boss/cutscene
maps, etc.), `MAPS_extracted/msprites/` for level-specific sprite data.

## What exists already (shared with the StarFrog skill — don't re-derive)

`gsu.hpp`/`gsu.cpp`, `BusGsuHost` in `bus.cpp`, the ares reference source under
`ares-ref/`, and all the env-var debug tooling (`SNESFOX_GSU_TRACE`,
`SNESFOX_GSU_IO`, `SNESFOX_CPU_TRACE`/`_ALWAYS`, `SNESFOX_GSU_RAM_WATCH`) are
described in detail in the `emulate-gsu-starfrog` skill — read that first for
the mechanics (note: `SNESFOX_WRAM_WATCH`, a main-WRAM write-watchpoint that
skill used to describe, has since been removed from `bus.cpp` — it ran
unconditionally on every WRAM write; `SNESFOX_GSU_RAM_WATCH` for GSU RAM banks
`$70`/`$71` still exists). This skill only covers what's specific to using
`StarFox/` as the target instead of `StarFrog/`.

## Observed state

**Verified 2026-08-19, visually, after the GSU r15-epilogue pipeline fix (see
`CLAUDE.md`'s GSU "RESOLVED" section)**: `./snesfox snap StarFox/starfox.sfc 1500`
then inspecting `/tmp/snap.ppm` shows the title screen rendering **correctly** —
the "SUPER STAR FOX WEEKEND COMPETITION" logo, the rotating planet, the
starfield dust, and the copyright text all present and in the right place.
Before that fix (i.e. on any snesfox checkout predating it), the GSU
self-corrupted its own r15 within the first few multi-byte instructions of
almost every launch — this is *why* the "not yet verified" caveat below used to
exist: the aggregate stats (`plotCount`, nonzero VRAM counts) looked plausible
even while the actual rendered image was wrong, because a stuck-mid-render GSU
session still plots *something* before failing. Always dump and actually look
at `/tmp/snap.ppm` before trusting plot/launch counts alone as proof of correct
rendering — that lesson is why this section now leads with a screenshot-checked
claim instead of aggregate stats.

Older, still-useful aggregate numbers for reference (pre-verification, same
frame targets, counts may shift slightly with future fixes but the shape
should stay the same):
- Frame 300: still in an early boot/intro phase — `forcedBlank=1` (screen
  off), but the GSU is already launching and plotting, and DMA is actively
  loading VRAM (BG1/BG2 CHR and tilemaps show real nonzero data).
- Frame 1500: `forcedBlank=0` (screen turned on), real framebuffer output,
  audio RMS nonzero (music/SFX playing).

Use this as the starting point for any StarFox-specific investigation instead
of re-establishing it from scratch: if a change regresses these numbers
(fewer plots, forced blank staying on, a hang before frame 300), that's a
real signal something broke, cross-checked against the fact that all the
GSU-core/CPU-timing fixes documented in the StarFrog skill (`Bus::gsuReadRom`
LoROM half-bank formula, `fineCycles()`, DMA/HDMA cycle-stealing, dp,X/dp,Y
16-bit indexing, SFR IRQ read semantics, HTIME=0 edge detection) apply equally
here — they're general CPU/Bus/GSU bugs, not StarFrog-specific band-aids, so
StarFox should benefit from the same fixes without any StarFox-specific
patching.

## Checking VRAM fill state — the key GSU correctness signal

`plotCount`/`launches` only prove the GSU *executed* pixel-plot instructions —
they say nothing about whether those pixels ever landed correctly in VRAM as
real CHR/tilemap data the PPU can render. GSU output reaches VRAM indirectly
(GSU plots into its own RAM buffer/cache, the main CPU then DMAs that buffer
into VRAM once per frame), so a GSU bug, a stale-buffer bug, or a DMA
mis-addressing bug can all look identical from `plotCount` alone but produce
very different VRAM contents. Always check VRAM directly, not just the
aggregate plot/launch counters:

- **`snap`'s per-frame dump already reports this** — it's not something you
  need to add. It prints, for each of BG1/BG2/BG3: tilemap nonzero word count
  (`nonzero=NNN/1024`), CHR nonzero word count (`nonzero=NNN/4096`), a raw
  word sample, and the actual 4bpp tile data at the address a specific
  tilemap entry points to (`tile#N ... 4bpp data at each CHR base`) — plus
  `OBJ CHR@$... nonzero=` for sprites and a fixed `VRAM@$0400` word dump. Use
  the nonzero counts as a fill-progress metric across frame counts the same
  way you'd track `plotCount`: they should climb from 0 toward a stable,
  plausible-looking count as boot progresses (see the frame 300 numbers in
  "Observed state" above — BG1 CHR was already `1049/4096` nonzero by then).
  A count that stays at 0 well past when the GSU should have plotted
  something, or one that's nonzero but frozen at a suspiciously round/uniform
  value (e.g. StarFrog's still-open `02A1`-repeating tilemap bug, noted in the
  `emulate-gsu-starfrog` skill), is the signal to dig further — not the
  framebuffer or `plotCount` alone.
- **`SNESFOX_DMA_VRAM_WATCH=<hex>` or `<hexLo>-<hexHi>` (`bus.cpp`)** traces
  every `$420B` DMA transfer whose destination VMADD falls in that VRAM word
  range, printing the transfer's actual source bytes (`srcBytes[0..7]`,
  read straight from the DMA's source bank:address — useful for confirming
  the GSU's plot buffer/cache genuinely holds the bytes you expect *before*
  they're copied) plus a post-DMA VRAM snapshot (`firstNZ@...`, and fixed
  probe words at `$0000`/`$2000`/`$6800`). Use this when the per-frame
  nonzero counts above tell you *something* landed in a VRAM region but you
  need to see the exact DMA call and source bytes that put it there — e.g.
  narrowing whether wrong VRAM content is a GSU pixel-plot/color bug (source
  bytes already wrong) or a DMA/addressing bug (source bytes right, VMADD or
  copied bytes wrong).
- **The Dear ImGui Tiles Viewer** (`./snesfox emu StarFox/starfox.sfc`,
  bottom panel) decodes the *entire* VRAM into an ARGB tile sheet live —
  the fastest way to visually eyeball "does VRAM actually contain
  recognizable Star Fox tiles/sprites" versus noise, without reading raw hex
  dumps. Hovering a tile shows its index and VRAM address, which you can then
  cross-reference against the `tile#N` lookups in `snap`'s dump or against
  `SG_extracted`'s shape/sprite data.
- Also dump `/tmp/snap.ppm` (written automatically by every `snap` run) to
  check the *composited* result — VRAM can be correctly filled with valid
  tiles/CHR data and the final frame can still render wrong if BG-mode,
  scroll, priority, or window/color-math registers are wrong downstream of
  VRAM. Checking VRAM fill state and checking the final framebuffer are two
  different, complementary checks — a passing one doesn't imply the other.

## Verifying the GSU's rendered tileset actually copies correctly into VRAM

The two checks above (nonzero counts, DMA-watch snapshots) tell you VRAM got
*written to*, but not whether the bytes that arrived are the *same bytes the
GSU actually produced* — a real, distinct failure mode from "VRAM never got
filled." Trace the full path explicitly:

1. GSU plots pixels into `m_pixelCache` (`gsu.cpp`), which `flushPixelCache`
   writes out to the GSU's own work RAM — banks `$70`/`$71`, backed by
   `Bus::m_gsuRam` (`bus.hpp`/`bus.cpp`) — as the actual 4bpp tile-plane bytes
   (this *is* "the tileset," logically: whatever image the GSU rendered this
   frame, whether from real 3D polygon rasterization or from a GSU-driven
   decompression pass unpacking compressed ROM graphics).
2. Later, the main CPU issues a `$420B` DMA that copies that GSU-RAM buffer
   into VRAM as CHR/tilemap data the PPU actually renders from.
3. Because `Bus::read`/`write` route banks `$70`/`$71` the same as any other
   address, `SNESFOX_DMA_VRAM_WATCH`'s existing `[DMA#N]` trace already gives
   you both ends of this copy for free when you find a hit whose `src=$70:...`
   or `src=$71:...` — that bank is the unambiguous signature of a
   GSU-authored-tileset copy (as opposed to a ROM- or CPU-WRAM-sourced DMA,
   which use other banks). Its `srcBytes[0..7]` is read straight out of GSU
   RAM (ground truth: what the GSU actually rendered), and the same trace's
   `post-DMA VWr=...`/probe-word fields show what landed in VRAM afterward —
   compare them byte-for-byte (extend the probe words/length in
   `snesfox_app.cpp`'s trace code if 8 bytes and 3 fixed probe addresses
   aren't enough to cover the region you care about) rather than trusting a
   "post-DMA VWr increased" as proof the copy was correct.
4. Use this to disambiguate *which side* a wrong-VRAM-content bug is on —
   critical since both look identical from `plotCount`/nonzero-count alone:
   - If GSU RAM (`srcBytes`) already holds wrong/repeating/garbage data
     *before* the DMA, the bug is upstream in GSU rendering (a `PLOT`/`RPIX`/
     `COLOR`/`CMODE` or pixel-cache-flush bug in `gsu.cpp`, or the GSU reading
     wrong source data to begin with — see the `ares-ref/component/processor/
     gsu/` instruction semantics to diff against).
   - If GSU RAM holds varied, plausible-looking data but VRAM ends up
     uniform/wrong/misaligned after the DMA, the bug is in the DMA/VRAM-write
     path (`Dma::trigger`/`runChannel` in `dma.cpp`, or `Ppu::write`'s
     `$2118`/`$2119`/VMAIN-increment handling) — not the GSU core at all.
   - The still-open `emulate-gsu-starfrog` finding (StarFrog's BG1 tilemap
     ending up `992/1024` nonzero but suspiciously uniform, repeating `02A1`)
     is exactly the shape of bug this check is for — it was never actually
     traced back to GSU-RAM-side vs. DMA-side using this method; that's the
     concrete next step if you pick that bug back up, on either fixture.
5. `SNESFOX_GSU_RAM_WATCH` (`gsu.cpp`) watches every write inside GSU RAM
   (banks `$70`/`$71`) if a session needs finer visibility than the DMA-trace
   snapshot above (e.g. watching one specific tile-buffer offset across many
   `PLOT`s before the next DMA flush). There is no equivalent for main WRAM
   any more — `SNESFOX_WRAM_WATCH` (`bus.cpp`) was removed since it ran
   unconditionally on every WRAM write; re-add an equivalent there, following
   `SNESFOX_GSU_RAM_WATCH`'s pattern, if a session genuinely needs it.

## Workflow

1. **Build/snap StarFox the same way as StarFrog**:
   `./snesfox snap StarFox/starfox.sfc <N>` for a quick headless stats dump,
   `./snesfox emu StarFox/starfox.sfc` for the interactive Dear ImGui window.
   Because this is the full game, expect to need *larger* frame counts than
   StarFrog to reach any given milestone (intro sequences, attract-mode
   demos, and the actual title screen all play out in sequence before
   gameplay starts) — don't conclude something is stuck until you've traced
   far enough to see the same PC/state repeating, the same way the general
   hang-diagnosis method in the StarFrog skill describes.
2. **Trace and cross-reference exactly as with StarFrog**
   (`SNESFOX_GSU_TRACE`, `SNESFOX_CPU_TRACE_ALWAYS=1`, `SNESFOX_GSU_RAM_WATCH`),
   but resolve addresses against `StarFox/SG_extracted/`+`MAPS_extracted/`
   instead of StarFrog's copies — same files in most cases (this is the
   unedited source those were forked from), but StarFox actually reaches the
   AI-strategy and per-level map files, which StarFrog's build never executes.
   `./snesfox disasm StarFox/starfox.sfc /tmp/out.asm` for auto-labeled
   ground truth when the extracted source's labels don't line up 1:1 with
   what the disassembler sees.
3. **Prefer StarFrog for isolating a specific hang/bug** (smaller, deterministic,
   already has known-fixed-bug writeups to compare against), **then confirm the
   fix against StarFox** to make sure it's a real hardware-accuracy fix and not
   something that happens to unstick StarFrog's one narrow loop. A fix that
   helps StarFrog but regresses StarFox's plot count/frame-300 baseline above
   is a sign the fix was too narrow or wrong.
4. **When StarFox reveals a bug StarFrog's narrow path can't reach** (e.g.
   something only exercised by AI-strategy code, per-level map scripts, or
   deeper 3D shape rendering), treat it as a new, general CPU/GSU/Bus bug the
   same way the StarFrog skill's "Known-fixed bugs" section does: root-cause
   against `ares-ref/`, fix in the general core (never add a StarFox-specific
   or address-specific special case), and re-verify both fixtures plus
   `./snesfox selftest` and `./release.sh`'s round-trip after the change.
