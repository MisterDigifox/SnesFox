# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SnesFox: a from-scratch SNES toolkit in C++20 — a 65C816 CPU/PPU/APU emulator with a Dear ImGui debug UI, plus a LoROM-oriented disassembler and a reassembler that round-trips the disassembler's output back into byte-identical ROMs. Single flat source tree, no external framework; only external dependency is SDL2 — Dear ImGui is vendored in-tree under `imgui/` and compiled straight into the binary.

## Build

```bash
./build.sh
```

Compiles every `*.cpp` in the repo root plus `tests/*.cpp` and the vendored `imgui/*.cpp`/`imgui/backends/*.cpp` in one `clang++ -std=c++20` invocation into the single `snesfox` binary (also committed at repo root), then ad-hoc codesigns it on macOS so local runs don't get silently killed (`zsh: killed` symptom). If SDL2 isn't at `/opt/homebrew/opt/...`, edit the `-I`/`-L` paths in `build.sh` directly — there is no configure step or CMake.

`tests/cpu_test.cpp`/`ppu_test.cpp`/`sdsp_test.cpp` are hand-written assertion-style regression tests that drive `CPU`/`Ppu`/`Sdsp` public APIs directly (no framework, no ROM/`Bus` needed) and compile straight into the main binary — run them with `./snesfox selftest` (calls `runCpuSelfTests()`/`runPpuSelfTests()`/`runSdspSelfTests()`, nonzero exit on any failure). Beyond that, verification is:

```bash
./check.sh    # opcode-table coverage vs cpu.cpp's switch, plus an optional BG3 CHR heuristic
./release.sh  # build → disasm hello_world.sfc → reasm → cmp against the original ROM (round-trip test)
```

`check.sh` diffs the opcodes declared in `opcodes.cpp`'s `cpuOpcodesTable` against the `case 0x..` labels actually implemented in `cpu.cpp::step()` — run it after adding/removing 65C816 opcodes. `release.sh`'s final `cmp` is the closest thing to a regression test: if `disasm`+`reasm` no longer reproduces `hello_world.sfc` byte-for-byte, something in the disassembler/reassembler pipeline broke.

## CLI usage

```bash
./snesfox emu <rom.sfc>                              # SDL debug window: pause, single-step
./snesfox snap <rom.sfc> [frames]                     # headless PPU/VRAM heuristic dump
./snesfox header <rom.sfc>                            # parse + print SNES header, LoROM/HiROM detect
./snesfox cov <rom.sfc> <coverage.out> [frames]       # headless run, record every PC fetched as an instruction
./snesfox disasm <rom.sfc> [output.asm [coverage.out]] # full recursive disassembly (annotate with coverage)
./snesfox reasm <input.asm> [output.sfc]              # reassemble disasm output back into a ROM
./snesfox selftest                                    # run in-binary CPU/PPU/S-DSP regression tests, no ROM needed
```

A bare `./snesfox` (no subcommand) only prints usage and exits 1 — always pass one of the above.

## Architecture

**Dispatch**: `main.cpp` → `SnesFoxApp::run` (`snesfox_app.cpp`) parses `argv[1]` as the subcommand and wires together `Rom`, `Bus`, `CPU`, `Ppu`, `Display`, disasm/reasm as needed. All the emulation subcommands (`emu`, `snap`, `cov`) share the same core loop: step `CPU`, call `Bus::stepPeripherals`, deliver NMI/IRQ if raised.

**Memory/IO hub (`bus.hpp`/`.cpp`)**: `Bus` owns WRAM, SRAM, and instances of `APU`, `Dma`, `Ppu`; it is the single `read()`/`write()`/`readReg()`/`writeReg()` router for the full 24-bit address space (LoROM/HiROM mapping, PPU/APU/DMA MMIO windows, WRAM ports, joypad, hardware multiply/divide unit, HV counters/IRQ). `Bus::stepPeripherals(totalCycles)` is the scanline/frame scheduler: it advances the H/V counters against `kCyclesPerScanline` (114) and `kScanlinesPerFrame` (262), drives `Ppu::renderScanline`, runs DMA/HDMA, ticks the APU, and returns whether VBlank/NMI should fire this call. `kCyclesPerFrame` (= 114×262) is the number of CPU cycles `emu`/`cov` advance per drawn frame — this must stay in lock-step with the scanline math or timing drifts (an older `30000`-cycle constant used to drift ~132 cycles/frame).

