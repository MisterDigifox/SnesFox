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
DebugPanel makeDebugPanel(
    const std::vector<std::string>& headerLines,
    const CPU& cpu,
    const Ppu& ppu,
    const Bus& bus,
    const std::deque<std::string>& instructionLog,
    bool paused
);
