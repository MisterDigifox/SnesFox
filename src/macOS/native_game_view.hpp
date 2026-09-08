#pragma once
#include <cstdint>
#include <optional>
#include <string>

// Draws the game framebuffer through plain AppKit/Quartz view compositing instead of SDL's
// Metal renderer. Mesen2's actual macOS app never lets SDL own a window's presentation either
// — Avalonia embeds SdlRenderer into a small native subview and Avalonia's own (Skia-based)
// compositor owns the real window. The macOS SDL_RENDERER_PRESENTVSYNC/CAMetalLayer
// "direct-to-display" present path is the specific thing multiple unfixed Apple bug reports
// (libsdl-org/SDL#4918, libretro/RetroArch#14807) blame for inconsistent vsync timing on
// ProMotion displays — plain NSView drawRect: drawing goes through the ordinary windowed
// compositor instead and isn't affected by that bug. Bare (non-debug) mode only; --debug still
// uses SDL_Renderer for ImGui.

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