**CPU (`cpu.hpp`/`.cpp` + `opcodes.cpp`)**: 65C816 core (native + emulation mode). `opcodes.cpp` holds two *separate* 256-entry decode tables reused across otherwise-unrelated processors: `cpuOpcodesTable` (65C816, executed by `CPU::step`) and `apuOpcodesTable` (SPC700, executed by `Spc700::step`) — a third table, `gsuOpcodesTable`, is declared in `opcodes.hpp` but is dead code with no definition or references anywhere (the real GSU decode/execution engine is `gsu.cpp`'s `instruction()` switch, see below). `AddrMode` in `opcodes.hpp` intentionally mixes addressing modes for all three ISAs (65816 `DirectPage*`/`Absolute*`, SPC-specific `Spc*` modes, GSU's `GsuImmediateWord`) rather than having per-CPU enums.

`CPU` tracks two parallel cycle counters: `cycles()` (used for scanline/NMI/DMA/HDMA scheduling cadence, rounded to a whole unit per instruction) and `fineCycles()` (kept at un-rounded "×8" resolution, used only to derive `Bus`'s H-counter — see `fineCycles()`'s doc comment). The extra precision matters for H/V-counter (`$213C`/`$213D`) polling loops: rounding per instruction throws away the fractional difference a single fixed-6-cycle I/O access makes against 8-cycle SlowROM code, which is enough to make a fully-deterministic polling loop alias onto the same wrong, finite set of counter values forever instead of eventually landing on the value it's waiting for. Any code path that adds to `m_cycles` (interrupt dispatch, WAI, DMA/HDMA cycle-stealing) must add the matching `m_fineCycles` amount too, or the two counters silently desync.

**PPU (`ppu.hpp`/`.cpp`)**: register file for `$21xx` (BG modes 0–7 incl. Mode 7 affine/perspective, windowing, color math, OAM/sprites) plus a scanline renderer that composites BG/OBJ/Mode-7 layers into a 256×224 ARGB framebuffer consumed directly by `Display`/`snap`.

