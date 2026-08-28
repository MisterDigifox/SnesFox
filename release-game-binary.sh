#!/bin/bash
set -e

ROM_NAME="Transparency"
ROM_SRC="roms/$ROM_NAME.sfc"
GEN_HEADER="src/game/embedded_rom.generated.hpp"

xxd -i "$ROM_SRC" \
  | sed -e 's/unsigned char [A-Za-z0-9_]*\[\]/unsigned char kEmbeddedRomData[]/' \
        -e 's/unsigned int [A-Za-z0-9_]*_len/unsigned int kEmbeddedRomSize/' \
  > "$GEN_HEADER"

rm -f "$ROM_NAME"

clang++ -std=c++20 -O2 -DSNESFOX_KIOSK_MODE=1 "-DSNESFOX_APP_NAME=\"$ROM_NAME\"" src/game/main_game.cpp src/core/*.cpp src/macOS/*.mm tests/*.cpp imgui/*.cpp imgui/backends/*.cpp -o "$ROM_NAME" \
  -I. \
  -Iimgui \
  -Iimgui/backends \
  -I/opt/homebrew/opt/sdl2/include \
  -I/opt/homebrew/opt/sdl2/include/SDL2 \
  -L/opt/homebrew/opt/sdl2/lib \
  -lSDL2 \
  -framework Cocoa \
  -framework UniformTypeIdentifiers

# macOS: ad-hoc codesign avoids some machines killing unsigned local binaries (symptom: zsh: killed).
if [ "$(uname -s)" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
  codesign --force -s - "./$ROM_NAME" 2>/dev/null || true
fi
