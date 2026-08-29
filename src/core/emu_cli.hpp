#pragma once

#include <string>

// CLI entry point for the interactive emulator (bare and --debug modes): opens the SDL
// window/audio device and runs the main CPU/PPU/APU loop, reloading a new ROM in place
// whenever the Load button or File > Open ROM… menu item is used. `writeTrace` dumps every
// executed CPU instruction to cpu.asm; `debugUi` toggles the full toolbar/panels layout.
int runEmu(const std::string& initialRomPath, bool writeTrace, bool debugUi);
