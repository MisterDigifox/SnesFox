// Windows implementation of the native_file_dialog.hpp interface (declared under
// src/macOS/ since that's where it was first written — the header itself is
// platform-agnostic, only the .mm/.cpp implementation differs per platform).
#include "../macOS/native_file_dialog.hpp"

#include <cstdio>
#include <windows.h>
#include <commdlg.h>

std::optional<std::string> showOpenRomDialog() {
    char path[MAX_PATH] = {0};

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "SNES ROM (*.sfc, *.smc)\0*.sfc;*.smc\0All Files\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(path);
    }
    return std::nullopt;
}

// No native menu-bar integration on Windows (there's no shared app menu bar the way
// macOS has one) — same "no-op everywhere else" contract the header documents.
void installOpenRomMenu() {}
void removeDefaultWindowMenu() {}
std::optional<std::string> takeMenuOpenRomPath() { return std::nullopt; }

std::optional<std::string> showSaveSampleDialog(const std::string& suggestedName) {
    char path[MAX_PATH] = {0};
    std::snprintf(path, sizeof(path), "%s", suggestedName.c_str());

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "WAV Audio (*.wav)\0*.wav\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrDefExt = "wav";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(path);
    }
    return std::nullopt;
}
