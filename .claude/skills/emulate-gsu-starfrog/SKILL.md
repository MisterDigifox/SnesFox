---
name: emulate-gsu-starfrog
description: How to debug and extend snesfox's Super FX (GSU) core, and how to use StarFrog/starfrog.sfc as the real-world regression target for it. Use whenever asked to work on GSU/Super FX emulation, or to get StarFrog (or any Super FX game) running further in snesfox.
---

# Emulating the GSU (Super FX) and using StarFrog as its test target

## What exists already

`gsu.hpp`/`gsu.cpp` (~1200 lines) is a full GSU execution core — register file,
instruction dispatch (`GSU::instruction()`), ROM/RAM buffer timing, pixel-plot
cache, cache RAM, MMIO at `$3000-$34FF` — structurally ported from ares's
`ares/component/processor/gsu/*` and `ares/sfc/coprocessor/superfx/*`. It is
**not** a stub: it decodes and executes the real opcode set. Don't assume it
needs to be written from scratch; assume it has subtle bugs to find, the same
way the rest of this toolkit does (see top-level `CLAUDE.md`).

`bus.cpp`'s `BusGsuHost` bridges it to the rest of the emulator: GSU work RAM
lives at banks `$70`/`$71` (`m_gsuRam`, routed through normal `Bus::read`/
`write` so the main CPU sees the same storage), GSU's own ROM view goes
through `Bus::gsuReadRom()`, and `Bus::stepPeripherals` calls `m_gsu.run()`
every CPU step scaled by a 4x/8x clock multiplier (`m_clsr`).

