#pragma once

// Runs targeted regression tests against Sdsp's envelope generator and BRR
// resampling (no ROM/CPU required). Returns 0 if all tests pass, 1 otherwise.
int runSdspSelfTests();
