#include "input.hpp"

#include <SDL2/SDL.h>

#if defined(__APPLE__) || defined(_WIN32)
#include "../macOS/native_input.hpp"
#endif

#ifdef _WIN32
#include "../windows/native_gamepad.hpp"
#endif

// Controller input forks by platform below: Windows talks to XInput/DirectInput directly
// (src/windows/native_gamepad.*) rather than through SDL_GameController, because SDL only
// delivers joystick/controller state while it believes the app has OS focus — tracked via
// windows *it* created with SDL_CreateWindow. Bare mode's window is a plain native HWND
// (native_window.cpp), so SDL never sees it as focused there and silently drops every
// button/axis update (pad opens fine via SDL_GameControllerOpen, but nothing ever reads as
// pressed). Mesen2 avoids the same problem on Windows the same way (XInputManager.cpp +
// DirectInputManager.cpp, no SDL involved). macOS doesn't hit this — its SDL backend checks
// focus via NSApplication's own isActive state rather than SDL-tracked windows — so it keeps
// using SDL_GameController below.
#ifndef _WIN32

namespace {

// Lazily turns on SDL's game-controller subsystem (which pulls in the joystick subsystem too)
// on first use, rather than unconditionally in Display's constructor — bare mode on macOS
// never touches SDL video at all (see display.cpp), so this is the only place that ever needs
// it. Must keep running on whichever thread actually samples input (the dedicated emulation
// thread in bare mode, the main thread in --debug) since SDL expects joystick/controller
// polling to stay on the thread that opened the device — sampleJoy1/2 are always called from
// that same single thread for the life of a session, so this holds.
void ensureControllerSubsystem() {
    static bool initialized = false;
    if (initialized) return;
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    initialized = true;
}

// Assigns up to 2 pads to P1/P2, first-come-first-served by SDL joystick index, re-scanned on
// every call so a pad plugged in mid-session gets picked up without a restart — bare mode on
// macOS never pumps SDL events (see display.cpp's processEvents), so there's no
// SDL_CONTROLLERDEVICEADDED to react to; polling attach state here is the only option.
SDL_GameController* padFor(int playerIndex) {
    static SDL_GameController* pads[2] = {nullptr, nullptr};
    ensureControllerSubsystem();

    // Refreshes cached button/axis state for every open controller. SDL_PumpEvents()/
    // SDL_PollEvent() normally do this implicitly, but bare mode on macOS/Windows never calls
    // those, so it has to happen explicitly here instead.
    SDL_GameControllerUpdate();

    if (pads[playerIndex] && !SDL_GameControllerGetAttached(pads[playerIndex])) {
        SDL_GameControllerClose(pads[playerIndex]);
        pads[playerIndex] = nullptr;
    }
    if (!pads[playerIndex]) {
        const int otherIndex = playerIndex == 0 ? 1 : 0;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (!SDL_IsGameController(i)) continue;
            SDL_GameController* candidate = SDL_GameControllerOpen(i);
            if (!candidate) continue;
            // SDL refcounts opens of the same physical device by instance ID — if this index
            // is the pad already claimed by the other player, undo our extra refcount bump and
            // keep scanning instead of sharing one pad between both players.
            if (candidate == pads[otherIndex]) {
                SDL_GameControllerClose(candidate);
                continue;
            }
            pads[playerIndex] = candidate;
            break;
        }
    }
    return pads[playerIndex];
}

// ~37% of the full ±32767 axis range — big enough that a worn/uncalibrated stick resting
// slightly off-center never registers as a held d-pad direction, small enough to still feel
// responsive for deliberate stick movement.
constexpr int16_t kStickDeadzone = 12000;

