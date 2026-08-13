#pragma once

// Runs targeted regression tests against CPU/Bus cycle-timing behavior (no ROM required).
// Returns 0 if all tests pass, 1 if any failed.
int runCpuSelfTests();
