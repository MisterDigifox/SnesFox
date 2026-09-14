#include "native_input.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

namespace {
bool g_keyState[256] = {};
bool g_active = false;
id g_keyMonitor = nil;
bool g_escapePending = false;
bool g_f11Pending = false;

void setKey(int code, bool down) {
    if (code < 0 || code >= 256) return;
    const bool wasDown = g_keyState[code];
    g_keyState[code] = down;
    if (down && !wasDown) {
        if (code == 0x35) g_escapePending = true; // kVK_Escape
        if (code == 0x67) g_f11Pending = true;    // kVK_F11
    }
}
} // namespace

void installNativeKeyMonitor() {
    if (g_keyMonitor) return;
    NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged;
    g_keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* event) {
        if ([event type] == NSEventTypeKeyDown) {
            setKey(static_cast<int>([event keyCode]), true);
            // We've already recorded the key state above; nothing downstream (no NSView
            // overrides keyDown: in bare mode) will otherwise handle this event, and an
            // unhandled keyDown: falls through to NSResponder's default implementation, which
            // beeps. Swallow plain keys (joypad/emulator controls) so they never reach that
            // fallback. Cmd-chord events (Cmd+O, Cmd+Q, Cmd+W, ...) still need to flow through
            // to NSApp's menu key-equivalent matching, so let those continue.
            if (([event modifierFlags] & NSEventModifierFlagCommand) == 0) return nil;
        } else if ([event type] == NSEventTypeKeyUp) {
            setKey(static_cast<int>([event keyCode]), false);
            if (([event modifierFlags] & NSEventModifierFlagCommand) == 0) return nil;
        } else if ([event type] == NSEventTypeFlagsChanged) {
            // Modifier keys (Shift/Control/etc.) never generate KeyDown/KeyUp — only the
            // physical key that changed is in `keyCode`, and whether it's now up or down has
            // to be inferred from whether the corresponding modifier bit is set afterward.
            const int code = static_cast<int>([event keyCode]);
            bool down = false;
            switch (code) {
                case 0x38: case 0x3C: // left/right Shift
                    down = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
                    break;
                case 0x3B: case 0x3E: // left/right Control
                    down = ([event modifierFlags] & NSEventModifierFlagControl) != 0;
                    break;
                case 0x3A: case 0x3D: // left/right Option
                    down = ([event modifierFlags] & NSEventModifierFlagOption) != 0;
                    break;
                default:
                    break;
            }
            setKey(code, down);
        }
        return event;
    }];
    g_active = true;
}

void removeNativeKeyMonitor() {
    if (g_keyMonitor) {
        [NSEvent removeMonitor:g_keyMonitor];
        g_keyMonitor = nil;
    }
    g_active = false;
    for (bool& state : g_keyState) state = false;
}

bool isNativeInputActive() {
    return g_active;
}

void pumpNativeEvents() {
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES]) != nil) {
            [NSApp sendEvent:event];
        }
    }
}

bool isNativeKeyDown(int virtualKeyCode) {
    if (virtualKeyCode < 0 || virtualKeyCode >= 256) return false;
    return g_keyState[virtualKeyCode];
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
#else
void installNativeKeyMonitor() {}
void removeNativeKeyMonitor() {}
bool isNativeInputActive() { return false; }
void pumpNativeEvents() {}
bool isNativeKeyDown(int) { return false; }
bool takeNativeEscapePressed() { return false; }
bool takeNativeF11Pressed() { return false; }
#endif
