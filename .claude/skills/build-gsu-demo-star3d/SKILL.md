---
name: build-gsu-demo-star3d
description: How Star3D (a minimal, from-scratch, hand-assembled Super FX demo — a 3D wireframe cube) was built, and how to build similar minimal GSU/65816 test ROMs the same way. Use whenever asked to create a new small Super FX demo/test ROM from scratch, or to extend Star3D itself.
---

# Building a minimal GSU demo from scratch (Star3D)

## What this is

`Star3D/` is a minimal, real Super FX demo: a static 3D wireframe cube, rotated
once around the Y axis, **projected and drawn entirely by the GSU** (real
`FMULT`-based rotation math, real `PLOT`-based rasterization — nothing
precomputed on the 65816 side). It is exactly two files:

- `Star3D/build.py` — a small Python script containing a hand-rolled
  "mini-assembler" (helper functions like `A_lda8`/`G_iwt`/`G_plot`, one per
  65816/GSU opcode actually used) that emits raw machine-code bytes directly
  and writes `Star3D/star3d.sfc`.
- `Star3D/star3d.sfc` — the generated 256 KB ROM (regenerate with
  `python3 Star3D/build.py`).

**No external assembler, no DOSBox.** This is deliberately different from
`StarFox/`/`StarFrog/` (which use Argonaut's real `SASMX`/`SL` DOS toolchain
via DOSBox-X, per the `emulate-gsu-starfrog`/`emulate-gsu-starfox` skills) —
for a *new*, tiny, from-scratch ROM, hand-emitting bytes in Python is far less
friction than fighting a DOS toolchain's macro dialect for a handful of
instructions, and it makes the full round-trip (edit → rebuild → run against
`snesfox` → inspect) a few seconds. Use *this* approach when writing a new
small GSU/65816 test ROM from scratch; use the DOSBox/SASMX approach only when
you specifically need to build/modify the real leaked Star Fox source.

## ROM/header layout (LoROM) — pitfalls that will silently break header detection

- **Pad to at least 64 KB, and prefer 256 KB.** `HeaderParser::parse`
  (`header.cpp`) reads `data[offset+i]` with **no bounds checking** at both
  `0x7FC0` (LoROM) and `0xFFC0` (HiROM) to score which mapping wins
  (`HeaderParser::detect`) — a ROM smaller than `0x10000` bytes triggers
  out-of-bounds `vector::operator[]` reads (UB/crash) on the HiROM probe.
  256 KB also makes the `romSize` header byte (`0x08`) honestly correct
  instead of cosmetically wrong.
- **Avoid `fileSize % 0x8000 == 512`** (`Rom::hasCopierHeader` in `rom.cpp`)
  — it'll silently strip the first 512 bytes of your ROM as a "copier header".
  256 KB (`0x40000`) is safe (`% 0x8000 == 0`).
- Header fields live at file offset `0x7FC0-0x7FFF` (bank `$00`'s upper-half
  end): title (21 bytes @ `0x00`), mapMode @ `0x15` (`0x20` = LoROM/SlowROM),
  **romType @ `0x16` must be `0x13` (SuperFX)** — this single byte is what
  `Bus::m_hasSuperFx` (`bus.cpp`) gates the entire GSU wiring on; get it wrong
  and the GSU silently never exists. romSize @ `0x17`, sramSize @ `0x18`,
  country @ `0x19`, license @ `0x1A`, version @ `0x1B`.
- Checksum: zero the checksum/complement fields first, then
  `checksum = sum(all_rom_bytes) & 0xFFFF`, `complement = checksum ^ 0xFFFF`
  (`HeaderParser::validateHeader` just checks `checksum+complement==0xFFFF`).
- LoROM bank `$00`'s CPU-address upper half (`$8000-$FFFF`) = file offset
  `0x0000-0x7FFF`; the emulation-mode reset vector at CPU `$00:FFFC` = file
  offset `0x7FFC`. LoROM bank `$01` (`$8000-$FFFF` again, different bank) =
  file offset `0x8000-0xFFFF` — this is where GSU code goes (see below).

## GSU launch sequence — gotchas found empirically, don't assume from the register map alone

- **`SCMR` needs `RON`(`0x08`)/`RAN`(`0x10`) set**, not just the `MD`
  (color-depth) and `HT` (screen-height) bits — `gsu.cpp`'s `readRom`/
  `readRam`/`writeRam` all early-return (no-op / return 0) if the
  corresponding enable bit is clear. Without `RON`, the GSU can't even fetch
  its own opcodes from ROM. Star3D uses `SCMR=0x19` (`RON|RAN|MD=01(4bpp)`,
  `HT=00`→height 128).
