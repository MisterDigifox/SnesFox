#include "native_gamepad.hpp"

#ifdef _WIN32

#include <xinput.h>

#include "direct_input_manager.hpp"
#include "xinput_manager.hpp"

namespace NativeGamepad {

namespace {

// ~37% of the full ±32767 axis range — big enough that a worn/uncalibrated stick resting
// slightly off-center never registers as a held d-pad direction, small enough to still feel
// responsive for deliberate stick movement. Same value the SDL_GameController path used before
// (see input.cpp's kStickDeadzone for non-Windows platforms).
constexpr int16_t kStickDeadzone = 12000;

enum class Source { None, XInput, DirectInput };
struct Slot {
    Source source = Source::None;
    int index = -1;
};
Slot g_slots[2];

bool slotConnected(const Slot& slot) {
    switch (slot.source) {
        case Source::XInput: return XInputManager::state(slot.index).connected;
        case Source::DirectInput: return DirectInputManager::state(slot.index).connected;
        case Source::None: return false;
    }
    return false;
}

// Sticky first-come-first-served assignment across both backends' connected devices, same
// dedup rationale the SDL-based padFor() it replaces used: without this, unplugging player 1's
// pad could silently hand player 2's pad to player 1 mid-session.
void assign(int playerIndex) {
    Slot& mine = g_slots[playerIndex];
    if (mine.source != Source::None && !slotConnected(mine)) mine = Slot{};
    if (mine.source != Source::None) return;

    const Slot& other = g_slots[playerIndex == 0 ? 1 : 0];
    for (int i = 0; i < XInputManager::kMaxPads; i++) {
        if (other.source == Source::XInput && other.index == i) continue;
        if (XInputManager::state(i).connected) {
            mine = Slot{Source::XInput, i};
            return;
        }
    }
    for (int i = 0; i < DirectInputManager::deviceCount(); i++) {
        if (other.source == Source::DirectInput && other.index == i) continue;
        if (DirectInputManager::state(i).connected) {
            mine = Slot{Source::DirectInput, i};
            return;
        }
    }
}

uint16_t sampleXInput(const XInputManager::PadState& pad) {
    auto held = [&](uint16_t bit) { return (pad.buttons & bit) != 0; };
    uint16_t joy = 0;
    // Same physical-position mapping the SDL path used (bottom->B, right->A, left->Y, top->X)
    // — XInput's A/B/X/Y naming already matches SDL_GameController's 1:1 since both describe
    // the same physical Xbox pad layout.
    if (held(XINPUT_GAMEPAD_A)) joy |= 0x8000; // B (bottom)
    if (held(XINPUT_GAMEPAD_X)) joy |= 0x4000; // Y (left)
    if (held(XINPUT_GAMEPAD_BACK)) joy |= 0x2000; // Select
    if (held(XINPUT_GAMEPAD_START)) joy |= 0x1000; // Start
    if (held(XINPUT_GAMEPAD_B)) joy |= 0x0080; // A (right)
    if (held(XINPUT_GAMEPAD_Y)) joy |= 0x0040; // X (top)
    if (held(XINPUT_GAMEPAD_LEFT_SHOULDER)) joy |= 0x0020; // L
    if (held(XINPUT_GAMEPAD_RIGHT_SHOULDER)) joy |= 0x0010; // R

    // XInput's thumbstick Y is positive-up (opposite of SDL_GameController's LEFTY, which is
    // positive-down) — comparisons are flipped rather than negating the value so the deadzone
    // constant's sign convention stays identical to the rest of this file.
    if (held(XINPUT_GAMEPAD_DPAD_UP) || pad.thumbLY > kStickDeadzone) joy |= 0x0800;
    if (held(XINPUT_GAMEPAD_DPAD_DOWN) || pad.thumbLY < -kStickDeadzone) joy |= 0x0400;
    if (held(XINPUT_GAMEPAD_DPAD_LEFT) || pad.thumbLX < -kStickDeadzone) joy |= 0x0200;
    if (held(XINPUT_GAMEPAD_DPAD_RIGHT) || pad.thumbLX > kStickDeadzone) joy |= 0x0100;
    return joy;
}

// Generic-pad button layout, since DirectInput exposes only a raw numbered button array with
// no standard naming (that's exactly why XInput exists) and this app has no input-remapping UI
// to let a player fix a wrong guess. This follows the button order most PS-style/generic USB
// pad drivers report on Windows (1/2/3/4 face buttons, then shoulder/trigger, then
// select/start) — the same default long-used by DirectInput-based SNES emulators — but isn't
// guaranteed to match every controller.
uint16_t sampleDirectInput(const DirectInputManager::PadState& pad) {
    auto held = [&](int bit) { return (pad.buttons & (1u << bit)) != 0; };
    uint16_t joy = 0;
    if (held(0)) joy |= 0x8000; // B
    if (held(1)) joy |= 0x0080; // A
    if (held(2)) joy |= 0x4000; // Y
    if (held(3)) joy |= 0x0040; // X
    if (held(4)) joy |= 0x0020; // L
    if (held(5)) joy |= 0x0010; // R
    if (held(8)) joy |= 0x2000; // Select
    if (held(9)) joy |= 0x1000; // Start

    const bool povCentered = pad.pov == 0xFFFFFFFF;
    const int povDirection = povCentered ? -1 : static_cast<int>(pad.pov / 4500);
    const bool povUp = !povCentered && (povDirection == 7 || povDirection == 0 || povDirection == 1);
    const bool povDown = !povCentered && (povDirection >= 3 && povDirection <= 5);
    const bool povLeft = !povCentered && (povDirection >= 5 && povDirection <= 7);
    const bool povRight = !povCentered && (povDirection >= 1 && povDirection <= 3);

    if (povUp || pad.y < -kStickDeadzone) joy |= 0x0800;
    if (povDown || pad.y > kStickDeadzone) joy |= 0x0400;
    if (povLeft || pad.x < -kStickDeadzone) joy |= 0x0200;
    if (povRight || pad.x > kStickDeadzone) joy |= 0x0100;
    return joy;
}

} // namespace

uint16_t sample(int playerIndex) {
    XInputManager::refreshAll();
    DirectInputManager::refreshAll();
    assign(playerIndex);

    const Slot& slot = g_slots[playerIndex];
    switch (slot.source) {
        case Source::XInput: return sampleXInput(XInputManager::state(slot.index));
        case Source::DirectInput: return sampleDirectInput(DirectInputManager::state(slot.index));
        case Source::None: return 0;
    }
    return 0;
}

} // namespace NativeGamepad

#endif
