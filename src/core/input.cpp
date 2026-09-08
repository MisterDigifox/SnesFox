#include "input.hpp"

#include <SDL2/SDL.h>

#if defined(__APPLE__) || defined(_WIN32)
#include "../macOS/native_input.hpp"
#endif

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
uint16_t sampleJoy1(bool suppress) {
    if (suppress) return 0;
#if defined(__APPLE__) || defined(_WIN32)
    if (isNativeInputActive()) {
        uint16_t joy = 0;
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
        return joy;
    }
#endif
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    uint16_t joy = 0;
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
    return joy;
}

uint16_t sampleJoy2(bool suppress) {
    if (suppress) return 0;
#if defined(__APPLE__) || defined(_WIN32)
    if (isNativeInputActive()) {
        uint16_t joy = 0;
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
        return joy;
    }
#endif
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    uint16_t joy = 0;
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
    return joy;
}
