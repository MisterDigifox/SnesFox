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

**New, different bug found and root-caused (not yet fixed)** — this is StarFrog game-logic
archaeology, not general emulator engineering. Re-traced with an added `sp=` trace field and
found the earlier "RTI-corrupts-stack" read was a mis-diagnosis of a downstream *symptom*:
walking back from the garbage-bank landing, the actual origin is a legitimate 65816 idiom at
`L_1FD313` (found via `./snesfox disasm StarFrog/starfrog.sfc`, cross-referenced against real
labels) — StarFrog's per-object "strategy pointer" dispatcher, which builds a computed long
jump by pushing a bank+address pulled from an object's WRAM fields (`$1246`=bank, `$1244`=
address, indexed by the object slot in `X`) and executing `RTL` to "return" into it (a common
technique since 65816 has no indirect/indexed `JSL`). For the object slot being processed
here (`X=0`), those fields hold `bank=$00, addr=$3000` — not a valid strategy-routine address
at all (`$3000-$34FF` is the *GSU's own MMIO register window*, not code). This means either:
this object's strategy pointer was never properly initialized (most likely, since it's exactly
$00:$3000-ish — plausibly still-zeroed/leftover WRAM), or something upstream corrupted it.

This is worth flagging clearly: `StarFrog/CLAUDE.md`'s own "Stripping the game down to just
the title screen" notes say every screen except `title.asm` is believed unreachable and was
never verified at runtime once disabled — object-slot/strategy-pointer initialization that
would normally happen during a menu/level-transition boot path *that this stripped build
deliberately never reaches* is a very plausible explanation for an uninitialized strategy
pointer surfacing here. Chasing this further needs StarFrog-specific object-lifecycle tracing
(what should have cleared/skipped object slot 0's "has a strategy call pending" flag at
`$1D,X` bit `$80`, and whether that init path was itself part of what got stripped) — a
different, narrower kind of investigation than the general timing work above.

## Verification
- `./build.sh` succeeds.
- `./snesfox selftest` passes (22/22 as of the last session).
- `./check.sh` still passes (unaffected, but cheap to confirm nothing broke).
- `./release.sh`'s `cmp hello_world.sfc out.sfc` round-trip stays byte-identical.
- `./snesfox snap StarFrog/starfrog.sfc <N>` — the H/V-counter wait loop no
  longer wedges (confirmed fixed); next milestone is `plotCount > 0` and
  non-black framebuffer output once the object-strategy-pointer bug above is
  resolved.