**Audio (`apu.hpp`/`.cpp`, `spc700.hpp`/`.cpp`, `sdsp.hpp`/`.cpp`)**: `APU` wraps a 64 KiB ARAM, the `Spc700` core (booting from the real IPL at `$FFC0`), and the `Sdsp` synthesizer (BRR decode, Gaussian mix, 8 voices → stereo PCM). Main-CPU `$2140`–`$2143` are the CPU→SPC latches, which the SPC sees as `$00F4`–`$00F7`; SPC writes to those same addresses are the SPC→CPU side read back at `$2140`–`$2143`. Scheduling between the two CPUs uses a coarse 12:7 cycle-carry ratio (SNES CPU clock vs the SMP's 1.024 MHz). Debug env vars: `SNESFOX_SPC_LOG=1` logs illegal opcodes/`STOP`; `SNESFOX_SPC_STRICT=1` halts on undefined opcodes instead of treating them as a 2-cycle NOP; `SNESFOX_APU_PORTS_ZERO=1` zeroes port latches at reset for ROMs that expect that before the IPL handshake.

**GSU / Super FX (`gsu.hpp`/`.cpp`)**: a real, executing Super FX coprocessor core (~1200 lines, structurally ported from ares's `component/processor/gsu`+`sfc/coprocessor/superfx`) — register file, full instruction dispatch (`GSU::instruction()`), ROM/RAM buffer timing, pixel-plot cache, cache RAM, MMIO at `$3000-$34FF`. `Bus` bridges it in via `BusGsuHost` (`bus.cpp`): GSU work RAM lives at banks `$70`/`$71` (routed through the same `Bus::read`/`write` the main CPU sees), GSU's own ROM view goes through `Bus::gsuReadRom()` (LoROM half-bank formula for banks `$00-$3F`: `((addr&0x3F0000)>>1)|(addr&0x7FFF)` — each 64KB CPU bank mirrors the same 32KB ROM chunk into both halves; banks `$40-$5F` are a direct/linear mirror), and `Bus::stepPeripherals` calls `m_gsu.run()` every CPU step, clock-scaled by `$3039`'s CLSR bit (4x/8x). Debug env vars: `SNESFOX_GSU_TRACE=<N>` traces the first N GSU instructions executed while `SFR.GO` is set; `SNESFOX_GSU_IO=1` traces CPU-side `$3000-$303B` register access; `SNESFOX_CPU_TRACE=<N>` + `SNESFOX_CPU_TRACE_ALWAYS=1` traces main-CPU instructions unconditionally (without `_ALWAYS` it only fires while the GSU is running). See the `emulate-gsu-starfrog` skill (`.claude/skills/`) for the debugging methodology and known-fixed/remaining bugs found working against the `StarFrog/` fixture below.

**DMA/HDMA (`dma.hpp`/`.cpp`)**: general-purpose DMA via `$420B` and per-scanline HDMA via `$420C` (direct and indirect table modes), called from `Bus::stepPeripherals` once per scanline — this is what drives effects like per-line scrolling in the `ParallaxScrolling.sfc` fixture and in `SplitScrolling.sfc` (built from the sibling untracked `SplitScrolling/` PVSnesLib source project, not committed as a prebuilt ROM — see `tests/ppu_test.cpp` for the window/OBJ-address regressions it originally caught). Both genuinely steal CPU cycles matching real hardware (`Dma::trigger`/`beginHdmaFrame`/`runHdmaForScanline` return the master-clock cost — 8 cycles/byte plus fixed overhead per the formulas in `ares-ref/sfc/cpu/dma.cpp` — which `CPU::step()`/`Bus` fold into both cycle counters above); this matters for any timing-sensitive polling loop running near a large DMA transfer, not just GSU titles.

**ROM/header (`rom.hpp`/`.cpp`, `header.hpp`/`.cpp`)**: `Rom` strips an optional 512-byte copier header and exposes raw bytes; `HeaderParser` reads the SNES-internal header fields (title, map mode, ROM/SRAM size, checksum pair, cartridge chip incl. SuperFX/SA-1 detection) independent of the emulation core, used by `header` and at `Bus` construction to pick LoROM vs HiROM addressing.

**Disasm/reasm (`disasm_dump.hpp`/`.cpp`, `reasm.hpp`/`.cpp`)**: `dumpRomAsAsmFull` walks the ROM recursively from the reset vector, auto-labels code, and dumps unreached bytes as `.db`; it can annotate lines with `; cov` from a `cov`-produced coverage file (stripped back out by `reasm`, so annotated dumps still round-trip). `reassembleDumpAsmToRom(File)` is a from-scratch mini-assembler for exactly the dialect `disasm_dump` emits — it is not a general-purpose 65816 assembler.

**Debug UI (`display.hpp`/`.cpp`)**: Dear ImGui (vendored under `imgui/`+`imgui/backends/`, SDL2 renderer backend) rather than a raw SDL2 text panel — `applyModernDarkTheme()` sets a custom dark/rounded theme. Layout: a left panel with ROM/CPU/PPU state, an instruction log, and a Pause/Resume/Step/Next-Frame/Reset toolbar (`drawControls()`); the scaled 256×224 framebuffer centered; a right panel rendering all 16 CGRAM palettes as clickable swatch grids with a popup RGB color editor that writes live via `Bus::ppu().setCgramEntry()` (wired in `snesfox_app.cpp`); and a bottom full-VRAM Tiles Viewer with hover tooltips showing tile index and VRAM address. Directly below the Tiles Viewer, a GSU RAM Viewer (only shown when `panel.hasGsu`) decodes whatever bitplane framebuffer the Super FX chip last plotted into its work RAM ($70/$71) at the current SCBR/RAMBR/SCMR — `decodeGsuRam()` (`snesfox_app.cpp`) re-derives the exact same tile-address (`cn`) and bit-plane-interleave formulas as `GSU::rpix`/`flushPixelCache` (`gsu.cpp`) via `Bus::gsuWorkRam()` and `GSU::scbr()`/`rambr()`/`scmr()`/`porObj()`/`screenHeight()`, cropping the decoded 256×256 buffer down to the active screen mode's real size (128×128/160/192, or 256×256 in OBJ mode); hovering shows pixel coords plus the SCBR/RAMBR/bpp that produced them. `Display::wantsKeyboardCapture()` suppresses the emulated joypad while an ImGui widget (e.g. the palette editor's text fields) has focus.

## Fixture ROMs

The root directory holds prebuilt `.sfc` test ROMs (mostly PVSnesLib demos: `Mode0`/`Mode1`/`Mode7`/`Mode7Affine`/`Mode7Perspective`/`ParallaxScrolling`/`Transparency`/`AnimatedSprite`/`Logo*`/`hello_world`/`music`) used as fixtures when developing/checking specific PPU, DMA, or APU features. `music.sfc` and `SplitScrolling.sfc` are each built from a sibling PVSnesLib source project (`music/`, `SplitScrolling/` — both untracked, containing their own `Makefile`+`src/`); `SplitScrolling.sfc` itself isn't committed, so rebuild it from `SplitScrolling/` (`make`, requires `PVSNESLIB_HOME`) before relying on it — it's a DBZ Super Butouden 1 battle-screen romhack used to regression-test HDMA windowing and OBJ name-base addressing (see `tests/ppu_test.cpp`). `tools/check_bg3_chr.py` (invoked from `check.sh`) is a read-only heuristic check against `Mode1BG3HighPriority.sfc`; `tools/gen_apu_opcodes_cpp.py` generates SPC700 opcode-table rows from the Sony SPC700 manual text.

`StarFrog/` (sibling directory, untracked) is a real Super FX game — the leaked/renamed Star Fox source (see `StarFrog/CLAUDE.md`), buildable via its own DOSBox-era toolchain into `StarFrog/starfrog.sfc`, which *is* checked in and ready to use as the GSU core's real-world regression target without rebuilding. This particular checkout has been edited to boot straight into its title screen and stay there forever (every other screen is unreachable) — see the `emulate-gsu-starfrog` skill for how to use it.

`StarFox/` (sibling directory, untracked, own nested git repo) is the leaked
Argonaut source for the **complete, unmodified** Star Fox — a broader fixture
than `StarFrog/` above (see the `emulate-gsu-starfox` skill for how they
differ and when to use which). Buildable via `StarFox/build.sh` (DOSBox-X)
into `StarFox/starfox.sfc`, which is checked in and ready to use. Inside
`StarFox/SG_extracted/` (git-tracked, human-readable source extraction):
**`bank1.asm` is the entire GSU program** — everything else in the tree is
65816 main-CPU code. A `bank 1`/`bankend 1` block wraps a `mario on`/
`mario off` toggle (Argonaut's `SASMX` assembler directive that switches from
65816 to GSU instruction encoding) which `incfile`s ~20 `.mc` files in a
fixed order (`mvars.mc`, `mmacs.mc`, `mshtab.mc`, `mmaths.mc`, `mwrot.mc`,
`mwcrot.mc`, `mobj.mc`, `mclip.mc`, `mdrawc.mc`, `mdrawp.mc`, `msprite.mc`,
`mgdots.mc`, `mcircle.mc`, `mdrawlis.mc`, `mdecru.mc`, `mtxtprt.mc`,
`mplanet.mc`, `mdsprite.mc`, `mpart.mc`, `mbumwipe.mc`, `mhud.mc`) — this is
the entire shared GSU microcode program (used for both gameplay and the
title screen), assembling into ROM bank `$01`, which matches exactly what
`snesfox`'s own GSU core reports at runtime (`GSU: ... pc=$0x01:xxxx`,
`PBR=$01` in `./snesfox snap`) — an independent cross-check that the
emulator's bank/PBR handling lands in the right place. The SPC700 side is
the opposite: there is **no readable SPC700 assembly anywhere in this leak**
— `sound.asm` is only the 65816-side driver (`bootapu #snd_title`/
`#snd_map`/etc., uploading a numbered blob into ARAM and booting it); the
actual SPC700 machine code (sound-engine driver plus song/sample data,
combined) is opaque binary in `snd/*.bin` (one file per song/sound-bank),
pulled in via `incbins.asm` — Argonaut evidently built the sound engine with
a separate tool and just linked the compiled result in, so unlike the GSU
program there's nothing to cross-reference here against source.

`ares-ref/` (untracked) holds real vendored reference source from the [ares](https://ares-emulator.net/) emulator — `component/processor/gsu/` and `sfc/coprocessor/superfx/` (the GSU core) plus `sfc/cpu/` (65816 core, including DMA/HDMA timing) — pulled in to diff `gsu.cpp`/`dma.cpp`/`cpu.cpp` against ground truth rather than relying on memory of the ISA/timing rules; see the `emulate-gsu-starfrog` skill for the fetch script if more of it is needed (raw.githubusercontent.com is rate-limited in this environment — use the GitHub git-blob API instead). `third_party/gme/` and `docs/` are empty and unreferenced by any build or source file. `preview/preview.png` is the only one actually used — it's the screenshot embedded in `README.md`.
