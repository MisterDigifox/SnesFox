#pragma once

#include <cstdint>
#include <string>

// CLI entry point for the `cov` subcommand: runs a ROM headlessly for `frames` video
// frames, recording every unique PC fetched, and writes the sorted set to `covPath` in the
// `snesfox-cov-v1` text format consumed by `disasm`'s coverage-annotation option.
int runCov(const std::string& romPath, const std::string& covPath, uint64_t frames);
