#pragma once
#include <optional>
#include <string>

// Shows the OS-native "Open File" panel (NSOpenPanel on macOS), filtered to SNES ROM
// extensions (.sfc/.smc). Returns the chosen path, or std::nullopt if the user cancelled
// or no native dialog is implemented for the current platform.
std::optional<std::string> showOpenRomDialog();
