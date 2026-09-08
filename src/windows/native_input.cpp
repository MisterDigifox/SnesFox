// Windows implementation of the ../macOS/native_input.hpp interface (declared under src/macOS/
// — see native_file_dialog.cpp's comment on that convention). Mirrors Mesen2's
// WindowsKeyManager.cpp in spirit (native key state, no SDL), but polls GetAsyncKeyState
// directly rather than installing a keyboard hook — simplest correct approach for a
// once-per-frame poll rather than a queued event stream.
#include "../macOS/native_input.hpp"

#ifdef _WIN32
#include <windows.h>

namespace {
bool g_active = false;
bool g_escapeWasDown = false;
bool g_f11WasDown = false;
bool g_escapePending = false;
bool g_f11Pending = false;
} // namespace

void installNativeKeyMonitor() {
    g_active = true;
}

void removeNativeKeyMonitor() {
    g_active = false;
    g_escapeWasDown = false;
    g_f11WasDown = false;
    g_escapePending = false;
    g_f11Pending = false;
}

bool isNativeInputActive() {
    return g_active;
}

void pumpNativeEvents() {
    // Drains the thread's message queue (feeds WndProc — window close/resize/paint/drop —
    // same as macOS's pumpNativeEvents draining NSApp's event queue via
    // nextEventMatchingMask:). Must run on the thread that created the window (the main
    // thread), same requirement SDL_PollEvent had.
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Edge-detect Escape/F11 here (once per pump call) rather than via a low-level keyboard
    // hook — GetAsyncKeyState already gives us live down/up state, we just need to remember
    // last frame's state to turn it into a one-shot "was just pressed" signal.
    const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escapeDown && !g_escapeWasDown) g_escapePending = true;
    g_escapeWasDown = escapeDown;

    const bool f11Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f11Down && !g_f11WasDown) g_f11Pending = true;
    g_f11WasDown = f11Down;
}

bool isNativeKeyDown(int virtualKeyCode) {
    return (GetAsyncKeyState(virtualKeyCode) & 0x8000) != 0;
}

bool takeNativeEscapePressed() {
    const bool v = g_escapePending;
    g_escapePending = false;
    return v;
}

bool takeNativeF11Pressed() {
    const bool v = g_f11Pending;
    g_f11Pending = false;
    return v;
}
#endif
