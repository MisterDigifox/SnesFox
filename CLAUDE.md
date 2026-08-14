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

**CPU (`cpu.hpp`/`.cpp` + `opcodes.cpp`)**: 65C816 core (native + emulation mode). `opcodes.cpp` holds three *separate* 256-entry decode tables reused across otherwise-unrelated processors: `cpuOpcodesTable` (65C816, executed by `CPU::step`), `apuOpcodesTable` (SPC700, executed by `Spc700::step`), and `gsuOpcodesTable` (Super FX/GSU — decode table only; there is no GSU execution engine yet, so Super FX ROMs won't actually run under `emu` — there's no Super FX fixture ROM in this repo to try it on). `AddrMode` in `opcodes.hpp` intentionally mixes addressing modes for all three ISAs (65816 `DirectPage*`/`Absolute*`, SPC-specific `Spc*` modes, GSU's `GsuImmediateWord`) rather than having per-CPU enums.

**PPU (`ppu.hpp`/`.cpp`)**: register file for `$21xx` (BG modes 0–7 incl. Mode 7 affine/perspective, windowing, color math, OAM/sprites) plus a scanline renderer that composites BG/OBJ/Mode-7 layers into a 256×224 ARGB framebuffer consumed directly by `Display`/`snap`.

**Audio (`apu.hpp`/`.cpp`, `spc700.hpp`/`.cpp`, `sdsp.hpp`/`.cpp`)**: `APU` wraps a 64 KiB ARAM, the `Spc700` core (booting from the real IPL at `$FFC0`), and the `Sdsp` synthesizer (BRR decode, Gaussian mix, 8 voices → stereo PCM). Main-CPU `$2140`–`$2143` are the CPU→SPC latches, which the SPC sees as `$00F4`–`$00F7`; SPC writes to those same addresses are the SPC→CPU side read back at `$2140`–`$2143`. Scheduling between the two CPUs uses a coarse 12:7 cycle-carry ratio (SNES CPU clock vs the SMP's 1.024 MHz). Debug env vars: `SNESFOX_SPC_LOG=1` logs illegal opcodes/`STOP`; `SNESFOX_SPC_STRICT=1` halts on undefined opcodes instead of treating them as a 2-cycle NOP; `SNESFOX_APU_PORTS_ZERO=1` zeroes port latches at reset for ROMs that expect that before the IPL handshake.

**DMA/HDMA (`dma.hpp`/`.cpp`)**: general-purpose DMA via `$420B` and per-scanline HDMA via `$420C` (direct and indirect table modes), called from `Bus::stepPeripherals` once per scanline — this is what drives effects like per-line scrolling in the `ParallaxScrolling.sfc` fixture and in `SplitScrolling.sfc` (built from the sibling untracked `SplitScrolling/` PVSnesLib source project, not committed as a prebuilt ROM — see `tests/ppu_test.cpp` for the window/OBJ-address regressions it originally caught).

**ROM/header (`rom.hpp`/`.cpp`, `header.hpp`/`.cpp`)**: `Rom` strips an optional 512-byte copier header and exposes raw bytes; `HeaderParser` reads the SNES-internal header fields (title, map mode, ROM/SRAM size, checksum pair, cartridge chip incl. SuperFX/SA-1 detection) independent of the emulation core, used by `header` and at `Bus` construction to pick LoROM vs HiROM addressing.

**Disasm/reasm (`disasm_dump.hpp`/`.cpp`, `reasm.hpp`/`.cpp`)**: `dumpRomAsAsmFull` walks the ROM recursively from the reset vector, auto-labels code, and dumps unreached bytes as `.db`; it can annotate lines with `; cov` from a `cov`-produced coverage file (stripped back out by `reasm`, so annotated dumps still round-trip). `reassembleDumpAsmToRom(File)` is a from-scratch mini-assembler for exactly the dialect `disasm_dump` emits — it is not a general-purpose 65816 assembler.

**Debug UI (`display.hpp`/`.cpp`)**: Dear ImGui (vendored under `imgui/`+`imgui/backends/`, SDL2 renderer backend) rather than a raw SDL2 text panel — `applyModernDarkTheme()` sets a custom dark/rounded theme. Layout: a left panel with ROM/CPU/PPU state, an instruction log, and a Pause/Resume/Step/Next-Frame/Reset toolbar (`drawControls()`); the scaled 256×224 framebuffer centered; a right panel rendering all 16 CGRAM palettes as clickable swatch grids with a popup RGB color editor that writes live via `Bus::ppu().setCgramEntry()` (wired in `snesfox_app.cpp`); and a bottom full-VRAM Tiles Viewer with hover tooltips showing tile index and VRAM address. `Display::wantsKeyboardCapture()` suppresses the emulated joypad while an ImGui widget (e.g. the palette editor's text fields) has focus.

## Fixture ROMs

The root directory holds prebuilt `.sfc` test ROMs (mostly PVSnesLib demos: `Mode0`/`Mode1`/`Mode7`/`Mode7Affine`/`Mode7Perspective`/`ParallaxScrolling`/`Transparency`/`AnimatedSprite`/`Logo*`/`hello_world`/`music`) used as fixtures when developing/checking specific PPU, DMA, or APU features. `music.sfc` and `SplitScrolling.sfc` are each built from a sibling PVSnesLib source project (`music/`, `SplitScrolling/` — both untracked, containing their own `Makefile`+`src/`); `SplitScrolling.sfc` itself isn't committed, so rebuild it from `SplitScrolling/` (`make`, requires `PVSNESLIB_HOME`) before relying on it — it's a DBZ Super Butouden 1 battle-screen romhack used to regression-test HDMA windowing and OBJ name-base addressing (see `tests/ppu_test.cpp`). `tools/check_bg3_chr.py` (invoked from `check.sh`) is a read-only heuristic check against `Mode1BG3HighPriority.sfc`; `tools/gen_apu_opcodes_cpp.py` generates SPC700 opcode-table rows from the Sony SPC700 manual text.

`ares-ref/` is an empty scaffold (`component/`, `gsu/`, `sfc/coprocessor/superfx`, `superfx/`, not git-tracked) meant to hold reference sources from the [ares](https://ares-emulator.net/) emulator when doing GSU/Super FX work — currently unpopulated. `third_party/gme/` and `docs/` are similarly empty and unreferenced by any build or source file. `preview/preview.png` is the only one actually used — it's the screenshot embedded in `README.md`.
