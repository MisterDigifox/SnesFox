# Get the GSU (Super FX) core to correctly render Star Frog's title screen

## Scope note

This is explicitly **not** a "100% accurate GSU" effort. Per
`StarFrog/CLAUDE.md`'s own "Stripping the game down to just the title screen"
work, this checkout of Star Frog has already been edited so the compiled ROM
only ever reaches and stays on the title screen (`MAPS_extracted/maps/title.asm`
— a rotating `petecube` logo plus the GSU-driven starfield via `mshowdust` in
`mgdots.mc`, driven every frame from `mdo_3d_display` in `mdrawlis.mc`)
forever; every other screen (levels, menus, AI-strategy banks) is
unreachable at runtime in this build. The goal here is narrowly: get the GSU
core correct enough that `title.asm`'s boot path actually runs and renders
that title screen, not to validate the full instruction set against the
entire game's gameplay/strategy code.

## Context

`snesfox`'s goal is a from-scratch, byte-accurate SNES toolkit. Two prior commits
("feat: Super FX features", "feat: superfx implementation - part 1") added a
~1200-line `gsu.cpp`/`gsu.hpp` core structurally derived from ares's Super FX
implementation (register file, instruction dispatch, ROM/RAM buffer timing,
pixel-plot cache, cache RAM, MMIO at `$3000-$34FF`), and wired it into `bus.cpp`
(GSU work RAM at banks `$70`/`$71`, `stepPeripherals` calling `m_gsu.run()`,
IRQ line merged into the CPU's IRQ check).

`StarFrog/` (this session) is the leaked/renamed Star Fox source, buildable via
its own DOSBox-era toolchain into `StarFrog/starfrog.sfc`. This particular
checkout has already been stripped down (see Scope note above) to boot
straight into `title.asm` and stay there forever, so it's a real, GSU-driven,
but narrowly-scoped end-to-end correctness target: get that one screen
rendering correctly, not a synthetic smoke test but also not the whole game.

**Where things stood at the start** (verified by building and running
`./snesfox snap StarFrog/starfrog.sfc <N>` with `SNESFOX_GSU_TRACE`):
- Header/ROM-type detection, LoROM mapping, and MMIO windowing for the GSU
  already work — `./snesfox header` correctly reports `Special Chip: SuperFX`.
- The main 65816 CPU boots, runs, and correctly writes SFR to launch the GSU
  (`launches=1` in the snap stats).
- The GSU core executes real instructions from the ROM (confirmed via
  `SNESFOX_GSU_TRACE`) but **gets stuck almost immediately** in an
  unconditional 4-instruction loop (`STORE (R1)` → `IBT R6,#imm` →
  `AND R0,R12` → `JMP (R8)`, at `$01:0000-0004`) that never breaks out —
  `plotCount` stays 0 across 130 frames, so no real rendering work ever
  happens. This is a genuine emulation bug (a legitimate GSU program does not
  self-loop on an *unconditional* jump forever), not expected hardware
  behavior.
- `gsu.cpp` also contained a clearly ROM-specific hack:
  ```cpp
  if (m_go && m_launchRombr == 0 && m_lastLaunchR15 == 0xAC1D && m_sessionCycles > 250000) {
      // forces STOP
  ```
  This isn't real hardware behavior and doesn't apply to Star Frog's launch
  address — it's a leftover workaround from whatever fixture an earlier
  session used, masking a still-unfixed hang rather than fixing it.
- There was no GSU-specific test coverage: `tests/` has `cpu_test`, `ppu_test`,
  `sdsp_test` but no `gsu_test`. There was also no reference source under
  `ares-ref/` (documented in the top-level `CLAUDE.md` as an intentionally
  empty scaffold for this — now populated, see below).
- The top-level `CLAUDE.md`'s GSU description was stale: it says "there is no
  GSU execution engine yet" and describes `gsuOpcodesTable` as the only GSU
  artifact — that table is now actually dead code (no references anywhere);
  `gsu.cpp`'s `instruction()` switch is the real, current execution engine.

The plan was scoped to one concrete, testable milestone: **unstick Star
Frog's boot loop so the GSU does real work and correctly renders the title
screen** (`title.asm`'s rotating `petecube` + starfield). Instruction-level
test coverage is still worth adding (it's how to actually verify fixes and
avoid another `0xAC1D`-style band-aid), but it only needs to cover the
opcodes this boot/title path actually exercises — not the full ISA surface
the rest of the game (which this build can never reach) would use.

## Approach

### 1. Pull in the ares GSU reference source for diffing
Populate the already-scaffolded (but empty, untracked) `ares-ref/` with the
real ares Super FX source (`ares/component/processor/gsu/*.cpp` — MIT/BSD-ish
permissive license, appropriate to vendor a reference copy of for porting
work, same spirit as the existing `imgui/` vendoring). This gives a concrete,
line-by-line ground truth to diff `gsu.cpp` against instead of relying on
memory of the ISA — critical since the current core was "ported from ares"
per its own comments but was clearly not verified opcode-by-opcode (the
`0xAC1D` hack is proof a divergence was patched over rather than root-caused).

### 2. Add `tests/gsu_test.cpp` (mirrors `cpu_test.cpp`/`ppu_test.cpp` pattern)
Hand-written assertion-style tests driving `GSU`'s public API directly
(construct a `GSU`, a trivial in-memory `GsuHost` test double, poke registers/
opcodes into fake ROM, single-`step()`, assert register/flag/RAM state) —
no full ROM or `Bus` needed, consistent with the existing test style and
wired into `runGsuSelfTests()` alongside the other `run*SelfTests()` calls in
`selftest`. Cover, in priority order (highest risk / most load-bearing for
Star Frog's boot path first):
- Register-move family: `TO`/`FROM`/`WITH`/`MOVES`, ALT1/ALT2/ALT3 prefix
  interaction (the loop we were debugging pivots entirely on whether `WITH`/
  `ALT` prefix state is tracked correctly across instructions).
- `STORE`/`LOAD`/`SBK`/`IBT`/`LMS`/`SMS`/`IWT`/`LM`/`SM` RAM addressing and
  the RAM read/write buffer timing (`syncRamBuffer`/`readRamBuffer`/
  `writeRamBuffer` — a stale-buffer bug here would exactly explain registers
  silently not updating the way straight-line code expects).
- `JMP`/`LJMP` register-indirect branching and `INC`/`DEC`/`AND`/`OR`/`XOR`/
  `ADD`/`SUB`/`CMP` ALU flag semantics.
- `CACHE`/cache-RAM read/write and invalidation on `CBR` change (relevant
  since Star Frog's boot path executes a `CACHE` instruction early).
- `PLOT`/`RPIX`/`COLOR`/`CMODE` pixel pipeline, `MULT`/`UMULT`/`FMULT`/
  `LMULT`, `MERGE`, `GETB`/`GETC`/`RAMB`/`ROMB`.
- SFR/CFGR/SCMR/SCBR/PBR/ROMBR/RAMBR MMIO register read/write semantics
  (`readRegister`/`writeRegister`), including the GO-bit launch/stop
  transition (`onLaunch`/`onStop`) and IRQ line behavior.

(Not yet done as of the last session — still a good next step for regression
safety, independent of the timing work below.)

### 3. Root-cause and fix the Star Frog boot hang
Using `SNESFOX_GSU_TRACE`/`SNESFOX_GSU_IO` (both already exist as env-var
hooks) plus unit tests as a correctness oracle: trace from GSU launch
through the point registers stop changing, diff instruction-by-instruction
against `ares-ref/`, and fix the actual bug — not by adding another
ROM-specific timeout hack.

### 4. Remove the `0xAC1D` hack
Once the real bug is fixed, delete the `m_lastLaunchR15 == 0xAC1D` special
case in `gsu.cpp::mainStep` entirely. If it turns out to be masking a
*second*, unrelated hang (it references a different launch address than Star
Frog's, so it was clearly for some other test scenario), leave a note in a
follow-up rather than re-adding a magic-number band-aid. (Not yet done — the
actual bug this session found and fixed was a ROM-mapping bug, not the same
class of thing this hack was masking; worth revisiting whether it's still
needed once GSU test coverage exists.)

### 5. Verify end-to-end against Star Frog
Re-run `./snesfox snap StarFrog/starfrog.sfc <N>` (and `SNESFOX_GSU_TRACE`)
after each fix, watching for: the boot loop actually breaking, `plotCount`
becoming nonzero, and eventually non-black framebuffer output for the title
screen (the `petecube` rotating logo, per `StarFrog/CLAUDE.md`'s notes on
what the stripped-down build renders). This is the concrete "did we actually
fix it" signal, replacing guesswork.

### 6. Refresh stale docs
Update the top-level `CLAUDE.md`'s GSU paragraph (currently says "no GSU
execution engine yet" / describes `gsuOpcodesTable` as the GSU artifact) to
reflect that `gsu.cpp` is the real, current GSU core, `gsuOpcodesTable` is
dead code, and that `StarFrog/starfrog.sfc` is now available in-repo as a
real Super FX fixture ROM. (Not yet done.)

## Out of scope for this pass (follow-on work)
- Full Star Frog gameplay accuracy (3D shape rendering fidelity, sound,
  strategy/AI scripts) — this plan only targets getting the GSU core correct
  enough to make real forward progress instead of hanging.
- Cycle-exact GSU timing tuning (the `run()`/`tick()` clock-scaling math)
  beyond what's needed to stop the hang and produce a plausible frame.
- Removing `gsuOpcodesTable`'s now-dead declaration/whatever remnants exist
  in `opcodes.hpp` — small cleanup, can be folded into the doc-refresh step
  if it doesn't risk anything else built against it.

## Progress log

1. **Fixed**: `Bus::gsuReadRom` (`bus.cpp`) was treating GSU ROM addresses as a
   flat `bank*0x10000+addr` array. Pulled ares's real `SuperFX::read()`
   (`ares-ref/sfc/coprocessor/superfx/memory.cpp`, now vendored) and confirmed
   banks `$00-$3F` need the LoROM half-bank formula
   `((address&0x3F0000)>>1)|(address&0x7FFF)` (each 64KB bank mirrors the same
   32KB ROM chunk into both halves) while `$40-$5F` is a direct/linear mirror.
   Fixed in `gsuReadRom`. Result, confirmed via `SNESFOX_GSU_TRACE`: the GSU
   went from an immediate 4-instruction unconditional-JMP hang (reading
   garbage as "code") to real, varied program flow across multiple banks,
   legitimate `STOP`s (`launches=4 stops=4`), and populated real output (BG1
   tilemap 992/1024 nonzero, real CGRAM palette data) — strong evidence Star
   Fox's GSU-driven decompression pass now runs correctly.
2. **Found, then fixed** (see "cycle-accurate timing rework" below): past that
   point the *main 65816 CPU* (not GSU) hung forever in an H/V-latch polling
   loop at `$02:DCBF-DCD2` (`LDA $2137`/`LDX $213C`/`LDA $213C` waiting for
   the H-counter to land in `[0x5A,0x64)`). Traced with a new
   `SNESFOX_CPU_TRACE_ALWAYS=1` env var (added to `cpu.cpp`, gates the
   existing `SNESFOX_CPU_TRACE` trace without requiring
   `bus.gsu().running()`) plus `a=`/`x=`/`sp=` register fields added to the
   same trace line. Root cause: `Bus::stepPeripherals`'s
   `m_hCounter = (m_cycleAccum * H_TOTAL) / kCyclesPerScanline` recomputed
   the H-counter from `m_cycles`, which is accounted in coarse,
   already-rounded "access count" units, not real master clocks. Since this
   loop is fully deterministic (interrupts masked, no jitter), it advanced
   the H-counter by the exact same wrong step every pass — confirmed
   empirically: only 44 distinct H-counter values ever occurred across
   3000+ frames, cleanly skipping `90-106` (window `90-99` needed) forever.

## Progress log, continued — cycle-accurate timing rework

User approved investigating the master-clock timing rework after the above hang was
diagnosed. Result, in order (all verified: `./snesfox selftest` 22/22 pass, `./release.sh`
round-trip still byte-identical, after every step):

1. Added `CPU::fineCycles()` — a second, un-rounded cycle counter alongside the existing
   `cycles()`, kept at "×8" resolution specifically so a single fixed-6-cycle I/O access
   (e.g. `$2137`/`$213C`) doesn't get its fractional difference from 8-cycle SlowROM code
   rounded away every instruction. `Bus::stepPeripherals` now takes a second `totalFineCycles`
   parameter and derives `m_hCounter` from a parallel `m_fineCycleAccum` (scanline/NMI/DMA
   cadence untouched, still keyed off the original `m_cycleAccum`/`kCyclesPerScanline`).
   Extended operand-vs-fetch region splitting to Absolute/DirectPage and their
   X/Y-indexed variants (matching real effective-address formulas exactly).
   Effect: H-counter aliasing set went 44 → 23 unique values (window still missed).
2. **GP-DMA cycle-stealing** (`Dma::trigger`, triggered from `$420B`) — real hardware halts
   the CPU for the full transfer (~8 master clocks/byte + overhead); our DMA previously stole
   zero. `Dma::trigger`/`runChannel` now return bytes transferred; `Bus::write`'s `$420B`
   handler accumulates the master-clock cost into a new `Bus::takeDmaStolenMasterClocks()`,
   drained by `CPU::step()` right after the triggering instruction.
   Effect: 23 → 52 unique values (StarFrog was actively doing several 60K+ byte VRAM DMAs
   right before the wait loop — by far the largest timing gap found).
3. **HDMA cycle-stealing** (`Dma::runHdmaForScanline`/`beginHdmaFrame`) — same reasoning,
   folded directly into `Bus`'s accumulators (this path runs autonomously off the scanline
   transition, not synchronously from a CPU instruction, so it doesn't round-trip through
   `CPU::step()`). Effect: 52 → 54, gap narrowed to 19 values (87-105), tantalizingly close
   but still missing the needed 90-99.
4. Fixed `fineCycles()` not being updated at all in `triggerNmi`/`triggerIrq`/
   `wakeFromWaiSilently`/the WAI-spin and invalid-opcode early-returns in `CPU::step()` (all
   added `m_cycles` but not `m_fineCycles` — a real desync source, though this particular ROM
   turned out not to exercise WAI/NMI during the affected window, so no observed effect here).
5. Pulled ares's actual `sfc/cpu/dma.cpp` (now vendored in `ares-ref/sfc/cpu/`) and found my
   approximations were structurally wrong: GP-DMA has one flat 8-cycle overhead for the whole
   `$420B` write (I only had it per-channel), and HDMA's flat 8 is once per scanline
   (regardless of active-channel count, not per active channel as I had it) plus 8 cycles per
   byte *read* reloading a channel's line-count/indirect-pointer table entry (which I wasn't
   charging at all). Rewrote both to match `dmaRun`/`hdmaSetup`/`hdmaRun` precisely
   (`hdmaReadLineCount` now reports bytes consumed via an out-param).
   **Effect: the H/V-counter wait loop actually resolves** — traced landing on `X=$5C` (92,
   inside the needed [90,99) window) and falling through into real DMA setup and
   ~5,000,000 more cycles of legitimate-looking subroutine calls (this is the core milestone
   this whole timing investigation was chasing).

**Earlier write-up of this bug was a mis-diagnosis — corrected below.** A previous session
pass, re-tracing with an added `sp=` field, walked back the garbage-bank landing to a
legitimate 65816 idiom at `L_1FD313` — StarFrog's per-object "strategy pointer" dispatcher,
which builds a computed long jump by pushing a bank+address pulled from an object's WRAM
fields (`$1246`=bank, `$1244`=address, indexed by the object slot in `X`) and executing `RTL`
to "return" into it. For the object being processed, those fields evaluated to
`bank=$00, addr=$3000` — the GSU's own MMIO window, not code — and that session concluded
this was most likely an *uninitialized object slot*, a StarFrog-content limitation from the
"strip to title screen" edits rather than a general emulator bug. **That conclusion was
wrong.** The next session (see below) proved the object's WRAM fields held the correct value
the whole time (`$0B:$B53A`, set once at spawn and never touched again — confirmed with a new
`SNESFOX_WRAM_WATCH=lo[-hi]` env var added to `bus.cpp` that logs every write to a WRAM
address/range) — the CPU was simply computing the *wrong effective address* when reading them
back.

## Progress log, continued — the two real root causes

Root-caused with the same "trace it, diff against ares/spec, fix, re-verify" method as above,
this time using two new trace fields added to `CPU::step()`'s `SNESFOX_CPU_TRACE` line
(`y=`/`d=`/`dbr=`, alongside the existing `a=`/`x=`/`sp=`) plus the new `SNESFOX_WRAM_WATCH`
Bus-level write-watchpoint mentioned above:

1. **CPU bug: Direct Page,X / Direct Page,Y addressing truncated the index register to 8 bits
   even in 16-bit index mode.** `cpu.cpp`'s dp,X/dp,Y effective-address formulas (opcodes like
   `LDA dp,X`, `STA dp,Y`, `INC dp,X`, etc. — 22 call sites in total, plus the two in the
   cycle-timing pricing helper) all computed `m_d + b1 + static_cast<uint8_t>(m_x & 0x00FF)` —
   masking the index register down to 8 bits *unconditionally*, regardless of the X flag. Real
   65816 behavior (confirmed empirically via `SNESFOX_WRAM_WATCH`, not just spec-reading): the
   *full* current-width register is used, and `applyREP`/`applySEP` already zero-extend
   `m_x`/`m_y` correctly whenever the X flag indicates 8-bit mode — so the extra mask was both
   redundant in 8-bit mode and actively wrong in 16-bit mode, silently discarding the high byte
   of any dp,X/dp,Y address with X/Y ≥ 256. StarFrog's per-object WRAM arrays (`al_size`=0x36
   bytes × 70 objects ⇒ offsets up to ~0xEC4) are exactly this shape: `LDA $16,X` with `X=$0336`
   (a real object's base offset) should read WRAM `$034C` but instead read `$004C` — unrelated,
   effectively garbage data — which is where the "$00:$3000 strategy pointer" illusion came
   from. Fixed by replacing all 22 occurrences of the masked expression with the plain
   `m_d + b1 + m_x` / `m_d + b1 + m_y` (`cpu.cpp`). This is a **general CPU correctness bug**,
   not StarFrog-specific — any ROM using 16-bit-indexed direct-page addressing with an index
   ≥ 256 was affected.

2. **GSU bug: reading SFR's high byte (`$3031`) masked out the IRQ bit before returning it to
   the CPU, and reading the low byte (`$3030`) incorrectly cleared the IRQ flag too.**
   `gsu.cpp::readRegister`'s `$3031` case did `(sfrRead() >> 8) & ~0x80u` — clearing bit 7 of
   the *returned* high byte (SFR bit 15, the IRQ flag) even though the *internal* `m_irq` flag
   was correctly cleared as the read's side effect, so real hardware's "read once to see it,
   the read itself acknowledges it" contract was broken: the CPU could never actually observe
   the IRQ bit set. Cross-checked against `ares-ref/sfc/coprocessor/superfx/io.cpp`'s
   `readIO` (`case 0x3031: { n8 r = regs.sfr >> 8; regs.sfr.irq = 0; cpu.irq(0); return r; }`)
   — ares returns the true, unmasked byte and clears the flag as a *separate* step. Also fixed
   `$3030` (low-byte read) to no longer clear `m_irq` at all, matching ares (only the high-byte
   read acknowledges the IRQ). This alone didn't explain StarFrog's specific hang (see next
   item) but is a real, independently-verified divergence from hardware worth having fixed.

3. **Bus bug: H-IRQ target of `$4207`/`$4208`=0 (HTIME=0) could never fire.** `Bus::
   stepPeripherals`'s H-edge detector was `prevHIn < m_htime && m_hCounter >= m_htime` — a
   "crossing" check that is structurally unsatisfiable when `m_htime == 0`, since `prevHIn` is
   unsigned and can never be less than 0. StarFrog configures `$4200`=H+V IRQ mode with
   `HTIME=0, VTIME=0` (a firmly real, common hardware pattern — effectively "IRQ once per frame
   at the very start of scanline 0") once near boot and never touches it again; because HTIME=0
   could never edge-trigger, this IRQ **never fired even once** in the entire run (confirmed:
   zero occurrences of the copied-to-WRAM IRQ handler entry point across a full 600-frame,
   2,000,000-instruction `SNESFOX_CPU_TRACE_ALWAYS` capture). StarFrog's main loop
   (`$02:DA35: LDA $18BD / BEQ $DA35`) waits on a flag only the real IRQ handler
   (`irqcode_l`, gated by `SG_extracted/nmi.asm`'s `.irq` — itself gated on GSU's SFR IRQ bit,
   bug #2 above) ever sets, so the game wedged there forever once it reached that point. Fixed
   by tracking whether a scanline boundary was crossed during a given `stepPeripherals` call
   (`scanlineBoundaryCrossed`) and using that as the H-edge condition specifically when
   `m_htime == 0` (the counter "reaching 0" for this target means "wrapped into a new
   scanline", not "rose through 0" — a different check than the general case).

**Result**, verified via `./snesfox snap StarFrog/starfrog.sfc <N>` before/after: the object
dispatcher no longer derails into GSU MMIO space, the `$18BD` wait loop resolves, and the game
progresses dramatically further — `forcedBlank` flips to 0 (screen turned on) around frame
~900, `GSU plotCount` goes from stuck-at-0 to actively incrementing every frame (23068 by
frame 1200), the framebuffer shows real non-black pixel data (~20K opaque px, ~15 unique
colors) with plausible-looking sprite tiles (OBJ CHR 930/1024 nonzero words, arranged as a row
of tiles at y≈182-183 — very plausibly the "STAR FROG" title text), and APU output has real
non-zero RMS (music/SFX playing, not silence). **However, the rendered image itself is still
wrong** — dumping `/tmp/snap.ppm` (written automatically by `snap`) shows a stable but
incoherent field of colored noise filling roughly the top-left 2/3 of the frame plus one
clean blue rectangle sprite, not a recognizable `petecube`/title layout — confirmed stable
(byte-identical) between frame 1200 and frame 2000, so it's not still converging, just
genuinely wrong content. **Next milestone**: BG1's tilemap (`nonzero=992/1024`, suspiciously
uniform `02A1` filler) and/or GSU's pixel-plot → VRAM path are the most likely next suspects —
this needs the same trace-and-diff treatment as the bugs above, focused on what GSU is
actually plotting into VRAM vs. what the BG1/BG2 tilemaps + CHR data downstream of that
describe.

## New debug tooling added this session
- `SNESFOX_WRAM_WATCH=<hex>` or `SNESFOX_WRAM_WATCH=<hexLo>-<hexHi>` (`bus.cpp`): logs every
  write that lands in WRAM (any bank/mirror combination that resolves to the same physical
  byte) within that address or range to stderr as `[WRAM-WATCH #<seq>] bank=XX addr=XXXX <=
  XX`. Use this instead of grepping `SNESFOX_CPU_TRACE` for `STA` instructions by hand when you
  need to know the *actual stored value* at a WRAM location over time, independent of which
  addressing mode/bank a given instruction used to reach it (mirrors, DBR-relative absolute,
  and direct-page all funnel through the same watch).
- `SNESFOX_CPU_TRACE`'s line format gained `y=`, `d=`, and `dbr=` fields (alongside the
  pre-existing `a=`/`x=`/`sp=`) — needed to diagnose bug #1 above (couldn't tell whether the
  CPU was really using DP=0 without a `d=` field, or reconstruct which physical WRAM object a
  given `X` value pointed at without `y=`/`dbr=` to cross-reference against absolute,Y writes
  in other routines).

## Verification
- `./build.sh` succeeds.
- `./snesfox selftest` passes (22/22).
- `./check.sh` still passes (unaffected, but cheap to confirm nothing broke).
- `./release.sh`'s `cmp hello_world.sfc out.sfc` round-trip stays byte-identical.
- `./snesfox snap StarFrog/starfrog.sfc <N>` — both the object-dispatcher derail and the
  `$18BD` wait loop are confirmed fixed; current milestone reached is real (if visually
  incorrect) rendered output — see "Result" above for exact frame numbers/metrics.

## Progress log, continued — root-causing the "continuous refresh" corruption (StarFrog *and* StarFox)

Follow-up session, prompted by the user's hunch that StarFrog/StarFox render worse than
`Star3D/` (the from-scratch, single-GSU-launch demo, see `build-gsu-demo-star3d` skill)
specifically *because* they relaunch the GSU every frame ("rafraîchissement continu") while
Star3D only launches once.

**Confirmed the hunch, and found the mechanism.** `gsu.cpp`'s leftover `SNESFOX_GSU_NO_AC1D_HACK`
band-aid (see "Known-fixed bugs" above — still present, `mainStep`'s `m_lastLaunchR15 == 0xAC1D`
check) force-`STOP`s the GSU once a session launched at `$01:AC1D` (`mdo_3d_display`, the shared
per-frame 3D-scene entry point in `mdrawlis.mc` — used by *both* StarFrog's title-screen cube and
StarFox's full game) exceeds 250,000 GSU cycles. Because this routine relaunches every single
frame, the hack's effect compounds differently depending on what that routine is drawing:

- **StarFrog**: the routine's real content (the rotating petecube) gets chopped mid-draw *every
  frame*, forever — this is the direct cause of the stable, converged colored-noise framebuffer
  documented above (confirmed via `SNESFOX_GSU_NO_AC1D_HACK=1` A/B test: the noise disappears
  when the hack is removed, `/tmp/snap.ppm` goes to a mostly-black frame instead).
- **StarFox**: as of this session, `$01:AC1D` only drives the title screen's decorative
  starfield dust — the "SUPER STAR FOX WEEKEND COMPETITION" logo itself is ordinary sprite/BG
  art, unaffected by the truncation. Verified visually (`/tmp/snap.ppm` at frame 1500 *and*
  4000, `SNESFOX_GSU_NO_AC1D_HACK` unset): the title screen renders **correctly and stably**.
  This corrects the earlier `emulate-gsu-starfox` skill note that this was "not yet visually
  verified" — it now has been, and it's good, *specifically because of* (not despite) the hack.

**The hack is not the root cause — it's masking a real, still-unfixed infinite loop.** Removing
it (`SNESFOX_GSU_NO_AC1D_HACK=1`) does not fix rendering; it trades symptoms:
- StarFrog: noise disappears, but BG1 CHR drops from 3918/4096 to 56/4096 nonzero words — the
  session now runs far longer than a frame's budget and never finishes, so the CPU DMAs out a
  barely-started buffer instead.
- StarFox: the title screen never appears at all (stays black) — confirmed via extended runs
  (`SNESFOX_GSU_NO_AC1D_HACK=1 ./snesfox snap StarFox/starfox.sfc 6000`) that a *single* GSU
  launch (`launches` stays at 28 from frame 800 through frame 6000) keeps accumulating plots at
  a steady ~1100/frame rate — `plotCount` 1.1M at frame 1500 → 6M at frame 6000, still climbing
  linearly with no sign of convergence. This rules out "just slow, real hardware genuinely takes
  many frames" — 6M+ plots into a 256×224 framebuffer is not a legitimate single 3D-scene draw.

**Localized (not yet fully root-caused) via `SNESFOX_GSU_JMP_TRACE=1`** (new env var added this
session to `insnJMP_LJMP`, same pattern as the existing GSU trace tools — prints every `JMP`/
`LJMP Rn` with its resolved target and `R11`). The stuck StarFox session (`SNESFOX_GSU_NO_AC1D_HACK=1
./snesfox snap StarFox/starfox.sfc 500`, ~10K JMP events for 500 frames) settles into an exact,
indefinitely-repeating 4-step cycle of `JMP R11` calls (two call sites, `$01:B06A` and `$01:B08D`,
both physically inside `mdrawc.mc`'s polygon/line-fill code — see the `PLOT`/`LOOP` addresses
`$01:B072`/`$01:B073` right next door, confirmed via raw ROM byte dump + the LoROM bank-1
`file_offset == address` shortcut, *not* via the mainStep trace fields, which are one instruction
**behind** the PC they're printed against — `peekpipe()`'s "return the previously-fetched byte,
prefetch the current one" pipeline means a naive `(pcBefore, opcode)` trace pairing is off by one;
don't repeat that mistake, always spot-check against a raw `xxd` byte dump before trusting a
manual disassembly from trace output):

```
[JMP] from=$01:B08E n=R11 target=AF96 ...
[JMP] from=$01:B06B n=R11 target=AFB3 ...
[JMP] from=$01:B08E n=R11 target=AFCC ...
[JMP] from=$01:B06B n=R11 target=AFE8 ...
```
(repeats exactly — sample at #100001 matches sample at #360001, 260,000 apart)

Each individual inner `LOOP`/`PLOT` pass is legitimate and small (`r12` sampled in the low tens,
occasionally low hundreds — checked via a temporary instrumented build, not a bug in `LOOP`
itself or in the delay-slot/pipeline semantics, both cross-checked byte-for-byte against
`ares-ref`'s `SuperFX::main`/`peekpipe`/`pipe` and found to match exactly). The bug is one level
up: two `JMP R11` return sites are being re-entered with 4 different, *never-progressing* `R11`
return addresses forever, meaning whatever outer object/vertex-list traversal is supposed to
eventually exhaust (most likely `mdrawlis.mc`'s `mshow`/`mdrawloop` NULL-terminated draw-list walk
at `.nextobj` — `ldw [rlistptr]` / `mlbne mdrawloop`, see that file's top comment for the struct
layout — or an analogous per-vertex loop inside whatever calls into `mdrawc.mc`'s `msc_poly`/
`mline`) never does. **Not yet found**: what sets `R11` to `AF96`/`AFB3`/`AFCC`/`AFE8` on each
pass, and why that source never produces a 5th value. The concrete next step is tracing backward
from those two `JMP R11` sites to whatever `LINK`/`TO R11`/`FROM_MOVES R11` last wrote it, and
diffing that against `ares-ref` the same way the earlier bugs were found.

**One real, confirmed-independent bug found and fixed along the way** (kept regardless of the
above): `GSU::tick()` was decrementing the ROM and RAM pending-buffer countdown timers
(`m_romcl`/`m_ramcl`) **sequentially off a shared, shrinking `clocks` budget** (RAM's countdown
only got whatever ticks were left over after ROM's own countdown consumed its share this call).
Diffed against `ares-ref/sfc/coprocessor/superfx/timing.cpp`'s `SuperFX::step()`: real hardware
runs these two timers **independently**, each counting down the *full* `clocks` amount every
call. Fixed by removing the `clocks -= step` lines so both branches always see the full budget.
Verified: `./snesfox selftest` (22/22) and `./release.sh`'s round-trip both still pass; StarFrog/
StarFox launch/plot counts are byte-for-byte identical before/after with the `AC1D` hack in its
default (enabled) state, so this is a real correctness fix with no observed behavior change on
the current known-good paths (StarFox's title screen, StarFrog's noise) — it just wasn't the fix
for *this* particular hang. Left in place; do not revert it while chasing the `JMP R11` bug above.

**Net effect of this session**: the `AC1D` hack currently produces the *better* outcome for both
fixtures than removing it does (StarFox title screen: correct; StarFrog: noise but at least
`running`, not fully blank) — so it was deliberately left in place rather than removed, pending
the real fix. Do not delete it until the `JMP R11` cycle above is root-caused; removing it now
would regress StarFox's currently-correct title screen to a permanently blank one.

New debug tool: `SNESFOX_GSU_JMP_TRACE=1` (`gsu.cpp::insnJMP_LJMP`) — prints every `JMP`/`LJMP Rn`
as `[JMP] from=$PBR:R15 n=Rn target=XXXX alt1=0/1 r11=XXXX` (note the `from=` address has the same
one-instruction pipeline lag as everything else driven off `mainStep`'s `pcBefore` — see above).
(This trace was later removed again once it had served its purpose — see below — but the technique
and the pipeline-lag caveat are worth keeping for next time.)

## Progress log, continued — the real root cause, the hack removed, and a second bug uncovered

Follow-up session (same day), user: "corrige le bug et vire le hack" — fix it for real, not just
narrow it down further.

### Root cause #1 (fixed): `m_r15Modified` isn't set by every instruction that can write r15

Traced backward from the repeating `JMP R11` cycle above using a **static GSU disassembler**
written this session (`/tmp/.../scratchpad/gsu_disasm.py` — not checked in, rebuild if needed;
tracks `ALT1`/`ALT2`/`ALT3` prefix state instruction-by-instruction, which the earlier
free-hand byte reading did not, and which matters a lot: opcodes `0x30-0x3B`/`0x40-0x4B`/
`0xA0-0xAF`/`0xF0-0xFF` each mean 2-3 *different* things depending on the active prefix). Static
disassembly sidesteps the pipeline-lag trap entirely (no runtime PC/opcode pairing to get wrong)
and is the more reliable tool for this kind of control-flow archaeology — reach for it before a
live trace next time.

Ground truth from the disassembly: `mboostmeter` (`mdrawlis.mc`) does the standard
`mpush r11 / mcall mdrawbox / mcall mdrawsolidbox / mpop pc` pattern. `mpop pc` compiles to
`dec rsp / dec rsp / to r15 / ldw (rsp)` — a register-indirect **word load into r15**, GSU's only
form of "return from stack". This goes through `insnLoad()`, which — unlike `insnIWT_LM_SM`
(which has an explicit `if (n == 15) m_r15Modified = true;`) — never sets `m_r15Modified`. Result:
`mainStep`'s post-instruction check (`if (m_r15Modified) {...} else { ++m_r[15]; }`) didn't know
r15 had just been restored from the stack, and **incremented the freshly-popped return address by
one**, silently corrupting *every* GSU subroutine return that goes through `mpop pc` (or any other
non-IWT/JMP/branch instruction that happens to target r15 via `TO`/`WITH` prefix chaining — there
are ~20 raw `dr() = ...` call sites in `gsu.cpp`, matching the codebase's existing "22 call sites"
precedent from the earlier dp,X/dp,Y bug, and auditing all of them individually would be exactly
that same error-prone pattern again).

Root cause, confirmed against `ares-ref/component/processor/gsu/registers.hpp`: ares's `Register`
type is a wrapper whose `operator=`/`assign()` sets `modified = true` on **every** write,
automatically, regardless of which instruction did it. Our port uses a raw `std::array<uint16_t,16>`
with manual `m_r15Modified = true` calls at only a few sites (`setR15`/`addR15`/IWT-LM-SM) — a
structural gap versus ares's design, not a typo at one call site.

**Fix** (`gsu.cpp::mainStep`, ~15 lines, no call-site auditing needed): instead of trusting
`m_r15Modified` alone, compare `m_r[15]` against `pcBefore` (already captured earlier in the same
function for the debug-log/trace code) — exactly the same belt-and-suspenders pattern already used
two lines above for r14 (`if (m_r14Modified || m_r[14] != r14Before)`). This catches *any*
instruction that changes r15, regardless of whether it remembered to set the flag, with a single
central fix instead of patching 20 call sites.

**Verified impact**: before this fix, `SNESFOX_GSU_NO_AC1D_HACK=1 ./snesfox snap
StarFox/starfox.sfc 6000` never got past `launches=28` — a single GSU session ran forever,
`plotCount` climbing linearly and unboundedly (1.1M at frame 1500 → 6M at frame 6000, no sign of
convergence — ruled out "just legitimately slow," real hardware doesn't need millions of `PLOT`s
for one title-screen frame). After the fix, the *same* command reaches `launches=78` by frame
3000 with each session completing in ~1000 GSU cycles (not millions) — the pathological runaway
session is gone. `./snesfox selftest` (22/22) and `./release.sh`'s round-trip both still pass.

### The hack: removed

With root cause #1 fixed, the `SNESFOX_GSU_NO_AC1D_HACK`-guarded force-`STOP` block in
`gsu.cpp::mainStep` (the `m_lastLaunchR15 == 0xAC1D && m_sessionCycles > 250000` check) was
**deleted outright** (not left behind an env var) — confirmed via the same StarFrog/StarFox A/B
testing as before that leaving it in now actively *hurts* rather than helps: with root cause #1
fixed, overall program timing/flow changed enough that the hack's tuned 250,000-cycle threshold
now fires at different, wrong moments and visibly regresses StarFox's rendering (dropped from
`launches=758 plotCount=705064` down to `launches=688 plotCount=232`, screen showing scrambled
vertical stripes instead of the title). Removing the hack restores the pre-regression numbers'
*shape* (many launches, small per-session cycle counts) even though a second bug (below) still
blocks a fully-rendered screen.

### Root cause #2 (found, not yet fixed): draw-list head pointer gets clobbered mid-boot

Both fixtures now hang again, but differently and much later than before: `launches` climbs
normally for dozens of `$01:AC1D` (`mdo_3d_display`) cycles (each completing in ~1000 GSU cycles,
proof root cause #1's fix is real) and then gets stuck on one specific `AC1D` launch that never
completes — StarFox stuck at `launches=78` from frame ~3000 through at least frame 20000 (confirmed
truly stuck, not just slow, via a real-time background run), StarFrog similarly stuck around
`launches=58`. PC settles inside `mshow`'s `mdrawloop` (`mdrawlis.mc` — the same NULL-terminated
object draw-list walk speculated about in the *previous* session's write-up, now confirmed for
real this time, not a red herring).

New tool this session, **kept** (mirrors `SNESFOX_WRAM_WATCH`'s exact syntax/pattern but for GSU
RAM banks `$70`/`$71` instead of WRAM — closing the gap the `emulate-gsu-starfox` skill's "no
dedicated GSU RAM watch tool yet" section called out): `SNESFOX_GSU_RAM_WATCH=<hex>` or
`<hexLo>-<hexHi>` (`gsu.cpp::writeRam`/`debugGsuRamWatch`) — logs every write landing in that GSU
RAM range as `[GSU-RAM-WATCH #<seq>] pc=$PBR:R15 addr=XXXX <= XX r1=.. r10=.. r12=..` (same
pipeline-lag caveat on `pc=` as everything else).

Using it on `m_dlptr` (`mallrotzsort`'s list-head variable, confirmed at GSU RAM `$021E` by
matching `mallrotzsort`'s compiled entry — found by launch-address cross-reference, `$01:B17F` —
against its disassembled opening bytes: `sub r0/[alt3]romb/sm($021E),r0/iwt r9,#$0EF2/lms
r12,[$01B6]/.../bne .noret/stop`, an exact byte-for-byte match to `mdrawlis.mc`'s source): the
pointer is written correctly (`0xF2F9`, a plausible in-buffer address) by `mallrotzsort` on at
least 3 successful passes, then on the pass right before the hang, something at `$01:B0C6`
(inside a `stw (r1)` fill-loop template that also appears — with different immediate operands —
at `$01:B0AB` and `$01:B0D2`, all three sharing the shape `cache/with r15/to r13/stw
(r1)/inc r1/loop/inc r1/[stop|jmp r11]`) overwrites it with `0x001E` — a bogus, clearly-uninitialized-
looking address nowhere near `m_drawlist`'s real base (`$0EF2`). Confirmed downstream: the very
next `mshow` invocation genuinely loads `rlistptr=$001E` from the clobbered pointer and repeatedly
re-reads "object" fields starting there forever (watched directly via `SNESFOX_GSU_RAM_WATCH=0000-0300`
— the same four `sms` writes at `$0020`/`$0022`/`$0024`/`$0026`, `r1` cycling `001E→0020→0022→
0024→0026→(back to 001E)`, repeating identically pass after pass) — i.e., this one clobbered
pointer is fully sufficient to explain the hang, no need to look further downstream.

**Not yet found**: why `$01:B0C6`'s `stw (r1)` fires with `r1=$021E` and `r12=$BC43` — neither
matches any of the three fill-loop template's own hardcoded setup values (`r1` seeded to `$2C00`
or `$5600`, `r12` seeded to `$1500`/`$1800`), and `r12=$BC43` is consistent with the loop having
run *far* past its own stated bound (rough arithmetic: reaching `r1=$021E` from a `$5600` start
needs on the order of 22,000 word-store iterations, i.e., several full 16-bit wraps of a counter
that was only supposed to run ~5,376 times) — suggesting either the loop's own `LOOP`-based
termination is somehow not firing on this specific pass (despite `insnLOOP` matching
`ares-ref/component/processor/gsu/instructions.cpp::instructionLOOP` exactly — re-checked this
session, not the bug) or — more likely, not yet confirmed — this isn't actually a live wraparound
of a real running loop at all, but execution landing *mid-block*, several bytes into one of these
three near-identical fill-loop templates, via some *other* still-buggy jump/return upstream,
inheriting whatever `r1`/`r12` happened to hold from unrelated prior code. The concrete next step:
use `SNESFOX_GSU_RAM_WATCH` on a *wider* window (or extend it to also print `r11`/`r13`) around
the write immediately preceding the first `$021E` corruption, and/or add a temporary
`SNESFOX_GSU_JMP_TRACE`-style instrumentation to `insnJMP_LJMP`/`insnBranch`/`LOOP` specifically
gated to fire only from frame ~2900 onward (the corruption happens once, late, not from the first
launch — an un-gated trace drowns in ~2 million irrelevant earlier instructions, as it did this
session) to catch whichever branch/jump/loop actually misfires into this region.

### Verification (this session)
- `./build.sh` succeeds, `./snesfox selftest` 22/22, `./release.sh` round-trip byte-identical —
  reconfirmed after root cause #1's fix, after the hack removal, and after adding
  `SNESFOX_GSU_RAM_WATCH`.
- `./snesfox snap StarFox/starfox.sfc <N>` / `StarFrog/starfrog.sfc <N>`: both fixtures no longer
  exhibit the original unbounded-single-session runaway (root cause #1, fixed); both still hang
  later, on `mdo_3d_display`/`mshow`'s draw-list walk, due to root cause #2 (not yet fixed) — see
  above for the exact trace commands to reproduce and continue from.

## Progress log, continued — mid-session pause, isolated LOOP test in progress (pick up here)

User paused mid-investigation (asked to resume "tomorrow"). State: **nothing further has changed
in the repo since root cause #2's write-up above** — `gsu.cpp` currently has root cause #1's fix,
the hack removal, and `SNESFOX_GSU_RAM_WATCH`, and nothing else. The following is an in-progress,
not-yet-conclusive side investigation into root cause #2, done entirely in a standalone scratchpad
program (not checked in, and the scratchpad directory is session-scoped so it will be gone — recreate
from the listing below, takes seconds).

**Why this test exists**: root cause #2's write-up above ends on the hypothesis that a `LOOP`-based
fill loop (`$01:B0BB` in `StarFox/starfox.sfc`, one of three near-identical
`cache/with r15/to r13/stw (r1)/inc r1/loop/inc r1/[stop|jmp r11]` templates) is somehow running for
~22,000 iterations instead of its own `r12`-bounded ~5,376, wrapping `r1` all the way around from
`$5600` back down to `$021E` (`m_dlptr`) and clobbering it. Before chasing that further inside the
full ROM (slow, confounded by everything else running), the plan was to isolate `LOOP` + the
`move r13,pc` idiom (`with r15`/`to r13`) in a minimal standalone program against `GSU`'s public API
directly — exactly the kind of test `tests/gsu_test.cpp` was always meant to be (still doesn't exist;
this could become its first case once it's actually passing).

**The test** (recreate at e.g. `<scratchpad>/loop_test.cpp`, compile with
`clang++ -std=c++20 -I<repo-root> -o loop_test loop_test.cpp gsu.cpp`, run with no args):

```cpp
#include "gsu.hpp"
#include <cstdio>
#include <vector>
#include <cstring>

class TestHost final : public GsuHost {
public:
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram70;

    TestHost() : rom(0x10000, 0), ram70(0x10000, 0) {}

    uint8_t read(uint8_t bank, uint16_t addr) override {
        if (bank == 0x70) return ram70[addr];
        return 0;
    }
    void write(uint8_t bank, uint16_t addr, uint8_t value) override {
        if (bank == 0x70) ram70[addr] = value;
    }
    uint8_t readRom(uint32_t address24) override {
        uint16_t addr = address24 & 0xFFFF;
        return rom[addr];
    }
};

int main() {
    TestHost host;
    GSU gsu;
    gsu.reset();

    uint16_t base = 0x8000;
    // Mirror the real b0c2-b0c9 shape:
    //   cache            @base+0
    //   with r15         @base+1
    //   to r13           @base+2   -> r13 = base+3 (address right after "to r13")
    //   inc r1           @base+3   <- loop body starts here (r13 target)
    //   loop             @base+4
    //   inc r1           @base+5   <- delay slot, always executes
    //   stop             @base+6
    int i = 0;
    host.rom[base + i++] = 0x02; // cache
    host.rom[base + i++] = 0x2F; // with r15
    host.rom[base + i++] = 0x1D; // to r13 (n=13 -> opcode 0x10+13=0x1D)
    host.rom[base + i++] = 0xD1; // inc r1 (n=1 -> opcode 0xD0+1=0xD1)  <- loop target
    host.rom[base + i++] = 0x3C; // loop
    host.rom[base + i++] = 0xD1; // inc r1 (delay slot)
    host.rom[base + i++] = 0x00; // stop

    gsu.writeRegister(host, 0x303B, 0x19); // SCMR: RON|RAN|MD=1
    gsu.writeRegister(host, 0x3034, 0x01); // PBR = 1
    gsu.writeRegister(host, 0x3000 + 12*2, 3);      // R12 lo = 3 (loop counter)
    gsu.writeRegister(host, 0x3000 + 12*2 + 1, 0);  // R12 hi
    gsu.writeRegister(host, 0x3000 + 1*2, 0);       // R1 = 0
    gsu.writeRegister(host, 0x3000 + 1*2 + 1, 0);
    gsu.writeRegister(host, 0x3000 + 15*2, static_cast<uint8_t>(base & 0xFF));   // R15 lo
    gsu.writeRegister(host, 0x3000 + 15*2 + 1, static_cast<uint8_t>(base >> 8)); // R15 hi -> launches

    printf("after setup: running=%d pc=%04x sfr=%04x pbr=%02x\n", gsu.running(), gsu.pc(), gsu.sfr(), gsu.pbr());

    int steps = 0;
    while (gsu.running() && steps < 100) {
        printf("step %2d: pc=%04x r1=%04x r12=%u r13=%04x sfr=%04x\n", steps, gsu.pc(), gsu.reg(1), gsu.reg(12), gsu.reg(13), gsu.sfr());
        gsu.step(host);
        printf("  -> after: running=%d pc=%04x sfr=%04x\n", gsu.running(), gsu.pc(), gsu.sfr()); // added but never run — see below
        ++steps;
    }
    printf("final: running=%d r1=%04x r12=%u pc=%04x steps=%d\n", gsu.running(), gsu.reg(1), gsu.reg(12), gsu.pc(), steps);
    if (steps > 20) { printf("FAIL: loop did not terminate within expected steps\n"); return 1; }
    if (gsu.reg(12) != 0) { printf("FAIL: r12 did not reach 0 (stuck at %u)\n", gsu.reg(12)); return 1; }
    printf("PASS\n");
    return 0;
}
```

**Last actual run** (the version *without* the `"  -> after: ..."` line — that line was added but the
rebuild+rerun never happened before the pause; do that first, it's the very next step):

```
after setup: running=1 pc=8000 sfr=0020 pbr=01
step  0: pc=8000 r1=0000 r12=3 r13=0000 sfr=0020
step  1: pc=8001 r1=0000 r12=3 r13=0000 sfr=0020
final: running=0 r1=0000 r12=3 pc=8002 steps=2
FAIL: r12 did not reach 0 (stuck at 3)
```

**What's suspicious about this** (not yet resolved): `sfr=0020` at the end means only `SFR.GO`'s bit
is showing in a *stale* read — but `running()`/`m_go` is already `0`, and `insnSTOP` unconditionally
sets `SFR.IRQ` (bit 15, `0x8000`) before clearing `GO`, so if `STOP` (opcode `0x00`) had genuinely
executed, `sfr` should show `0x8020` or similar, not `0x0020`. That means **the GSU stopped running
without ever executing the `stop` opcode at `base+6`** — something else is clearing `m_go` (grep
confirms only two sites: `reset()` and `insnSTOP`), which is a contradiction worth chasing first,
independent of the original `LOOP`-timeout question. Also suspicious: `r13` never leaves `0000`
across both printed steps, meaning `to r13` (expected to fire during step 1, per the pipeline-lag
accounting below) hasn't visibly taken effect yet at the point of that print — but the print happens
*before* `step()` is called for that iteration, so this alone isn't necessarily wrong, just worth
re-checking once the "after" line's output is available.

Hand-traced expected sequence, accounting for `peekpipe()`'s one-step fetch-behind-execute lag
(established firmly earlier this session — see the `mdrawc.mc`/`PLOT`/`LOOP` byte-dump
cross-reference above): `reset()` primes `m_pipeline = 0x01` (an implicit `NOP`), so **the first
`step()` call always executes that NOP**, not the byte at the launch address — it just prefetches
`ROM[8000]=0x02 (cache)` into the pipeline for next time, and `r15` advances `8000→8001`. The
*second* `step()` call is the one that actually executes `CACHE` (fetched last time), while
prefetching `ROM[8001]=0x2F (with r15)`; `CACHE`'s own body (`newCbr = m_r[15] & 0xFFF0`) reads
`m_r[15]` *before* the post-instruction `++m_r[15]`, so it sees `8001`, computes `newCbr=0x8000`,
differs from the initial `m_cbr=0`, calls `flushCache()` — nothing there should touch `m_go`. That
prediction (still running after 2 steps, now about to execute `WITH R15`) does not match the
observed `running=0` after only 2 `step()` calls. **This is the concrete next step**: rebuild with
the `"-> after:"` line active (already added above, just needs a rebuild+rerun) to see the exact
`step()` call after which `running()` flips to 0 and what `pc`/`sfr` look like at that exact
instant, then single-step through `readOpcode()`/`instruction()` by hand (or with a debugger) for
that one call to find where `m_go` actually gets cleared. Once this toy program's `LOOP` either
provably works or provably doesn't, that directly resolves (or redirects) the root cause #2
investigation above.

## Progress log, continued — isolated LOOP test resolved: it was the test, not `gsu.cpp`

Resumed session, reran the standalone test above with the `"-> after:"` line active (as instructed)
plus `SNESFOX_GSU_TRACE=30` for a full opcode trace. Root cause of the "GSU stopped without ever
executing `STOP`" contradiction: **the test itself** wrote `SCMR` to `0x303B` —
`GSU::writeRegister`'s real switch (`gsu.cpp` ~line 1328) maps `0x303A` to `parseScmr(value)`;
`0x303B` isn't a write case at all (falls through to `default: return;`, silent no-op — it's only a
*read*-side alias for VCR, `case 0x303B: return m_vcr;` in the read switch). So `m_scmrRon` stayed at
its `reset()` default (`false`), and `GSU::readRom()` (`gsu.cpp` ~line 387) unconditionally returns
`0x00` whenever `!m_scmrRon` — which happens to be `STOP`'s opcode. The pipeline's first real ROM
fetch (for `CACHE` at `base+0`) silently came back `0x00` instead of `0x02`, so the "opcode" executed
two `step()` calls in was a real, correctly-dispatched `STOP` — just fetching zeroed-out fake ROM, not
the test's actual bytes. Not a pipeline-lag bug, not a `m_go`-clearing bug: a wrong MMIO address in
the test harness.

**Fix**: changed `gsu.writeRegister(host, 0x303B, 0x19)` to `0x303A` in the scratchpad test. Rerun
(`SNESFOX_GSU_TRACE=30`): full opcode trace now shows `CACHE`/`WITH R15`/`TO R13`/`INC R1`/`LOOP`
executing exactly as expected — three loop passes (`r12: 3→2→1→0`, `r1` incrementing by 2 each pass
as the template's `INC R1` fires both in the loop body and the delay slot), then falls through to the
real `STOP` at `base+6`. Program prints `PASS`.

**This resolves the standalone side-investigation conclusively**: `LOOP` (`insnLOOP`) and the `with
r15`/`to r13` idiom both work correctly in isolation — the earlier hypothesis that `LOOP`'s own
termination check was somehow not firing (letting a counter wrap ~22,000 times instead of stopping at
~5,376) is **ruled out**. Root cause #2's own write-up already flagged the alternative, now the
leading hypothesis: execution isn't looping-and-wrapping at all, but **landing mid-block** inside one
of the three near-identical `cache/with r15/to r13/stw (r1)/inc r1/loop/inc r1/[stop|jmp r11]`
templates (`$01:B0AB`/`$01:B0BB`/`$01:B0D2` in `starfox.sfc`) via some other, still-unidentified
buggy jump/branch/return upstream — inheriting whatever `r1`/`r12` a prior, unrelated code path left
behind, rather than running its own seeded loop past its own bound.

**Next step** (unchanged in substance from before, now the sole remaining direction — no longer
"first rule out a LOOP bug, then do this"): use `SNESFOX_GSU_RAM_WATCH` on a wider window around the
GSU-RAM write immediately preceding the first `$021E`/`$001E` corruption (or extend the watch to also
print `r11`/`r13`, the values a `jmp r11`/return-address register would carry), and/or add a
temporary trace to `insnJMP_LJMP`/`insnBranch`/`insnLOOP` gated to fire only from ~frame 2900 onward
(the corruption happens once, late — an un-gated trace drowns in ~2 million earlier instructions, as
it did two sessions ago) to catch whichever branch/jump actually misfires into one of the three
fill-loop template addresses.

## Progress log, continued — the culprit `LOOP` found: `$01:B2FD`, relying on a caller-supplied `r13` that never arrives

Follow-up session, user: "les 2" (do both the wider `SNESFOX_GSU_RAM_WATCH` and the gated
jump/branch/loop instrumentation suggested above). All new instrumentation added this session is
**kept** (env-var gated, zero cost when unset, matching this codebase's existing debug-tooling
convention) — `./build.sh`/`./snesfox selftest` (10/10 across all suites)/`./release.sh` round-trip
reconfirmed clean after every change below.

**Step 1 - widened `SNESFOX_GSU_RAM_WATCH`**: extended `debugGsuRamWatch` (`gsu.cpp`) to also print
`r11`/`r13`/launch count, not just `r1`/`r10`/`r12`. `SNESFOX_GSU_RAM_WATCH=021E ./snesfox snap
StarFox/starfox.sfc 3500` pinpoints the exact corrupting write:
```
[GSU-RAM-WATCH] launch=76 pc=$01:B0C6 addr=021E <= 1E r1=021E r10=04C5 r11=0100 r12=BC43 r13=B0C5
```
`r13=B0C5` matches the fill-loop template's own loop-body address exactly (confirmed earlier
session) - i.e. this store really is executing *inside* that shared template's code.

**Step 2 - new `SNESFOX_GSU_LOOP_TRACE=<minLaunch>`**: added a trace in `insnLOOP()` that fires
whenever `r12` is near the zero/wraparound boundary (`<=5` or `>=0xFFFB`), gated by launch count.
`SNESFOX_GSU_LOOP_TRACE=70` shows launches 77 and 78 (fresh, correctly-seeded fill-loop instances)
hit `r12=0000 z=1 take=0` and correctly do not branch - proof `insnLOOP` itself is correct (again;
this reconfirms the standalone test's finding from earlier in this session). Crucially, **launch 76
- the one that actually corrupts `$021E` - never appears in this near-zero trace at all**, meaning
`r12` was already far from zero for that entire ~1024-cycle session; the "~22,000-iteration overshoot"
from the previous session's write-up cannot have happened in one session's budget (confirmed via
`lastSessionCycles=1024` in the snap output - a single session can only do on the order of ~150-250
LOOP iterations, nowhere near 22,000).

**Step 3 - new `SNESFOX_GSU_CACHE_TRACE=<minLaunch>`**: traces every entry into the fill-loop's own
`CACHE` prologue (`$01:B0A0-B0FF`, PBR=1). Across the whole 3500-frame run it shows only 10 clean
entries - launches 17/18, 27/28, 37/38, 47/48, 77/78 - always with the correct seed (`r1=2C00` or
`5600`, `r12=00FC`). No entry at all for launch 76, meaning whatever executes the corrupting `STW` at
`$01:B0C6` during launch 76 never went through this template's own prologue that launch - it's
reusing residual register state from a much earlier invocation.

**Step 4 - new `SNESFOX_GSU_STW_TRACE=1`**: traces every `STW (R1)` inside the fill-loop's address
range, logging only on launch-boundaries or non-`+2` address deltas (steady state is `+2`/store, so
this stays a sparse signal). This is the key timeline:
```
launch=18 addr=5600 r12=00FC   <- fresh, correct seed
launch=26 addr=8400 r12=FFFF   <- already wrapped past zero, big unexplained jump
launch=36 addr=4600 r12=FFFF
launch=46 addr=0800 r12=FFFF
launch=56 addr=CA00 r12=FFFF
launch=66 addr=7C93 r12=EAED
launch=76 addr=DB26 r12=CFBF, then addr=0000 r12=BD52 (delta=-65534, address wrapped 0xFFFF+2)
launch=77 addr=2C00 r12=00FC   <- fresh again (different template, B0DD)
launch=78 addr=5600 r12=00FC   <- fresh again
```
`r12` sits at exactly `0xFFFF` across launches 26/36/46/56 (four separate, ~10-launches-apart
sightings) before finally moving further at 66 and 76 - inconsistent with one continuous session
that merely got interrupted and resumed (that would show smooth incremental drift each visit, not
the same frozen value four times running).

**Step 5 - new `SNESFOX_GSU_FOREIGN_LOOP_TRACE=1`**: the disambiguating experiment. Fires inside
`insnLOOP()` whenever the branch is taken (`!m_z`) with `r13` equal to one of the two known fill-loop
target addresses (`0xB0C5`/`0xB0DC`) while the executing `LOOP` instruction itself is outside the
fill-loop's own code range - i.e. it catches some *other* routine's `LOOP` jumping into the fill-loop
by accident via a stale `r13`. All 6 hits across the whole run:
```
[FOREIGN-LOOP] launch=26/36/46/56/66/76 from=$01:B2FE -> r13=B0C5 r1=... r12=...
```
Every single one originates from the exact same address, `$01:B2FE` (the logged `pc`, which per the
runtime debug log's one-instruction pipeline lag - `entry.opcode` at row *N* is always the ROM byte
last queued by row *N-1*'s own fetch, not `ROM[entry.pc]` - corresponds to the real `LOOP` opcode
byte sitting at `$01:B2FD`). This is not a random corruption; it's the same, otherwise-benign code
path firing on a predictable ~10-launch cadence.

**Static decode of the real cause** (avoided the runtime debug-log's pipeline-lag ambiguity entirely
- see the "static disassembler" lesson from two sessions ago - by hand-decoding the raw ROM bytes at
`$01:B2D0` onward using `gsu.cpp`'s own instruction-length rules, cross-checked against a `BRA` at
`$01:B2D9` (`05 12` = branch always, displacement `+0x12`) landing exactly on `$01:B2ED`, matching
the runtime trace's observed jump target - strong confirmation the byte alignment is correct):
```
$01:B2ED  A8 02        IBT R8,#$02
$01:B2EF  28           WITH R8
$01:B2F0  59           ADD R9        ; r8 = r8 + r9
$01:B2F1  B1           FROM R1
$01:B2F2  38           STW (R8)      ; store r1's value to [r8]
$01:B2F3  A8 00        IBT R8,#$00
$01:B2F5  28           WITH R8
$01:B2F6  59           ADD R9        ; r8 = r8 + r9 (again)
$01:B2F7  B7           FROM R7
$01:B2F8  38           STW (R8)      ; store r7's value to [r8]
$01:B2F9  A0 1E        IBT R0,#$1E
$01:B2FB  19           TO R9
$01:B2FC  59           ADD R9        ; r9 = r0 + r9  (r9 += 0x1E)
$01:B2FD  3C           LOOP          ; r12 -= 1; if r12 != 0: pc = r13
```
This `LOOP` at `$01:B2FD` has no `TO R13` anywhere in its own block - by the same calling convention
used everywhere else in this codebase (`move r13,pc` idiom, i.e. `with r15/to r13` executed once on
entry to seed a subroutine's own loop-back target), this routine expects its caller to have set `r13`
(and, by the matching pattern, `r12` as the loop count) immediately before jumping in - exactly like
the three fill-loop templates do for themselves. It never does so locally. This one block is a
completely different, unrelated piece of code from the three fill-loop templates - it just happens
to inherit whatever `r13`/`r12` last held, which on every one of these 6 occasions happens to still
be the fill-loop's own `B0C5`/wrapped-counter values from ~10 launches earlier, because nothing in
between has overwritten them.

**This is very unlikely to be a bug in `insnLOOP`/`insnCACHE`/`insnStore` themselves - all three are
independently re-verified correct multiple times this session.** Since `StarFox/starfox.sfc` is
Argonaut's real, shipped, hardware-tested ROM, this exact code must work correctly on real hardware,
which means either: (a) `r12` legitimately holds `0` at this point on real hardware (so the branch is
never taken) and something upstream of `$01:B2ED` is producing the wrong `r12` in snesfox - the same
"caller doesn't (re)seed a register the callee silently depends on" shape as the `r13` question, just
one register earlier in the same call; or (b) control flow shouldn't even reach `$01:B2D9`'s `BRA`
(or whatever calls into the `$01:B2D0`-ish block containing it) on this specific pass at all - i.e.
the real divergence is further upstream still, in whatever decides to enter this block in the first
place.

**Concrete next step**: trace backward from `$01:B2D0` (what calls into this block, and does it set
`r12`/`r13` immediately before the call on a "good" path but not on this one?) using the same static
hand-decode technique (reliable, sidesteps the pipeline-lag trap) combined with a narrowly-gated
`SNESFOX_GSU_RANGE_TRACE=<lo>-<hi>:<count>:<minLaunch>` (the `:minLaunch` gate is new this session,
`gsu.cpp` `mainStep`, avoids drowning in earlier launches) centered on whatever address calls into
`$01:B2D0`, gated to launch ~20-26 (the first `FOREIGN-LOOP` firing) to catch the actual call site and
its register state on entry.

## Progress log, continued — traced to `mallrotzsort`'s own numshapes check; pipeline byte-mapping caveat discovered

Same session, continuing "les 2" after finding `$01:B2FD`. User: "continue".

**`$01:B2ED` is not foreign code — it's `mallrotzsort`'s own list-append tail.** Static hand-decode of
`$01:B2A0-B2C3` (same technique as before, cross-checked against the ground-truth, human-readable
Argonaut source at `StarFox/SG_extracted/mdrawlis.mc:1382-1400`, `mallrotzsort`) shows this is a
lazy-reinit check: read `m_dlptr` ($021E) into r7, test if zero, and if so, rewrite it from r9 and
jump to the same `$01:B2ED` list-append code found last time. The real source:
```asm
mallrotzsort
	sub	r0
	romb
	sm	[m_dlptr],r0

	iwt	rdlptr,#m_drawlist&WM     ; rdlptr = r9

	lms	r12,[m_numshapes]         ; r12 = current shape count
	with	r12
	lob
	bne	.noret                    ; if numshapes != 0, continue
	nop
	stop                              ; if numshapes == 0, stop here
	nop
.noret
	mcache
	move	r13,r15                   ; r13 freshly re-seeded HERE, only on this path
.sortloop
	...
	loop
	nop
	stop
```
**This resolves the "why doesn't `$01:B2ED`'s block set r13 itself" question from last entry**: it
doesn't need to — on the correct path, `r13` gets freshly set by `move r13,r15` in `.noret`,
*immediately before* `.sortloop` (which is what eventually reaches the `$01:B2FD` `LOOP`). The
mystery isn't "missing r13 setup" in the abstract — it's *whether this run actually takes the
`.noret` path at all*, since `.noret` is only reached when `numshapes != 0`.

**New `SNESFOX_GSU_B2ED_TRACE=1`** (kept, cheap, `gsu.cpp` `mainStep`): fires once per launch when
`pcBefore == 0xB2ED`, unconditionally (not just on the known-bad launches). Result across the whole
3500-frame run — **only 7 total visits, ever**, and `r12` is `0000` (or already-corrupted-onward) at
literally every single one, with no counterexample:
```
launch=12  r12=0000 r13=0000   <- very first visit, r13 not yet even B0C5 (fill-loop hasn't run yet)
launch=26  r12=0000 r13=B0C5
launch=36  r12=0000 r13=B0C5
launch=46  r12=0000 r13=B0C5
launch=56  r12=0000 r13=B0C5
launch=66  r12=EAEE r13=B0C5   <- garbage from the PREVIOUS misfire's own detour, not fresh
launch=76  r12=CFC0 r13=B0C5   <- same
```
This means the `.noret`/fresh-r13 path is *never* the one reached in practice in this run — every
visit to `$01:B2ED` arrives with `r12` reading as (effectively) zero, which is exactly the condition
that source says should `STOP` immediately, never reaching `.sortloop`/`$01:B2ED` at all.

**Open question, not yet resolved — a real pipeline byte-accounting subtlety, not obviously a bug**:
trying to statically map exactly which ROM bytes correspond to `lms r12,[m_numshapes]` /`with r12`/
`lob`/`bne .noret` (expected around `$01:B189-B196`, between the confirmed-correct `iwt rdlptr,#...`
at `$01:B186` and the confirmed-landing address `$01:B197`) hit a genuine, previously-undocumented
subtlety in `peekpipe()`/`pipe()`'s one-item-queue bookkeeping: **when an instruction consumes operand
bytes via `pipe()`, the very *first* `pipe()` call inside that instruction returns whatever
`peekpipe()` *just* re-fetched this same call using the *unchanged* `r15`** — and if the *previous*
instruction itself had `j>=1` pipe() calls, that unchanged `r15` already equals the new opcode's own
address, so the new instruction's first "operand" byte is a *repeat* of its own opcode byte's ROM
value, not a fresh subsequent byte. Confirmed empirically via a new standalone harness
(`<scratchpad>/mallrotz_test.cpp` — loads the real `StarFox/starfox.sfc` bytes, seeds GSU RAM `$01B6`
= `m_numshapes` to a controlled value, launches at `$01:B17F`) plus `SNESFOX_GSU_TRACE`: the `iwt
rdlptr,#$F2F9`-equivalent at `$01:B186` (opcode `0xF9`) genuinely only advances `r15` by 2 (to
`$01:B188`), not 3, because of this quirk — and `r9` still comes out to exactly `0xF2F9`, matching
every other session's independently-confirmed-correct `m_drawlist` base pointer, which is strong
evidence this is *correct, faithful* GSU pipeline behavior (matching however `ares`/real hardware
truly works), not an emulation bug. The byte at `$01:B188` (`0x0E`, a real `BVC` opcode per the
decode) genuinely does not match `lms r12,[m_numshapes]`'s expected encoding (`0xAC`) at that exact
offset, meaning either this compiled ROM's `mallrotzsort` doesn't lay out 1:1 with the `.mc` source's
apparent instruction order at this byte-level of granularity (macro-expansion differences,
assembler-side pipeline-aware instruction reordering, or this reviewer's `$01:B17F` entry-point
assumption — inherited from an even earlier session — being slightly off), or there is a real,
subtle mismatch in `insnIWT_LM_SM`'s pipe()-call count vs `ares`'s reference. **Not yet distinguished
between these — this is the concrete next step**, and it should be resolved by diffing
`gsu.cpp::insnIWT_LM_SM`/`pipe()`/`peekpipe()` line-by-line against
`ares-ref/component/processor/gsu/memory.cpp` (`Read`/`ReadOpcode`/`Pipe`, whatever they're actually
named there) rather than continuing to hand-derive the byte mapping — this class of pipeline-timing
subtlety is exactly what ares got right by construction and is easy to get subtly wrong by intuition
alone.

**New debug tooling added this session** (all kept, env-var gated, zero cost when unset,
`./build.sh`/`./snesfox selftest` 10/10/`./release.sh` round-trip reconfirmed clean after every
addition): `SNESFOX_GSU_RAM_WATCH` widened to print `r11`/`r13`/launch count;
`SNESFOX_GSU_LOOP_TRACE=<minLaunch>` (fires near r12's zero/wraparound boundary);
`SNESFOX_GSU_CACHE_TRACE=<minLaunch>` (fires on entry to the fill-loop's own `CACHE` prologue);
`SNESFOX_GSU_STW_TRACE=1` (sparse, fires only on launch-boundary/non-`+2`-delta `STW (R1)` writes in
the fill-loop range); `SNESFOX_GSU_FOREIGN_LOOP_TRACE=1` (fires when a `LOOP` outside the fill-loop's
own range branches into it via stale `r13`, dumps the last 48 instructions from the existing
`m_debugLog` ring buffer); `SNESFOX_GSU_RANGE_TRACE` gained a third `:minLaunch` field;
`SNESFOX_GSU_B2ED_TRACE=1` (one-shot per-launch marker at `$01:B2ED`); `SNESFOX_GSU_POST_TRACE=1`
(post-execution, lag-free `m_r[15]` trace for a hardcoded `$01:B180-B200` window — the most reliable
of the bunch for control-flow reconstruction since it needs no lag interpretation, only the
opcode-to-address attribution still does).

**Resume point**: read this whole section plus the two before it before continuing. The chain is now:
symptom (`m_dlptr` clobbered) → traced to `$01:B2FD`'s `LOOP` branching on stale `r13`/garbage `r12`
→ traced to `$01:B2ED` being `mallrotzsort`'s own list-append tail, reached (7/7 times, no exceptions)
with `r12` reading as zero, which per source should `STOP` immediately instead → blocked on precisely
confirming the ROM byte layout around `$01:B186-B197` (the `numshapes` load/test) due to a genuine
pipeline self-referential-first-operand subtlety in `peekpipe()`/`pipe()` that needs cross-checking
against `ares-ref` before trusting further hand-decode in this specific spot. `mallrotz_test.cpp`'s
source is preserved below (recreate at `<scratchpad>/mallrotz_test.cpp`, compile with
`clang++ -std=c++20 -I<repo-root> -o mallrotz_test mallrotz_test.cpp gsu.cpp`, run from the repo root
with an optional `argv[1]` = forced `m_numshapes` value, defaults to 5) for whoever continues this —
it is the fastest way to get a controlled, deterministic repro of this exact code path without
waiting on real game timing:

```cpp
#include "gsu.hpp"
#include <cstdio>
#include <vector>
#include <fstream>

class TestHost final : public GsuHost {
public:
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram70;

    TestHost() : rom(0x100000, 0), ram70(0x10000, 0) {}

    uint8_t read(uint8_t bank, uint16_t addr) override {
        if (bank == 0x70) return ram70[addr];
        return 0;
    }
    void write(uint8_t bank, uint16_t addr, uint8_t value) override {
        if (bank == 0x70) ram70[addr] = value;
    }
    uint8_t readRom(uint32_t address24) override {
        // LoROM half-bank formula matching bus.cpp::gsuReadRom for banks $00-$3F.
        uint32_t off = ((address24 & 0x3F0000) >> 1) | (address24 & 0x7FFF);
        return rom[off];
    }
};

int main(int argc, char** argv) {
    uint16_t numshapes = argc > 1 ? static_cast<uint16_t>(std::strtol(argv[1], nullptr, 0)) : 5;

    TestHost host;
    {
        std::ifstream f("StarFox/starfox.sfc", std::ios::binary);
        f.read(reinterpret_cast<char*>(host.rom.data()), host.rom.size());
    }

    GSU gsu;
    gsu.reset();
    gsu.writeRegister(host, 0x303A, 0x19); // SCMR: RON|RAN|MD=1 (correct address)
    gsu.writeRegister(host, 0x3034, 0x01); // PBR = 1

    // Seed GSU RAM $01B6 = numshapes (LMS r12,[m_numshapes] reads this).
    host.ram70[0x01B6] = static_cast<uint8_t>(numshapes & 0xFF);
    host.ram70[0x01B7] = static_cast<uint8_t>(numshapes >> 8);

    // Launch at $01:B17F (mallrotzsort entry, per previous session's cross-reference).
    gsu.writeRegister(host, 0x3000 + 15*2, static_cast<uint8_t>(0xB17F & 0xFF));
    gsu.writeRegister(host, 0x3000 + 15*2 + 1, static_cast<uint8_t>(0xB17F >> 8));

    printf("numshapes=%u  running=%d pc=%04x\n", numshapes, gsu.running(), gsu.pc());

    int steps = 0;
    while (gsu.running() && steps < 60) {
        printf("step %2d: PRE pc=$01:%04X  r0=%04X r9=%04X r12=%04X r13=%04X  byte@pc=%02X\n",
            steps, gsu.pc(), gsu.reg(0), gsu.reg(9), gsu.reg(12), gsu.reg(13),
            host.readRom(0x010000u | gsu.pc()));
        gsu.step(host);
        printf("         POST pc=$01:%04X running=%d\n", gsu.pc(), gsu.running());
        ++steps;
    }
    printf("final: running=%d pc=$01:%04X steps=%d\n", gsu.running(), gsu.pc(), steps);
    return 0;
}
```

Run it with `SNESFOX_GSU_TRACE=40 ./mallrotz_test 5 2>&1 >/dev/null` to see the emulator's own
ground-truth `[GSU pbr:pc] op=XX ...` sequence (no manual lag-guessing needed) alongside this
program's own PRE/POST prints.

## Progress log, continued — `pipe()`/`peekpipe()` verified byte-identical to ares; `$01:B17F` entry-point assumption now suspect

Same session, user: "ok fais ça" (do the ares diff suggested above).

**`gsu.cpp`'s `pipe()`/`peekpipe()` and every instruction body touched by this investigation
(`instructionCACHE`/`Store`/`LOOP`/`Branch`/`IWT_LM_SM`/`IBT_LMS_SMS`/`GETC_RAMB_ROMB`) match
`ares-ref/sfc/coprocessor/superfx/memory.cpp` and `ares-ref/component/processor/gsu/instructions.cpp`
exactly**, line for line (only cosmetic differences: ares increments `r15` via pre-increment inside
the `readOpcode(++regs.r[15])` call argument, `gsu.cpp` computes `r15+1` then increments separately —
mathematically identical). `ares-ref/sfc/coprocessor/superfx/superfx.cpp::main()` also matches
`gsu.cpp::mainStep()`'s structure exactly (peekpipe → instruction → r14-modified check →
r15-modified-or-auto-increment check). **This rules out an emulator bug in the pipeline/instruction
mechanics themselves** — the earlier "pipeline byte-mapping caveat" from the previous entry is real,
faithful GSF/ares behavir, not a snesfox defect.

**Redid the full manual instruction-by-instruction trace from `$01:B17F` using ares's own semantics
precisely** (peekpipe fetch happens once per call using **unchanged** r15; each `pipe()` call inside
an instruction delivers whatever was *just* queued, then re-fills using the **current, possibly
already-incremented-by-this-instruction** r15) and it is **internally self-consistent and matches
every empirically observed checkpoint exactly** (the `r9=0xF2F9` result, the `$01:B186→B188` landing,
and now also the final `$01:B188→B197` landing) — track record: 100% match across every checkpoint
checked. The trace shows: `$01:B186`'s `IWT R9,#$F2F9` only *effectively* advances `r15` by 2 (not 3)
because of the "first operand re-reads the opcode's own just-fetched byte" pipeline quirk described
last entry, landing the *next* opcode exactly on `$01:B188` — whose real ROM byte is `0x0E` (`BVC`,
branch if overflow-clear). **`BVC`'s own displacement operand suffers the exact same self-referential
quirk** (since the preceding `IWT` had `pipe()` calls too): it re-reads its own opcode byte (`0x0E`)
as the displacement (`+14`). With `sfr.ov` still clear from the earlier `SUB R0` (0-0 can't overflow),
the branch is taken: `r15 = ($01:B189) + 14 = $01:B197` — **exactly matching the empirically observed
jump, skipping `$01:B189-B196` (confirmed via `python3` byte-scan to be exactly `3D AC DB` =
`alt1`/`lms r12,[$DB]` i.e. `[m_numshapes]` at `$01:B189`) entirely, every single time, regardless of
what `m_numshapes` actually holds** — confirmed by rerunning `mallrotz_test` with a forced
`numshapes=5`: `r12` never picks up the value, because the `lms` instruction is provably never
reached via this path.

**This strongly suggests the `$01:B17F` "mallrotzsort entry point" assumption — inherited from an
even earlier session's cross-reference, never independently re-verified this session — is wrong.**
The instruction sequence found there (`sub r0` / `[alt3]romb` / `sm [m_dlptr],r0` / `iwt
rdlptr,#$F2F9`) is a generic "zero a register, clear a pointer" idiom that could plausibly appear in
more than one subroutine; it is not strong enough evidence on its own that this is really
`mallrotzsort`'s entry, and a self-referential-pipeline-quirk `BVC` unconditionally skipping the very
next block on *every* visit is exactly the kind of symptom you'd expect from being subtly misaligned
with the real routine boundary (a few bytes off from the true entry, landing inside what looks like a
plausible-but-wrong prologue) rather than from real, intentional Argonaut assembly relying on this
quirk (no assembly programmer would deliberately code around anything this fragile).

**Concrete next step, corrected**: before trusting any further hand-decode in this area,
independently re-derive the *real* `mallrotzsort` entry address from scratch — do not reuse
`$01:B17F`. Two ways to do this reliably: (1) search `StarFox/SG_extracted/bank1.asm` (or the
assembled listing, if one can be produced) for `mallrotzsort`'s label and cross-reference against
known-fixed landmarks already independently confirmed this session (`$01:B2FD`'s `LOOP`, `$01:B2ED`'s
list-append code, `$01:B0C5`/`$01:B0DC` the two fill-loop templates) by their relative byte offsets
from `mallrotzsort`'s start in the source, since those addresses are solid; or (2) use
`SNESFOX_GSU_B2ED_TRACE`/a similar marker at a *provably-correct* landmark (e.g. `$01:B2ED` itself)
and walk *backward* through `m_debugLog`'s 48-entry ring buffer (already exists, see
`debugLogEntry()`/`debugLogCount()`) at the moment of a real `B2ED-ENTRY` firing, reading
`operand1`/`operand2` fields directly (these are raw, lag-free ROM byte snapshots — `entry.operand1 =
host.readRom(pbr<<16|pc)`, `entry.operand2 = ROM[pc+1]` — safe to read without re-deriving the
opcode-address pipeline lag by hand at all) to reconstruct the real preceding bytes mechanically
instead of guessing a starting address and hoping it aligns.
