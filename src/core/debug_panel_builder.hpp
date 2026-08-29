#pragma once

#include <deque>
#include <string>
#include <vector>

class Bus;
class CPU;
class Ppu;
struct DebugPanel;

// Builds the full debug-UI panel snapshot (ROM header, CPU/PPU registers, tile sheet, S-DSP
// voices, GSU state) from the current emulator state. See display.hpp for DebugPanel's shape.
// `debugUi` gates the panels the bare (no --debug) window never shows (Tiles Viewer, GSU RAM
// viewer) — those decode the *entire* VRAM/GSU work RAM into ARGB images every single call,
// which cost real time even when nothing ever displays the result (confirmed by profiling: a
// bare-mode session was still paying full VRAM-tilesheet-decode cost every frame).
DebugPanel makeDebugPanel(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const Ppu& ppu,
    const Bus& bus,
    const std::deque<std::string>& instructionLog,
    bool paused,
    bool debugUi
);
