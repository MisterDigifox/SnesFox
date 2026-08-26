#pragma once
#include <optional>
#include <string>

// Shows the OS-native "Open File" panel (NSOpenPanel on macOS), filtered to SNES ROM
// extensions (.sfc/.smc). Returns the chosen path, or std::nullopt if the user cancelled
// or no native dialog is implemented for the current platform.
std::optional<std::string> showOpenRomDialog();

// Adds a "File > Open ROM…" (Cmd+O) item to the app's native menu bar (macOS only; a no-op
// everywhere else). Call once at startup, after SDL_Init/SDL_CreateWindow have already given
// the app its default menu bar (Apple/Window). Clicking the item runs the same dialog as
// showOpenRomDialog() and stashes the result for takeMenuOpenRomPath() to pick up.
void installOpenRomMenu();

// Call once per frame from the main loop; returns the path chosen via the File > Open menu
// item since the last call, or std::nullopt if none was chosen.
std::optional<std::string> takeMenuOpenRomPath();