uint16_t sampleController(SDL_GameController* pad) {
    auto held = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    uint16_t joy = 0;
    // Xbox pad buttons map onto SNES's diamond by physical position, not by letter — same
    // convention most emulators (e.g. RetroArch's default) use, since matching the on-screen
    // SNES prompts by letter would put "confirm" under a different physical finger than an
    // Xbox-pad player expects: bottom->B, right->A, left->Y, top->X.
    if (held(SDL_CONTROLLER_BUTTON_A)) joy |= 0x8000; // B (bottom)
    if (held(SDL_CONTROLLER_BUTTON_X)) joy |= 0x4000; // Y (left)
    if (held(SDL_CONTROLLER_BUTTON_BACK)) joy |= 0x2000; // Select
    if (held(SDL_CONTROLLER_BUTTON_START)) joy |= 0x1000; // Start
    if (held(SDL_CONTROLLER_BUTTON_B)) joy |= 0x0080; // A (right)
    if (held(SDL_CONTROLLER_BUTTON_Y)) joy |= 0x0040; // X (top)
    if (held(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) joy |= 0x0020; // L
    if (held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) joy |= 0x0010; // R

    const int16_t stickX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const int16_t stickY = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (held(SDL_CONTROLLER_BUTTON_DPAD_UP) || stickY < -kStickDeadzone) joy |= 0x0800;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || stickY > kStickDeadzone) joy |= 0x0400;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || stickX < -kStickDeadzone) joy |= 0x0200;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || stickX > kStickDeadzone) joy |= 0x0100;
    return joy;
}

} // namespace

#endif // !_WIN32

// Reads SDL's keyboard-state snapshot without pumping events — SDL_PumpEvents (Cocoa's
// nextEventMatchingMask under the hood) may only be called from the main thread, but this is
// called from the dedicated emulation thread in emu_cli.cpp's bare-window path. The main
// thread's own event loop (display.processEvents()) already pumps every video frame via
// SDL_PollEvent, so the keyboard-state array this reads is kept fresh regardless.
//
// Bare mode never initializes SDL's video subsystem on macOS or Windows (see docs/tickets/01
// for macOS's case), so SDL_GetKeyboardState has nothing to read there — isNativeInputActive()
// is true instead, and the platform's own native_input.mm/.cpp key-state array (kept fresh by
// its own event pump) is read instead.
//
// Keyboard and pad are OR'd together rather than the pad taking exclusive priority when
// present: a real-world Windows report (back when this went through SDL_GameController — see
// the platform fork above for why Windows no longer does) showed a wired Xbox pad opening
// successfully while its button/axis state never actually read as pressed — with the old
// early-return-on-pad-present logic that silently locked out the keyboard too, since the branch
// below never ran. OR'ing means a misbehaving/idle pad can never block the keyboard fallback, at
// the cost of only mattering if someone's mashing both at once.
uint16_t sampleJoy1(bool suppress) {
    if (suppress) return 0;
    uint16_t joy = 0;
#if defined(__APPLE__) || defined(_WIN32)
    if (isNativeInputActive()) {
        if (isNativeKeyDown(NativeKey::kB)) joy |= 0x8000; // B
        if (isNativeKeyDown(NativeKey::kY)) joy |= 0x4000; // Y
        if (isNativeKeyDown(NativeKey::kSpace)) joy |= 0x2000; // Select
        if (isNativeKeyDown(NativeKey::kReturn)) joy |= 0x1000; // Start
        if (isNativeKeyDown(NativeKey::kUp)) joy |= 0x0800; // Up
        if (isNativeKeyDown(NativeKey::kDown)) joy |= 0x0400; // Down
        if (isNativeKeyDown(NativeKey::kLeft)) joy |= 0x0200; // Left
        if (isNativeKeyDown(NativeKey::kRight)) joy |= 0x0100; // Right
        if (isNativeKeyDown(NativeKey::kA)) joy |= 0x0080; // A
        if (isNativeKeyDown(NativeKey::kX)) joy |= 0x0040; // X
        if (isNativeKeyDown(NativeKey::kL)) joy |= 0x0020; // L
        if (isNativeKeyDown(NativeKey::kR)) joy |= 0x0010; // R
    } else {
#endif
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_B]) joy |= 0x8000; // B
    if (k[SDL_SCANCODE_Y]) joy |= 0x4000; // Y
    if (k[SDL_SCANCODE_SPACE]) joy |= 0x2000; // Select
    if (k[SDL_SCANCODE_RETURN]) joy |= 0x1000; // Start
    if (k[SDL_SCANCODE_UP]) joy |= 0x0800; // Up
    if (k[SDL_SCANCODE_DOWN]) joy |= 0x0400; // Down
    if (k[SDL_SCANCODE_LEFT]) joy |= 0x0200; // Left
    if (k[SDL_SCANCODE_RIGHT]) joy |= 0x0100; // Right
    if (k[SDL_SCANCODE_A]) joy |= 0x0080; // A
    if (k[SDL_SCANCODE_X]) joy |= 0x0040; // X
    if (k[SDL_SCANCODE_L]) joy |= 0x0020; // L
    if (k[SDL_SCANCODE_R]) joy |= 0x0010; // R
