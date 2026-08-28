#include "input.hpp"

#include <SDL2/SDL.h>

uint16_t sampleJoy1(bool suppress) {
    if (suppress) return 0;
    SDL_PumpEvents();
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
    SDL_PumpEvents();
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