- **Writing GSU `R15`'s high byte (`$301F`) launches the GSU directly** —
  simpler than toggling the `SFR` `GO` bit via `$3030`/`$3031`. Write `$301E`
  (R15 low) then `$301F` (R15 high) last.
- **`ROMBR` is irrelevant to opcode fetch** — only `PBR` (`$3034`) gates which
  bank `readOpcode` fetches from; `ROMBR` only matters for `GETB`-style GSU
  data reads, which a simple plot-only demo never needs. Don't bother setting
  it.
- Poll for completion by reading `$3030` and checking bit 5 (`GO`) — loop
  while set, matching the `STOP` opcode (which clears it) at the end of the
  GSU program.

## GSU RAM screen-buffer layout (writing pixels the CPU can later DMA out)

Reverse-derived from `gsu.cpp::flushPixelCache` (confirmed against real
hardware docs in `[[gsu-hardware-reference-docs]]` /
`.claude/memory/reference_gsu_hardware_docs.md`) — needed any time a GSU
program calls `PLOT`/`COLOR` and something downstream has to find the result:

- Buffer base = `SCBR * 1024` bytes into GSU RAM (banks `$70`/`$71`).
- Tile number is **column-major, not row-major**: for screen-height option
  `HT` (0/1/2 → height 128/160/192), `cn = tile_col*(height/8) + tile_row`
  — *not* `row*cols+col` like a normal tilemap. Getting this backwards makes
  the DMA'd-out image look scrambled/striped, not just "a bit off."
  A CPU-side tilemap that displays this buffer correctly must therefore store
  tile index `cn` at the *normal* row-major tilemap slot for `(row, col)` —
  i.e. the tilemap itself has to bake in this column-major-to-row-major
  translation (see `build.py`'s `tilemap` generation loop).
  MD=4bpp\(bpp=4\)): each tile is `bpp*8=32` bytes at `base + cn*32`, standard
  interleaved-plane byte layout (bytes 0-15 = planes 0/1 by row, bytes 16-31 =
  planes 2/3), row `y&7` selects `(y&7)*2` within each half.
