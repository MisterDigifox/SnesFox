#pragma once

// Generic (non-Xbox) joystick/gamepad support for Windows bare mode via DirectInput8 — the
// other half of Mesen2's Windows input approach (Windows/DirectInputManager.cpp): XInput only
// recognizes Xbox-family controllers, so PlayStation-style and other generic USB/Bluetooth pads
// need DirectInput instead. A WMI-based check filters out any device XInput already claims, so
// a single Xbox pad never gets counted twice across both APIs.

#ifdef _WIN32

#include <cstdint>
#include <windows.h>

namespace DirectInputManager {

struct PadState {
    bool connected = false;
    uint32_t buttons = 0;       // bit i set == rgbButtons[i] held (up to 32 buttons)
    long x = 0, y = 0;          // DIPROP_RANGE-scaled to INT16_MIN..INT16_MAX, like XInput's axes
    DWORD pov = 0xFFFFFFFF;     // hundredths of a degree, clockwise from north; 0xFFFFFFFF = centered/no POV
};

// Creates the shared IDirectInput8 object and enumerates currently-attached non-XInput game
// controllers. Call once at startup, from the thread that owns hWnd (DirectInput's cooperative
// level is scoped to that window) — mirrors AudioOutput's DirectSound setup taking the same
// handle via Display::nativeWindowHandle(). A no-op if called more than once or if hWnd is null.
void initialize(HWND hWnd);

// Re-enumerates to pick up newly-attached devices (throttled — see .cpp) and polls every
// currently-known device's state. Call once per sample, after initialize().
void refreshAll();

int deviceCount();
const PadState& state(int deviceIndex);

} // namespace DirectInputManager

#endif
