#pragma once
#include <string>

// Creates a native macOS window (NSWindow) sized in points and returns an opaque handle
// suitable for SDL_CreateWindowFrom() — SDL attaches its renderer to this already-existing
// window instead of creating (and owning) one itself. This matches how Mesen2's Avalonia UI
// hosts its SDL-rendered surface: a mature native windowing layer owns the actual NSWindow,
// SDL is only responsible for drawing into it. Returns nullptr on non-macOS platforms.
void* createNativeWindow(const std::string& title, int width, int height, bool resizable);

// Installs an NSWindowDelegate on the given window that tracks close requests, replacing SDL's
// SDL_QUIT event (which required SDL's video subsystem — see docs/tickets/01, 03).
void installNativeWindowDelegate(void* nativeWindow);

// True once the window's close box (or Cmd+Q) has been triggered.
bool nativeWindowWantsClose(void* nativeWindow);

// Toggles borderless, screen-covering fullscreen (mirrors SDL_WINDOW_FULLSCREEN_DESKTOP —
// not macOS's animated Spaces-based fullscreen, so the caller's own pillarbox/letterbox
// drawing keeps working exactly as it does today).
void toggleNativeFullscreen(void* nativeWindow);
bool isNativeFullscreen(void* nativeWindow);

// Sets the window's minimum content size, replacing SDL_SetWindowMinimumSize.
void setNativeMinimumSize(void* nativeWindow, int width, int height);

// Current content-view size in points, replacing SDL_GetWindowSize for the letterbox/pillarbox
// scale math in presentNativeFrame().
void getNativeContentSize(void* nativeWindow, int* outWidth, int* outHeight);
