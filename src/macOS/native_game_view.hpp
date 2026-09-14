#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Installs a plain NSView as the given native window's content view. Returns an opaque handle
// to the view (nullptr on non-macOS).
void* attachNativeGameView(void* nativeWindow);

// Uploads one 256x224 ARGB8888 frame (SDL_PIXELFORMAT_ARGB8888 byte layout) and synchronously
// redraws the view, letterboxed/pillarboxed to its current bounds with nearest-neighbor
// scaling. Safe to call every frame from the main thread.
void presentNativeGameFrame(void* view, const uint32_t* pixels);

// The view registers itself as a drag-and-drop target for .sfc/.smc files (replacing
// SDL_DROPFILE — see docs/tickets/04-native-drag-and-drop.md). Call once per frame; returns
// the dropped file's path if one was dropped since the last call.
std::optional<std::string> takeNativeDroppedRomPath();
