#pragma once

// Runs targeted regression tests against Ppu's public register API (no ROM required).
// Returns 0 if all tests pass, 1 if any failed.
int runPpuSelfTests();
