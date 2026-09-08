#pragma once

// Cocoa keyboard + event pump for macOS bare mode, replacing SDL_PollEvent/
// SDL_GetKeyboardState — those require SDL's video subsystem, which bare mode no longer
// initializes (see docs/tickets/01-remove-sdl-video-init.md and 02-native-event-loop.md).
// Mirrors Mesen2's MacOSKeyManager.mm, which does the same thing via
// [NSEvent addLocalMonitorForEventsMatchingMask:...] instead of any SDL API.

// Installs a local NSEvent monitor tracking key down/up state. Call once at startup.
void installNativeKeyMonitor();
void removeNativeKeyMonitor();

// True between installNativeKeyMonitor()/removeNativeKeyMonitor() — lets input.cpp fall back
// to the SDL path (--debug mode) when this hasn't been installed.
bool isNativeInputActive();

// Drains and dispatches pending AppKit events (feeds the key monitor above, and anything a
// window delegate/drag-and-drop handler needs) without blocking. Call once per frame from the
// main thread.
void pumpNativeEvents();

// Physical-key-position query, keyed by a macOS virtual keycode (kVK_* — same spirit as
// SDL_SCANCODE_*: physical position, not the printed character, so it matches regardless of
// keyboard layout).
bool isNativeKeyDown(int virtualKeyCode);

// Edge-triggered (consumed on read) — true only once per actual key-down, not held-repeat.
bool takeNativeEscapePressed();
bool takeNativeF11Pressed();

// Virtual keycodes for the specific physical keys sampleJoy1()/sampleJoy2() (src/core/input.cpp)
// already map to via SDL_SCANCODE_* — same keys, same positions, just each native platform's
// own numbering instead of SDL's, so no behavior changes for players using the emulator.
namespace NativeKey {
#ifdef __APPLE__
constexpr int kA      = 0x00;
constexpr int kB      = 0x0B;
constexpr int kX      = 0x07;
constexpr int kY      = 0x10;
constexpr int kL      = 0x25;
constexpr int kR      = 0x0F;
constexpr int kReturn = 0x24;
constexpr int kSpace  = 0x31;
constexpr int kUp     = 0x7E;
constexpr int kDown   = 0x7D;
constexpr int kLeft   = 0x7B;
constexpr int kRight  = 0x7C;
constexpr int k1      = 0x12;
constexpr int k2      = 0x13;
constexpr int k3      = 0x14;
constexpr int k4      = 0x15;
constexpr int k5      = 0x17;
constexpr int k6      = 0x16;
constexpr int k7      = 0x1A;
constexpr int k8      = 0x1C;
constexpr int k9      = 0x19;
constexpr int k0      = 0x1D;
constexpr int kRShift = 0x3C;
#elif defined(_WIN32)
// Windows virtual-key codes (VK_*, per winuser.h) — 'A'-'Z'/'0'-'9' equal their ASCII
// uppercase/digit codes on Windows, so no <windows.h> include is needed just for these
// literals (native_input.cpp includes it for the VK_* constants used elsewhere).
constexpr int kA      = 0x41; // 'A'
constexpr int kB      = 0x42; // 'B'
constexpr int kX      = 0x58; // 'X'
constexpr int kY      = 0x59; // 'Y'
constexpr int kL      = 0x4C; // 'L'
constexpr int kR      = 0x52; // 'R'
constexpr int kReturn = 0x0D; // VK_RETURN
constexpr int kSpace  = 0x20; // VK_SPACE
constexpr int kUp     = 0x26; // VK_UP
constexpr int kDown   = 0x28; // VK_DOWN
constexpr int kLeft   = 0x25; // VK_LEFT
constexpr int kRight  = 0x27; // VK_RIGHT
constexpr int k1      = 0x31; // '1'
constexpr int k2      = 0x32;
constexpr int k3      = 0x33;
constexpr int k4      = 0x34;
constexpr int k5      = 0x35;
constexpr int k6      = 0x36;
constexpr int k7      = 0x37;
constexpr int k8      = 0x38;
constexpr int k9      = 0x39;
constexpr int k0      = 0x30;
constexpr int kRShift = 0xA1; // VK_RSHIFT
#endif
} // namespace NativeKey
