# SnesFox

Toolkit for SNES ROM exploration: lightweight CPU emulation with a debug UI, LoROM-oriented disassembler and round-trip reassembler, plus optional execution coverage.

Emulation notes: general-purpose DMA (`$420B`) and per-scanline HDMA (`$420C`, direct and indirect table modes) are supported for PPU B-bus targets such as per-line scrolling. The CPU loop advances **`Bus::kCyclesPerFrame`** cycles per drawn frame (**262 × 114**), matching scanline stepping in `Bus::stepPeripherals` (older builds used `30000`, which drifted ~132 cycles/frame vs the raster).

## Dependencies (macOS)

```bash
brew install sdl2
brew install sdl2_ttf
```

Adjust Include/Library paths in `build.sh` if Homebrew lives somewhere other than `/opt/homebrew`.

## Build

```bash
./build.sh
```

Produces the `snesfox` executable in the project directory.

## Usage

### Read ROM header

Print copier-header detection (512-byte `.smc` strip), cartridge metadata at the SNES-internal header offsets, LoROM/HiROM detection, title, checksum pair, and related fields:

```bash
./snesfox header smw.sfc
```

### Disassemble a ROM

```bash
./snesfox disasm smw.sfc
```

Writes `output.asm` by default. Specify the output path:

```bash
./snesfox disasm smw.sfc mydump.asm
```

### Execution coverage (optional)

Run the emulator headlessly for a number of simulated frames and record every 24-bit PC fetched as an instruction. Writes a small text file (sorted hex PCs plus a header line):

```bash
./snesfox cov smw.sfc trace.cov
```

Default duration is **600 frames**. Override with a third numeric argument:

```bash
./snesfox cov smw.sfc trace.cov 1200
```

Annotate the disassembly so PCs seen in the trace get a trailing `  ; cov` comment (stripped by `reasm`, safe for round-trip):

```bash
./snesfox cov smw.sfc trace.cov
./snesfox disasm smw.sfc output.asm trace.cov
```

Coverage reflects what **this emulator** executed in that run, not necessarily every path in the game.

### Reassemble into a ROM

```bash
./snesfox reasm output.asm out.sfc
```

Designed to round-trip the assembler text produced by `disasm` for supported dumps.

### Interactive emulation (SDL)

```bash
./snesfox emu smw.sfc
```

Opens the debug window (pause, single-step).

## Preview

![snesfox emulator preview](preview/preview.png)
