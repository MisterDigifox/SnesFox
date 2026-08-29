#pragma once

#include <cstdint>
#include <string>

// CLI entry point for the `snap` subcommand: runs a ROM headlessly for `frames` video
// frames and dumps PPU/audio/GSU heuristics plus a /tmp/snap.ppm framebuffer capture.
int runPpuSnap(const std::string& romPath, uint64_t frames);
