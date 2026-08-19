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
   `[CPU bank:pc] MNEMONIC operand cyc=... p=.. a=.... x=.... y=.... sp=....
   d=.... dbr=..` for every instruction (`y=`/`d=`/`dbr=` were added in a
   later session — needed to reconstruct which physical WRAM address a
   direct-page or absolute,Y instruction actually touched; don't assume
   D=0/DBR=PBR without checking, that assumption cost real time once).
   Without `_ALWAYS`, it only fires while `bus.gsu().running()`, which is
   useless for debugging what the *main* CPU is doing between GSU launches —
   always set `_ALWAYS=1` for that. N needs to be large (millions) to reach
   anything deep into boot; redirect to a file (`... > /tmp/trace.log 2>&1`,
   **not** `2>&1 > file` — that reorders wrong in zsh and drops stderr) and
   grep/tail rather than trying to read it inline. To find where execution
   is stuck in a loop: grep for a suspected address (`grep -c "02:DCBF\]"`)
   or look for the same handful of addresses repeating forever near the end
   of the file. `SNESFOX_WRAM_WATCH=<hex>` or `<hexLo>-<hexHi>` (`bus.cpp`)
   is the complementary tool for "what value does this WRAM address actually
   hold over time" — it logs every write that lands there (across all
   bank/mirror combinations that resolve to the same physical byte) as
   `[WRAM-WATCH #<seq>] bank=XX addr=XXXX <= XX`, which is what proved a
   CPU-trace-observed "wrong value" was actually the CPU reading the wrong
   *address*, not the memory holding wrong data.

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

- **CPU direct-page,X/Y addressing truncated the index register to 8 bits
  unconditionally** (22 call sites in `cpu.cpp`, e.g. `LDA dp,X`), even when
  the X flag indicated 16-bit index mode — silently discarding the high byte
  of any dp,X/dp,Y effective address ≥ 256. This is what earlier looked like
  StarFrog's per-object "strategy pointer" dispatcher (`L_1FD313`) reading an
  *uninitialized* WRAM field (`bank=$00,addr=$3000`, inside the GSU's own
  MMIO window) — it wasn't uninitialized at all; the CPU was reading the
  wrong address entirely (confirmed with a new `SNESFOX_WRAM_WATCH=<hex>`
  Bus-level write-watchpoint — see `bus.cpp`). Fixed by using the full
  `m_x`/`m_y` (already correctly zero-extended by `applyREP`/`applySEP`)
  instead of an extra `& 0x00FF` mask.
- **GSU `readRegister($3031)` masked out the IRQ bit (SFR bit 15) before
  returning it to the CPU**, breaking the real "read once to observe it, the
  read itself acknowledges it" IRQ contract; `$3030` also incorrectly cleared
  the IRQ flag (only `$3031` should, per `ares-ref/sfc/coprocessor/superfx/
  io.cpp`'s `readIO`). Fixed to match ares: return the true byte from
  `$3031`, clear `m_irq` only there.
- **`Bus::stepPeripherals`'s H-IRQ edge detector couldn't fire for
  `HTIME=0`** — `prevHIn < m_htime` is unsatisfiable when `m_htime==0` since
  `prevHIn` is unsigned. StarFrog configures H+V IRQ mode with `HTIME=0,
  VTIME=0` (a normal, common "once per frame at scanline 0" pattern) once at
  boot and never touches it again, so this IRQ never fired even a single
  time in a 600-frame trace. Fixed by tracking whether a scanline boundary
  was crossed this call and using that as the edge condition specifically
  when `m_htime == 0`.

These three (found together in one session, via the new `y=`/`d=`/`dbr=`
`SNESFOX_CPU_TRACE` fields plus `SNESFOX_WRAM_WATCH`) took StarFrog from
"wedged in an object dispatcher reading garbage" through a previously
zero-firing per-frame IRQ, all the way to the title screen's screen turning
on (`forcedBlank`→0), the GSU actively plotting pixels every frame
(`plotCount` incrementing continuously), real non-black framebuffer output,
and audio playing (non-zero RMS) — by frame ~900-1200 of `snap`.

## Known-fixed bug: GSU r15 auto-increment epilogue (2026-08-19)

`mainStep()`'s r15 auto-increment epilogue used `m_r[15] != pcBefore` as a
proxy for "r15 was explicitly overwritten this instruction" — but `pipe()`
itself also advances r15 (once per operand byte fetched), so *any* multi-byte
instruction (`IWT`/`LM`/`SM`/`IBT`/`LMS`/`SMS`, a branch's displacement byte,
…) already had `m_r[15] != pcBefore` for entirely legitimate reasons, and the
epilogue wrongly skipped the final `+1` every time — leaving r15 one byte
short and corrupting the *next* instruction's first operand. This silently
broke almost every real GSU program within its first few multi-byte
instructions per launch (confirmed via a minimal repro: `IWT R3,#0x0080`
immediately followed by `IWT R7,#0x0040` produced `r7=0x40F7`, not `0x0040`).
Fixed by tracking `m_pipeCallCount` per instruction and comparing against
`pcBefore + m_pipeCallCount` instead of raw `pcBefore` — see `CLAUDE.md`'s GSU
section for the full root-cause writeup (it was initially misdiagnosed as a
bug in the prefetch queue itself, `peekpipe()`/`pipe()`, which turned out to
match `ares-ref` exactly the whole time).

This one fix is what actually explains the "known remaining issue" that used
to be documented here — the colored-noise/wrong-tilemap symptom below was a
downstream *consequence* of GSU registers being corrupted mid-render, not a
separate VRAM/DMA-addressing bug. Re-verify anything below against a fresh
trace; treat it as historical context for how the bug used to look, not a
live TODO list.

## Known remaining issue (as of 2026-08-19, post-pipeline-fix)

`petesphere`/`petecube` (the title screen's rotating logo object,
`MAPS_extracted/maps/title.asm`'s `mapobj 0,0,0,90,petesphere,tit_istrat`)
now renders as a real, recognizable shape — confirmed by tracking
`framebuffer: non-black opaque pix=` across frame counts with
`./snesfox snap StarFrog/starfrog.sfc <N>`: 0 at frame 250, climbing
(2401→42560) from frame 300 to 400, then back to 0 by frame 420 and
staying there. That growth curve (screen coverage increasing roughly like
1/z as z shrinks, then the object vanishing entirely) means **the object is
translating toward the camera and passing through/past the near clip plane**
— it should stay at a constant depth (`z=90` set once at placement) and only
rotate in place, matching real hardware (confirmed by the user against bsnes
and Mesen2, where it does not drift). `tit_strat` (`endseq.asm`, the
per-frame half of the title's istrat) calls `s_add_playerz x` every frame —
a routine that adds the player's current/delta world-Z to the object's world
Z, presumably so a HUD-anchored object stays at a fixed distance in front of
a *moving* camera. Since the title screen explicitly zeroes `lastplayz`/
`pviewposz`/`al_worldz,x` before placing the object and disables player
control (`pshipflags |= psf_noctrl`), the player's Z should never actually
change here on real hardware, so `s_add_playerz` should be adding ~0 every
frame. The likely bug is that something in snesfox causes the tracked
player-Z (or whatever `s_add_playerz` reads) to drift anyway — not yet
root-caused; the concrete next step is tracing `al_worldz`/`pviewposz`/
`lastplayz`/whatever WRAM field holds player Z with `SNESFOX_WRAM_WATCH`
across frames 250-420 to see which one moves and trace that back to its
writer, the same method used for every fixed bug above.
