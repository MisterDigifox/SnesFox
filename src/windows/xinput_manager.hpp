#pragma once

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