#if defined(__APPLE__) || defined(_WIN32)
    }
#endif
#ifdef _WIN32
    joy |= NativeGamepad::sample(0);
#else
    if (SDL_GameController* pad = padFor(0)) joy |= sampleController(pad);
#endif
    return joy;
}

uint16_t sampleJoy2(bool suppress) {
    if (suppress) return 0;
    uint16_t joy = 0;
#if defined(__APPLE__) || defined(_WIN32)
    if (isNativeInputActive()) {
        if (isNativeKeyDown(NativeKey::k2)) joy |= 0x8000; // B
        if (isNativeKeyDown(NativeKey::k4)) joy |= 0x4000; // Y
        if (isNativeKeyDown(NativeKey::kRShift)) joy |= 0x2000; // Select
        if (isNativeKeyDown(NativeKey::kReturn)) joy |= 0x1000; // Start
        if (isNativeKeyDown(NativeKey::k7)) joy |= 0x0800; // Up
        if (isNativeKeyDown(NativeKey::k8)) joy |= 0x0400; // Down
        if (isNativeKeyDown(NativeKey::k9)) joy |= 0x0200; // Left
        if (isNativeKeyDown(NativeKey::k0)) joy |= 0x0100; // Right
        if (isNativeKeyDown(NativeKey::k1)) joy |= 0x0080; // A
        if (isNativeKeyDown(NativeKey::k3)) joy |= 0x0040; // X
        if (isNativeKeyDown(NativeKey::k5)) joy |= 0x0020; // L
        if (isNativeKeyDown(NativeKey::k6)) joy |= 0x0010; // R
    } else {
#endif
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_2]) joy |= 0x8000; // B
    if (k[SDL_SCANCODE_4]) joy |= 0x4000; // Y
    if (k[SDL_SCANCODE_RSHIFT]) joy |= 0x2000; // Select
    if (k[SDL_SCANCODE_RETURN]) joy |= 0x1000; // Start
    if (k[SDL_SCANCODE_7]) joy |= 0x0800; // Up
    if (k[SDL_SCANCODE_8]) joy |= 0x0400; // Down
    if (k[SDL_SCANCODE_9]) joy |= 0x0200; // Left
    if (k[SDL_SCANCODE_0]) joy |= 0x0100; // Right
    if (k[SDL_SCANCODE_1]) joy |= 0x0080; // A
    if (k[SDL_SCANCODE_3]) joy |= 0x0040; // X
    if (k[SDL_SCANCODE_5]) joy |= 0x0020; // L
    if (k[SDL_SCANCODE_6]) joy |= 0x0010; // R
#if defined(__APPLE__) || defined(_WIN32)
    }
#endif
#ifdef _WIN32
    joy |= NativeGamepad::sample(1);
#else
    if (SDL_GameController* pad = padFor(1)) joy |= sampleController(pad);
#endif
    return joy;
}
