#pragma once

// Windows-only glue shared between native_window.cpp and native_game_view.cpp — not part of
// the cross-platform native_*.hpp interfaces (those stay platform-agnostic headers under
// src/macOS/, per native_file_dialog.cpp's comment on that convention). Both files hook into
// the single Win32 message loop native_window.cpp's WndProc owns, so this is just the minimal
// wiring between them.

#include <windows.h>
#include <string>

// Repaints the last uploaded frame (or fills black if none yet), letterboxed/pillarboxed to
// hwnd's current client size. Called from native_window.cpp's WndProc on WM_PAINT so
// resizing/uncovering the window doesn't wait for the next emulated frame to redraw —
// mirrors AppKit automatically re-invoking NSView drawRect: on macOS.
void windowsRedrawGameView(HWND hwnd);

// Records a dropped .sfc/.smc file path from WM_DROPFILES, surfaced later via
// takeNativeDroppedRomPath() (native_game_view.hpp).
void windowsSetDroppedRomPath(const std::string& path);