`StarFrog/` (sibling directory, this session's checkout) is the leaked/renamed
Star Fox source — see `StarFrog/CLAUDE.md` for what it is and how to build it
(`cd StarFrog && ./build.sh`, needs DOSBox-X). It has already been edited to
boot straight into `title.asm` (a rotating `petecube` logo + GSU-driven
starfield) and stay there forever — every other screen is unreachable. This
makes it a real, GSU-driven, but narrowly-scoped regression target:
`StarFrog/starfrog.sfc` is checked in and ready to use with no rebuild needed
unless you're editing the StarFrog source itself.

`ares-ref/component/processor/gsu/` and `ares-ref/sfc/cpu/` and
`ares-ref/sfc/coprocessor/superfx/` hold the actual ares reference source,
vendored in during a prior session specifically to diff `gsu.cpp` against
instead of relying on memory of the ISA. If a file you need isn't there yet,
fetch it the same way (works even though `raw.githubusercontent.com` is
aggressively rate-limited from this environment — use the git-blob API
instead, which isn't):

```bash
python3 -c "
import json, base64, urllib.request
def get(url):
    return json.load(urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent':'curl/8.7.1'}), timeout=15))
entries = get('https://api.github.com/repos/ares-emulator/ares/contents/ares/component/processor/gsu')
for e in entries:
    blob = get(e['url'])
    open(f\"ares-ref/component/processor/gsu/{e['name']}\", 'wb').write(base64.b64decode(blob['content']))
    print(e['name'])
"
```

## Debugging workflow

1. **Build and run headless.** `./build.sh` then
   `./snesfox snap StarFrog/starfrog.sfc <N>` for N frames — prints a big
   PPU/GSU/framebuffer state dump at the end (`GSU: launches=... stops=...
   plotCount=... pc=$xx:xxxx`, `BG1 tilemap ... nonzero=`, `CGRAM[...]`,
   `framebuffer: non-black opaque pix=`). This is the fast, non-interactive
   way to check "did anything change." `./snesfox emu StarFrog/starfrog.sfc`
   opens the real Dear ImGui debug window if you want to see it visually
   (CPU Debug panel shows live PC/opcode/instruction; Palette panel shows
   CGRAM directly — real GSU-decompressed colors showing up there is a good
   quick sanity signal that GSU is doing real work).

2. **Trace GSU instructions**: `SNESFOX_GSU_TRACE=<N> ./snesfox snap ...`
   prints the first N *executed-while-GO* GSU instructions to stderr as
   `[GSU bank:pc] op=XX r1=.. r2=.. r14=.. sfr=....` — only fires while
   `SFR.GO` is set, so bump N and/or frame count until you're past whatever
   launch you care about. `SNESFOX_GSU_IO=1` traces every CPU-side
   register read/write to `$3000-$303B` (`Bus::gsuReadRom`/`readRegister`/
   `writeRegister`).

3. **Trace main-CPU instructions**: `SNESFOX_CPU_TRACE=<N>
   SNESFOX_CPU_TRACE_ALWAYS=1 ./snesfox snap ...` prints
   `[CPU bank:pc] MNEMONIC operand cyc=... p=.. a=.... x=.... sp=....` for
   every instruction (without `_ALWAYS`, it only fires while
   `bus.gsu().running()`, which is useless for debugging what the *main* CPU
   is doing between GSU launches — always set `_ALWAYS=1` for that). N needs
   to be large (millions) to reach anywhing deep into boot; redirect to a
   file (`... > /tmp/trace.log 2>&1`, **not** `2>&1 > file` — that reorders
   wrong in zsh and drops stderr) and grep/tail rather than trying to read
   it inline. To find where execution is stuck in a loop: grep for a
   suspected address (`grep -c "02:DCBF\]"`) or look for the same handful of
   addresses repeating forever near the end of the file.

4. **Cross-reference real addresses against StarFrog's actual source.**
   `./snesfox disasm StarFrog/starfrog.sfc /tmp/out.asm` auto-labels
   reachable code (`L_1FD313:`-style labels, `DataWords_...` for anything it
   only sees as data) — grep the bank:offset you're stuck at. For deeper
   confirmation, `StarFrog/SG_extracted/*.asm`/`StarFrog/MAPS_extracted/`
   is the *actual* leaked Argonaut source with real labels/comments; use it
   to check whether a given code path is legitimate game logic (and what
   it's *supposed* to do) versus wondering if it's emulator garbage.
   `StarFrog/SG_extracted/mmaths.mc`/`mwrot.mc`/`mdrawlis.mc`/`mgdots.mc`/
   `mobj.mc`/`msprite.mc` are the actual GSU microcode files — the single
   shared 3D-rendering program used for both gameplay and the title-screen
   starfield.

## Diagnosing a GSU/timing hang — the general method

A GSU program (or main-CPU polling loop) that never terminates is almost
always **reading wrong data**, not "impossible on real hardware" — real
Nintendo-shipped code doesn't ship with genuine infinite loops. When you find
one:

1. Trace it (`SNESFOX_GSU_TRACE` or `SNESFOX_CPU_TRACE_ALWAYS`) and read the
   *exact* instruction sequence. Decode it against the real ISA semantics
   (`ares-ref/component/processor/gsu/instructions.cpp` for GSU;
   `ares-ref/sfc/cpu/*.cpp` for the 65816/DMA/HDMA side) — don't guess opcode
   meanings from memory, they're subtle (ALT1/ALT2 prefix state, `n`-nibble
   register mapping differs per opcode range, etc.).
2. If the loop is an **unconditional** jump/branch back to itself with no
   register ever changing that could break it: the *bug is almost certainly
   upstream* — either the fetched bytes are wrong (address-mapping bug — see
   the `Bus::gsuReadRom` LoROM half-bank fix below for exactly this class of
   bug) or a register got a wrong value from something executed earlier.
3. If the loop **is** conditional on some hardware counter (H-counter
   `$213C`/V-counter `$213D` polling is the classic pattern — `LDA $2137`
   latches, then two reads of `$213C`/`$213D` alternate low/high byte via a
   toggle) and it never resolves: suspect **cycle-timing precision**, not
   logic. Dump the sampled counter values across many loop iterations
   (grep the trace, extract the register field after each read) and check
   whether they're a small, exactly-repeating set that skips the target
   window — that's proof of integer aliasing in the cycle-accounting model,
   not bad luck. See the timing section below for what to check.
4. Change one thing, rebuild, re-trace, and check whether the *set* of
   observed values changed at all — if it didn't, that fix wasn't in the
   path that matters for this specific loop; if it changed but the loop
   still doesn't resolve, you're converging (narrower gap) — keep going.
   Always confirm `./snesfox selftest` (22 checks) and `./release.sh`'s
   `cmp hello_world.sfc out.sfc` round-trip still pass after every change —
   this is real, easy-to-silently-break global timing/addressing code.

## Known-fixed bugs (context for anything still using old assumptions)

- **`Bus::gsuReadRom` (bus.cpp)** used to treat GSU ROM addresses as flat
  `bank*0x10000+addr`. Real GSU ROM decode for banks `$00-$3F` is the LoROM
  half-bank formula `((address & 0x3F0000) >> 1) | (address & 0x7FFF)` (each
  64KB CPU bank mirrors the same 32KB ROM chunk into both its low and high
  half — only 15 bits of intra-bank address are actually decoded); banks
  `$40-$5F` are a direct/linear mirror of the same ROM. Getting this wrong
  makes the GSU execute *plausible-looking garbage* (real bytes, wrong
  location) — worth re-checking first if a Super FX ROM's GSU core "runs"
  but never does anything sensible.
- **CPU cycle-timing precision**: `CPU::cycles()` rounds to a whole unit
  *per instruction*, which is fine for scanline/NMI/DMA scheduling
  cadence but throws away the fractional difference a single fixed-6-cycle
  I/O register access (`$2000-3FFF`/`$4200-5FFF`) makes against 8-cycle
  SlowROM code — enough to make a fully-deterministic H-counter polling
  loop alias onto the same finite, wrong set of counter values forever.
  `CPU::fineCycles()` is a parallel, un-rounded counter (kept at "×8"
  resolution) that `Bus` uses (via a second `m_fineCycleAccum`, separate
  from the original `m_cycleAccum` that still drives scanline/NMI/DMA
  cadence unchanged) specifically to derive `m_hCounter` more precisely.
  If you add a new place that adds to `m_cycles`, add the matching
  `m_fineCycles` update too — several were missed initially
  (`triggerNmi`/`triggerIrq`/`wakeFromWaiSilently`/WAI-spin/invalid-opcode
  early-returns in `CPU::step()`) and silently desync the two counters.
- **GP-DMA/HDMA cycle-stealing**: real hardware halts the CPU while DMA
  runs — 8 master clocks per byte transferred, plus fixed overhead (see
  `ares-ref/sfc/cpu/dma.cpp` for the exact formulas: GP-DMA is one flat
  8-cycle overhead for the *whole* `$420B` write plus 8 cycles/channel plus
  8/byte; HDMA is one flat 8 cycles *per scanline* HDMA runs at all
  (regardless of how many channels are active) plus 8/byte transferred plus
  8 per byte read reloading a channel's line-count/indirect-pointer table
  entry). `snesfox`'s DMA/HDMA previously stole zero cycles — for a game
  doing large VRAM transfers (StarFrog does several 60K+ byte ones during
  boot) this was by far the largest timing gap. `Dma::trigger`/
  `beginHdmaFrame`/`runHdmaForScanline` return the master-clock cost;
  `Bus`/`CPU::step()` fold it into both cycle counters (GP-DMA synchronously
  from the triggering instruction via `Bus::takeDmaStolenMasterClocks()`;
  HDMA directly into `Bus`'s accumulators since it runs autonomously off
  the scanline transition, not a CPU instruction).

Fixing these three took StarFrog from "GSU hangs executing garbage within
microseconds of boot" all the way through real GSU decompression, a
previously-unresolvable H/V-counter wait loop, DMA setup, and several
million cycles of legitimate subroutine calls.

## Known remaining issue (as of this writing)

Past the point above, StarFrog hits a bug in its own per-object "strategy
pointer" dispatcher (`L_1FD313` in the real source — builds a computed long
jump by pushing a bank+address pulled from an object's WRAM fields, then
`RTL`s into it, since 65816 has no indexed/indirect `JSL`). For the object
slot being processed (`X=0` at the point this was found), those WRAM fields
hold `bank=$00, addr=$3000` — inside the GSU's own MMIO window, not valid
code, so execution runs off into garbage. This looks like an *uninitialized*
strategy pointer rather than an emulator bug: `StarFrog/CLAUDE.md`'s
"Stripping the game down to just the title screen" notes explicitly say
every non-title screen is believed unreachable and was never verified once
disabled — some object-slot initialization that a full boot would normally
run may itself have been part of what got stripped. Chasing this needs
StarFrog-specific object-lifecycle tracing (what should have cleared/skipped
this object slot's "has a pending strategy call" flag, `$1D,X` bit `$80`),
not general GSU/timing work — a different, narrower kind of investigation.
See `immutable-orbiting-treehouse.md` (alongside this file) for the full
session log and trace this was written up from — it reproduces
deterministically from a fresh `starfrog.sfc` build if you need to re-derive
any of it.
