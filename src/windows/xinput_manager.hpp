#pragma once

// Thin XInput wrapper for Windows bare mode, mirroring Mesen2's XInputManager.cpp in spirit
// (poll all 4 XInput user slots, cache state) rather than going through SDL_GameController —
// SDL only delivers joystick/controller state while it believes the app has OS focus, tracked
// via windows *it* created with SDL_CreateWindow. Bare mode's window is a plain native HWND
// (native_window.cpp), so SDL never sees it as focused and silently drops every button/axis
// update. XInputGetState is a global, focus-agnostic query by design, so talking to it directly
// sidesteps the problem instead of working around it via an SDL hint.

#ifdef _WIN32

#include <cstdint>

namespace XInputManager {

constexpr int kMaxPads = 4; // XUSER_MAX_COUNT

struct PadState {
    bool connected = false;
    uint16_t buttons = 0; // raw XINPUT_GAMEPAD_* bitmask
    int16_t thumbLX = 0;  // -32768..32767, positive = right
    int16_t thumbLY = 0;  // -32768..32767, positive = up (opposite of SDL's LEFTY axis)
};

// Polls all 4 XInput user slots and refreshes their cached state. Call once per sample, before
// state(). Cheap for already-connected slots; disconnected slots are only re-checked
// periodically (see .cpp) since XInputGetState is known to be slow to fail when nothing is
// plugged into that slot.
void refreshAll();

const PadState& state(int userIndex);

} // namespace XInputManager

#endif
