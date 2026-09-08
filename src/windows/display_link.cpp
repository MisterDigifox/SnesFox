// Windows implementation of the ../macOS/display_link.hpp interface (declared under src/macOS/
// — see native_file_dialog.cpp's comment on that convention). DwmFlush() is Windows' equivalent
// primitive to CVDisplayLink for this purpose: it blocks the calling thread until the next DWM
// composition pass (effectively the next vsync), no Direct3D device or swap chain required —
// see release-emu-binary-windows.sh / release-game-binary-windows.sh for the added -ldwmapi.
#include "../macOS/display_link.hpp"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>

void startDisplayLink() {}
void stopDisplayLink() {}

void waitForVsync() {
    // DwmFlush() fails if DWM composition isn't running (e.g. some remote desktop sessions,
    // or composition explicitly disabled) — degrade to a fixed ~60Hz sleep rather than spin
    // the emulation loop at unthrottled speed in that case.
    if (FAILED(DwmFlush())) {
        Sleep(16);
    }
}
#endif
