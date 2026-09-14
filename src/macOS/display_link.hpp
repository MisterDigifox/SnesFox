#pragma once

// CVDisplayLink-backed vsync wait — used instead of trusting SDL_RENDERER_PRESENTVSYNC's own
// Metal-layer present blocking, which is a widely-reported unreliable/inconsistent timing
// source on macOS (esp. ProMotion/adaptive-refresh displays — see libsdl-org/SDL#4918,
// libretro/RetroArch#14807). CVDisplayLink is Apple's own API for getting notified precisely
// once per real hardware vsync, so we use it directly as the single source of truth for when
// to present, rather than a second (and per those reports, buggy) vsync path inside SDL/Metal.
// No-ops on non-macOS platforms.

// Starts the display link for the display the app is running on. Call once at startup,
// after SDL_Init has created the window.
void startDisplayLink();

// Stops and releases the display link. Call once at shutdown.
void stopDisplayLink();

// Blocks until the next real vsync tick. If the caller fell behind (took longer than one
// refresh interval), any backlog is drained first so this always waits for a tick that
// hasn't happened yet, not one already queued up while busy.
void waitForVsync();
