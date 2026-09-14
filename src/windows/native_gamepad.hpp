#pragma once

// Combines XInputManager (Xbox-family pads) and DirectInputManager (everything else) into the
// same SNES-button-mapped uint16_t sampleJoy1()/sampleJoy2() (src/core/input.cpp) already
// expects from the SDL_GameController path it replaces on Windows. See xinput_manager.hpp for
// why Windows needs its own native path instead of going through SDL here.

#ifdef _WIN32

#include <cstdint>

namespace NativeGamepad {

// playerIndex is 0 or 1. Refreshes both backends and returns that player's pad state already
// mapped onto the SNES button bitmask (same bit layout sampleController() in input.cpp used:
// 0x8000=B 0x4000=Y 0x2000=Select 0x1000=Start 0x0800=Up 0x0400=Down 0x0200=Left 0x0100=Right
// 0x0080=A 0x0040=X 0x0020=L 0x0010=R), or 0 if no pad is assigned to that player.
uint16_t sample(int playerIndex);

} // namespace NativeGamepad

#endif