- **`PLOT` unconditionally auto-increments `R1` (x)** — even when you don't
  want it to (e.g. drawing a vertical line where x must stay fixed). It never
  touches `R2` (y). Any loop that isn't a pure horizontal run must explicitly
  compensate (`DEC R1` after each `PLOT` in Star3D's vertical-edge code) or
  lean into it (`INC R2` alongside the free `R1` increment, for a diagonal).
  This exact bug (screen X drifting during vertical edges) is what a first
  build of Star3D hit — confirmed via the pixel range spilling wider than
  predicted, then fixed with the compensating `DEC`.

## Fixed-point math tricks used (both build-time-in-Python, both worth reusing)

1. **The "double the input" trick to cancel `FMULT`'s implicit `/2`.**
   `FMULT`'s real semantics (`gsu.cpp::insnFMULT_LMULT`) are
   `dr = (int16(sr) * int16(R6)) >> 16` — a top-16-bits-of-32-bit-product
   multiply. If `R6` holds a Q1.15 fixed-point coefficient (`round(x*32768)`,
   representing `x` in `[-1,1)`), the result is `sr * x / 2`, **not** `sr * x`
   — the top-16-bits convention divides by `65536`, but Q1.15's scale is only
   `32768`, so there's a stray factor of `1/2`. Fix: pre-double the *other*
   operand (`sr`) before the multiply, e.g. store `2*Vx` instead of `Vx` in
   the register being multiplied — the `2` and the stray `/2` cancel exactly,
   leaving `Vx * x`. Verified exactly (not approximately) in
   `build.py::fmult_top16_exact`, which replicates the C++ arithmetic in
   Python bit-for-bit (Python's `>>` on an int is an arithmetic right shift,
   matching C++'s `int32_t >> 16` for negative values too — confirmed, not
   assumed).
2. **The same-coefficient Y-shear trick to get exact 45° diagonals without
   Bresenham.** Star3D's first working version used single-axis (Y) rotation
   with Z dropped entirely for projection — mathematically correct, but it
   made the cube's 4 depth-only edges collapse onto the same 2 screen rows as
   the other 8 edges (Y-axis rotation never changes Y), rendering as a flat
   rectangle with two internal dividers instead of a recognizable cube (see
   git history / this skill's origin conversation for the picture). Fix:
   add `Yr = Vy - term2` where `term2` is *the exact same* `sin(angle)*Vz`
   value already computed for the X-rotation term (not a new angle/register)
   — reused, not recomputed. For the 8 edges where `Vz` (or `Vx`) is constant
   along the edge, this is just a constant per-edge offset — they stay
   exactly horizontal/vertical. For the 4 depth-only edges (`Vz` varies,
   `Vx`/`Vy` fixed), **both** the existing X-shear term and the new Y-shear
   term vary by the identical amount (same coefficient, same `Vz` delta) —
   so `|dx| == |dy|` *exactly*, by construction, making those 4 edges perfect
   45° diagonals. `build.py` asserts `dx == dy` unconditionally (not a soft
   check) when classifying an edge as diagonal — if the math is ever wrong,
   the build crashes instead of silently emitting a broken ROM. This is a
   general technique: pick projection/shear coefficients so that any
   deliberately-diagonal line has a build-time-*known*, exact integer slope
   (ideally 1:1), and the renderer never needs a division or a general
   line-drawing algorithm.

## Design principle: avoid `LOOP`/branches entirely for small, fixed programs

Real GSU code (`StarFox/SG_extracted/mdrawlis.mc`'s `mdrawhorzline`, etc.)
uses the `move r13,r15 / loop / plot` idiom to repeat a body N times — but
this relies on the GSU's **pipelined, one-instruction-delay-slot branch
semantics** (a genuine, subtle hardware quirk: `gsu.cpp`'s `peekpipe`/`pipe`
prefetch mechanism; see the sneslab.net "developers must insert dummy opcodes
to prevent unintended execution after a branch" note in
`[[gsu-hardware-reference-docs]]`). Getting this exactly right by paper
analysis alone is error-prone.

**Star3D's GSU program contains zero branches and zero `LOOP` instructions.**
Since the cube is fixed (not animated) and the vertex count/edge list is
known at Python build time, `build.py` fully **unrolls** every repeated
operation directly in the emitted byte stream (a Python `for` loop emits N
copies of `G_plot()`, not a GSU-level loop). This trades a slightly larger
ROM (still tiny — a few hundred bytes) for eliminating an entire, subtle class
of hardware-quirk risk. **Prefer this for any small, fixed-shape GSU program**
— reach for real `LOOP`/branch instructions only once the program is large
enough, or animated/data-dependent enough, that unrolling genuinely isn't
practical, and if you do, verify the delay-slot behavior empirically first
with `SNESFOX_GSU_TRACE` on a tiny standalone snippet rather than trusting a
paper trace (this was the original plan for Star3D, abandoned in favor of
unrolling specifically because of this risk).

## Testing/verification method

1. `./snesfox header Star3D/star3d.sfc` — confirm LoROM + `Special Chip:
   SuperFX` detected, header valid.
2. `./snesfox snap Star3D/star3d.sfc <N>` — check `GSU: launches=/stops=/
   plotCount=` (does it match the expected total pixel count computed in
   Python?) and `framebuffer: non-black opaque pix=`.
3. **Actually look at the image — stats alone aren't enough** (this is how
   Star3D's "collapsed rectangle" bug was caught, not from the plot count,
   which looked fine). Convert `/tmp/snap.ppm` to PNG and view it:
   ```python
   from PIL import Image
   Image.open("/tmp/snap.ppm").resize((512, 448), Image.NEAREST).save("/tmp/preview.png")
   ```
   then read `/tmp/preview.png` with the Read tool.
4. **`/tmp/snap.ppm` is a shared, fixed path** — if anything else might be
   running `snap` concurrently (another session, another agent), the file can
   get overwritten between your `snesfox snap` call and your PIL read,
   silently showing you a stale/unrelated frame. Copy it to a private path in
   the *same* shell command as the `snap` invocation:
   `./snesfox snap Star3D/star3d.sfc 5 && cp /tmp/snap.ppm /tmp/my_private.ppm`.
   This exact race condition happened once while iterating on Star3D.
5. Cross-check specific pixel positions (not just aggregate counts) against
   Python-predicted `screenX`/`screenY` per vertex — `build.py` already
   prints these (`for i, (sx, sy) in enumerate(...)`) for exactly this
   purpose.
