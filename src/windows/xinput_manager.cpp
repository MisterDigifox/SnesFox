#include "xinput_manager.hpp"

#ifdef _WIN32

#include <windows.h>
#include <xinput.h>

namespace XInputManager {

namespace {

PadState g_pads[kMaxPads];
bool g_connected[kMaxPads] = {false, false, false, false};

// XInputGetState takes noticeably longer to return when the slot is empty than when it's
// occupied (a long-documented XInput quirk, still true on modern Windows) — polling all 4 empty
// slots every single emulated frame would burn a real slice of the ~16ms frame budget for
// nothing. Only rechecking disconnected slots this often (~2s at 60fps) still picks up a
// controller plugged in mid-session promptly, without paying that cost every frame.
constexpr int kRescanEveryNCalls = 120;
int g_callsSinceRescan = 0;

} // namespace

void refreshAll() {
    const bool rescanDisconnected = (g_callsSinceRescan++ % kRescanEveryNCalls) == 0;
    for (DWORD i = 0; i < static_cast<DWORD>(kMaxPads); i++) {
        if (!g_connected[i] && !rescanDisconnected) continue;

        XINPUT_STATE raw{};
        if (XInputGetState(i, &raw) == ERROR_SUCCESS) {
            g_connected[i] = true;
            g_pads[i].connected = true;
            g_pads[i].buttons = raw.Gamepad.wButtons;
            g_pads[i].thumbLX = raw.Gamepad.sThumbLX;
            g_pads[i].thumbLY = raw.Gamepad.sThumbLY;
        } else {
            g_connected[i] = false;
            g_pads[i] = PadState{};
        }
    }
}

const PadState& state(int userIndex) {
    return g_pads[userIndex];
}

} // namespace XInputManager

#endif
